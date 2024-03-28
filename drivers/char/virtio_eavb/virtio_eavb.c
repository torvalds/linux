// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include<linux/init.h>
#include<linux/kernel.h>
#include<linux/module.h>
#include <linux/debugfs.h>

#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>

#include <linux/cdev.h>
#include <linux/delay.h>

#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/sched/clock.h>
#include <soc/qcom/boot_stats.h>
#include <uapi/linux/eavb_shared.h>

#include "vio_eavb.h"

/* Virtio ID of eavb : 0xC006 */
#define VIRTIO_ID_EAVB		49158
/* Virtio ID of eavb for Backward compatibility : 0x24 */
#define VIRTIO_ID_EAVB_BC	36

/* support feature */
#define VIRTIO_EAVB_F_SHMEM	1

#define MINOR_NUM_DEV		0
#define DEVICE_NAME		"virt-eavb"
#define DEVICE_NUM		1

#define LEVEL_DEBUG	1
#define LEVEL_INFO	2
#define LEVEL_ERR	3

static unsigned int log_level = LEVEL_INFO;

static char *prix[] = {"", "debug", "info", "error"};
static void log_eavb(int level, const char *fmt, ...)
{
	va_list args;

	if ((level) >= log_level) {
		va_start(args, fmt);
		vprintk(fmt, args);
		va_end(args);
	}
}
#define LOG_EAVB(level, format, args...) \
log_eavb(level, "eavb: pid %.8x: %s: %s(%d) "format, \
current->pid, prix[0x3 & (level)], __func__, __LINE__, ## args)

#define ASSERT(x) \
do { \
	if (unlikely(!(x))) { \
		pr_err("Assertion failed! %s %s:%d\n", \
			__func__, __FILE__, __LINE__); \
		dump_stack(); \
	} \
} while (0)

static unsigned int timeout_msec = 5000; /* default 5s */

/*
 *    device_priv (struct virtio_eavb_priv)
 *    |
 *    |-- file_priv (struct eavb_file)
 *    |   |
 *    |   |-- stream0 (struct stream)
 *    |   |-- stream1
 *    |   |-- stream2
 */

struct fe_msg;
struct virtio_eavb_priv {
	char *name;
	struct virtio_device *vdev;
	struct virtqueue     *svq;
	struct virtqueue     *rvq;
	spinlock_t rvqlock;

	bool has_shmem;

	struct mutex lock;

	struct device *dev;
	struct cdev cdev;
	struct class *class;
	dev_t dev_no;
	int file_index;

	spinlock_t msglock;
#define FE_MSG_MAX		256 /* 1<<8 */
#define mod(index)		((index) & 0xff) /* ((index) % FE_MSG_MAX) */
	struct fe_msg *msgtable[FE_MSG_MAX];
#define RX_BUF_MAX_LEN		1024
	void *rxbufs[FE_MSG_MAX];
	struct work_struct reclaim_work;

#ifdef EAVB_DEBUGFS
	struct dentry *debugfs_root;

	struct rx_record {
		u32 msgid;
		u64 ts;
	} rxrecords[32];
	int next_rxrecords_index;
#endif

	u64 crf_ts;
	struct completion crf_ts_update;
};

struct mapping {
	struct list_head list;
	u64 va; /* kernel virtual address */
	u64 ua; /* user address */
	u64 pa; /* phy address */
	size_t size;
};

struct eavb_file;
struct stream {
	int index;
	struct eavb_file *fl;
	u64 hdl;
	u32 idx;
	enum stream_status {
		UNCREATED = 0,
		CREATED = 1,
		CONNECTED = 2,
		DISCONNECTED = 3,
		DESTROYED = 4,
	} status;
#ifdef EAVB_DEBUGFS
	struct dentry *debugfs;
	struct tx_record {
		u32 msgid;
		u64 begin;
		u64 end;
#define RECORD_COUNT  (1<<4)
	} records[RECORD_COUNT];
	int next_records_index;
	spinlock_t recordslock;
#endif
};

struct eavb_file {
	int index;
	struct virtio_eavb_priv *priv;

#define MAPPING_MAX		8
	int mapping_count;
	struct mapping mapping[MAPPING_MAX];
	spinlock_t mappinglock;

	struct stream streams[MAX_STREAM_NUM];
	int stream_count;
	spinlock_t streamlock;
};


static inline const char *cmd2str(uint32_t cmd)
{
	switch (cmd) {
	case VIRTIO_EAVB_T_CREATE_STREAM:
		return "VIRTIO_EAVB_T_CREATE_STREAM";
	case VIRTIO_EAVB_T_GET_STREAM_INFO:
		return "VIRTIO_EAVB_T_GET_STREAM_INFO";
	case VIRTIO_EAVB_T_CONNECT_STREAM:
		return "VIRTIO_EAVB_T_CONNECT_STREAM";
	case VIRTIO_EAVB_T_RECEIVE:
		return "VIRTIO_EAVB_T_RECEIVE";
	case VIRTIO_EAVB_T_TRANSMIT:
		return "VIRTIO_EAVB_T_TRANSMIT";
	case VIRTIO_EAVB_T_DISCONNECT_STREAM:
		return "VIRTIO_EAVB_T_DISCONNECT_STREAM";
	case VIRTIO_EAVB_T_DESTROY_STREAM:
		return "VIRTIO_EAVB_T_DESTROY_STREAM";
	case VIRTIO_EAVB_T_CREATE_STREAM_PATH:
		return "VIRTIO_EAVB_T_CREATE_STREAM_PATH";
	case VIRTIO_EAVB_T_MMAP:
		return "VIRTIO_EAVB_T_MMAP";
	case VIRTIO_EAVB_T_MUNMAP:
		return "VIRTIO_EAVB_T_MUNMAP";
	case VIRTIO_EAVB_T_UPDATE_CLK:
		return "VIRTIO_EAVB_T_UPDATE_CLK";
	default:
		return "not supported";
	}
}

#ifdef EAVB_DEBUGFS
void update_crf_ts(struct virtio_eavb_priv *priv, u64 value);
static ssize_t log_write(struct file *file,
		const char __user *buf,
		size_t count, loff_t *ppos)
{
	char mybuffer[32] = {0};
	size_t len = min(count, sizeof(mybuffer));
	long input;

	if (copy_from_user(mybuffer, buf, len)) {
		LOG_EAVB(LEVEL_ERR, "copy_from_user failed\n");
		return -EFAULT;
	}

	if (kstrtol(mybuffer, 0, &input) == 0) {
		log_level = input & 0x3;
		LOG_EAVB(LEVEL_INFO, "update log_level to %d\n", log_level);
	}

	return len;
}

static int do_rxrecords_snapshoot(struct virtio_eavb_priv *priv,
		char *buffer, size_t size)
{
	int i, index, len;
	struct rx_record records[32];

	memcpy(records, priv->rxrecords, sizeof(records));
	index = priv->next_rxrecords_index - 1;

	len = 0;
	for (i = 0; i < 32; i++) {
		u32 msgid;
		u64 ts, rem_nsec;
		int l;

		if (index < 0)
			break;
		msgid = records[index & 0x1f].msgid;
		ts = records[index & 0x1f].ts;
		rem_nsec = do_div(ts, 1000000000);

		l = scnprintf(&buffer[len], size - len, "%llu.%06llu: %d\n",
				ts, rem_nsec/1000, msgid);
		if (l <= 0)
			break;
		if (l + len > size)
			break;
		len += l;
		index--;
	}
	return len;
}
static ssize_t log_read(struct file *file,
		char __user *userbuf,
		size_t count, loff_t *ppos)
{
	struct virtio_eavb_priv *priv = file->private_data;
	char *buffer;
	int len;
	ssize_t return_count;

	if (*ppos > 0)
		return 0;
#define BUF_SIZE 512
	buffer = kzalloc(BUF_SIZE, GFP_KERNEL);
	if (!buffer)
		return 0;
	len = do_rxrecords_snapshoot(priv, buffer, BUF_SIZE);
#undef BUF_SIZE
	return_count = simple_read_from_buffer(userbuf, count,
			ppos, buffer, len);
	kfree(buffer);
	return return_count;
}

static ssize_t timeout_write(struct file *file,
		const char __user *buf,
		size_t count, loff_t *ppos)
{
	char mybuffer[32] = {0};
	size_t len = min(count, sizeof(mybuffer));
	long input;

	if (copy_from_user(mybuffer, buf, len)) {
		LOG_EAVB(LEVEL_ERR, "copy_from_user failed\n");
		return -EFAULT;
	}

	if (kstrtol(mybuffer, 0, &input) == 0) {
		timeout_msec = input;
		LOG_EAVB(LEVEL_INFO, "set timeout to %d ms\n", timeout_msec);
	}

	return len;
}
static ssize_t timeout_read(struct file *file,
		char __user *userbuf,
		size_t count, loff_t *ppos)
{
	char *buffer;
	int len;
	ssize_t return_count;

	if (*ppos > 0)
		return 0;
#define BUF_SIZE 512
	buffer = kzalloc(BUF_SIZE, GFP_KERNEL);
	if (!buffer)
		return 0;
	len = scnprintf(buffer, BUF_SIZE, "%d msec\n", timeout_msec);
#undef BUF_SIZE
	return_count = simple_read_from_buffer(userbuf, count,
			ppos, buffer, len);
	kfree(buffer);
	return return_count;
}

static int do_stream_snapshoot(struct stream *stream, char *buffer, size_t size)
{
	struct tx_record records[RECORD_COUNT];
	int current_records_index, save_index, i, len;
	u64 total_taken_us = 0;

	spin_lock(&stream->recordslock);
	memcpy(records, stream->records, sizeof(records));
	current_records_index = stream->next_records_index - 1;
	spin_unlock(&stream->recordslock);

	len = scnprintf(buffer, size,
		"stream%d: status(%d)\n",
		stream->index, stream->status);
	for (i = 0; i < RECORD_COUNT; i++) {
		u64 taken_us;
		int l;
		struct tx_record *record;

		if (current_records_index < 0)
			break;
		save_index = current_records_index & (RECORD_COUNT - 1);
		record = &records[save_index];
		taken_us = (record->end - record->begin)/1000;

		l = scnprintf(&buffer[len], size - len,
			"[%d]msgid %d, %lld us\n",
			current_records_index, record->msgid, taken_us);
		if (l <= 0)
			break;
		if (l + len > size)
			break;
		len += l;
		current_records_index--;
		total_taken_us += taken_us;
	}
	if (i > 0 && len < size) {
		/* average */
		int l = snprintf(&buffer[len], size - len,
			"average %lld us\n", total_taken_us / i);
		if (l > 0 && l + len <= size)
			len += l;
	}
	return len;
}

static ssize_t stream_read(struct file *file,
		char __user *userbuf,
		size_t count, loff_t *ppos)
{
	struct stream *stream = file->private_data;
	char *buffer;
	int len;
	ssize_t return_count;

	if (*ppos > 0)
		return 0;
#define BUF_SIZE 512
	buffer = kzalloc(BUF_SIZE, GFP_KERNEL);
	if (!buffer)
		return 0;
	len = do_stream_snapshoot(stream, buffer, BUF_SIZE);
#undef BUF_SIZE
	return_count = simple_read_from_buffer(userbuf, count,
			ppos, buffer, len);
	kfree(buffer);
	return return_count;
}

static const struct file_operations fops_debugfs_log = {
	.open = simple_open,
	.write = log_write,
	.read = log_read,
	.owner = THIS_MODULE,
};

static const struct file_operations fops_debugfs_timeout = {
	.open = simple_open,
	.write = timeout_write,
	.read = timeout_read,
	.owner = THIS_MODULE,
};

static const struct file_operations fops_debugfs_stream = {
	.open = simple_open,
	.read = stream_read,
	.owner = THIS_MODULE,
};
#endif

static struct stream *getStream(struct eavb_file *fl, u64 hdl)
{
	int i;

	spin_lock(&fl->streamlock);
	for (i = 0; i < MAX_STREAM_NUM; i++) {
		struct stream *stream = &fl->streams[i];

		if (stream->hdl == hdl) {
			if (hdl == 0) {
				stream->index = i;
				stream->hdl = 0xffffffffffffffff;
			}
			spin_unlock(&fl->streamlock);
			return stream;
		}
	}
	spin_unlock(&fl->streamlock);
	return NULL;
}

static void reset_stream(struct stream *stream)
{
	stream->hdl = 0;
	stream->idx = 0;
}

#ifdef EAVB_DEBUGFS
static void create_dbugfs_stream(struct stream *stream)
{
	struct eavb_file *fl = stream->fl;
	struct virtio_eavb_priv *priv = fl->priv;
	char name[16] = {0};

	snprintf(name, sizeof(name), "%d-stream%d", fl->index, stream->index);
	stream->debugfs = debugfs_create_dir(name, priv->debugfs_root);
	debugfs_create_file("status", 00400 | 00200,
			stream->debugfs, stream,
			&fops_debugfs_stream);
	stream->next_records_index = 0;
	spin_lock_init(&stream->recordslock);
}

static void destroy_dbugfs_stream(struct stream *stream)
{
	debugfs_remove_recursive(stream->debugfs);
}

static void records(struct stream *stream, u32 msgid, u64 begin, u64 end)
{
	int index;

	spin_lock(&stream->recordslock);
	index = (stream->next_records_index++) & 0x0f;
	spin_unlock(&stream->recordslock);

	stream->records[index].msgid = msgid;
	stream->records[index].begin = begin;
	stream->records[index].end = end;
}
#else
#define create_dbugfs_stream(stream)
#define destroy_dbugfs_stream(stream)
#define records(stream, cmd, begin, end)
#endif

static struct mapping *get_file_mapping(struct eavb_file *fl, u64 addr, u32 len)
{
	u64 start, end;
	int i;

	for (i = 0; i < MAPPING_MAX; i++) {
		start = fl->mapping[i].ua;
		end = fl->mapping[i].ua + fl->mapping[i].size;
		if (addr >= start && addr + len <= end)
			return &fl->mapping[i];
	}
	return NULL;
}
static u64 get_mapping_phyaddr(struct mapping *mapping, u64 ua)
{
	return mapping->pa + (ua - mapping->ua);
}

static int add_mapping_info(struct eavb_file *fl,
		u64 va, u64 ua, u64 pa, size_t size)
{
	int i;

	for (i = 0; i < MAPPING_MAX; i++) {
		if (fl->mapping[i].va == 0) {
			fl->mapping[i].size = size;
			fl->mapping[i].va = va;
			fl->mapping[i].ua = ua;
			fl->mapping[i].pa = pa;
			break;
		}
	}
	return i;
}

struct fe_msg {
	u16 msgid;
	struct completion work;
	void *txbuf;
	void *rxbuf;
	u32 txbuf_size;
	u32 rxbuf_used;
	u64 ts_begin;
	u64 ts_end;
};

static struct fe_msg *virt_alloc_msg(struct virtio_eavb_priv *priv,
		int tsize, int rsize)
{
	struct fe_msg *msg;
	unsigned long flags;
	u8 *buf;
	int i;
	static int next; /* default 0 */

	ASSERT(rsize < RX_BUF_MAX_LEN);
	/* kzalloc needn't memset 0 */
	msg = kzalloc(sizeof(*msg) + tsize, GFP_KERNEL);
	if (!msg) {
		LOG_EAVB(LEVEL_ERR, "cannot alloc msg memory\n");
		return NULL;
	}
	buf = kzalloc(tsize, GFP_KERNEL);
	if (!buf) {
		LOG_EAVB(LEVEL_ERR, "cannot alloc txbuf memory\n");
		kfree(msg);
		return NULL;
	}

	spin_lock_irqsave(&priv->msglock, flags);
	for (i = 0; i < FE_MSG_MAX; i++) {
		int index = mod(next + i);

		if (!priv->msgtable[index]) {
			priv->msgtable[index] = msg;
			msg->msgid = index;
			next = mod(index + 1);
			break;
		}
	}
	spin_unlock_irqrestore(&priv->msglock, flags);

	if (i == FE_MSG_MAX) {
		LOG_EAVB(LEVEL_ERR, "fe message queue is full\n");
		kfree(buf);
		kfree(msg);
		return NULL;
	}

	msg->txbuf = buf;
	msg->txbuf_size = tsize;
	init_completion(&msg->work);

	return msg;
}

static void reclaim_rxbuf(struct virtio_eavb_priv *priv, void *rxbuf)
{
	struct scatterlist sg;
	unsigned long flags;

	sg_init_one(&sg, rxbuf, RX_BUF_MAX_LEN);
	spin_lock_irqsave(&priv->rvqlock, flags);
	virtqueue_add_inbuf(priv->rvq, &sg, 1, rxbuf, GFP_KERNEL);
	virtqueue_kick(priv->rvq);
	spin_unlock_irqrestore(&priv->rvqlock, flags);
}

static void virt_free_msg(struct virtio_eavb_priv *priv, struct fe_msg *msg)
{
	unsigned long flags;

	if (msg->rxbuf_used)
		reclaim_rxbuf(priv, msg->rxbuf);

	spin_lock_irqsave(&priv->msglock, flags);
	if (priv->msgtable[msg->msgid] == msg)
		priv->msgtable[msg->msgid] = NULL;
	else
		LOG_EAVB(LEVEL_ERR, "can't find msg %d in table\n",
			msg->msgid);
	spin_unlock_irqrestore(&priv->msglock, flags);

	kfree(msg);
	schedule_work(&priv->reclaim_work);
}

static void reclaim_function(struct work_struct *work)
{
	struct virtio_eavb_priv *priv;

	priv = container_of(work, struct virtio_eavb_priv, reclaim_work);
	while (1) {
		struct vio_msg_hdr *vhdr;
		unsigned int len;

		mutex_lock(&priv->lock);
		/* remove a used txbuf */
		vhdr = virtqueue_get_buf(priv->svq, &len);
		mutex_unlock(&priv->lock);

		if (!vhdr)
			break;

		ASSERT(vhdr->len == len);
		LOG_EAVB(LEVEL_DEBUG, "msgid %d txbuf used\n", vhdr->msgid);
		kfree(vhdr);
	}
}

#define fill_vmsg_hdr(msg, stream, cmdtype) \
	do { \
		struct vio_msg_hdr *vhdr; \
		vhdr = (struct vio_msg_hdr *)msg->txbuf; \
		vhdr->cmd = cmdtype; \
		vhdr->len = msg->txbuf_size; \
		vhdr->streamctx_hdl = stream->hdl; \
		vhdr->stream_idx = stream->idx; \
	} while (0)

static int send_msg(struct virtio_eavb_priv *priv, struct fe_msg *msg)
{
	struct vio_msg_hdr *req, *rsp = NULL;
	struct scatterlist sg[1];
	int ret = 0;
	int rsv;
	uint32_t cmd_req, cmd_rsp = 0;
	int32_t stream_idx_req, stream_idx_rsp = 0;
	uint16_t msgid_req, msgid_rsp = 0;

	req = (struct vio_msg_hdr *)msg->txbuf;
	msgid_req = msg->msgid;
	cmd_req = req->cmd;
	stream_idx_req = req->stream_idx;
	msg->rxbuf = NULL;

	if ((cmd_req != VIRTIO_EAVB_T_RECEIVE) && (cmd_req != VIRTIO_EAVB_T_TRANSMIT))
		LOG_EAVB(LEVEL_INFO, "[eavb#%d] request: msgid %d, cmd %s\n",
			stream_idx_req, msgid_req, cmd2str(cmd_req));

	mutex_lock(&priv->lock);
	req->msgid = msg->msgid;
	sg_init_one(sg, req, req->len);
	ret = virtqueue_add_outbuf(priv->svq, sg, 1, req, GFP_KERNEL);
	if (ret) {
		LOG_EAVB(LEVEL_ERR, "fail to add output buffer, return %d\n",
			ret);
		mutex_unlock(&priv->lock);
		kfree(req);
		msg->txbuf = NULL;
		goto error;
	}
	virtqueue_kick(priv->svq);
	mutex_unlock(&priv->lock);

	rsv = wait_for_completion_timeout(&msg->work, msecs_to_jiffies(timeout_msec));
	if (rsv == 0) {
		LOG_EAVB(LEVEL_ERR, "msgid %d timeout\n", msg->msgid);
		ret = -ETIMEDOUT;
		goto error;
	}
	rsp = (struct vio_msg_hdr *)msg->rxbuf;
	msgid_rsp = msg->msgid;
	if (msgid_req != msgid_rsp) {
		LOG_EAVB(LEVEL_ERR, "msgid mismatch, msgid_req %d, msgid_rsp %d\n",
			msgid_req, msgid_rsp);
		ret = -EFAULT;
		goto error;
	}

	if (rsp) {
		cmd_rsp = rsp->cmd;
		stream_idx_rsp = rsp->stream_idx;
		if (rsp->result) {
			LOG_EAVB(LEVEL_ERR, "[eavb#%d] unexpected rsp: msgid %d, cmd %s, ret %d\n",
				stream_idx_req, msgid_req, cmd2str(cmd_req), rsp->result);
			ret = -EINVAL;
		}
	} else {
		LOG_EAVB(LEVEL_ERR, "fail to get rsp buffer, msgid %d\n",
			msgid_rsp);
		ret = -ENOMEM;
		goto error;
	}

	if ((ret != 0) ||
		((cmd_rsp != VIRTIO_EAVB_T_RECEIVE) && (cmd_rsp != VIRTIO_EAVB_T_TRANSMIT)))
		LOG_EAVB(LEVEL_INFO, "[eavb#%d] response: msgid %d, cmd %s, ret %d\n",
			stream_idx_rsp, msgid_rsp, cmd2str(cmd_rsp), ret);
	return ret;
error:
	LOG_EAVB(LEVEL_ERR, "[eavb#%d] REQ/RSP ERROR: msgid %d, cmd %s, ret %d\n",
		stream_idx_req, msgid_req, cmd2str(cmd_req), ret);
	return ret;
}


static int vio_eavb_disconnect(struct eavb_file *fl, struct stream *stream)
{
	struct virtio_eavb_priv *priv = fl->priv;
	struct fe_msg *msg;
	struct vio_msg_hdr *vhdr;
	int tsize, rsize;
	int ret;

	tsize = rsize = sizeof(struct vio_disconnect_stream_msg);
	msg = virt_alloc_msg(priv, tsize, rsize);
	if (!msg) {
		LOG_EAVB(LEVEL_ERR, "stream%d alloc msg fail!\n",
			stream->index);
		return -ENOMEM;
	}

	fill_vmsg_hdr(msg, stream, VIRTIO_EAVB_T_DISCONNECT_STREAM);
	vhdr = (struct vio_msg_hdr *)msg->txbuf;

	LOG_EAVB(LEVEL_DEBUG, "(%d:%d:%d)\n",
		fl->index, stream->index, msg->msgid);
	LOG_EAVB(LEVEL_DEBUG, "stream%d (ctx 0x%llx, idx %d)\n",
		stream->index, vhdr->streamctx_hdl, vhdr->stream_idx);
	ret = send_msg(priv, msg);
	LOG_EAVB(LEVEL_DEBUG, "(%d:%d:%d) ret=%d\n",
		fl->index, stream->index, msg->msgid, ret);

	vhdr = (struct vio_msg_hdr *)msg->rxbuf;
	if (!ret && vhdr) {
		ret = vhdr->result;
		stream->status = DISCONNECTED;
	}

	virt_free_msg(priv, msg);
	return 0;
}

static int vio_eavb_destroy(struct eavb_file *fl, struct stream *stream)
{
	struct virtio_eavb_priv *priv = fl->priv;
	struct fe_msg *msg;
	struct vio_msg_hdr *vhdr;
	int tsize, rsize;
	int ret;

	tsize = rsize = sizeof(struct vio_destroy_stream_msg);
	msg = virt_alloc_msg(priv, tsize, rsize);
	if (!msg) {
		LOG_EAVB(LEVEL_ERR, "stream%d alloc msg fail!\n",
			stream->index);
		return -ENOMEM;
	}

	fill_vmsg_hdr(msg, stream, VIRTIO_EAVB_T_DESTROY_STREAM);
	vhdr = (struct vio_msg_hdr *)msg->txbuf;

	LOG_EAVB(LEVEL_DEBUG, "(%d:%d:%d)\n",
		fl->index, stream->index, msg->msgid);
	ret = send_msg(priv, msg);
	LOG_EAVB(LEVEL_DEBUG, "(%d:%d:%d) ret=%d\n",
		fl->index, stream->index, msg->msgid, ret);

	vhdr = (struct vio_msg_hdr *)msg->rxbuf;
	if (!ret && vhdr) {
		ret = vhdr->result;

		destroy_dbugfs_stream(stream);

		reset_stream(stream);
		stream->status = DESTROYED;
	}

	virt_free_msg(priv, msg);
	return 0;
}

static int virtio_eavb_open(struct inode *inode, struct file *file)
{
	struct cdev *cdev = inode->i_cdev;
	struct virtio_eavb_priv *priv;
	struct eavb_file *fl;

	priv = container_of(cdev, struct virtio_eavb_priv, cdev);
	fl = kzalloc(sizeof(*fl), GFP_KERNEL);
	if (!fl) {
		LOG_EAVB(LEVEL_ERR, "malloc fail\n");
		return -ENOMEM;
	}

	memset(fl, 0, sizeof(*fl));
	fl->priv = priv;
	file->private_data = fl;
	spin_lock_init(&fl->mappinglock);
	spin_lock_init(&fl->streamlock);

	fl->index = priv->file_index++;
	LOG_EAVB(LEVEL_INFO, "fl->index=%d\n", fl->index);
	return 0;
}

static int virtio_eavb_release(struct inode *inode, struct file *file)
{
	struct eavb_file *fl = file->private_data;
	int i;

	LOG_EAVB(LEVEL_INFO, "fl->index=%d\n", fl->index);
	for (i = 0; i < MAX_STREAM_NUM; i++) {
		struct stream *stream = &fl->streams[i];

		LOG_EAVB(LEVEL_INFO, "[%d], status %d\n",
			i, stream->status);
		if (stream->status == CONNECTED)
			vio_eavb_disconnect(fl, stream);

		if (stream->status == DISCONNECTED
			|| stream->status == CREATED) {
			vio_eavb_destroy(fl, stream);
		}
	}

	file->private_data = NULL;
	for (i = 0; i < MAPPING_MAX; i++) {
		if (fl->mapping[i].va)
			kfree((void *)fl->mapping[i].va);
	}
	memset(fl->mapping, 0, sizeof(fl->mapping));
	fl->mapping_count = 0;

	LOG_EAVB(LEVEL_INFO, "fl->index=%d\n", fl->index);
	kfree(fl);
	return 0;
}

static int qavb_create_stream(struct eavb_file *fl, void __user *buf)
{
	struct virtio_eavb_priv *priv = fl->priv;
	struct fe_msg *msg;
	struct vio_msg_hdr *vhdr;
	struct vio_create_stream_msg *vmsg;
	struct eavb_ioctl_create_stream create;
	int tsize, rsize;
	struct stream *stream;
	int ret;

	if (copy_from_user(&create, buf, sizeof(create))) {
		LOG_EAVB(LEVEL_ERR, "copy_from_user failed\n");
		return -EFAULT;
	}

	LOG_EAVB(LEVEL_INFO, "fl->index=%d\n", fl->index);

	stream = getStream(fl, 0);
	if (!stream) {
		LOG_EAVB(LEVEL_ERR, "stream full\n");
		return -EINVAL;
	}

	tsize = rsize = sizeof(struct vio_create_stream_msg);
	msg = virt_alloc_msg(priv, tsize, rsize);
	if (!msg) {
		LOG_EAVB(LEVEL_ERR, "stream%d alloc msg fail!\n",
			stream->index);
		reset_stream(stream);
		return -ENOMEM;
	}

	vhdr = (struct vio_msg_hdr *)msg->txbuf;
	vhdr->cmd = VIRTIO_EAVB_T_CREATE_STREAM;
	vhdr->len = msg->txbuf_size;

	ASSERT(sizeof(vmsg->cfg) == sizeof(create.config));
	vmsg = (struct vio_create_stream_msg *)vhdr;
	memcpy(&vmsg->cfg, &create.config, sizeof(vmsg->cfg));

	LOG_EAVB(LEVEL_DEBUG, "(%d:%d:%d)\n",
		fl->index, stream->index, msg->msgid);
	ret = send_msg(priv, msg);
	LOG_EAVB(LEVEL_DEBUG, "(%d:%d:%d) ret=%d\n",
		fl->index, stream->index, msg->msgid, ret);

	vhdr = (struct vio_msg_hdr *)msg->rxbuf;
	if (!ret && vhdr) {
		ret = vhdr->result;

		create.hdr.streamCtx = vhdr->streamctx_hdl;
		stream->hdl = vhdr->streamctx_hdl;
		stream->idx = vhdr->stream_idx;
		stream->status = CREATED;
		stream->fl = fl;
		LOG_EAVB(LEVEL_INFO, "stream%d (ctx 0x%llx, idx %d)\n",
			stream->index, vhdr->streamctx_hdl, vhdr->stream_idx);

		create_dbugfs_stream(stream);
	} else {
		reset_stream(stream);
	}

	virt_free_msg(priv, msg);

	if (copy_to_user(buf, &create, sizeof(create))) {
		LOG_EAVB(LEVEL_ERR, "copy_to_user failed\n");
		return -EFAULT;
	}

	return ret;
}

static int qavb_create_stream_with_path(struct eavb_file *fl, void __user *buf)
{
	struct virtio_eavb_priv *priv = fl->priv;
	struct fe_msg *msg;
	struct vio_msg_hdr *vhdr;
	struct vio_create_stream_path_msg *vmsg;
	struct eavb_ioctl_create_stream_with_path create;
	int tsize, rsize;
	struct stream *stream;
	int ret;

	if (copy_from_user(&create, buf, sizeof(create))) {
		LOG_EAVB(LEVEL_ERR, "copy_from_user failed\n");
		return -EFAULT;
	}

	LOG_EAVB(LEVEL_INFO, "fl->index=%d\n", fl->index);
	LOG_EAVB(LEVEL_INFO, "M - DRIVER EAVB FE create stream\n");

	stream = getStream(fl, 0);
	if (!stream) {
		LOG_EAVB(LEVEL_ERR, "stream full\n");
		return -EINVAL;
	}

	tsize = rsize = sizeof(struct vio_create_stream_path_msg);
	msg = virt_alloc_msg(priv, tsize, rsize);
	if (!msg) {
		LOG_EAVB(LEVEL_ERR, "stream%d alloc msg fail!\n",
			stream->index);
		reset_stream(stream);
		return -ENOMEM;
	}

	vhdr = (struct vio_msg_hdr *)msg->txbuf;
	vhdr->cmd = VIRTIO_EAVB_T_CREATE_STREAM_PATH;
	vhdr->len = msg->txbuf_size;

	vmsg = (struct vio_create_stream_path_msg *)vhdr;
	memcpy(vmsg->path, create.path, MAX_CONFIG_FILE_PATH);

	LOG_EAVB(LEVEL_DEBUG, "(%d:%d:%d)\n",
		fl->index, stream->index, msg->msgid);
	ret = send_msg(priv, msg);
	LOG_EAVB(LEVEL_DEBUG, "(%d:%d:%d) ret=%d\n",
		fl->index, stream->index, msg->msgid, ret);

	vhdr = (struct vio_msg_hdr *)msg->rxbuf;
	if (!ret && vhdr) {
		ret = vhdr->result;

		create.hdr.streamCtx = vhdr->streamctx_hdl;

		stream->hdl = vhdr->streamctx_hdl;
		stream->idx = vhdr->stream_idx;
		stream->status = CREATED;
		stream->fl = fl;
		LOG_EAVB(LEVEL_INFO, "stream%d (ctx 0x%llx, idx %d)\n",
			stream->index, vhdr->streamctx_hdl, vhdr->stream_idx);
		LOG_EAVB(LEVEL_INFO, "M - DRIVER EAVB FE create stream success\n");

		create_dbugfs_stream(stream);
	} else {
		reset_stream(stream);
	}

	virt_free_msg(priv, msg);

	if (copy_to_user(buf, &create, sizeof(create))) {
		LOG_EAVB(LEVEL_ERR, "copy_to_user failed\n");
		return -EFAULT;
	}

	return ret;
}

static int qavb_destroy_stream(struct eavb_file *fl, void __user *buf)
{
	struct eavb_ioctl_destroy_stream destroy;
	struct stream *stream;
	int ret;

	if (copy_from_user(&destroy, buf, sizeof(destroy))) {
		LOG_EAVB(LEVEL_ERR, "copy_from_user failed\n");
		return -EFAULT;
	}

	stream = getStream(fl, destroy.hdr.streamCtx);
	if (!stream) {
		LOG_EAVB(LEVEL_ERR, "invalid streamCtx=0x%llx\n",
			destroy.hdr.streamCtx);
		return -EINVAL;
	}

	ret = vio_eavb_destroy(fl, stream);

	return ret;
}

static int qavb_get_stream_info(struct eavb_file *fl, void __user *buf)
{
	struct virtio_eavb_priv *priv = fl->priv;
	struct fe_msg *msg;
	struct vio_msg_hdr *vhdr;
	struct eavb_ioctl_get_stream_info get_info;
	int tsize, rsize;
	struct stream *stream;
	int ret;

	if (copy_from_user(&get_info, buf, sizeof(get_info))) {
		LOG_EAVB(LEVEL_ERR, "copy_from_user failed\n");
		return -EFAULT;
	}

	LOG_EAVB(LEVEL_INFO, "streamCtx=0x%llx\n", get_info.hdr.streamCtx);
	LOG_EAVB(LEVEL_INFO, "M - DRIVER EAVB FE get stream info\n");

	stream = getStream(fl, get_info.hdr.streamCtx);
	if (!stream) {
		LOG_EAVB(LEVEL_ERR, "invalid streamCtx=0x%llx\n",
			get_info.hdr.streamCtx);
		return -EINVAL;
	}

	tsize = rsize = sizeof(struct vio_get_stream_info_msg);
	msg = virt_alloc_msg(priv, tsize, rsize);
	if (!msg) {
		LOG_EAVB(LEVEL_ERR, "stream%d alloc msg fail!\n",
			stream->index);
		return -ENOMEM;
	}

	fill_vmsg_hdr(msg, stream, VIRTIO_EAVB_T_GET_STREAM_INFO);
	vhdr = (struct vio_msg_hdr *)msg->txbuf;

	LOG_EAVB(LEVEL_DEBUG, "(%d:%d:%d)\n",
		fl->index, stream->index, msg->msgid);
	ret = send_msg(priv, msg);
	LOG_EAVB(LEVEL_DEBUG, "(%d:%d:%d) ret=%d\n",
		fl->index, stream->index, msg->msgid, ret);

	vhdr = (struct vio_msg_hdr *)msg->rxbuf;
	if (!ret && vhdr) {
		struct eavb_stream_info *info;

		ret = vhdr->result;
		info = (struct eavb_stream_info *)(vhdr + 1);
		ASSERT(sizeof(get_info.info) == sizeof(*info));
		memcpy(&get_info.info, info, sizeof(*info));

		LOG_EAVB(LEVEL_INFO, "M - DRIVER EAVB FE get stream info success\n");
	}

	virt_free_msg(priv, msg);
	if (copy_to_user(buf, &get_info, sizeof(get_info))) {
		LOG_EAVB(LEVEL_ERR, "copy_to_user failed\n");
		return -EFAULT;
	}

	return ret;
}

static int qavb_connect_stream(struct eavb_file *fl, void __user *buf)
{
	struct virtio_eavb_priv *priv = fl->priv;
	struct fe_msg *msg;
	struct vio_msg_hdr *vhdr;
	struct eavb_ioctl_connect_stream connect;
	int tsize, rsize;
	struct stream *stream;
	int ret;

	if (copy_from_user(&connect, buf, sizeof(connect))) {
		LOG_EAVB(LEVEL_ERR, "copy_from_user failed\n");
		return -EFAULT;
	}

	LOG_EAVB(LEVEL_INFO, "streamCtx=0x%llx\n", connect.hdr.streamCtx);
	LOG_EAVB(LEVEL_INFO, "M - DRIVER EAVB FE connect stream\n");

	stream = getStream(fl, connect.hdr.streamCtx);
	if (!stream) {
		LOG_EAVB(LEVEL_ERR, "invalid streamCtx=0x%llx\n",
			connect.hdr.streamCtx);
		return -EINVAL;
	}

	tsize = rsize = sizeof(struct vio_connect_stream_msg);
	msg = virt_alloc_msg(priv, tsize, rsize);
	if (!msg) {
		LOG_EAVB(LEVEL_ERR, "stream%d alloc msg fail!\n",
			stream->index);
		return -ENOMEM;
	}

	fill_vmsg_hdr(msg, stream, VIRTIO_EAVB_T_CONNECT_STREAM);
	vhdr = (struct vio_msg_hdr *)msg->txbuf;

	LOG_EAVB(LEVEL_DEBUG, "(%d:%d:%d)\n",
		fl->index, stream->index, msg->msgid);
	LOG_EAVB(LEVEL_DEBUG, "stream%d (ctx 0x%llx, idx %d)\n",
		stream->index, vhdr->streamctx_hdl, vhdr->stream_idx);
	ret = send_msg(priv, msg);
	LOG_EAVB(LEVEL_DEBUG, "(%d:%d:%d) ret=%d\n",
		fl->index, stream->index, msg->msgid, ret);

	vhdr = (struct vio_msg_hdr *)msg->rxbuf;
	if (!ret && vhdr) {
		ret = vhdr->result;
		stream->status = CONNECTED;
		LOG_EAVB(LEVEL_INFO, "M - DRIVER EAVB FE connect stream success\n");
	}

	virt_free_msg(priv, msg);

	return ret;
}

static int qavb_disconnect_stream(struct eavb_file *fl, void __user *buf)
{
	struct eavb_ioctl_disconnect_stream disconnect;
	struct stream *stream;
	int ret;

	if (copy_from_user(&disconnect, buf, sizeof(disconnect))) {
		LOG_EAVB(LEVEL_ERR, "copy_from_user failed\n");
		return -EFAULT;
	}

	LOG_EAVB(LEVEL_INFO, "streamCtx=0x%llx\n", disconnect.hdr.streamCtx);

	stream = getStream(fl, disconnect.hdr.streamCtx);
	if (!stream) {
		LOG_EAVB(LEVEL_ERR, "invalid streamCtx=0x%llx\n",
			disconnect.hdr.streamCtx);
		return -EINVAL;
	}

	ret = vio_eavb_disconnect(fl, stream);

	return ret;
}

static int qavb_receive(struct eavb_file *fl, void __user *buf)
{
	struct virtio_eavb_priv *priv = fl->priv;
	struct fe_msg *msg;
	struct vio_msg_hdr *vhdr;
	struct vio_receive_msg *vmsg;
	struct eavb_ioctl_receive receive;
	int tsize, rsize;
	struct mapping *mapping;
	struct stream *stream;
	int ret;

	if (copy_from_user(&receive, buf, sizeof(receive))) {
		LOG_EAVB(LEVEL_ERR, "copy_from_user failed\n");
		return -EFAULT;
	}

	mapping = get_file_mapping(fl, receive.data.pbuf,
				receive.data.hdr.payload_size);
	if (!mapping) {
		LOG_EAVB(LEVEL_ERR, "invalid buf address 0x%llx\n",
			receive.data.pbuf);
		return -EINVAL;
	}

	stream = getStream(fl, receive.hdr.streamCtx);
	if (!stream) {
		LOG_EAVB(LEVEL_ERR, "invalid streamCtx=0x%llx\n",
			receive.hdr.streamCtx);
		return -EINVAL;
	}

	tsize = rsize = sizeof(struct vio_receive_msg);
	msg = virt_alloc_msg(priv, tsize, rsize);
	if (!msg) {
		LOG_EAVB(LEVEL_ERR, "stream%d alloc msg fail!\n",
			stream->index);
		return -ENOMEM;
	}

	fill_vmsg_hdr(msg, stream, VIRTIO_EAVB_T_RECEIVE);
	vhdr = (struct vio_msg_hdr *)msg->txbuf;

	vmsg = (struct vio_receive_msg *)vhdr;
	ASSERT(sizeof(vmsg->data) == sizeof(receive.data));
	memcpy(&vmsg->data, &receive.data, sizeof(vmsg->data));

	vmsg->data.gpa = get_mapping_phyaddr(mapping, receive.data.pbuf);
	LOG_EAVB(LEVEL_DEBUG, "pass phy address 0x%llx\n", vmsg->data.gpa);
	LOG_EAVB(LEVEL_DEBUG, "stream%d (ctx 0x%llx, idx %d)\n",
		stream->index, vhdr->streamctx_hdl, vhdr->stream_idx);

	msg->ts_begin = local_clock();
	ret = send_msg(priv, msg);
	msg->ts_end = local_clock();
	records(stream, msg->msgid, msg->ts_begin, msg->ts_end);

	vhdr = (struct vio_msg_hdr *)msg->rxbuf;
	if (!ret && vhdr) {
		ret = vhdr->result;
		vmsg = (struct vio_receive_msg *)vhdr;
		receive.received = vmsg->received;
		memcpy(&receive.data, &vmsg->data,
				sizeof(struct eavb_buf_data));
		if (receive.received)
			LOG_EAVB(LEVEL_DEBUG, "M - DRIVER EAVB FE First received data\n");
	}

	virt_free_msg(priv, msg);

	if (copy_to_user(buf, &receive, sizeof(receive))) {
		LOG_EAVB(LEVEL_ERR, "copy_to_user failed\n");
		return -EFAULT;
	}
	return ret;
}

static int qavb_recv_done(struct eavb_file *fl, void __user *buf)
{
	return 0;
}

static int qavb_transmit(struct eavb_file *fl, void __user *buf)
{
	struct virtio_eavb_priv *priv = fl->priv;
	struct fe_msg *msg;
	struct vio_msg_hdr *vhdr;
	struct vio_transmit_msg *vmsg;
	struct eavb_ioctl_transmit transmit;
	int tsize, rsize;
	struct mapping *mapping;
	struct stream *stream;
	int ret;

	if (copy_from_user(&transmit, buf, sizeof(transmit))) {
		LOG_EAVB(LEVEL_ERR, "copy_from_user failed\n");
		return -EFAULT;
	}

	mapping = get_file_mapping(fl, transmit.data.pbuf,
				transmit.data.hdr.payload_size);
	if (!mapping) {
		LOG_EAVB(LEVEL_ERR, "invalid buf address 0x%llx\n",
			transmit.data.pbuf);
		return -EINVAL;
	}

	stream = getStream(fl, transmit.hdr.streamCtx);
	if (!stream) {
		LOG_EAVB(LEVEL_ERR, "invalid streamCtx=0x%llx\n",
			transmit.hdr.streamCtx);
		return -EINVAL;
	}

	tsize = rsize = sizeof(struct vio_transmit_msg);
	msg = virt_alloc_msg(priv, tsize, rsize);
	if (!msg) {
		LOG_EAVB(LEVEL_ERR, "stream%d alloc msg fail!\n",
			stream->index);
		return -ENOMEM;
	}

	fill_vmsg_hdr(msg, stream, VIRTIO_EAVB_T_TRANSMIT);
	vhdr = (struct vio_msg_hdr *)msg->txbuf;

	vmsg = (struct vio_transmit_msg *)vhdr;
	ASSERT(sizeof(vmsg->data) == sizeof(transmit.data));
	memcpy(&vmsg->data, &transmit.data, sizeof(vmsg->data));

	vmsg->mapping_size = mapping->size;
	vmsg->data.gpa = get_mapping_phyaddr(mapping, transmit.data.pbuf);
	LOG_EAVB(LEVEL_DEBUG, "pass phy address 0x%llx\n", vmsg->data.gpa);
	LOG_EAVB(LEVEL_DEBUG, "stream%d (ctx 0x%llx, idx %d)\n",
		stream->index, vhdr->streamctx_hdl, vhdr->stream_idx);

	ret = send_msg(priv, msg);

	vhdr = (struct vio_msg_hdr *)msg->rxbuf;
	if (!ret && vhdr) {
		ret = vhdr->result;
		vmsg = (struct vio_transmit_msg *)vhdr;
		transmit.written = vmsg->written;
	}

	virt_free_msg(priv, msg);

	if (copy_to_user(buf, &transmit, sizeof(transmit))) {
		LOG_EAVB(LEVEL_ERR, "copy_to_user failed\n");
		return -EFAULT;
	}
	return ret;
}

static int qavb_get_crf_ts(struct eavb_file *fl, void __user *buf)
{
	struct virtio_eavb_priv *priv = fl->priv;
	u64 value;

	init_completion(&priv->crf_ts_update);
	wait_for_completion(&priv->crf_ts_update);
	value = priv->crf_ts;
	if (copy_to_user(buf, &value, sizeof(value))) {
		LOG_EAVB(LEVEL_ERR, "copy_to_user failed\n");
		return -EFAULT;
	}
	return 0;
}

void update_crf_ts(struct virtio_eavb_priv *priv, u64 value)
{
	LOG_EAVB(LEVEL_DEBUG, "update crf clock %llu\n", value);
	priv->crf_ts = value;
	complete_all(&priv->crf_ts_update);
}

static long virtio_eavb_ioctl(struct file *file, unsigned int ioctl_cmd,
			unsigned long ioctl_param)
{
	struct eavb_file *fl = file->private_data;
	void __user *buf = (void __user *)ioctl_param;
	int ret = 0;

	switch (ioctl_cmd) {
	case EAVB_IOCTL_CREATE_STREAM:
		ret = qavb_create_stream(fl, buf);
		break;
	case EAVB_IOCTL_CREATE_STREAM_WITH_PATH:
		ret = qavb_create_stream_with_path(fl, buf);
		break;
	case EAVB_IOCTL_GET_STREAM_INFO:
		ret = qavb_get_stream_info(fl, buf);
		break;
	case EAVB_IOCTL_CONNECT_STREAM:
		ret = qavb_connect_stream(fl, buf);
		break;
	case EAVB_IOCTL_RECEIVE:
		ret = qavb_receive(fl, buf);
		break;
	case EAVB_IOCTL_RECV_DONE:
		ret = qavb_recv_done(fl, buf);
		break;
	case EAVB_IOCTL_TRANSMIT:
		ret = qavb_transmit(fl, buf);
		break;
	case EAVB_IOCTL_DISCONNECT_STREAM:
		ret = qavb_disconnect_stream(fl, buf);
		break;
	case EAVB_IOCTL_DESTROY_STREAM:
		ret = qavb_destroy_stream(fl, buf);
		break;
	case EAVB_IOCTL_GET_CRF_TS:
		ret = qavb_get_crf_ts(fl, buf);
		break;
	default:
		ret = -ENOTTY;
		LOG_EAVB(LEVEL_ERR, "unsupported ioctl 0x%x\n", ioctl_cmd);

	}

	return ret;
}

static long virtio_eavb_compat_ioctl(struct file *file,
				unsigned int ioctl_cmd,
				unsigned long ioctl_param)
{
	return -EOPNOTSUPP;
}

static int virtio_eavb_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct eavb_file *fl = file->private_data;
	int ret = 0;
	unsigned long vsize;
	u8 *buf;

	spin_lock(&fl->mappinglock);
	if (fl->mapping_count < MAPPING_MAX)
		fl->mapping_count++;
	else
		ret = -EBUSY;

	spin_unlock(&fl->mappinglock);
	if (ret) {
		LOG_EAVB(LEVEL_ERR, "Too many mapping (%d):fl->index %d\n",
			fl->mapping_count, fl->index);
		return ret;
	}

	vsize = vma->vm_end - vma->vm_start;
	LOG_EAVB(LEVEL_DEBUG, "vsize = %ld(0x%lx~0x%lx)\n",
		vsize, vma->vm_start, vma->vm_end);
	buf = kmalloc(vsize, GFP_KERNEL);
	if (!buf) {
		spin_lock(&fl->mappinglock);
		fl->mapping_count--;
		spin_unlock(&fl->mappinglock);
		LOG_EAVB(LEVEL_ERR, "kmalloc failed\n");
		return -ENOMEM;
	}
	vm_flags_set(vma, VM_IO);
	vm_flags_set(vma, VM_LOCKED);

	if (remap_pfn_range(vma, vma->vm_start,
		virt_to_phys(buf)>>PAGE_SHIFT, vsize, vma->vm_page_prot)) {
		LOG_EAVB(LEVEL_ERR, "remap pfn range failed\n");
		spin_lock(&fl->mappinglock);
		fl->mapping_count--;
		spin_unlock(&fl->mappinglock);
		ret = -EAGAIN;
	}
	memset(buf, 0, vsize);

	add_mapping_info(fl, (u64)buf, vma->vm_start, virt_to_phys(buf), vsize);

	return ret;
}

static const struct file_operations fops = {
	.open = virtio_eavb_open,
	.release = virtio_eavb_release,
	.unlocked_ioctl = virtio_eavb_ioctl,
	.compat_ioctl = virtio_eavb_compat_ioctl,
	.mmap = virtio_eavb_mmap,
};

static void fe_recv_done(struct virtqueue *rvq)
{
	struct virtio_eavb_priv *priv = rvq->vdev->priv;
	struct vio_msg_hdr *rxvhdr, *rsp;
	struct fe_msg *msg;
	unsigned int len;
	unsigned long flags;
#ifdef EAVB_DEBUGFS
	int index;
#endif

	while (1) {
		spin_lock_irqsave(&priv->rvqlock, flags);
		rxvhdr = virtqueue_get_buf(rvq, &len);
		spin_unlock_irqrestore(&priv->rvqlock, flags);

		if (!rxvhdr)
			break;

		ASSERT(len == RX_BUF_MAX_LEN);
		if (len < rxvhdr->len) {
			LOG_EAVB(LEVEL_ERR,
				"rxvhdr msgid %d len %d > received len %d!\n",
				rxvhdr->msgid, rxvhdr->len, len);
			continue;
		}

		if (rxvhdr->cmd == VIRTIO_EAVB_T_UPDATE_CLK) {
			struct vio_update_clk_msg *vmsg;

			vmsg = (struct vio_update_clk_msg *)rxvhdr;
			update_crf_ts(priv, vmsg->clk);
			reclaim_rxbuf(priv, rxvhdr);
			continue;
		}

		rsp = rxvhdr;
		if (rsp->msgid > FE_MSG_MAX - 1) {
			LOG_EAVB(LEVEL_ERR, "rsp msgid %d is invalid!\n",
				rsp->msgid);
			continue;
		}

		msg = priv->msgtable[rsp->msgid];
		if (!msg) {
			LOG_EAVB(LEVEL_ERR, "rsp msgid %d found no msg!\n",
				rsp->msgid);
			continue;
		}
		if (msg->msgid != rsp->msgid) {
			LOG_EAVB(LEVEL_ERR, "rsp msgid %d mismatch %d!\n",
				rsp->msgid, msg->msgid);
			continue;
		}

		msg->rxbuf = (void *)rsp;
		msg->rxbuf_used = 1;
		LOG_EAVB(LEVEL_DEBUG, "msgid %d rxbuf used\n", msg->msgid);
#ifdef EAVB_DEBUGFS
		index = priv->next_rxrecords_index & 0x1f;
		priv->rxrecords[index].msgid = msg->msgid;
		priv->rxrecords[index].ts = local_clock();
		priv->next_rxrecords_index++;
#endif
		complete(&msg->work);
	}
}

static int init_vqs(struct virtio_eavb_priv *priv)
{
	struct virtqueue *vqs[2];
	static const char *const names[] = { "eavb_tx", "eavb_rx" };
	vq_callback_t *cbs[] = {NULL, fe_recv_done};
	int ret;

	ret = virtio_find_vqs(priv->vdev, 2, vqs, cbs, names, NULL);
	if (ret) {
		LOG_EAVB(LEVEL_ERR, "virtio_find_vqs fail\n");
		return ret;
	}

	priv->svq = vqs[0];
	priv->rvq = vqs[1];

	return 0;
}

static int virtio_eavb_probe(struct virtio_device *vdev)
{
	int ret;
	struct device *dev = NULL;
	struct virtio_eavb_priv *priv;
	int i;

	LOG_EAVB(LEVEL_DEBUG, "\n");
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto fail;
	}

	if (virtio_has_feature(vdev, VIRTIO_EAVB_F_SHMEM))
		priv->has_shmem = true;

	mutex_init(&priv->lock);
	spin_lock_init(&priv->msglock);
	spin_lock_init(&priv->rvqlock);
	INIT_WORK(&priv->reclaim_work, reclaim_function);
	init_completion(&priv->crf_ts_update);

	vdev->priv = priv;
	priv->vdev = vdev;
	priv->dev = vdev->dev.parent;
	priv->name = "eavb";

	ret = init_vqs(priv);
	if (ret)
		goto free_priv;

	virtio_device_ready(vdev);

	ret = alloc_chrdev_region(&priv->dev_no, 0, DEVICE_NUM, DEVICE_NAME);
	if (ret)
		goto free_priv;

	cdev_init(&priv->cdev, &fops);
	priv->cdev.owner = THIS_MODULE;
	ret = cdev_add(&priv->cdev, MKDEV(MAJOR(priv->dev_no), 0), DEVICE_NUM);
	if (ret)
		goto free_chrdev;

	priv->class = class_create(THIS_MODULE, "virt-eavb");
	if (IS_ERR(priv->class))
		goto class_create_fail;

	dev = device_create(priv->class, NULL,
			MKDEV(MAJOR(priv->dev_no), MINOR_NUM_DEV),
			NULL, DEVICE_NAME);
	if (IS_ERR_OR_NULL(dev))
		goto device_create_fail;

	priv->rxbufs[0] = kmalloc(FE_MSG_MAX * RX_BUF_MAX_LEN, GFP_KERNEL);
	if (!priv->rxbufs[0]) {
		ret = -ENOMEM;
		goto alloc_rxbufs_fail;
	}

	for (i = 0; i < FE_MSG_MAX; i++) {
		struct scatterlist sg;
		u8 *rxbuf;

		rxbuf = priv->rxbufs[0] + i * RX_BUF_MAX_LEN;
		priv->rxbufs[i] = rxbuf;

		sg_init_one(&sg, rxbuf, RX_BUF_MAX_LEN);
		ret = virtqueue_add_inbuf(priv->rvq, &sg, 1, rxbuf, GFP_KERNEL);
		WARN_ON(ret);
	}
	virtqueue_disable_cb(priv->svq);

	virtqueue_enable_cb(priv->rvq);
	virtqueue_kick(priv->rvq);

#ifdef EAVB_DEBUGFS
	priv->debugfs_root = debugfs_create_dir("eavb", NULL);
	debugfs_create_file("log", 00400 | 00200,
			priv->debugfs_root, priv,
			&fops_debugfs_log);
	debugfs_create_file("timeout", 00400 | 00200,
			priv->debugfs_root, NULL,
			&fops_debugfs_timeout);
#endif
	LOG_EAVB(LEVEL_INFO, "M - DRIVER EAVB FE Ready\n");
	return 0;

alloc_rxbufs_fail:
	device_destroy(priv->class, MKDEV(MAJOR(priv->dev_no), MINOR_NUM_DEV));
device_create_fail:
	if (!IS_ERR_OR_NULL(dev))
		device_destroy(priv->class, MKDEV(MAJOR(priv->dev_no),
				MINOR_NUM_DEV));
	class_destroy(priv->class);
class_create_fail:
	cdev_del(&priv->cdev);
free_chrdev:
	unregister_chrdev_region(priv->dev_no, DEVICE_NUM);
free_priv:
	kfree(priv);
fail:
	return ret;
}

static void virtio_eavb_remove(struct virtio_device *vdev)
{
	struct virtio_eavb_priv *priv;

	LOG_EAVB(LEVEL_DEBUG, "\n");
	priv = vdev->priv;

#ifdef EAVB_DEBUGFS
	debugfs_remove_recursive(priv->debugfs_root);
#endif
	device_destroy(priv->class, MKDEV(MAJOR(priv->dev_no), MINOR_NUM_DEV));
	class_destroy(priv->class);
	cdev_del(&priv->cdev);
	unregister_chrdev_region(priv->dev_no, DEVICE_NUM);

	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);
	kfree(priv->rxbufs[0]);
	kfree(priv);
}


static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_EAVB, VIRTIO_DEV_ANY_ID },
	{ VIRTIO_ID_EAVB_BC, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	VIRTIO_EAVB_F_SHMEM,
};

static struct virtio_driver virtio_eavb_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = id_table,
	.probe = virtio_eavb_probe,
	.remove = virtio_eavb_remove,
};

static int __init virtio_eavb_init(void)
{
	LOG_EAVB(LEVEL_INFO, "M - DRIVER EAVB FE Init\n");
	return register_virtio_driver(&virtio_eavb_driver);
}

static void __exit virtio_eavb_exit(void)
{
	unregister_virtio_driver(&virtio_eavb_driver);
}

module_init(virtio_eavb_init);
module_exit(virtio_eavb_exit);

MODULE_DESCRIPTION("Virtio eavb driver");
MODULE_LICENSE("GPL");
