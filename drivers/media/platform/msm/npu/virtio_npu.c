// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/uaccess.h>
#include <linux/bitmap.h>
#include <linux/msm_npu.h>

#define NPU_ERR(fmt, args...)                            \
	pr_err("NPU_ERR: %s: %d " fmt, __func__,  __LINE__, ##args)
#define NPU_WARN(fmt, args...)                           \
	pr_warn("NPU_WARN: %s: %d " fmt, __func__,  __LINE__, ##args)
#define NPU_INFO(fmt, args...)                           \
	pr_info("NPU_INFO: %s: %d " fmt, __func__,  __LINE__, ##args)
#define NPU_DBG(fmt, args...)                           \
	pr_debug("NPU_DBG: %s: %d " fmt, __func__,  __LINE__, ##args)

#define CLASS_NAME              "npu"
#define DRIVER_NAME             "msm_npu"

/* indicates multi-buffer per command is supported */
#define VIRTIO_NPU_F_MULTI_BUF	1

#define VIRTIO_ID_NPU           0xC005

#define NPU_MAX_STATS_BUF_SIZE	16384
#define NPU_MAX_PATCH_NUM       160

#define NPU_MSG_MAX             256
#define MAX_NPU_BUF_SIZE        (32*1024)
#define MAX_LOADED_NETWORK      32
#define MAX_DEBUGFS_PARAM_NUM   4
#define MAX_BIST_PARAM_NUM      4
#define MAX_NPU_POWER_LVL_NUM   6

#define VIRTIO_NPU_CMD_OPEN                     1
#define VIRTIO_NPU_CMD_CLOSE                    2
#define VIRTIO_NPU_CMD_GET_INFO                 3
#define VIRTIO_NPU_CMD_MMAP                     4
#define VIRTIO_NPU_CMD_MUNMAP                   5
#define VIRTIO_NPU_CMD_LOAD_NETWORK             6
#define VIRTIO_NPU_CMD_LOAD_NETWORK_V2          7
#define VIRTIO_NPU_CMD_UNLOAD_NETWORK           8
#define VIRTIO_NPU_CMD_EXEC_NETWORK             9
#define VIRTIO_NPU_CMD_EXEC_NETWORK_V2          10
#define VIRTIO_NPU_CMD_SET_PROPERTY             11
#define VIRTIO_NPU_CMD_GET_PROPERTY             12
#define VIRTIO_NPU_CMD_DEBUGFS                  13
#define VIRTIO_NPU_CMD_BIST                     14

#define VIRTIO_NPU_VERSION_MAJOR	1
#define VIRTIO_NPU_VERSION_MINOR	1

/* debugfs sub command */
enum npu_debugfs_subcmd {
	DEBUGFS_SUBCMD_CTRL_ON = 0x100,
	DEBUGFS_SUBCMD_CTRL_OFF,
	DEBUGFS_SUBCMD_CTRL_SSR,
	DEBUGFS_SUBCMD_CTRL_SSR_WDT,
	DEBUGFS_SUBCMD_CTRL_LOOPBACK,
};

/* bist sub command */
enum npu_bist_subcmd {
	BIST_SUBCMD_LOOPBACK = 0x100,
};

struct virt_msg_hdr {
	u16 major_version;
	u16 minor_version;
	u32 pid;	/* GVM pid */
	u32 tid;	/* GVM tid */
	s32 cid;	/* channel id connected to DSP */
	u32 cmd;	/* command type */
	u32 len;	/* command length */
	u32 msgid;	/* unique message id */
	u32 result;	/* message return value */
} __packed;

struct virt_npu_msg {
	struct completion work;
	u32 msgid;
	void *txbuf;
	u32 tx_buf_num;
	void *rxbuf;
	u32 rx_buf_num;
};

struct virt_npu_buf {
	u64 pv;		/* buffer physical address, 0 for non-ION buffer */
	u64 len;	/* buffer length */
};

struct virt_debugfs_msg {
	struct virt_msg_hdr hdr;	/* virtio npu message header */
	u32 subcmd;		/* subcmd */
	u32 num_of_params;
	u32 params[MAX_DEBUGFS_PARAM_NUM];	/* parameters */
} __packed;

struct virt_bist_msg {
	struct virt_msg_hdr hdr;	/* virtio npu message header */
	u32 subcmd;					/* subcmd */
	u32 num_of_params;
	u32 params[MAX_BIST_PARAM_NUM];				/* parameters */
} __packed;

struct virt_mmap_msg {
	struct virt_msg_hdr hdr;	/* virtio npu message header */
	u32 nents;			/* number of map entries */
	u32 flags;			/* mmap flags */
	u64 size;			/* mmap length */
	u64 vnpu;			/* npu address */
	struct virt_npu_buf sgl[0]; /* sg list */
} __packed;

struct virt_munmap_msg {
	struct virt_msg_hdr hdr;	/* virtio npu message header */
	u64 vnpu;			/* npu address */
	u64 size;			/* mmap length */
} __packed;

struct virt_patch_info_v2 {
	/* patch value */
	uint32_t value;
	/* chunk id */
	uint32_t chunk_id;
	/* instruction size in bytes */
	uint32_t instruction_size_in_bytes;
	/* variable size in bits */
	uint32_t variable_size_in_bits;
	/* shift value in bits */
	uint32_t shift_value_in_bits;
	/* location offset */
	uint32_t loc_offset;
};

struct virt_load_network_v2_msg {
	struct virt_msg_hdr hdr;	/* virtio npu message header */

	/* original struct msm_npu_load_network_ioctl_v2 */
	/* physical address */
	uint64_t buf_phys_addr;
	/* buffer ion handle */
	int32_t buf_ion_hdl;
	/* buffer size */
	uint32_t buf_size;
	/* first block size */
	uint32_t first_block_size;
	/* load flags */
	uint32_t flags;
	/* network handle */
	uint32_t network_hdl;
	/* priority */
	uint32_t priority;
	/* perf mode */
	uint32_t perf_mode;
	/* number of layers in the network */
	uint32_t num_layers;
	/* number of layers to be patched */
	uint32_t patch_info_num;
	/* reserved */
	uint32_t reserved;
	/* patch info(v2) for all input/output layers */
	struct virt_patch_info_v2 patch_info[0];
} __packed;

struct virt_unload_network_msg {
	struct virt_msg_hdr hdr;	/* virtio npu message header */

	/* network handle */
	uint32_t network_hdl;
} __packed;


struct virt_property_msg {
	struct virt_msg_hdr hdr;	/* virtio npu message header */

	/* original struct msm_npu_property */
	uint32_t prop_id;
	uint32_t num_of_params;
	uint32_t network_hdl;
	uint32_t prop_param[PROP_PARAM_MAX_SIZE];
} __packed;

struct virt_patch_buf_info {
	/* physical address to be patched */
	uint64_t buf_phys_addr;
	/* buffer id */
	uint32_t buf_id;
	uint32_t reserved;
};

struct virt_exec_network_v2_msg {
	struct virt_msg_hdr hdr;	/* virtio npu message header */

	/* original struct msm_npu_exec_network_ioctl_v2 */
	/* network handle */
	uint32_t network_hdl;
	/* asynchronous execution */
	uint32_t async;
	/* execution flags */
	uint32_t flags;
	/* stats buffer offset to be filled with execution stats */
	uint32_t stats_buf_off;
	/* stats buf size allocated */
	uint32_t stats_buf_size;
	/* number of layers to be patched */
	uint32_t patch_buf_info_num;
	/* reserved */
	uint32_t reserved[2];
	/* patch info(v2) for all input/output layers */
	struct virt_patch_buf_info patch_buf_info[0];
	/* stats buf at the end */
} __packed;

struct virt_get_info_msg {
	struct virt_msg_hdr hdr;	/* virtio npu message header */

	/* original struct msm_npu_get_info_ioctl */
	/* firmware version */
	uint32_t firmware_version;
	/* reserved */
	uint32_t flags;
} __packed;

struct npu_ion_buf {
	int fd;
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *attachment;
	struct sg_table *table;
	dma_addr_t iova;
	uint32_t size;
	void *phys_addr;
	void *buf;
	struct list_head list;
};

struct npu_client {
	struct npu_device *npu_dev;

	int tgid;
	int cid;

	struct mutex list_lock;
	/* mapped buffer list */
	struct list_head mapped_buffer_list;
};

struct npu_network {
	uint32_t network_hdl;
	struct npu_client *client;
	bool valid;
};

struct npu_pwrctrl {
	uint32_t num_pwrlevels;
	uint32_t perf_mode_override;
	uint32_t dcvs_mode;
};

struct npu_buf_mgr {
	void *bufs;
	unsigned int buf_size;
	unsigned int total_buf_num;
	unsigned int free_buf_num;
	unsigned long *bitmap;
};

struct npu_device {
	struct virtio_device *vdev;
	struct virtqueue *rvq;
	struct virtqueue *svq;
	void *bufs;
	unsigned int order;
	unsigned int num_descs;
	unsigned int num_bufs;
	unsigned int buf_size;
	struct npu_buf_mgr rbufs;
	struct npu_buf_mgr sbufs;

	struct mutex lock;
	spinlock_t vq_lock;

	struct device *device;
	struct cdev cdev;
	struct class *class;
	dev_t dev_num;

	struct npu_pwrctrl pwrctrl;

	int32_t network_num;
	struct npu_network networks[MAX_LOADED_NETWORK];

	spinlock_t msglock;
	struct virt_npu_msg *msgtable[NPU_MSG_MAX];

	struct dentry *debugfs_root;
};

/* -------------------------------------------------------------------------
 * IOCTL Implementations
 * -------------------------------------------------------------------------
 */

static int npu_add_network(struct npu_client *client, uint32_t network_hdl)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct npu_network *network = NULL;
	int i, rc = 0;

	mutex_lock(&npu_dev->lock);
	if (npu_dev->network_num == MAX_LOADED_NETWORK) {
		NPU_ERR("No free network available\n");
		rc = -ENOMEM;
		goto fail;
	}

	for (i = 0; i < MAX_LOADED_NETWORK; i++) {
		network = &npu_dev->networks[i];
		if (!network->valid)
			break;
	}

	network->client = client;
	network->network_hdl = network_hdl;
	network->valid = true;
	npu_dev->network_num++;
	NPU_DBG("loaded network num %d\n", npu_dev->network_num);
fail:
	mutex_unlock(&npu_dev->lock);
	return rc;
}

static int npu_remove_network(struct npu_client *client, uint32_t network_hdl)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct npu_network *network = NULL;
	int i;

	mutex_lock(&npu_dev->lock);
	for (i = 0; i < MAX_LOADED_NETWORK; i++) {
		network = &npu_dev->networks[i];
		if (network->valid && (network->client == client) &&
			(network->network_hdl == network_hdl))
			break;
	}

	if (i == MAX_LOADED_NETWORK) {
		NPU_ERR("can't find network %x\n", network_hdl);
		goto fail;
	}

	network->client = NULL;
	network->network_hdl = 0;
	network->valid = false;
	npu_dev->network_num--;
	NPU_DBG("loaded network num  %d\n", npu_dev->network_num);
fail:
	mutex_unlock(&npu_dev->lock);
	return 0;
}

static bool npu_validate_network(struct npu_client *client,
	uint32_t network_hdl)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct npu_network *network = NULL;
	int i;
	bool rc = false;

	mutex_lock(&npu_dev->lock);
	for (i = 0; i < MAX_LOADED_NETWORK; i++) {
		network = &npu_dev->networks[i];
		if (network->valid && (network->client == client) &&
			(network->network_hdl == network_hdl)) {
			rc = true;
			break;
		}
	}
	mutex_unlock(&npu_dev->lock);
	return rc;
}

static void *get_tx_buf(struct npu_device *npu_dev, uint32_t *buf_num)
{
	struct npu_buf_mgr *buf_mgr = &npu_dev->sbufs;
	void *ret = NULL;
	int order, idx;

	/* support multiple concurrent senders */
	mutex_lock(&npu_dev->lock);
	if (buf_mgr->free_buf_num < *buf_num) {
		NPU_ERR("No enough free tx buffer\n");
		goto exit;
	}

	order = get_order(*buf_num);
	idx = bitmap_find_free_region(buf_mgr->bitmap,
		buf_mgr->total_buf_num, order);
	if (idx < 0) {
		NPU_ERR("can't find free region in bitmap order %d\n",
			order);
		goto exit;
	}
	ret = buf_mgr->bufs + npu_dev->buf_size * idx;
	buf_mgr->free_buf_num -= (1 << order);
	*buf_num = 1 << order;
	NPU_DBG("Get tx buffer %d:%d, total free num %d\n", idx, *buf_num,
		buf_mgr->free_buf_num);
exit:
	mutex_unlock(&npu_dev->lock);
	return ret;
}

static void put_tx_buf(struct npu_device *npu_dev, void *buf, uint32_t buf_num)
{
	struct npu_buf_mgr *buf_mgr = &npu_dev->sbufs;
	int order, idx;

	order = get_order(buf_num);
	idx = (buf - buf_mgr->bufs) / npu_dev->buf_size;

	mutex_lock(&npu_dev->lock);

	bitmap_release_region(buf_mgr->bitmap, idx, order);
	buf_mgr->free_buf_num += (1 << order);

	NPU_DBG("tx buffer freed %d:%d, total free num %d\n", idx,
		buf_num, buf_mgr->free_buf_num);
	mutex_unlock(&npu_dev->lock);
}

static struct virt_npu_msg *virt_alloc_msg(struct npu_device *npu_dev,
	uint32_t size)
{
	struct virt_npu_msg *msg;
	struct virt_msg_hdr *hdr;
	void *buf;
	unsigned long flags;
	uint32_t buf_num = 1;
	int i;

	if (size > npu_dev->buf_size) {
		buf_num = (size + npu_dev->buf_size - 1) / npu_dev->buf_size;
		NPU_WARN("message is %d, %d buffers required\n",
			size, buf_num);
	}

	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return NULL;

	buf = get_tx_buf(npu_dev, &buf_num);
	if (!buf) {
		NPU_ERR("can't get tx  buffer\n");
		kfree(msg);
		return NULL;
	}

	msg->txbuf = buf;
	msg->tx_buf_num = buf_num;
	init_completion(&msg->work);
	spin_lock_irqsave(&npu_dev->msglock, flags);
	for (i = 0; i < NPU_MSG_MAX; i++) {
		if (!npu_dev->msgtable[i]) {
			npu_dev->msgtable[i] = msg;
			msg->msgid = i;
			break;
		}
	}
	spin_unlock_irqrestore(&npu_dev->msglock, flags);

	if (i == NPU_MSG_MAX) {
		NPU_ERR("message queue is full\n");
		put_tx_buf(npu_dev, buf, buf_num);
		kfree(msg);
		return NULL;
	}

	hdr = (struct virt_msg_hdr *)buf;
	hdr->major_version = VIRTIO_NPU_VERSION_MAJOR;
	hdr->minor_version = VIRTIO_NPU_VERSION_MINOR;

	return msg;
}

static void virt_free_msg(struct npu_device *npu_dev, struct virt_npu_msg *msg)
{
	unsigned long flags;

	spin_lock_irqsave(&npu_dev->msglock, flags);
	if (npu_dev->msgtable[msg->msgid] == msg) {
		npu_dev->msgtable[msg->msgid] = NULL;
	} else {
		NPU_ERR("can't find msg %d in table\n", msg->msgid);
		spin_unlock_irqrestore(&npu_dev->msglock, flags);
		return;
	}
	spin_unlock_irqrestore(&npu_dev->msglock, flags);

	put_tx_buf(npu_dev, msg->txbuf, msg->tx_buf_num);
	kfree(msg);
}

static int32_t virt_npu_get_info(struct npu_client *client,
			struct msm_npu_get_info_ioctl *get_info_ioctl)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct virt_get_info_msg *vmsg, *rsp = NULL;
	struct virt_npu_msg *msg;
	struct scatterlist sg[1];
	unsigned long flags;
	int rc;

	msg = virt_alloc_msg(npu_dev, sizeof(*vmsg));
	if (!msg)
		return -ENOMEM;

	vmsg = (struct virt_get_info_msg *)msg->txbuf;
	vmsg->hdr.pid = client->tgid;
	vmsg->hdr.tid = current->pid;
	vmsg->hdr.cid = client->cid;
	vmsg->hdr.cmd = VIRTIO_NPU_CMD_GET_INFO;
	vmsg->hdr.len = sizeof(*vmsg);
	vmsg->hdr.msgid = msg->msgid;
	vmsg->hdr.result = 0xffffffff;
	sg_init_one(sg, vmsg, sizeof(*vmsg));

	spin_lock_irqsave(&npu_dev->vq_lock, flags);
	rc = virtqueue_add_outbuf(npu_dev->svq, sg, 1, vmsg, GFP_KERNEL);
	if (rc) {
		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
		NPU_ERR("fail to add output buffer\n");
		goto fail;
	}

	virtqueue_kick(npu_dev->svq);
	spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
	wait_for_completion(&msg->work);

	rsp = msg->rxbuf;
	rc = rsp->hdr.result;
	if (rc)
		goto fail;

	get_info_ioctl->firmware_version = vmsg->firmware_version;
	get_info_ioctl->flags = vmsg->flags;

fail:
	if (rsp) {
		sg_init_one(sg, rsp, npu_dev->buf_size);

		/* add the buffer back to the remote processor's virtqueue */
		spin_lock_irqsave(&npu_dev->vq_lock, flags);

		if (virtqueue_add_inbuf(npu_dev->rvq, sg, 1, rsp, GFP_KERNEL))
			NPU_ERR("fail to add input buffer\n");
		else
			virtqueue_kick(npu_dev->rvq);

		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
	}
	virt_free_msg(npu_dev, msg);

	return rc;
}

static int npu_get_info(struct npu_client *client, unsigned long arg)
{
	struct msm_npu_get_info_ioctl req;
	void __user *argp = (void __user *)arg;
	int rc = 0;

	rc = copy_from_user(&req, argp, sizeof(req));
	if (rc) {
		NPU_ERR("fail to copy from user\n");
		return -EFAULT;
	}

	rc = virt_npu_get_info(client, &req);
	if (rc) {
		NPU_ERR("npu_host_get_info failed\n");
		return rc;
	}

	rc = copy_to_user(argp, &req, sizeof(req));
	if (rc) {
		NPU_ERR("fail to copy to user\n");
		return -EFAULT;
	}
	return 0;
}

static struct npu_ion_buf *npu_alloc_npu_ion_buffer(struct npu_client
	*client, int buf_hdl, uint32_t size)
{
	struct npu_ion_buf *ret_val = NULL, *tmp;
	struct list_head *pos = NULL;

	mutex_lock(&client->list_lock);
	list_for_each(pos, &(client->mapped_buffer_list)) {
		tmp = list_entry(pos, struct npu_ion_buf, list);
		if (tmp->fd == buf_hdl) {
			ret_val = tmp;
			break;
		}
	}

	if (ret_val) {
		/* mapped already, treat as invalid request */
		NPU_ERR("ion buf has been mapped\n");
		ret_val = NULL;
	} else {
		ret_val = kzalloc(sizeof(*ret_val), GFP_KERNEL);
		if (ret_val) {
			ret_val->fd = buf_hdl;
			ret_val->size = size;
			ret_val->iova = 0;
			list_add(&(ret_val->list),
				&(client->mapped_buffer_list));
		}
	}
	mutex_unlock(&client->list_lock);

	return ret_val;
}

static struct npu_ion_buf *npu_get_npu_ion_buffer(struct npu_client
	*client, int buf_hdl)
{
	struct list_head *pos = NULL;
	struct npu_ion_buf *ret_val = NULL, *tmp;

	mutex_lock(&client->list_lock);
	list_for_each(pos, &(client->mapped_buffer_list)) {
		tmp = list_entry(pos, struct npu_ion_buf, list);
		if (tmp->fd == buf_hdl) {
			ret_val = tmp;
			break;
		}
	}
	mutex_unlock(&client->list_lock);

	return ret_val;
}

static void npu_free_npu_ion_buffer(struct npu_client
	*client, int buf_hdl)
{
	struct list_head *pos = NULL;
	struct npu_ion_buf *npu_ion_buf = NULL;

	mutex_lock(&client->list_lock);
	list_for_each(pos, &(client->mapped_buffer_list)) {
		npu_ion_buf = list_entry(pos, struct npu_ion_buf, list);
		if (npu_ion_buf->fd == buf_hdl) {
			list_del(&npu_ion_buf->list);
			kfree(npu_ion_buf);
			break;
		}
	}
	mutex_unlock(&client->list_lock);
}

static int virt_npu_munmap(struct npu_client *client, uint64_t iova,
				size_t size)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct virt_munmap_msg *vmsg, *rsp = NULL;
	struct virt_npu_msg *msg;
	struct scatterlist sg[1];
	int rc;
	unsigned long flags;

	msg = virt_alloc_msg(npu_dev, sizeof(*vmsg));
	if (!msg)
		return -ENOMEM;

	NPU_DBG("Unmap buffer %x\n", iova);
	vmsg = (struct virt_munmap_msg *)msg->txbuf;
	vmsg->hdr.pid = client->tgid;
	vmsg->hdr.tid = current->pid;
	vmsg->hdr.cid = client->cid;
	vmsg->hdr.cmd = VIRTIO_NPU_CMD_MUNMAP;
	vmsg->hdr.len = sizeof(*vmsg);
	vmsg->hdr.msgid = msg->msgid;
	vmsg->hdr.result = 0xffffffff;
	vmsg->vnpu = iova;
	vmsg->size = size;
	sg_init_one(sg, vmsg, sizeof(*vmsg));

	spin_lock_irqsave(&npu_dev->vq_lock, flags);
	rc = virtqueue_add_outbuf(npu_dev->svq, sg, 1, vmsg, GFP_KERNEL);
	if (rc) {
		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
		NPU_ERR("fail to add output buffer\n");
		goto fail;
	}

	virtqueue_kick(npu_dev->svq);
	spin_unlock_irqrestore(&npu_dev->vq_lock, flags);

	wait_for_completion(&msg->work);

	rsp = msg->rxbuf;
	rc = rsp->hdr.result;
fail:
	if (rsp) {
		sg_init_one(sg, rsp, npu_dev->buf_size);

		/* add the buffer back to the remote processor's virtqueue */
		spin_lock_irqsave(&npu_dev->vq_lock, flags);

		if (virtqueue_add_inbuf(npu_dev->rvq, sg, 1, rsp, GFP_KERNEL))
			NPU_ERR("fail to add input buffer\n");
		else
			virtqueue_kick(npu_dev->rvq);

		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);

	}
	virt_free_msg(npu_dev, msg);

	return rc;
}

static int32_t virt_npu_unmap_buf(struct npu_client *client,
		 int buf_hdl,  uint64_t iova)
{
	MODULE_IMPORT_NS(DMA_BUF);
	struct npu_ion_buf *ion_buf;

	/* clear entry and retrieve the corresponding buffer */
	ion_buf = npu_get_npu_ion_buffer(client, buf_hdl);
	if (!ion_buf) {
		NPU_ERR("could not find buffer\n");
		return -EINVAL;
	}

	if (ion_buf->iova != iova)
		NPU_WARN("unmap address %llu doesn't match %llu\n",
			iova, ion_buf->iova);

	virt_npu_munmap(client, iova, ion_buf->size);

	if (ion_buf->table)
		dma_buf_unmap_attachment(ion_buf->attachment, ion_buf->table,
			DMA_BIDIRECTIONAL);
	if (ion_buf->dma_buf && ion_buf->attachment)
		dma_buf_detach(ion_buf->dma_buf, ion_buf->attachment);
	if (ion_buf->dma_buf)
		dma_buf_put(ion_buf->dma_buf);

	NPU_DBG("unmapped mem addr:0x%llx size:0x%x\n", ion_buf->iova,
		ion_buf->size);
	npu_free_npu_ion_buffer(client, buf_hdl);

	return 0;
}

static int npu_unmap_buf(struct npu_client *client, unsigned long arg)
{
	struct msm_npu_unmap_buf_ioctl req;
	void __user *argp = (void __user *)arg;
	int rc = 0;

	rc = copy_from_user(&req, argp, sizeof(req));
	if (rc) {
		NPU_ERR("fail to copy from user\n");
		return -EFAULT;
	}

	rc = virt_npu_unmap_buf(client, req.buf_ion_hdl, req.npu_phys_addr);
	if (rc) {
		NPU_ERR("virt_npu_unmap_buf failed\n");
		return rc;
	}

	rc = copy_to_user(argp, &req, sizeof(req));
	if (rc) {
		NPU_ERR("fail to copy to user\n");
		return -EFAULT;
	}

	return 0;
}

static int virt_npu_mmap(struct npu_client *client,  uint32_t flags,
		struct scatterlist *table, unsigned int nents, size_t size,
		uint64_t *iova)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct virt_mmap_msg *vmsg, *rsp = NULL;
	struct virt_npu_msg *msg;
	struct virt_npu_buf *sgbuf;
	struct scatterlist sg[1], *sgl;
	int rc, sgbuf_size, total_size;
	int i = 0;
	unsigned long irq_flags;

	sgbuf_size = nents * sizeof(*sgbuf);
	total_size = sizeof(*vmsg) + sgbuf_size;

	msg = virt_alloc_msg(npu_dev, total_size);
	if (!msg)
		return -ENOMEM;

	vmsg = (struct virt_mmap_msg *)msg->txbuf;
	vmsg->hdr.pid = client->tgid;
	vmsg->hdr.tid = current->pid;
	vmsg->hdr.cid = client->cid;
	vmsg->hdr.cmd = VIRTIO_NPU_CMD_MMAP;
	vmsg->hdr.len = total_size;
	vmsg->hdr.msgid = msg->msgid;
	vmsg->hdr.result = 0xffffffff;
	vmsg->flags = flags;
	vmsg->size = size;
	vmsg->vnpu = 0;
	vmsg->nents = nents;
	sgbuf = vmsg->sgl;


	for_each_sg(table, sgl, nents, i) {
		if (sg_dma_len(sgl)) {
			sgbuf[i].pv = sg_dma_address(sgl);
			sgbuf[i].len = sg_dma_len(sgl);
		} else {
			sgbuf[i].pv = (uint64_t)sg_phys(sgl);
			sgbuf[i].len = sgl->length;
		}
		NPU_DBG("Add sg entry %d: addr %x len %d\n",
			i, sgbuf[i].pv, sgbuf[i].len);
	}

	sg_init_one(sg, vmsg, total_size);

	spin_lock_irqsave(&npu_dev->vq_lock, irq_flags);
	rc = virtqueue_add_outbuf(npu_dev->svq, sg, 1, vmsg, GFP_KERNEL);
	if (rc) {
		spin_unlock_irqrestore(&npu_dev->vq_lock, irq_flags);
		NPU_ERR("fail to add output buffer\n");
		goto fail;
	}

	virtqueue_kick(npu_dev->svq);
	spin_unlock_irqrestore(&npu_dev->vq_lock, irq_flags);

	wait_for_completion(&msg->work);

	rsp = msg->rxbuf;
	rc = rsp->hdr.result;
	if (rc)
		goto fail;
	*iova = rsp->vnpu;
	NPU_DBG("mapped buffer to %x\n", rsp->vnpu);

fail:
	if (rsp) {
		sg_init_one(sg, rsp, npu_dev->buf_size);

		/* add the buffer back to the remote processor's virtqueue */
		spin_lock_irqsave(&npu_dev->vq_lock, irq_flags);

		if (virtqueue_add_inbuf(npu_dev->rvq, sg, 1, rsp, GFP_KERNEL))
			NPU_ERR("fail to add input buffer\n");
		else
			virtqueue_kick(npu_dev->rvq);

		spin_unlock_irqrestore(&npu_dev->vq_lock, irq_flags);
	}
	virt_free_msg(npu_dev, msg);

	return rc;
}

static int32_t virt_npu_map_buf(struct npu_client *client,
		int buf_hdl, uint32_t size, uint64_t *iova)
{
	MODULE_IMPORT_NS(DMA_BUF);
	struct npu_device *npu_dev = client->npu_dev;
	struct npu_ion_buf *ion_buf = NULL;
	int rc = 0;

	ion_buf = npu_alloc_npu_ion_buffer(client, buf_hdl, size);
	if (!ion_buf) {
		NPU_ERR("fail to alloc npu_ion_buffer\n");
		rc = -ENOMEM;
		return rc;
	}

	ion_buf->dma_buf = dma_buf_get(ion_buf->fd);
	if (IS_ERR_OR_NULL(ion_buf->dma_buf)) {
		NPU_ERR("dma_buf_get failed %d\n", ion_buf->fd);
		rc = -ENOMEM;
		ion_buf->dma_buf = NULL;
		goto map_end;
	}

	ion_buf->attachment = dma_buf_attach(ion_buf->dma_buf,
			npu_dev->vdev->dev.parent);
	if (IS_ERR(ion_buf->attachment)) {
		NPU_ERR("failed to map attachment\n");
		rc = -ENOMEM;
		ion_buf->attachment = NULL;
		goto map_end;
	}

	ion_buf->attachment->dma_map_attrs = DMA_ATTR_SKIP_CPU_SYNC;

	ion_buf->table = dma_buf_map_attachment(ion_buf->attachment,
			DMA_BIDIRECTIONAL);
	if (IS_ERR(ion_buf->table)) {
		NPU_ERR("npu dma_buf_map_attachment failed\n");
		rc = -ENOMEM;
		ion_buf->table = NULL;
		goto map_end;
	}

	ion_buf->size = ion_buf->dma_buf->size;

	rc = virt_npu_mmap(client, 0, ion_buf->table->sgl,
		ion_buf->table->nents, size, &ion_buf->iova);

map_end:
	if (rc)
		virt_npu_unmap_buf(client, buf_hdl, 0);
	else
		*iova = ion_buf->iova;

	return rc;
}

static int npu_map_buf(struct npu_client *client, unsigned long arg)
{
	struct msm_npu_map_buf_ioctl req;
	void __user *argp = (void __user *)arg;
	int rc = 0;

	rc = copy_from_user(&req, argp, sizeof(req));
	if (rc) {
		NPU_ERR("fail to copy from user\n");
		return -EFAULT;
	}

	rc = virt_npu_map_buf(client, req.buf_ion_hdl, req.size,
		&req.npu_phys_addr);
	if (rc) {
		NPU_ERR("virt_npu_map_buf failed\n");
		return rc;
	}

	rc = copy_to_user(argp, &req, sizeof(req));
	if (rc) {
		NPU_ERR("fail to copy to user\n");
		return -EFAULT;
	}

	return 0;
}

static int32_t npu_virt_unload_network(struct npu_client *client,
			struct msm_npu_unload_network_ioctl *unload)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct virt_unload_network_msg *vmsg, *rsp = NULL;
	struct virt_npu_msg *msg;
	struct scatterlist sg[1];
	int rc, total_size;
	unsigned long flags;

	if (!npu_validate_network(client, unload->network_hdl)) {
		NPU_ERR("Invalid network handle %x\n", unload->network_hdl);
		return -EINVAL;
	}

	total_size = sizeof(*vmsg);
	msg = virt_alloc_msg(npu_dev, total_size);
	if (!msg)
		return -ENOMEM;

	vmsg = (struct virt_unload_network_msg *)msg->txbuf;
	vmsg->hdr.pid = client->tgid;
	vmsg->hdr.tid = current->pid;
	vmsg->hdr.cid = client->cid;
	vmsg->hdr.cmd = VIRTIO_NPU_CMD_UNLOAD_NETWORK;
	vmsg->hdr.len = total_size;
	vmsg->hdr.msgid = msg->msgid;
	vmsg->hdr.result = 0xffffffff;

	vmsg->network_hdl = unload->network_hdl;

	sg_init_one(sg, vmsg, total_size);

	spin_lock_irqsave(&npu_dev->vq_lock, flags);
	rc = virtqueue_add_outbuf(npu_dev->svq, sg, 1, vmsg, GFP_KERNEL);
	if (rc) {
		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);

		NPU_ERR("fail to add output buffer\n");
		goto fail;
	}

	virtqueue_kick(npu_dev->svq);
	spin_unlock_irqrestore(&npu_dev->vq_lock, flags);

	wait_for_completion(&msg->work);

	rsp = msg->rxbuf;
	rc = rsp->hdr.result;
	if (rc)
		NPU_ERR("unload_network failed\n");

	npu_remove_network(client, unload->network_hdl);
	NPU_DBG("network %x is unloaded\n", unload->network_hdl);

fail:
	if (rsp) {
		sg_init_one(sg, rsp, npu_dev->buf_size);

		/* add the buffer back to the remote processor's virtqueue */
		spin_lock_irqsave(&npu_dev->vq_lock, flags);

		if (virtqueue_add_inbuf(npu_dev->rvq, sg, 1, rsp, GFP_KERNEL))
			NPU_ERR("fail to add input buffer\n");
		else
			virtqueue_kick(npu_dev->rvq);

		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
	}
	virt_free_msg(npu_dev, msg);

	return rc;
}

static int npu_unload_network(struct npu_client *client,
	unsigned long arg)
{
	struct msm_npu_unload_network_ioctl req;
	void __user *argp = (void __user *)arg;
	int rc = 0;

	rc = copy_from_user(&req, argp, sizeof(req));
	if (rc) {
		NPU_ERR("fail to copy from user\n");
		return -EFAULT;
	}

	rc = npu_virt_unload_network(client, &req);
	if (rc) {
		NPU_ERR("npu_host_unload_network failed %d\n", rc);
		return rc;
	}

	rc = copy_to_user(argp, &req, sizeof(req));
	if (rc) {
		NPU_ERR("fail to copy to user\n");
		return -EFAULT;
	}

	return 0;
}

static int32_t npu_virt_load_network_v2(struct npu_client *client,
			struct msm_npu_load_network_ioctl_v2 *load_ioctl,
			struct msm_npu_patch_info_v2 *patch_info)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct virt_load_network_v2_msg *vmsg, *rsp = NULL;
	struct virt_npu_msg *msg;
	struct scatterlist sg[1];
	struct virt_patch_info_v2 *virt_patch_info;
	int rc, i, patch_info_size, total_size;
	unsigned long flags;

	patch_info_size = load_ioctl->patch_info_num *
		sizeof(struct virt_patch_info_v2);
	total_size = sizeof(*vmsg) + patch_info_size;

	msg = virt_alloc_msg(npu_dev, total_size);
	if (!msg)
		return -ENOMEM;

	vmsg = (struct virt_load_network_v2_msg *)msg->txbuf;
	vmsg->hdr.pid = client->tgid;
	vmsg->hdr.tid = current->pid;
	vmsg->hdr.cid = client->cid;
	vmsg->hdr.cmd = VIRTIO_NPU_CMD_LOAD_NETWORK_V2;
	vmsg->hdr.len = total_size;
	vmsg->hdr.msgid = msg->msgid;
	vmsg->hdr.result = 0xffffffff;

	vmsg->buf_phys_addr = load_ioctl->buf_phys_addr;
	vmsg->buf_ion_hdl = load_ioctl->buf_ion_hdl;
	vmsg->buf_size = load_ioctl->buf_size;
	vmsg->first_block_size = load_ioctl->first_block_size;
	vmsg->flags = load_ioctl->flags;
	vmsg->network_hdl = load_ioctl->network_hdl;
	vmsg->priority = load_ioctl->priority;
	vmsg->perf_mode = load_ioctl->perf_mode;
	vmsg->num_layers = load_ioctl->num_layers;
	vmsg->patch_info_num = load_ioctl->patch_info_num;
	vmsg->reserved = load_ioctl->reserved;

	virt_patch_info = vmsg->patch_info;
	for (i = 0; i < load_ioctl->patch_info_num; i++) {
		virt_patch_info[i].chunk_id = patch_info[i].chunk_id;
		virt_patch_info[i].instruction_size_in_bytes =
			patch_info[i].instruction_size_in_bytes;
		virt_patch_info[i].loc_offset = patch_info[i].loc_offset;
		virt_patch_info[i].shift_value_in_bits =
			patch_info[i].shift_value_in_bits;
		virt_patch_info[i].value = patch_info[i].value;
		virt_patch_info[i].variable_size_in_bits =
			patch_info[i].variable_size_in_bits;
	}

	sg_init_one(sg, vmsg, total_size);

	spin_lock_irqsave(&npu_dev->vq_lock, flags);
	rc = virtqueue_add_outbuf(npu_dev->svq, sg, 1, vmsg, GFP_KERNEL);
	if (rc) {
		NPU_ERR("fail to add output buffer\n");
		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
		goto fail;
	}

	virtqueue_kick(npu_dev->svq);
	spin_unlock_irqrestore(&npu_dev->vq_lock, flags);

	wait_for_completion(&msg->work);

	rsp = msg->rxbuf;
	rc = rsp->hdr.result;
	if (rc)
		goto fail;

	NPU_DBG("Load network %x\n", rsp->network_hdl);
	load_ioctl->network_hdl = rsp->network_hdl;
	rc = npu_add_network(client, load_ioctl->network_hdl);
	if (rc) {
		struct msm_npu_unload_network_ioctl unload;

		NPU_ERR("failed to add network\n");
		unload.network_hdl = load_ioctl->network_hdl;
		npu_virt_unload_network(client, &unload);
	}
fail:
	if (rsp) {
		sg_init_one(sg, rsp, npu_dev->buf_size);

		/* add the buffer back to the remote processor's virtqueue */
		spin_lock_irqsave(&npu_dev->vq_lock, flags);

		if (virtqueue_add_inbuf(npu_dev->rvq, sg, 1, rsp, GFP_KERNEL))
			NPU_ERR("fail to add input buffer\n");
		else
			virtqueue_kick(npu_dev->rvq);

		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
	}
	virt_free_msg(npu_dev, msg);

	return rc;
}

static int npu_load_network_v2(struct npu_client *client,
	unsigned long arg)
{
	struct msm_npu_load_network_ioctl_v2 req;
	struct msm_npu_unload_network_ioctl unload_req;
	void __user *argp = (void __user *)arg;
	struct msm_npu_patch_info_v2 *patch_info = NULL;
	int rc;

	rc = copy_from_user(&req, argp, sizeof(req));
	if (rc) {
		NPU_ERR("fail to copy from user\n");
		return -EFAULT;
	}

	if (req.patch_info_num > NPU_MAX_PATCH_NUM) {
		NPU_ERR("Invalid patch info num %d[max:%d]\n",
			req.patch_info_num, NPU_MAX_PATCH_NUM);
		return -EINVAL;
	}

	if (req.patch_info_num) {
		patch_info = kmalloc_array(req.patch_info_num,
			sizeof(*patch_info), GFP_KERNEL);
		if (!patch_info)
			return -ENOMEM;

		rc = copy_from_user(patch_info,
			(void __user *)req.patch_info,
			req.patch_info_num * sizeof(*patch_info));
		if (rc) {
			NPU_ERR("fail to copy patch info\n");
			kfree(patch_info);
			return -EFAULT;
		}
	}

	NPU_DBG("network load with perf request %d\n", req.perf_mode);

	rc = npu_virt_load_network_v2(client, &req, patch_info);

	kfree(patch_info);
	if (rc) {
		NPU_ERR("npu_virt_load_network_v2 failed %d\n", rc);
		return rc;
	}

	rc = copy_to_user(argp, &req, sizeof(req));
	if (rc) {
		NPU_ERR("fail to copy to user\n");
		rc = -EFAULT;
		unload_req.network_hdl = req.network_hdl;
		npu_virt_unload_network(client, &unload_req);
	}

	return rc;
}

static int32_t npu_virt_exec_network_v2(struct npu_client *client,
	struct msm_npu_exec_network_ioctl_v2 *exec_ioctl,
	struct msm_npu_patch_buf_info *patch_buf_info)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct virt_exec_network_v2_msg *vmsg, *rsp = NULL;
	struct virt_npu_msg *msg;
	struct virt_patch_buf_info *v_patch_buf_info;
	struct scatterlist sg[1];
	int rc, i, patch_info_size, total_size;
	unsigned long flags;

	if (!npu_validate_network(client, exec_ioctl->network_hdl)) {
		NPU_ERR("Invalid network handle %x\n",
			exec_ioctl->network_hdl);
		return -EINVAL;
	}

	patch_info_size = exec_ioctl->patch_buf_info_num *
		sizeof(struct virt_patch_buf_info);
	total_size = sizeof(*vmsg) + patch_info_size +
		exec_ioctl->stats_buf_size;

	msg = virt_alloc_msg(npu_dev, total_size);
	if (!msg)
		return -ENOMEM;

	vmsg = (struct virt_exec_network_v2_msg *)msg->txbuf;
	vmsg->hdr.pid = client->tgid;
	vmsg->hdr.tid = current->pid;
	vmsg->hdr.cid = client->cid;
	vmsg->hdr.cmd = VIRTIO_NPU_CMD_EXEC_NETWORK_V2;
	vmsg->hdr.len = total_size;
	vmsg->hdr.msgid = msg->msgid;
	vmsg->hdr.result = 0xffffffff;

	vmsg->network_hdl = exec_ioctl->network_hdl;
	vmsg->async = exec_ioctl->async;
	vmsg->flags = exec_ioctl->flags;
	vmsg->stats_buf_size = exec_ioctl->stats_buf_size;
	vmsg->patch_buf_info_num = exec_ioctl->patch_buf_info_num;
	vmsg->reserved[0] = exec_ioctl->reserved;
	vmsg->stats_buf_off = sizeof(*vmsg) + patch_info_size;
	v_patch_buf_info = vmsg->patch_buf_info;

	for (i = 0; i < vmsg->patch_buf_info_num; i++) {
		v_patch_buf_info[i].buf_id = patch_buf_info[i].buf_id;
		v_patch_buf_info[i].buf_phys_addr =
			patch_buf_info[i].buf_phys_addr;
	}

	sg_init_one(sg, vmsg, total_size);

	spin_lock_irqsave(&npu_dev->vq_lock, flags);
	rc = virtqueue_add_outbuf(npu_dev->svq, sg, 1, vmsg, GFP_KERNEL);
	if (rc) {
		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
		NPU_ERR("fail to add output buffer\n");
		goto fail;
	}

	virtqueue_kick(npu_dev->svq);
	spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
	wait_for_completion(&msg->work);

	rsp = msg->rxbuf;
	rc = rsp->hdr.result;
	if (rc)
		goto fail;

	exec_ioctl->stats_buf_size =
		(exec_ioctl->stats_buf_size < rsp->stats_buf_size) ?
		exec_ioctl->stats_buf_size : rsp->stats_buf_size;
	if (copy_to_user(
		(void __user *)exec_ioctl->stats_buf_addr,
		(void *)rsp + rsp->stats_buf_off,
		exec_ioctl->stats_buf_size)) {
		NPU_ERR("copy stats to user failed\n");
		exec_ioctl->stats_buf_size = 0;
	}

fail:
	if (rsp) {
		sg_init_one(sg, rsp, npu_dev->buf_size);

		/* add the buffer back to the remote processor's virtqueue */
		spin_lock_irqsave(&npu_dev->vq_lock, flags);
		if (virtqueue_add_inbuf(npu_dev->rvq, sg, 1, rsp, GFP_KERNEL))
			NPU_ERR("fail to add input buffer\n");
		else
			virtqueue_kick(npu_dev->rvq);

		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
	}
	virt_free_msg(npu_dev, msg);

	return rc;
}

static int npu_exec_network_v2(struct npu_client *client,
	unsigned long arg)
{
	struct msm_npu_exec_network_ioctl_v2 req;
	void __user *argp = (void __user *)arg;
	struct msm_npu_patch_buf_info *patch_buf_info = NULL;
	int rc;

	rc = copy_from_user(&req, argp, sizeof(req));
	if (rc) {
		NPU_ERR("fail to copy from user\n");
		return -EFAULT;
	}

	if ((req.patch_buf_info_num > NPU_MAX_PATCH_NUM) ||
		(req.patch_buf_info_num == 0)) {
		NPU_ERR("Invalid patch buf info num %d[max:%d]\n",
			req.patch_buf_info_num, NPU_MAX_PATCH_NUM);
		return -EINVAL;
	}

	if (req.stats_buf_size > NPU_MAX_STATS_BUF_SIZE) {
		NPU_ERR("Invalid stats buffer size %d max %d\n",
			req.stats_buf_size, NPU_MAX_STATS_BUF_SIZE);
		return -EINVAL;
	}

	if (req.patch_buf_info_num) {
		patch_buf_info = kmalloc_array(req.patch_buf_info_num,
			sizeof(*patch_buf_info), GFP_KERNEL);
		if (!patch_buf_info)
			return -ENOMEM;

		rc = copy_from_user(patch_buf_info,
			(void __user *)req.patch_buf_info,
			req.patch_buf_info_num * sizeof(*patch_buf_info));
		if (rc) {
			NPU_ERR("fail to copy patch buf info\n");
			kfree(patch_buf_info);
			return -EFAULT;
		}
	}

	rc = npu_virt_exec_network_v2(client, &req, patch_buf_info);

	kfree(patch_buf_info);
	if (rc) {
		NPU_ERR("npu_virt_exec_network_v2 failed %d\n", rc);
		return rc;
	}

	rc = copy_to_user(argp, &req, sizeof(req));
	if (rc) {
		NPU_ERR("fail to copy to user\n");
		rc = -EFAULT;
	}

	return rc;
}

static int32_t npu_virt_set_property(struct npu_client *client,
			struct msm_npu_property *prop)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct virt_property_msg *vmsg, *rsp = NULL;
	struct virt_npu_msg *msg;
	struct scatterlist sg[1];
	int rc, i, total_size;
	unsigned long flags;

	total_size = sizeof(*vmsg);

	msg = virt_alloc_msg(npu_dev, total_size);
	if (!msg)
		return -ENOMEM;

	vmsg = (struct virt_property_msg *)msg->txbuf;
	vmsg->hdr.pid = client->tgid;
	vmsg->hdr.tid = current->pid;
	vmsg->hdr.cid = client->cid;
	vmsg->hdr.cmd = VIRTIO_NPU_CMD_SET_PROPERTY;
	vmsg->hdr.len = total_size;
	vmsg->hdr.msgid = msg->msgid;
	vmsg->hdr.result = 0xffffffff;

	vmsg->prop_id = prop->prop_id;
	vmsg->num_of_params = prop->num_of_params;
	vmsg->network_hdl = prop->network_hdl;
	for (i = 0; i < PROP_PARAM_MAX_SIZE; i++)
		vmsg->prop_param[i] = prop->prop_param[i];

	sg_init_one(sg, vmsg, total_size);

	spin_lock_irqsave(&npu_dev->vq_lock, flags);
	rc = virtqueue_add_outbuf(npu_dev->svq, sg, 1, vmsg, GFP_KERNEL);
	if (rc) {
		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
		NPU_ERR("fail to add output buffer\n");
		goto fail;
	}

	virtqueue_kick(npu_dev->svq);
	spin_unlock_irqrestore(&npu_dev->vq_lock, flags);

	wait_for_completion(&msg->work);

	rsp = msg->rxbuf;
	rc = rsp->hdr.result;
	if (rc)
		NPU_ERR("set_property %d failed\n", prop->prop_id);

fail:
	if (rsp) {
		sg_init_one(sg, rsp, npu_dev->buf_size);

		/* add the buffer back to the remote processor's virtqueue */
		spin_lock_irqsave(&npu_dev->vq_lock, flags);
		if (virtqueue_add_inbuf(npu_dev->rvq, sg, 1, rsp, GFP_KERNEL))
			NPU_ERR("fail to add input buffer\n");
		else
			virtqueue_kick(npu_dev->rvq);

		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
	}
	virt_free_msg(npu_dev, msg);

	return rc;
}

static int npu_set_property(struct npu_client *client,
	unsigned long arg)
{
	struct msm_npu_property prop;
	void __user *argp = (void __user *)arg;
	int rc = -EINVAL;

	rc = copy_from_user(&prop, argp, sizeof(prop));
	if (rc) {
		NPU_ERR("fail to copy from user\n");
		return -EFAULT;
	}

	rc = npu_virt_set_property(client, &prop);

	return rc;
}

static int32_t npu_virt_get_property(struct npu_client *client,
			struct msm_npu_property *prop)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct virt_property_msg *vmsg, *rsp = NULL;
	struct virt_npu_msg *msg;
	struct scatterlist sg[1];
	int rc, i, total_size;
	unsigned long flags;

	total_size = sizeof(*vmsg);

	msg = virt_alloc_msg(npu_dev, total_size);
	if (!msg)
		return -ENOMEM;

	vmsg = (struct virt_property_msg *)msg->txbuf;
	vmsg->hdr.pid = client->tgid;
	vmsg->hdr.tid = current->pid;
	vmsg->hdr.cid = client->cid;
	vmsg->hdr.cmd = VIRTIO_NPU_CMD_GET_PROPERTY;
	vmsg->hdr.len = total_size;
	vmsg->hdr.msgid = msg->msgid;
	vmsg->hdr.result = 0xffffffff;

	vmsg->prop_id = prop->prop_id;
	vmsg->num_of_params = prop->num_of_params;
	vmsg->network_hdl = prop->network_hdl;
	for (i = 0; i < PROP_PARAM_MAX_SIZE; i++)
		vmsg->prop_param[i] = prop->prop_param[i];

	sg_init_one(sg, vmsg, total_size);

	spin_lock_irqsave(&npu_dev->vq_lock, flags);
	rc = virtqueue_add_outbuf(npu_dev->svq, sg, 1, vmsg, GFP_KERNEL);
	if (rc) {
		NPU_ERR("fail to add output buffer\n");
		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
		goto fail;
	}

	virtqueue_kick(npu_dev->svq);
	spin_unlock_irqrestore(&npu_dev->vq_lock, flags);

	wait_for_completion(&msg->work);

	rsp = msg->rxbuf;
	rc = rsp->hdr.result;
	if (rc) {
		NPU_ERR("get_property %d failed\n", prop->prop_id);
		goto fail;
	}

	prop->num_of_params = rsp->num_of_params;
	for (i = 0; i < PROP_PARAM_MAX_SIZE; i++)
		prop->prop_param[i] = rsp->prop_param[i];

fail:
	if (rsp) {
		sg_init_one(sg, rsp, npu_dev->buf_size);

		/* add the buffer back to the remote processor's virtqueue */
		spin_lock_irqsave(&npu_dev->vq_lock, flags);
		if (virtqueue_add_inbuf(npu_dev->rvq, sg, 1, rsp, GFP_KERNEL))
			NPU_ERR("fail to add input buffer\n");
		else
			virtqueue_kick(npu_dev->rvq);

		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
	}
	virt_free_msg(npu_dev, msg);

	return rc;
}

static int npu_get_property(struct npu_client *client,
	unsigned long arg)
{
	struct msm_npu_property prop;
	void __user *argp = (void __user *)arg;
	int rc = -EINVAL;

	rc = copy_from_user(&prop, argp, sizeof(prop));
	if (rc) {
		NPU_ERR("fail to copy from user\n");
		return -EFAULT;
	}

	rc = npu_virt_get_property(client, &prop);
	if (rc) {
		NPU_ERR("failed to get property\n");
		return rc;
	}

	rc = copy_to_user(argp, &prop, sizeof(prop));
	if (rc) {
		NPU_ERR("fail to copy to user\n");
		return -EFAULT;
	}

	return rc;
}

static long npu_ioctl(struct file *file, unsigned int cmd,
						 unsigned long arg)
{
	int rc = -ENOIOCTLCMD;
	struct npu_client *client = file->private_data;

	switch (cmd) {
	case MSM_NPU_GET_INFO:
		rc = npu_get_info(client, arg);
		break;
	case MSM_NPU_MAP_BUF:
		rc = npu_map_buf(client, arg);
		break;
	case MSM_NPU_UNMAP_BUF:
		rc = npu_unmap_buf(client, arg);
		break;
	case MSM_NPU_LOAD_NETWORK:
		NPU_ERR("npu_load_network_v1 is no longer supported\n");
		rc = -ENOTTY;
		break;
	case MSM_NPU_LOAD_NETWORK_V2:
		rc = npu_load_network_v2(client, arg);
		break;
	case MSM_NPU_UNLOAD_NETWORK:
		rc = npu_unload_network(client, arg);
		break;
	case MSM_NPU_EXEC_NETWORK:
		NPU_ERR("npu_exec_network_v1 is no longer supported\n");
		rc = -ENOTTY;
		break;
	case MSM_NPU_EXEC_NETWORK_V2:
		rc = npu_exec_network_v2(client, arg);
		break;
	case MSM_NPU_RECEIVE_EVENT:
		NPU_ERR("MSM_NPU_RECEIVE_EVENT is not supported\n");
		rc = -ENOTTY;
		break;
	case MSM_NPU_SET_PROP:
		rc = npu_set_property(client, arg);
		break;
	case MSM_NPU_GET_PROP:
		rc = npu_get_property(client, arg);
		break;
	default:
		NPU_ERR("unexpected IOCTL %x\n", cmd);
	}

	return rc;
}

static int virt_npu_open(struct npu_client *client)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct virt_msg_hdr *vmsg, *rsp = NULL;
	struct virt_npu_msg *msg;
	struct scatterlist sg[1];
	int rc;
	unsigned long flags;

	msg = virt_alloc_msg(npu_dev, sizeof(*vmsg));
	if (!msg)
		return -ENOMEM;

	vmsg = (struct virt_msg_hdr *)msg->txbuf;
	vmsg->pid = client->tgid;
	vmsg->tid = current->pid;
	vmsg->cid = -1;
	vmsg->cmd = VIRTIO_NPU_CMD_OPEN;
	vmsg->len = sizeof(*vmsg);
	vmsg->msgid = msg->msgid;
	vmsg->result = 0xffffffff;
	sg_init_one(sg, vmsg, sizeof(*vmsg));

	spin_lock_irqsave(&npu_dev->vq_lock, flags);
	rc = virtqueue_add_outbuf(npu_dev->svq, sg, 1, vmsg, GFP_KERNEL);
	if (rc) {
		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);

		NPU_ERR("fail to add output buffer\n");
		goto fail;
	}

	virtqueue_kick(npu_dev->svq);
	spin_unlock_irqrestore(&npu_dev->vq_lock, flags);

	wait_for_completion(&msg->work);

	rsp = msg->rxbuf;
	rc = rsp->result;
	if (rc) {
		NPU_ERR("response result %d\n", rc);
		goto fail;
	}

#ifndef LOOPBACK_TEST
	if (rsp->cid < 0) {
		NPU_ERR("channel id %d is invalid\n", rsp->cid);
		rc = -EINVAL;
		goto fail;
	}
#endif
	client->cid = rsp->cid;
	NPU_DBG("opened npu channel %d\n", rsp->cid);
fail:
	if (rsp) {
		sg_init_one(sg, rsp, npu_dev->buf_size);

		/* add the buffer back to the remote processor's virtqueue */
		spin_lock_irqsave(&npu_dev->vq_lock, flags);
		if (virtqueue_add_inbuf(npu_dev->rvq, sg, 1, rsp, GFP_KERNEL))
			NPU_ERR("fail to add input buffer\n");
		else
			virtqueue_kick(npu_dev->rvq);

		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
	}
	virt_free_msg(npu_dev, msg);

	return rc;
}


static int npu_open(struct inode *inode, struct file *filp)
{
	struct npu_device *npu_dev = container_of(inode->i_cdev,
		struct npu_device, cdev);
	struct npu_client *client;
	int rc;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	client->npu_dev = npu_dev;
	client->tgid = current->tgid;
	mutex_init(&client->list_lock);
	INIT_LIST_HEAD(&(client->mapped_buffer_list));

	rc = virt_npu_open(client);
	if (rc) {
		NPU_ERR("open npu failed\n");
		kfree(client);
		return rc;
	}

	filp->private_data = client;

	return 0;
}

static int virt_npu_close(struct npu_client *client)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct virt_msg_hdr *vmsg, *rsp = NULL;
	struct virt_npu_msg *msg;
	struct scatterlist sg[1];
	int rc;
	unsigned long flags;

	msg = virt_alloc_msg(npu_dev, sizeof(*vmsg));
	if (!msg)
		return -ENOMEM;

	NPU_DBG("close NPU channel %d\n", client->cid);
	vmsg = (struct virt_msg_hdr *)msg->txbuf;
	vmsg->pid = client->tgid;
	vmsg->tid = current->pid;
	vmsg->cid = client->cid;
	vmsg->cmd = VIRTIO_NPU_CMD_CLOSE;
	vmsg->len = sizeof(*vmsg);
	vmsg->msgid = msg->msgid;
	vmsg->result = 0xffffffff;
	sg_init_one(sg, vmsg, sizeof(*vmsg));

	spin_lock_irqsave(&npu_dev->vq_lock, flags);

	rc = virtqueue_add_outbuf(npu_dev->svq, sg, 1, vmsg, GFP_KERNEL);
	if (rc) {
		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
		NPU_ERR("fail to add output buffer\n");
		goto fail;
	}

	virtqueue_kick(npu_dev->svq);
	spin_unlock_irqrestore(&npu_dev->vq_lock, flags);

	wait_for_completion(&msg->work);

	rsp = msg->rxbuf;
	rc = rsp->result;
	if (rc)
		NPU_ERR("npu close failed\n");

fail:
	if (rsp) {
		sg_init_one(sg, rsp, npu_dev->buf_size);

		/* add the buffer back to the remote processor's virtqueue */
		spin_lock_irqsave(&npu_dev->vq_lock, flags);
		if (virtqueue_add_inbuf(npu_dev->rvq, sg, 1, rsp, GFP_KERNEL))
			NPU_ERR("fail to add input buffer\n");
		else
			virtqueue_kick(npu_dev->rvq);

		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
	}
	virt_free_msg(npu_dev, msg);

	return rc;

}

static int npu_release(struct inode *inode, struct file *file)
{
	struct npu_client *client = file->private_data;
	struct npu_device *npu_dev = client->npu_dev;
	struct npu_network *network = NULL;
	int rc, i;

	/*
	 * check if there is any active network, for those active
	 * networks, virt_npu_close will do all clean up jobs at
	 * backend. Here only needs to remove them from npu_dev
	 */
	mutex_lock(&npu_dev->lock);
	for (i = 0; i < MAX_LOADED_NETWORK; i++) {
		network = &npu_dev->networks[i];
		if (network->valid && (network->client == client)) {
			NPU_WARN("network %x is not unloaded before close\n",
				network->network_hdl);
			network->valid = false;
			network->client = NULL;
			network->network_hdl = 0;
			npu_dev->network_num--;
		}
	}
	mutex_unlock(&npu_dev->lock);

	rc = virt_npu_close(client);
	if (rc)
		NPU_ERR("failed to close npu\n");

	mutex_destroy(&client->list_lock);
	kfree(client);
	file->private_data = NULL;

	return 0;
}

static const struct file_operations npu_fops = {
	.open = npu_open,
	.release = npu_release,
	.unlocked_ioctl = npu_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = npu_ioctl,
#endif
};

/* -------------------------------------------------------------------------
 * SysFS - Power State
 * -------------------------------------------------------------------------
 */
static ssize_t perf_mode_override_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct npu_device *npu_dev = dev_get_drvdata(dev);
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pwr->perf_mode_override);
}

static ssize_t perf_mode_override_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct npu_client client;
	struct npu_device *npu_dev = dev_get_drvdata(dev);
	struct msm_npu_property prop;
	uint32_t val;
	int rc;

	rc = kstrtou32(buf, 10, &val);
	if (rc) {
		NPU_ERR("Invalid input for perf mode setting\n");
		return -EINVAL;
	}

	val = min(val, npu_dev->pwrctrl.num_pwrlevels);
	NPU_DBG("setting perf mode to %d\n", val);
	client.npu_dev = npu_dev;
	client.cid = -1;
	prop.prop_id = MSM_NPU_PROP_ID_PERF_MODE;
	prop.num_of_params = 1;
	prop.network_hdl = 0;
	prop.prop_param[0] = val;
	rc = npu_virt_set_property(&client, &prop);
	if (rc) {
		NPU_ERR("set perf_mode %d failed\n", val);
		return rc;
	}

	return count;
}

static ssize_t dcvs_mode_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct npu_device *npu_dev = dev_get_drvdata(dev);
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;

	return scnprintf(buf, PAGE_SIZE, "%d\n", pwr->dcvs_mode);
}

static ssize_t dcvs_mode_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct npu_device *npu_dev = dev_get_drvdata(dev);
	struct npu_client client;
	struct msm_npu_property prop;
	uint32_t val;
	int rc = 0;

	rc = kstrtou32(buf, 10, &val);
	if (rc) {
		NPU_ERR("Invalid input for dcvs mode setting\n");
		return -EINVAL;
	}

	val = min(val, (uint32_t)(npu_dev->pwrctrl.num_pwrlevels - 1));
	NPU_DBG("sysfs: setting dcvs_mode to %d\n", val);

	client.npu_dev = npu_dev;
	client.cid = -1;
	prop.prop_id = MSM_NPU_PROP_ID_DCVS_MODE;
	prop.num_of_params = 1;
	prop.network_hdl = 0;
	prop.prop_param[0] = val;

	rc = npu_virt_set_property(&client, &prop);
	if (rc) {
		NPU_ERR("npu_host_set_fw_property failed %d\n", rc);
		return rc;
	}

	npu_dev->pwrctrl.dcvs_mode = val;

	return count;
}

static DEVICE_ATTR_RW(perf_mode_override);
static DEVICE_ATTR_RW(dcvs_mode);

static struct attribute *npu_fs_attrs[] = {
	&dev_attr_perf_mode_override.attr,
	&dev_attr_dcvs_mode.attr,
	NULL
};

static struct attribute_group npu_fs_attr_group = {
	.attrs = npu_fs_attrs
};


/* -------------------------------------------------------------------------
 * DebugFS
 * -------------------------------------------------------------------------
 */

static int npu_debug_open(struct inode *inode, struct file *file)
{
	/* non-seekable */
	file->f_mode &= ~(FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE);
	file->private_data = inode->i_private;
	return 0;
}

static int32_t npu_virt_tx_debugfs_cmd(struct npu_device *npu_dev,
			uint32_t subcmd, uint32_t param_num, uint32_t *param)
{
	struct virt_debugfs_msg *vmsg, *rsp = NULL;
	struct virt_npu_msg *msg;
	int rc, i, total_size;
	struct scatterlist sg[1];
	unsigned long flags;

	if (param_num > MAX_DEBUGFS_PARAM_NUM) {
		NPU_ERR("Too many params %d\n", param_num);
		return -EINVAL;
	}

	if (!param && param_num > 0) {
		NPU_ERR("param is not valid\n");
		return -EINVAL;
	}

	total_size = sizeof(*vmsg);

	msg = virt_alloc_msg(npu_dev, total_size);
	if (!msg)
		return -ENOMEM;

	vmsg = (struct virt_debugfs_msg *)msg->txbuf;
	vmsg->hdr.pid = current->pid;
	vmsg->hdr.tid = current->pid;
	vmsg->hdr.cid = -1;
	vmsg->hdr.cmd = VIRTIO_NPU_CMD_DEBUGFS;
	vmsg->hdr.len = total_size;
	vmsg->hdr.msgid = msg->msgid;
	vmsg->hdr.result = 0xffffffff;

	vmsg->subcmd = subcmd;
	vmsg->num_of_params = param_num;
	for (i = 0; i < param_num; i++)
		vmsg->params[i] = param[i];

	sg_init_one(sg, vmsg, total_size);

	spin_lock_irqsave(&npu_dev->vq_lock, flags);
	rc = virtqueue_add_outbuf(npu_dev->svq, sg, 1, vmsg, GFP_KERNEL);
	if (rc) {
		NPU_ERR("fail to add output buffer\n");
		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
		goto fail;
	}

	virtqueue_kick(npu_dev->svq);
	spin_unlock_irqrestore(&npu_dev->vq_lock, flags);

	wait_for_completion(&msg->work);

	rsp = msg->rxbuf;
	rc = rsp->hdr.result;
	if (rc)
		NPU_ERR("debugfs cmd %d failed\n", subcmd);

fail:
	if (rsp) {
		sg_init_one(sg, rsp, npu_dev->buf_size);

		/* add the buffer back to the remote processor's virtqueue */
		spin_lock_irqsave(&npu_dev->vq_lock, flags);
		if (virtqueue_add_inbuf(npu_dev->rvq, sg, 1, rsp, GFP_KERNEL))
			NPU_ERR("fail to add input buffer\n");
		else
			virtqueue_kick(npu_dev->rvq);

		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
	}
	virt_free_msg(npu_dev, msg);

	return rc;
}

static ssize_t npu_debug_ctrl_write(struct file *file,
		const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[64];
	struct npu_device *npu_dev = file->private_data;
	uint32_t subcmd = 0;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	buf[count] = 0;	/* end of string */

	if (count >= 2)
		buf[count-1] = 0;/* remove line feed */

	if (strcmp(buf, "on") == 0) {
		NPU_DBG("triggering fw_init\n");
		subcmd = DEBUGFS_SUBCMD_CTRL_ON;
	} else if (strcmp(buf, "off") == 0) {
		NPU_DBG("triggering fw_deinit\n");
		subcmd = DEBUGFS_SUBCMD_CTRL_OFF;
	} else if (strcmp(buf, "ssr") == 0) {
		NPU_DBG("trigger error irq\n");
		subcmd = DEBUGFS_SUBCMD_CTRL_SSR;
	} else if (strcmp(buf, "ssr_wdt") == 0) {
		NPU_DBG("trigger wdt irq\n");
		subcmd = DEBUGFS_SUBCMD_CTRL_SSR_WDT;
	} else if (strcmp(buf, "loopback") == 0) {
		NPU_DBG("loopback test\n");
		subcmd = DEBUGFS_SUBCMD_CTRL_LOOPBACK;
	} else {
		NPU_WARN("Invalid command %s\n", buf);
		return -EINVAL;
	}

	npu_virt_tx_debugfs_cmd(npu_dev, subcmd, 0, NULL);

	return count;
}

static const struct file_operations npu_ctrl_fops = {
	.open = npu_debug_open,
	.write = npu_debug_ctrl_write,
};

static int32_t npu_virt_tx_bist_cmd(struct npu_device *npu_dev,
			uint32_t subcmd, uint32_t param_num, uint32_t *param)
{
	struct virt_bist_msg *vmsg, *rsp = NULL;
	struct virt_npu_msg *msg;
	int rc, i, total_size;
	struct scatterlist sg[1];
	unsigned long flags;

	if (param_num > MAX_BIST_PARAM_NUM) {
		NPU_ERR("Too many params %d\n", param_num);
		return -EINVAL;
	}

	if (!param && param_num > 0) {
		NPU_ERR("param is not valid\n");
		return -EINVAL;
	}

	total_size = sizeof(*vmsg);

	msg = virt_alloc_msg(npu_dev, total_size);
	if (!msg)
		return -ENOMEM;

	vmsg = (struct virt_bist_msg *)msg->txbuf;
	vmsg->hdr.pid = current->pid;
	vmsg->hdr.tid = current->pid;
	vmsg->hdr.cid = -1;
	vmsg->hdr.cmd = VIRTIO_NPU_CMD_BIST;
	vmsg->hdr.len = total_size;
	vmsg->hdr.msgid = msg->msgid;
	vmsg->hdr.result = 0xffffffff;

	vmsg->subcmd = subcmd;
	vmsg->num_of_params = param_num;
	for (i = 0; i < param_num; i++)
		vmsg->params[i] = param[i];

	sg_init_one(sg, vmsg, total_size);

	spin_lock_irqsave(&npu_dev->vq_lock, flags);
	rc = virtqueue_add_outbuf(npu_dev->svq, sg, 1, vmsg, GFP_KERNEL);
	if (rc) {
		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
		NPU_ERR("fail to add output buffer\n");
		goto fail;
	}

	virtqueue_kick(npu_dev->svq);
	spin_unlock_irqrestore(&npu_dev->vq_lock, flags);

	wait_for_completion(&msg->work);

	rsp = msg->rxbuf;
	rc = rsp->hdr.result;
	if (rc)
		NPU_ERR("debugfs cmd %d failed\n", subcmd);

fail:
	if (rsp) {
		sg_init_one(sg, rsp, npu_dev->buf_size);

		/* add the buffer back to the remote processor's virtqueue */
		spin_lock_irqsave(&npu_dev->vq_lock, flags);

		if (virtqueue_add_inbuf(npu_dev->rvq, sg, 1, rsp, GFP_KERNEL))
			NPU_ERR("fail to add input buffer\n");
		else
			virtqueue_kick(npu_dev->rvq);

		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
	}
	virt_free_msg(npu_dev, msg);

	return rc;
}

static ssize_t npu_debug_bist_write(struct file *file,
		const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[64], subcmd_str[64];
	struct npu_device *npu_dev = file->private_data;
	uint32_t subcmd = 0, loop_cnt = 1, arg_cnt;
	struct timespec64 tbefore, tafter, diff;
	u64 elapsed_ns;
	int rc = 0, i;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	buf[count] = 0;	/* end of string */

	arg_cnt = sscanf(buf, "%s %x", subcmd_str, &loop_cnt);
	if ((arg_cnt == 1) || (loop_cnt <= 0))
		loop_cnt = 1;

	if (strcmp(subcmd_str, "loopback") == 0) {
		NPU_DBG("bist loopback\n");
		subcmd = BIST_SUBCMD_LOOPBACK;
	} else {
		NPU_WARN("Invalid command %s\n", buf);
		return -EINVAL;
	}

	ktime_get_real_ts64(&tbefore);

	for (i = 0; i < loop_cnt; i++) {
		rc = npu_virt_tx_bist_cmd(npu_dev, subcmd, 1, &loop_cnt);
		if (rc) {
			NPU_ERR("failed in loop %d\n", i);
			break;
		}
	}
	ktime_get_real_ts64(&tafter);
	diff = timespec64_sub(tafter, tbefore);
	elapsed_ns = timespec64_to_ns(&diff);
	NPU_DBG("Total time elapsed %lldns average %lldns\n",
		elapsed_ns, elapsed_ns/loop_cnt);

	return count;
}

static const struct file_operations npu_bist_fops = {
	.open = npu_debug_open,
	.write = npu_debug_bist_write,
};

static void npu_debugfs_deinit(struct npu_device *npu_dev)
{
	if (!IS_ERR_OR_NULL(npu_dev->debugfs_root)) {
		debugfs_remove_recursive(npu_dev->debugfs_root);
		npu_dev->debugfs_root = NULL;
	}
}

static int npu_debugfs_init(struct npu_device *npu_dev)
{
	npu_dev->debugfs_root = debugfs_create_dir("npu", NULL);
	if (IS_ERR_OR_NULL(npu_dev->debugfs_root)) {
		NPU_ERR("debugfs_create_dir for npu failed, error %ld\n",
			PTR_ERR(npu_dev->debugfs_root));
		return -ENODEV;
	}

	if (!debugfs_create_file("ctrl", 0644, npu_dev->debugfs_root,
		npu_dev, &npu_ctrl_fops)) {
		NPU_ERR("debugfs_create_file ctrl fail\n");
		goto err;
	}

	if (!debugfs_create_file("bist", 0644, npu_dev->debugfs_root,
		npu_dev, &npu_bist_fops)) {
		NPU_ERR("debugfs_create_file bist fail\n");
		goto err;
	}

	return 0;

err:
	npu_debugfs_deinit(npu_dev);
	return -ENODEV;
}

static int recv_single(struct npu_device *npu_dev,
	struct virt_msg_hdr *rsp, unsigned int len)
{
	struct virt_npu_msg *msg;

	NPU_DBG("receive resp len %d id %d\n", rsp->len, rsp->msgid);

	if (len != rsp->len) {
		NPU_ERR("msg %u len mismatch,expected %u but %d found\n",
				rsp->cmd, rsp->len, len);
		return -EINVAL;
	}

	if (rsp->msgid >= NPU_MSG_MAX) {
		NPU_ERR("Invalid msg_id %d\n", rsp->msgid);
		return -EINVAL;
	}

	spin_lock(&npu_dev->msglock);
	msg = npu_dev->msgtable[rsp->msgid];
	spin_unlock(&npu_dev->msglock);

	if (!msg) {
		NPU_ERR("msg %u already free in table[%u]\n",
				rsp->cmd, rsp->msgid);
		return -EINVAL;
	}
	msg->rxbuf = (void *)rsp;
	complete(&msg->work);

	return 0;
}

static void recv_done(struct virtqueue *rvq)
{
	struct npu_device *npu_dev = rvq->vdev->priv;
	struct virt_msg_hdr *rsp;
	unsigned int len, msgs_received = 0;
	unsigned long flags;

	spin_lock_irqsave(&npu_dev->vq_lock, flags);
	rsp = virtqueue_get_buf(rvq, &len);
	spin_unlock_irqrestore(&npu_dev->vq_lock, flags);

	if (!rsp) {
		NPU_ERR("incoming signal, but no used buffer\n");
		return;
	}

	while (rsp) {
		if (recv_single(npu_dev, rsp, len))
			break;

		msgs_received++;
		spin_lock_irqsave(&npu_dev->vq_lock, flags);
		rsp = virtqueue_get_buf(rvq, &len);
		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
	}
}

static void tx_done(struct virtqueue *txq)
{
	struct npu_device *npu_dev = txq->vdev->priv;
	struct virt_msg_hdr *cmd;
	unsigned int len, msgs_received = 0;
	unsigned long flags;

	/* recycle the tx descs to free queue only*/
	spin_lock_irqsave(&npu_dev->vq_lock, flags);
	cmd = virtqueue_get_buf(txq, &len);
	spin_unlock_irqrestore(&npu_dev->vq_lock, flags);

	if (!cmd) {
		NPU_ERR("incoming signal, but no used buffer in tx queue\n");
		return;
	}

	while (cmd) {
		msgs_received++;
		NPU_DBG("get cmd from tx queue: total %d\n", msgs_received);
		spin_lock_irqsave(&npu_dev->vq_lock, flags);
		cmd = virtqueue_get_buf(txq, &len);
		spin_unlock_irqrestore(&npu_dev->vq_lock, flags);
	}
}

static int init_vqs(struct npu_device *npu_dev)
{
	struct virtqueue *vqs[2];
	const char * const names[] = { "output", "input" };
	vq_callback_t *cbs[] = { tx_done, recv_done };
	size_t total_buf_space;
	void *bufs;
	int rc;

	rc = virtio_find_vqs(npu_dev->vdev, 2, vqs, cbs, names, NULL);
	if (rc) {
		NPU_ERR("Can't find virtio queues\n");
		return rc;
	}

	npu_dev->svq = vqs[0];
	npu_dev->rvq = vqs[1];

	/* we expect symmetric tx/rx vrings */
	WARN_ON(virtqueue_get_vring_size(npu_dev->rvq) !=
			virtqueue_get_vring_size(npu_dev->svq));
	npu_dev->num_descs = virtqueue_get_vring_size(npu_dev->rvq);
	npu_dev->num_bufs = npu_dev->num_descs * 4;

	npu_dev->buf_size = MAX_NPU_BUF_SIZE;
	total_buf_space = npu_dev->num_bufs * npu_dev->buf_size;
	npu_dev->order = get_order(total_buf_space);
	bufs = (void *)__get_free_pages(GFP_KERNEL,
				npu_dev->order);
	if (!bufs) {
		NPU_ERR("Can't get %d pages\n", npu_dev->order);
		rc = -ENOMEM;
		goto vqs_del;
	}

	npu_dev->bufs = bufs;

	/* one fourth of the buffers is dedicated for RX */
	npu_dev->rbufs.bufs = bufs;
	npu_dev->rbufs.buf_size = MAX_NPU_BUF_SIZE;
	npu_dev->rbufs.total_buf_num = npu_dev->num_bufs / 4;
	npu_dev->rbufs.free_buf_num = npu_dev->rbufs.total_buf_num;
	npu_dev->rbufs.bitmap = bitmap_zalloc(npu_dev->rbufs.total_buf_num,
		GFP_KERNEL);
	if (!npu_dev->rbufs.bitmap) {
		NPU_ERR("can't allocate bitmap for rbufs\n");
		goto vqs_del;
	}

	/* and the rest is dedicated for TX */
	npu_dev->sbufs.bufs = bufs + total_buf_space / 4;
	npu_dev->sbufs.buf_size = MAX_NPU_BUF_SIZE;
	npu_dev->sbufs.total_buf_num = (npu_dev->num_bufs / 4) * 3;
	npu_dev->sbufs.free_buf_num = npu_dev->sbufs.total_buf_num;
	npu_dev->sbufs.bitmap = bitmap_zalloc(npu_dev->sbufs.total_buf_num,
		GFP_KERNEL);
	if (!npu_dev->sbufs.bitmap) {
		NPU_ERR("can't allocate bitmap for sbufs\n");
		goto vqs_del;
	}

	return 0;

vqs_del:
	free_pages((unsigned long)npu_dev->bufs, npu_dev->order);
	npu_dev->bufs = NULL;
	bitmap_free(npu_dev->rbufs.bitmap);
	npu_dev->rbufs.bitmap = NULL;
	npu_dev->vdev->config->del_vqs(npu_dev->vdev);
	return rc;
}

static int virt_npu_probe(struct virtio_device *vdev)
{
	struct npu_device *npu_dev = NULL;
	int rc, i;

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1)) {
		NPU_ERR("Only support VIRTIO_F_VERSION_1\n");
		return -ENODEV;
	}

	if (!virtio_has_feature(vdev, VIRTIO_NPU_F_MULTI_BUF)) {
		NPU_ERR("Update BE driver to support multi-buf\n");
		return -ENODEV;
	}

	npu_dev = kzalloc(sizeof(*npu_dev), GFP_KERNEL);
	if (!npu_dev)
		return -ENOMEM;

	mutex_init(&npu_dev->lock);
	spin_lock_init(&npu_dev->vq_lock);
	spin_lock_init(&npu_dev->msglock);

	vdev->priv = npu_dev;
	npu_dev->vdev = vdev;

	rc = init_vqs(npu_dev);
	if (rc) {
		NPU_ERR("failed to initialized virtqueue\n");
		goto init_vqs_fail;
	}

	rc = alloc_chrdev_region(&npu_dev->dev_num, 0, 1, DRIVER_NAME);
	if (rc < 0) {
		NPU_ERR("alloc_chrdev_region failed: %d\n", rc);
		goto alloc_chrdev_fail;
	}

	cdev_init(&npu_dev->cdev, &npu_fops);
	rc = cdev_add(&npu_dev->cdev,
			MKDEV(MAJOR(npu_dev->dev_num), 0), 1);
	if (rc < 0) {
		NPU_ERR("cdev_add failed %d\n", rc);
		goto cdev_init_fail;
	}

	npu_dev->class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(npu_dev->class)) {
		rc = PTR_ERR(npu_dev->class);
		NPU_ERR("class_create failed: %d\n", rc);
		goto class_create_fail;
	}

	npu_dev->device = device_create(npu_dev->class, NULL,
		npu_dev->dev_num, NULL, DRIVER_NAME);
	if (IS_ERR(npu_dev->device)) {
		rc = PTR_ERR(npu_dev->device);
		NPU_ERR("device_create failed: %d\n", rc);
		goto device_create_fail;
	}

	dev_set_drvdata(npu_dev->device, npu_dev);
	rc = sysfs_create_group(&npu_dev->device->kobj, &npu_fs_attr_group);
	if (rc) {
		NPU_ERR("unable to register npu sysfs nodes\n");
		goto device_create_fail;
	}

	virtio_device_ready(vdev);

	/* set up the receive buffers */
	for (i = 0; i < npu_dev->rbufs.total_buf_num; i++) {
		struct scatterlist sg;
		void *cpu_addr = npu_dev->rbufs.bufs +
			i * npu_dev->buf_size;

		sg_init_one(&sg, cpu_addr, npu_dev->buf_size);
		rc = virtqueue_add_inbuf(npu_dev->rvq, &sg, 1, cpu_addr,
				GFP_KERNEL);
		if (rc)
			NPU_WARN("failed to add inbuf\n");
	}

	/* suppress "tx-complete" interrupts */
	virtqueue_disable_cb(npu_dev->svq);

	virtqueue_enable_cb(npu_dev->rvq);
	virtqueue_kick(npu_dev->rvq);

	npu_debugfs_init(npu_dev);
	npu_dev->pwrctrl.num_pwrlevels = MAX_NPU_POWER_LVL_NUM;

	return 0;

device_create_fail:
	if (!IS_ERR_OR_NULL(npu_dev->device)) {
		sysfs_remove_group(&npu_dev->device->kobj, &npu_fs_attr_group);
		device_destroy(npu_dev->class, npu_dev->dev_num);
	}
	class_destroy(npu_dev->class);
class_create_fail:
	cdev_del(&npu_dev->cdev);
cdev_init_fail:
	unregister_chrdev_region(npu_dev->dev_num, 1);
alloc_chrdev_fail:
	vdev->config->del_vqs(vdev);
init_vqs_fail:
	kfree(npu_dev);
	vdev->priv = NULL;
	return rc;
}

static void virt_npu_remove(struct virtio_device *vdev)
{
	struct npu_device *npu_dev = vdev->priv;

	npu_debugfs_deinit(npu_dev);
	sysfs_remove_group(&npu_dev->device->kobj, &npu_fs_attr_group);
	device_destroy(npu_dev->class, npu_dev->dev_num);
	class_destroy(npu_dev->class);
	cdev_del(&npu_dev->cdev);
	unregister_chrdev_region(npu_dev->dev_num, 1);
	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);
	free_pages((unsigned long)npu_dev->bufs, npu_dev->order);
	bitmap_free(npu_dev->rbufs.bitmap);
	bitmap_free(npu_dev->sbufs.bitmap);
	kfree(npu_dev);
	vdev->priv = NULL;
}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_NPU, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	VIRTIO_NPU_F_MULTI_BUF,
};

static struct virtio_driver virtio_npu_driver = {
	.feature_table		= features,
	.feature_table_size	= ARRAY_SIZE(features),
	.driver.name		= KBUILD_MODNAME,
	.driver.owner		= THIS_MODULE,
	.id_table		= id_table,
	.probe			= virt_npu_probe,
	.remove			= virt_npu_remove,
};

static int __init virtio_npu_init(void)
{
	return register_virtio_driver(&virtio_npu_driver);
}

static void __exit virtio_npu_exit(void)
{
	unregister_virtio_driver(&virtio_npu_driver);
}
module_init(virtio_npu_init);
module_exit(virtio_npu_exit);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio NPU driver");
MODULE_LICENSE("GPL");
