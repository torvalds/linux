// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/rbtree.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/suspend.h>
#include <linux/types.h>
#include <soc/qcom/rpm-smd.h>
#include <soc/qcom/mpm.h>
#include <linux/delay.h>

#define CREATE_TRACE_POINTS
#include <trace/events/trace_rpm_smd.h>

#define DEFAULT_BUFFER_SIZE 256
#define DEBUG_PRINT_BUFFER_SIZE 512
#define MAX_SLEEP_BUFFER 128
#define INV_RSC "resource does not exist"
#define ERR "err\0"
#define MAX_ERR_BUFFER_SIZE 128
#define MAX_WAIT_ON_ACK 24
#define INIT_ERROR 1
#define V1_PROTOCOL_VERSION 0x31726576 /* rev1 */
#define V0_PROTOCOL_VERSION 0 /* rev0 */
#define RPM_MSG_TYPE_OFFSET 16
#define RPM_MSG_TYPE_SIZE 8
#define RPM_SET_TYPE_OFFSET 28
#define RPM_SET_TYPE_SIZE 4
#define RPM_REQ_LEN_OFFSET 0
#define RPM_REQ_LEN_SIZE 16
#define RPM_MSG_VERSION_OFFSET 24
#define RPM_MSG_VERSION_SIZE 8
#define RPM_MSG_VERSION 1
#define RPM_MSG_SET_OFFSET 28
#define RPM_MSG_SET_SIZE 4
#define RPM_RSC_ID_OFFSET 16
#define RPM_RSC_ID_SIZE 12
#define RPM_DATA_LEN_OFFSET 0
#define RPM_DATA_LEN_SIZE 16
#define RPM_HDR_SIZE ((rpm_msg_fmt_ver == RPM_MSG_V0_FMT) ?\
		sizeof(struct rpm_v0_hdr) : sizeof(struct rpm_v1_hdr))
#define CLEAR_FIELD(offset, size) (~GENMASK(offset + size - 1, offset))

#define for_each_kvp(buf, k) \
	for (k = (struct kvp *)get_first_kvp(buf); \
		((void *)k - (void *)get_first_kvp(buf)) < \
		 get_data_len(buf);\
		k = get_next_kvp(k))


#ifdef CONFIG_ARM
#define readq_relaxed(a) ({			\
	u64 val = readl_relaxed((a) + 4);	\
	val <<= 32;				\
	val |=  readl_relaxed((a));		\
	val;					\
})
#endif

/* Debug Definitions */
enum {
	MSM_RPM_LOG_REQUEST_PRETTY	= BIT(0),
	MSM_RPM_LOG_REQUEST_RAW		= BIT(1),
	MSM_RPM_LOG_REQUEST_SHOW_MSG_ID	= BIT(2),
};

static int msm_rpm_debug_mask;
module_param_named(
	debug_mask, msm_rpm_debug_mask, int, 0644
);

static uint32_t rpm_msg_fmt_ver;

struct msm_rpm_driver_data {
	const char *ch_name;
	uint32_t ch_type;
	struct smd_channel *ch_info;
	struct work_struct work;
	struct completion smd_open;
};

struct qcom_smd_rpm {
	struct rpmsg_endpoint *rpm_channel;
	struct device *dev;
	int irq;
	struct completion ack;
	struct mutex lock;
	int ack_status;
	struct notifier_block genpd_nb;
	bool use_rpmsg_no_sleep;
};

struct qcom_smd_rpm *rpm;
struct qcom_smd_rpm priv_rpm;

static bool standalone;
static int probe_status = -EPROBE_DEFER;
static void msm_rpm_process_ack(uint32_t msg_id, int errno);

enum {
	MSM_RPM_MSG_REQUEST_TYPE = 0,
	MSM_RPM_MSG_TYPE_NR,
};

static const uint32_t msm_rpm_request_service_v1[MSM_RPM_MSG_TYPE_NR] = {
	0x716572, /* 'req\0' */
};

enum {
	RPM_V1_REQUEST_SERVICE,
	RPM_V1_SYSTEMDB_SERVICE,
	RPM_V1_COMMAND_SERVICE,
	RPM_V1_ACK_SERVICE,
	RPM_V1_NACK_SERVICE,
} msm_rpm_request_service_v2;

struct rpm_v0_hdr {
	uint32_t service_type;
	uint32_t request_len;
};

struct rpm_v1_hdr {
	uint32_t request_hdr;
};

struct rpm_message_header_v0 {
	struct rpm_v0_hdr hdr;
	uint32_t msg_id;
	enum msm_rpm_set set;
	uint32_t resource_type;
	uint32_t resource_id;
	uint32_t data_len;
};

struct rpm_message_header_v1 {
	struct rpm_v1_hdr hdr;
	uint32_t msg_id;
	uint32_t resource_type;
	uint32_t request_details;
};

struct msm_rpm_ack_msg_v0 {
	uint32_t req;
	uint32_t req_len;
	uint32_t rsc_id;
	uint32_t msg_len;
	uint32_t id_ack;
};

struct msm_rpm_ack_msg_v1 {
	uint32_t request_hdr;
	uint32_t id_ack;
};

struct kvp {
	unsigned int k;
	unsigned int s;
};

struct msm_rpm_kvp_data {
	uint32_t key;
	uint32_t nbytes; /* number of bytes */
	uint8_t *value;
	bool valid;
};

struct slp_buf {
	struct rb_node node;
	char ubuf[MAX_SLEEP_BUFFER];
	char *buf;
	bool valid;
};

enum rpm_msg_fmts {
	RPM_MSG_V0_FMT,
	RPM_MSG_V1_FMT
};

static struct rb_root tr_root = RB_ROOT;
static uint32_t msm_rpm_get_next_msg_id(void);

static inline uint32_t get_offset_value(uint32_t val, uint32_t offset,
		uint32_t size)
{
	return (((val) & GENMASK(offset + size - 1, offset))
		>> offset);
}

static inline void change_offset_value(uint32_t *val, uint32_t offset,
		uint32_t size, int32_t val1)
{
	uint32_t member = *val;
	uint32_t offset_val = get_offset_value(member, offset, size);
	uint32_t mask = (1 << size) - 1;

	offset_val += val1;
	*val &= CLEAR_FIELD(offset, size);
	*val |= ((offset_val & mask) << offset);
}

static inline void set_offset_value(uint32_t *val, uint32_t offset,
		uint32_t size, uint32_t val1)
{
	uint32_t mask = (1 << size) - 1;

	*val &= CLEAR_FIELD(offset, size);
	*val |= ((val1 & mask) << offset);
}

static uint32_t get_msg_id(char *buf)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		return ((struct rpm_message_header_v0 *)buf)->msg_id;

	return ((struct rpm_message_header_v1 *)buf)->msg_id;

}

static uint32_t get_ack_msg_id(char *buf)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		return ((struct msm_rpm_ack_msg_v0 *)buf)->id_ack;

	return ((struct msm_rpm_ack_msg_v1 *)buf)->id_ack;

}

static uint32_t get_rsc_type(char *buf)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		return ((struct rpm_message_header_v0 *)buf)->resource_type;

	return ((struct rpm_message_header_v1 *)buf)->resource_type;

}

static uint32_t get_set_type(char *buf)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		return ((struct rpm_message_header_v0 *)buf)->set;

	return get_offset_value(((struct rpm_message_header_v1 *)buf)->
			request_details, RPM_SET_TYPE_OFFSET,
			RPM_SET_TYPE_SIZE);
}

static uint32_t get_data_len(char *buf)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		return ((struct rpm_message_header_v0 *)buf)->data_len;

	return get_offset_value(((struct rpm_message_header_v1 *)buf)->
			request_details, RPM_DATA_LEN_OFFSET,
			RPM_DATA_LEN_SIZE);
}

static uint32_t get_rsc_id(char *buf)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		return ((struct rpm_message_header_v0 *)buf)->resource_id;

	return get_offset_value(((struct rpm_message_header_v1 *)buf)->
			request_details, RPM_RSC_ID_OFFSET,
			RPM_RSC_ID_SIZE);
}

static uint32_t get_ack_req_len(char *buf)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		return ((struct msm_rpm_ack_msg_v0 *)buf)->req_len;

	return get_offset_value(((struct msm_rpm_ack_msg_v1 *)buf)->
			request_hdr, RPM_REQ_LEN_OFFSET,
			RPM_REQ_LEN_SIZE);
}

static uint32_t get_ack_msg_type(char *buf)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		return ((struct msm_rpm_ack_msg_v0 *)buf)->req;

	return get_offset_value(((struct msm_rpm_ack_msg_v1 *)buf)->
			request_hdr, RPM_MSG_TYPE_OFFSET,
			RPM_MSG_TYPE_SIZE);
}

static uint32_t get_req_len(char *buf)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		return ((struct rpm_message_header_v0 *)buf)->hdr.request_len;

	return get_offset_value(((struct rpm_message_header_v1 *)buf)->
			hdr.request_hdr, RPM_REQ_LEN_OFFSET,
			RPM_REQ_LEN_SIZE);
}

static void set_msg_ver(char *buf, uint32_t val)
{
	if (rpm_msg_fmt_ver) {
		set_offset_value(&((struct rpm_message_header_v1 *)buf)->
			hdr.request_hdr, RPM_MSG_VERSION_OFFSET,
			RPM_MSG_VERSION_SIZE, val);
	} else {
		set_offset_value(&((struct rpm_message_header_v1 *)buf)->
			hdr.request_hdr, RPM_MSG_VERSION_OFFSET,
			RPM_MSG_VERSION_SIZE, 0);
	}
}

static void set_req_len(char *buf, uint32_t val)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		((struct rpm_message_header_v0 *)buf)->hdr.request_len = val;
	else {
		set_offset_value(&((struct rpm_message_header_v1 *)buf)->
			hdr.request_hdr, RPM_REQ_LEN_OFFSET,
			RPM_REQ_LEN_SIZE, val);
	}
}

static void change_req_len(char *buf, int32_t val)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		((struct rpm_message_header_v0 *)buf)->hdr.request_len += val;
	else {
		change_offset_value(&((struct rpm_message_header_v1 *)buf)->
			hdr.request_hdr, RPM_REQ_LEN_OFFSET,
			RPM_REQ_LEN_SIZE, val);
	}
}

static void set_msg_type(char *buf, uint32_t val)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		((struct rpm_message_header_v0 *)buf)->hdr.service_type =
			msm_rpm_request_service_v1[val];
	else {
		set_offset_value(&((struct rpm_message_header_v1 *)buf)->
			hdr.request_hdr, RPM_MSG_TYPE_OFFSET,
			RPM_MSG_TYPE_SIZE, RPM_V1_REQUEST_SERVICE);
	}
}

static void set_rsc_id(char *buf, uint32_t val)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		((struct rpm_message_header_v0 *)buf)->resource_id = val;
	else
		set_offset_value(&((struct rpm_message_header_v1 *)buf)->
			request_details, RPM_RSC_ID_OFFSET,
			RPM_RSC_ID_SIZE, val);
}

static void set_data_len(char *buf, uint32_t val)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		((struct rpm_message_header_v0 *)buf)->data_len = val;
	else
		set_offset_value(&((struct rpm_message_header_v1 *)buf)->
			request_details, RPM_DATA_LEN_OFFSET,
			RPM_DATA_LEN_SIZE, val);
}
static void change_data_len(char *buf, int32_t val)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		((struct rpm_message_header_v0 *)buf)->data_len += val;
	else
		change_offset_value(&((struct rpm_message_header_v1 *)buf)->
			request_details, RPM_DATA_LEN_OFFSET,
			RPM_DATA_LEN_SIZE, val);
}

static void set_set_type(char *buf, uint32_t val)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		((struct rpm_message_header_v0 *)buf)->set = val;
	else
		set_offset_value(&((struct rpm_message_header_v1 *)buf)->
			request_details, RPM_SET_TYPE_OFFSET,
			RPM_SET_TYPE_SIZE, val);
}

static void set_msg_id(char *buf, uint32_t val)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		((struct rpm_message_header_v0 *)buf)->msg_id = val;
	else
		((struct rpm_message_header_v1 *)buf)->msg_id = val;

}

static void set_rsc_type(char *buf, uint32_t val)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		((struct rpm_message_header_v0 *)buf)->resource_type = val;
	else
		((struct rpm_message_header_v1 *)buf)->resource_type = val;
}

static inline int get_buf_len(char *buf)
{
	return get_req_len(buf) + RPM_HDR_SIZE;
}

static inline struct kvp *get_first_kvp(char *buf)
{
	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		return (struct kvp *)(buf +
				sizeof(struct rpm_message_header_v0));
	else
		return (struct kvp *)(buf +
				sizeof(struct rpm_message_header_v1));
}

static inline struct kvp *get_next_kvp(struct kvp *k)
{
	return (struct kvp *)((void *)k + sizeof(*k) + k->s);
}

static inline void *get_data(struct kvp *k)
{
	return (void *)k + sizeof(*k);
}

static void delete_kvp(char *buf, struct kvp *d)
{
	struct kvp *n;
	int dec;
	uint32_t size;

	n = get_next_kvp(d);
	dec = (void *)n - (void *)d;
	size = get_data_len(buf) -
		((void *)n - (void *)get_first_kvp(buf));

	memcpy((void *)d, (void *)n, size);

	change_data_len(buf, -dec);
	change_req_len(buf, -dec);
}

static inline void update_kvp_data(struct kvp *dest, struct kvp *src)
{
	memcpy(get_data(dest), get_data(src), src->s);
}

static void add_kvp(char *buf, struct kvp *n)
{
	int32_t inc = sizeof(*n) + n->s;

	if (get_req_len(buf) + inc > MAX_SLEEP_BUFFER) {
		WARN_ON(get_req_len(buf) + inc > MAX_SLEEP_BUFFER);
		return;
	}

	memcpy(buf + get_buf_len(buf), n, inc);

	change_data_len(buf, inc);
	change_req_len(buf, inc);
}

static struct slp_buf *tr_search(struct rb_root *root, char *slp)
{
	unsigned int type = get_rsc_type(slp);
	unsigned int id = get_rsc_id(slp);
	struct rb_node *node = root->rb_node;

	while (node) {
		struct slp_buf *cur = rb_entry(node, struct slp_buf, node);
		unsigned int ctype = get_rsc_type(cur->buf);
		unsigned int cid = get_rsc_id(cur->buf);

		if (type < ctype)
			node = node->rb_left;
		else if (type > ctype)
			node = node->rb_right;
		else if (id < cid)
			node = node->rb_left;
		else if (id > cid)
			node = node->rb_right;
		else
			return cur;
	}
	return NULL;
}

static int tr_insert(struct rb_root *root, struct slp_buf *slp)
{
	unsigned int type = get_rsc_type(slp->buf);
	unsigned int id = get_rsc_id(slp->buf);
	struct rb_node **node = &(root->rb_node), *parent = NULL;

	while (*node) {
		struct slp_buf *curr = rb_entry(*node, struct slp_buf, node);
		unsigned int ctype = get_rsc_type(curr->buf);
		unsigned int cid = get_rsc_id(curr->buf);

		parent = *node;

		if (type < ctype)
			node = &((*node)->rb_left);
		else if (type > ctype)
			node = &((*node)->rb_right);
		else if (id < cid)
			node = &((*node)->rb_left);
		else if (id > cid)
			node = &((*node)->rb_right);
		else
			return -EINVAL;
	}

	rb_link_node(&slp->node, parent, node);
	rb_insert_color(&slp->node, root);
	slp->valid = true;
	return 0;
}

static void tr_update(struct slp_buf *s, char *buf)
{
	struct kvp *e, *n;

	for_each_kvp(buf, n) {
		bool found = false;

		for_each_kvp(s->buf, e) {
			if (n->k == e->k) {
				found = true;
				if (n->s == e->s) {
					void *e_data = get_data(e);
					void *n_data = get_data(n);

					if (memcmp(e_data, n_data, n->s)) {
						update_kvp_data(e, n);
						s->valid = true;
					}
				} else {
					delete_kvp(s->buf, e);
					add_kvp(s->buf, n);
					s->valid = true;
				}
				break;
			}

		}
		if (!found) {
			add_kvp(s->buf, n);
			s->valid = true;
		}
	}
}
static atomic_t msm_rpm_msg_id = ATOMIC_INIT(0);

struct msm_rpm_request {
	uint8_t *client_buf;
	struct msm_rpm_kvp_data *kvp;
	uint32_t num_elements;
	uint32_t write_idx;
	uint8_t *buf;
	uint32_t numbytes;
};

/*
 * Data related to message acknowledgment
 */

LIST_HEAD(msm_rpm_wait_list);

struct msm_rpm_wait_data {
	struct list_head list;
	uint32_t msg_id;
	bool ack_recd;
	int errno;
	struct completion ack;
	bool delete_on_ack;
};

DEFINE_SPINLOCK(msm_rpm_list_lock);
LIST_HEAD(msm_rpm_ack_list);

static inline uint32_t msm_rpm_get_msg_id_from_ack(uint8_t *buf)
{
	return get_ack_msg_id(buf);
}

static inline int msm_rpm_get_error_from_ack(uint8_t *buf)
{
	uint8_t *tmp;
	uint32_t req_len = get_ack_req_len(buf);
	uint32_t msg_type = get_ack_msg_type(buf);
	int rc = -ENODEV;
	uint32_t err;
	uint32_t ack_msg_size = rpm_msg_fmt_ver ?
			sizeof(struct msm_rpm_ack_msg_v1) :
			sizeof(struct msm_rpm_ack_msg_v0);

	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT &&
			msg_type == RPM_V1_ACK_SERVICE) {
		return 0;
	} else if (rpm_msg_fmt_ver && msg_type == RPM_V1_NACK_SERVICE) {
		err = *(uint32_t *)(buf + sizeof(struct msm_rpm_ack_msg_v1));
		return err;
	}

	req_len -= ack_msg_size;
	req_len += 2 * sizeof(uint32_t);
	if (!req_len)
		return 0;

	pr_err("%s:rpm returned error or nack req_len: %d id_ack: %d\n",
				__func__, req_len, get_ack_msg_id(buf));

	tmp = buf + ack_msg_size;

	if (memcmp(tmp, ERR, sizeof(uint32_t))) {
		pr_err("%s rpm returned error\n", __func__);
		WARN_ON(1);
	}

	tmp += 2 * sizeof(uint32_t);

	if (!(memcmp(tmp, INV_RSC, min_t(uint32_t, req_len,
						sizeof(INV_RSC))-1))) {
		pr_err("%s(): RPM NACK Unsupported resource\n", __func__);
		rc = -EINVAL;
	} else {
		pr_err("%s(): RPM NACK Invalid header\n", __func__);
	}

	return rc;
}

int msm_rpm_smd_buffer_request(struct msm_rpm_request *cdata,
		uint32_t size, gfp_t flag)
{
	struct slp_buf *slp;
	static DEFINE_SPINLOCK(slp_buffer_lock);
	unsigned long flags;
	char *buf;

	buf = cdata->buf;

	if (size > MAX_SLEEP_BUFFER)
		return -ENOMEM;

	spin_lock_irqsave(&slp_buffer_lock, flags);
	slp = tr_search(&tr_root, buf);

	if (!slp) {
		slp = kzalloc(sizeof(struct slp_buf), GFP_ATOMIC);
		if (!slp) {
			spin_unlock_irqrestore(&slp_buffer_lock, flags);
			return -ENOMEM;
		}
		slp->buf = PTR_ALIGN(&slp->ubuf[0], sizeof(u32));
		memcpy(slp->buf, buf, size);
		if (tr_insert(&tr_root, slp)) {
			pr_err("Error updating sleep request\n");
			kfree(slp);
			spin_unlock_irqrestore(&slp_buffer_lock, flags);
			return -EINVAL;
		}
	} else {
		/* handle unsent requests */
		tr_update(slp, buf);
	}
	trace_rpm_smd_sleep_set(get_msg_id(cdata->client_buf),
			get_rsc_type(cdata->client_buf),
			get_req_len(cdata->client_buf));

	spin_unlock_irqrestore(&slp_buffer_lock, flags);

	return 0;
}

static struct msm_rpm_driver_data msm_rpm_data = {
	.smd_open = COMPLETION_INITIALIZER(msm_rpm_data.smd_open),
};

static int trysend_count = 20;
static int msm_rpm_trysend_smd_buffer(char *buf, uint32_t size)
{
	int ret;
	int count = 0;

	do {
		ret = rpmsg_trysend(rpm->rpm_channel, buf, size);
		if (!ret)
			break;
		udelay(10);
		count++;
	} while (count < trysend_count);

	return ret;
}

static int msm_rpm_flush_requests(void)
{
	struct rb_node *t;
	int ret;
	int count = 0;

	for (t = rb_first(&tr_root); t; t = rb_next(t)) {

		struct slp_buf *s = rb_entry(t, struct slp_buf, node);
		unsigned int type = get_rsc_type(s->buf);
		unsigned int id = get_rsc_id(s->buf);

		if (!s->valid)
			continue;

		set_msg_id(s->buf, msm_rpm_get_next_msg_id());

		if (rpm->use_rpmsg_no_sleep)
			ret = msm_rpm_trysend_smd_buffer(s->buf, get_buf_len(s->buf));
		else
			ret = rpmsg_send(rpm->rpm_channel, s->buf, get_buf_len(s->buf));

		WARN_ON(ret != 0);
		trace_rpm_smd_send_sleep_set(get_msg_id(s->buf), type, id);

		s->valid = false;
		count++;

		/*
		 * RPM acks need to be handled here if we have sent 24
		 * messages such that we do not overrun SMD buffer. Since
		 * we expect only sleep sets at this point (RPM PC would be
		 * disallowed if we had pending active requests), we need not
		 * process these sleep set acks.
		 */
		if (count >= MAX_WAIT_ON_ACK) {
			pr_err("Error: more than %d requests are buffered\n",
							MAX_WAIT_ON_ACK);
			return -ENOSPC;
		}
	}
	return 0;
}

static int msm_rpm_add_kvp_data_common(struct msm_rpm_request *handle,
		uint32_t key, const uint8_t *data, int size)
{
	uint32_t i;
	uint32_t data_size, msg_size;

	if (probe_status)
		return probe_status;

	if (!handle || !data) {
		pr_err("%s(): Invalid handle/data\n", __func__);
		return -EINVAL;
	}

	if (size < 0)
		return  -EINVAL;

	data_size = ALIGN(size, SZ_4);
	msg_size = data_size + 8;

	for (i = 0; i < handle->write_idx; i++) {
		if (handle->kvp[i].key != key)
			continue;
		if (handle->kvp[i].nbytes != data_size) {
			kfree(handle->kvp[i].value);
			handle->kvp[i].value = NULL;
		} else {
			if (!memcmp(handle->kvp[i].value, data, data_size))
				return 0;
		}
		break;
	}

	if (i >= handle->num_elements) {
		pr_err("Number of resources exceeds max allocated\n");
		return -ENOMEM;
	}

	if (i == handle->write_idx)
		handle->write_idx++;

	if (!handle->kvp[i].value) {
		handle->kvp[i].value = kzalloc(data_size, GFP_NOIO);

		if (!handle->kvp[i].value)
			return -ENOMEM;
	} else {
		/*
		 * We enter the else case, if a key already exists but the
		 * data doesn't match. In which case, we should zero the data
		 * out.
		 */
		memset(handle->kvp[i].value, 0, data_size);
	}

	if (!handle->kvp[i].valid)
		change_data_len(handle->client_buf, msg_size);
	else
		change_data_len(handle->client_buf,
			(data_size - handle->kvp[i].nbytes));

	handle->kvp[i].nbytes = data_size;
	handle->kvp[i].key = key;
	memcpy(handle->kvp[i].value, data, size);
	handle->kvp[i].valid = true;

	return 0;

}

static struct msm_rpm_request *msm_rpm_create_request_common(
		enum msm_rpm_set set, uint32_t rsc_type, uint32_t rsc_id,
		int num_elements)
{
	struct msm_rpm_request *cdata;
	uint32_t buf_size;

	if (probe_status)
		return ERR_PTR(probe_status);

	cdata = kzalloc(sizeof(struct msm_rpm_request), GFP_NOIO);

	if (!cdata)
		goto cdata_alloc_fail;

	if (rpm_msg_fmt_ver == RPM_MSG_V0_FMT)
		buf_size = sizeof(struct rpm_message_header_v0);
	else
		buf_size = sizeof(struct rpm_message_header_v1);

	cdata->client_buf = kzalloc(buf_size, GFP_NOIO);

	if (!cdata->client_buf)
		goto client_buf_alloc_fail;

	set_set_type(cdata->client_buf, set);
	set_rsc_type(cdata->client_buf, rsc_type);
	set_rsc_id(cdata->client_buf, rsc_id);

	cdata->num_elements = num_elements;
	cdata->write_idx = 0;

	cdata->kvp = kcalloc(num_elements, sizeof(struct msm_rpm_kvp_data),
				GFP_NOIO);

	if (!cdata->kvp) {
		pr_warn("%s(): Cannot allocate memory for key value data\n",
				__func__);
		goto kvp_alloc_fail;
	}

	cdata->buf = kzalloc(DEFAULT_BUFFER_SIZE, GFP_NOIO);

	if (!cdata->buf)
		goto buf_alloc_fail;

	cdata->numbytes = DEFAULT_BUFFER_SIZE;
	return cdata;

buf_alloc_fail:
	kfree(cdata->kvp);
kvp_alloc_fail:
	kfree(cdata->client_buf);
client_buf_alloc_fail:
	kfree(cdata);
cdata_alloc_fail:
	return NULL;

}

void msm_rpm_free_request(struct msm_rpm_request *handle)
{
	int i;

	if (!handle)
		return;
	for (i = 0; i < handle->num_elements; i++)
		kfree(handle->kvp[i].value);
	kfree(handle->kvp);
	kfree(handle->client_buf);
	kfree(handle->buf);
	kfree(handle);
}
EXPORT_SYMBOL_GPL(msm_rpm_free_request);

struct msm_rpm_request *msm_rpm_create_request(
		enum msm_rpm_set set, uint32_t rsc_type,
		uint32_t rsc_id, int num_elements)
{
	return msm_rpm_create_request_common(set, rsc_type, rsc_id,
			num_elements);
}
EXPORT_SYMBOL_GPL(msm_rpm_create_request);

int msm_rpm_add_kvp_data(struct msm_rpm_request *handle,
		uint32_t key, const uint8_t *data, int size)
{
	return msm_rpm_add_kvp_data_common(handle, key, data, size);

}
EXPORT_SYMBOL_GPL(msm_rpm_add_kvp_data);

int msm_rpm_add_kvp_data_noirq(struct msm_rpm_request *handle,
		uint32_t key, const uint8_t *data, int size)
{
	return msm_rpm_add_kvp_data_common(handle, key, data, size);

}
EXPORT_SYMBOL_GPL(msm_rpm_add_kvp_data_noirq);

bool msm_rpm_waiting_for_ack(void)
{
	bool ret;
	unsigned long flags;

	spin_lock_irqsave(&msm_rpm_list_lock, flags);
	ret = list_empty(&msm_rpm_wait_list);
	spin_unlock_irqrestore(&msm_rpm_list_lock, flags);

	return !ret;
}
EXPORT_SYMBOL_GPL(msm_rpm_waiting_for_ack);

static struct msm_rpm_wait_data *msm_rpm_get_entry_from_msg_id(uint32_t msg_id)
{
	struct list_head *ptr;
	struct msm_rpm_wait_data *elem = NULL;
	unsigned long flags;

	spin_lock_irqsave(&msm_rpm_list_lock, flags);

	list_for_each(ptr, &msm_rpm_wait_list) {
		elem = list_entry(ptr, struct msm_rpm_wait_data, list);
		if (elem && (elem->msg_id == msg_id))
			break;
		elem = NULL;
	}

	spin_unlock_irqrestore(&msm_rpm_list_lock, flags);
	return elem;
}

static uint32_t msm_rpm_get_next_msg_id(void)
{
	uint32_t id;

	/*
	 * A message id of 0 is used by the driver to indicate a error
	 * condition. The RPM driver uses a id of 1 to indicate unsent data
	 * when the data sent over hasn't been modified. This isn't a error
	 * scenario and wait for ack returns a success when the message id is 1.
	 */

	do {
		id = atomic_inc_return(&msm_rpm_msg_id);
	} while ((id == 0) || (id == 1) || msm_rpm_get_entry_from_msg_id(id));

	return id;
}

static int msm_rpm_add_wait_list(uint32_t msg_id, bool delete_on_ack)
{
	unsigned long flags;
	struct msm_rpm_wait_data *data =
		kzalloc(sizeof(struct msm_rpm_wait_data), GFP_ATOMIC);

	if (!data)
		return -ENOMEM;

	init_completion(&data->ack);
	data->ack_recd = false;
	data->msg_id = msg_id;
	data->errno = INIT_ERROR;
	data->delete_on_ack = delete_on_ack;
	spin_lock_irqsave(&msm_rpm_list_lock, flags);
	if (delete_on_ack)
		list_add_tail(&data->list, &msm_rpm_wait_list);
	else
		list_add(&data->list, &msm_rpm_wait_list);
	spin_unlock_irqrestore(&msm_rpm_list_lock, flags);

	return 0;
}

static void msm_rpm_free_list_entry(struct msm_rpm_wait_data *elem)
{
	unsigned long flags;

	spin_lock_irqsave(&msm_rpm_list_lock, flags);
	list_del(&elem->list);
	spin_unlock_irqrestore(&msm_rpm_list_lock, flags);
	kfree(elem);
}

static void msm_rpm_process_ack(uint32_t msg_id, int errno)
{
	struct list_head *ptr, *next;
	struct msm_rpm_wait_data *elem = NULL;
	unsigned long flags;

	spin_lock_irqsave(&msm_rpm_list_lock, flags);

	list_for_each_safe(ptr, next, &msm_rpm_wait_list) {
		elem = list_entry(ptr, struct msm_rpm_wait_data, list);
		if (elem->msg_id == msg_id) {
			elem->errno = errno;
			elem->ack_recd = true;
			complete(&elem->ack);
			if (elem->delete_on_ack) {
				list_del(&elem->list);
				kfree(elem);
			}
			break;
		}
	}
	/*
	 * Special case where the sleep driver doesn't
	 * wait for ACKs. This would decrease the latency involved with
	 * entering RPM assisted power collapse.
	 */

	if (!elem)
		trace_rpm_smd_ack_recvd(0, msg_id, 0xDEADBEEF);

	spin_unlock_irqrestore(&msm_rpm_list_lock, flags);
}

struct msm_rpm_kvp_packet {
	uint32_t id;
	uint32_t len;
	uint32_t val;
};

static void msm_rpm_log_request(struct msm_rpm_request *cdata)
{
	char buf[DEBUG_PRINT_BUFFER_SIZE];
	size_t buflen = DEBUG_PRINT_BUFFER_SIZE;
	char name[5];
	u32 value;
	uint32_t i;
	int j, prev_valid;
	int valid_count = 0;
	int pos = 0;
	uint32_t res_type, rsc_id;

	name[4] = 0;

	for (i = 0; i < cdata->write_idx; i++)
		if (cdata->kvp[i].valid)
			valid_count++;

	pos += scnprintf(buf + pos, buflen - pos, "%sRPM req: ", KERN_INFO);
	if (msm_rpm_debug_mask & MSM_RPM_LOG_REQUEST_SHOW_MSG_ID)
		pos += scnprintf(buf + pos, buflen - pos, "msg_id=%u, ",
				get_msg_id(cdata->client_buf));
	pos += scnprintf(buf + pos, buflen - pos, "s=%s",
		(get_set_type(cdata->client_buf) ==
				MSM_RPM_CTX_ACTIVE_SET ? "act" : "slp"));

	res_type = get_rsc_type(cdata->client_buf);
	rsc_id = get_rsc_id(cdata->client_buf);
	if ((msm_rpm_debug_mask & MSM_RPM_LOG_REQUEST_PRETTY)
	    && (msm_rpm_debug_mask & MSM_RPM_LOG_REQUEST_RAW)) {
		/* Both pretty and raw formatting */
		memcpy(name, &res_type, sizeof(uint32_t));
		pos += scnprintf(buf + pos, buflen - pos,
			", rsc_type=0x%08X (%s), rsc_id=%u; ",
			res_type, name, rsc_id);

		for (i = 0, prev_valid = 0; i < cdata->write_idx; i++) {
			if (!cdata->kvp[i].valid)
				continue;

			memcpy(name, &cdata->kvp[i].key, sizeof(uint32_t));
			pos += scnprintf(buf + pos, buflen - pos,
					"[key=0x%08X (%s), value=%s",
					cdata->kvp[i].key, name,
					(cdata->kvp[i].nbytes ? "0x" : "null"));

			for (j = 0; j < cdata->kvp[i].nbytes; j++)
				pos += scnprintf(buf + pos, buflen - pos,
						"%02X ",
						cdata->kvp[i].value[j]);

			if (cdata->kvp[i].nbytes)
				pos += scnprintf(buf + pos, buflen - pos, "(");

			for (j = 0; j < cdata->kvp[i].nbytes; j += 4) {
				value = 0;
				memcpy(&value, &cdata->kvp[i].value[j],
					min_t(uint32_t, sizeof(uint32_t),
						cdata->kvp[i].nbytes - j));
				pos += scnprintf(buf + pos, buflen - pos, "%u",
						value);
				if (j + 4 < cdata->kvp[i].nbytes)
					pos += scnprintf(buf + pos,
						buflen - pos, " ");
			}
			if (cdata->kvp[i].nbytes)
				pos += scnprintf(buf + pos, buflen - pos, ")");
			pos += scnprintf(buf + pos, buflen - pos, "]");
			if (prev_valid + 1 < valid_count)
				pos += scnprintf(buf + pos, buflen - pos, ", ");
			prev_valid++;
		}
	} else if (msm_rpm_debug_mask & MSM_RPM_LOG_REQUEST_PRETTY) {
		/* Pretty formatting only */
		memcpy(name, &res_type, sizeof(uint32_t));
		pos += scnprintf(buf + pos, buflen - pos, " %s %u; ", name,
			rsc_id);

		for (i = 0, prev_valid = 0; i < cdata->write_idx; i++) {
			if (!cdata->kvp[i].valid)
				continue;

			memcpy(name, &cdata->kvp[i].key, sizeof(uint32_t));
			pos += scnprintf(buf + pos, buflen - pos, "%s=%s",
				name, (cdata->kvp[i].nbytes ? "" : "null"));

			for (j = 0; j < cdata->kvp[i].nbytes; j += 4) {
				value = 0;
				memcpy(&value, &cdata->kvp[i].value[j],
					min_t(uint32_t, sizeof(uint32_t),
						cdata->kvp[i].nbytes - j));
				pos += scnprintf(buf + pos, buflen - pos, "%u",
						value);

				if (j + 4 < cdata->kvp[i].nbytes)
					pos += scnprintf(buf + pos,
						buflen - pos, " ");
			}
			if (prev_valid + 1 < valid_count)
				pos += scnprintf(buf + pos, buflen - pos, ", ");
			prev_valid++;
		}
	} else {
		/* Raw formatting only */
		pos += scnprintf(buf + pos, buflen - pos,
			", rsc_type=0x%08X, rsc_id=%u; ", res_type, rsc_id);

		for (i = 0, prev_valid = 0; i < cdata->write_idx; i++) {
			if (!cdata->kvp[i].valid)
				continue;

			pos += scnprintf(buf + pos, buflen - pos,
					"[key=0x%08X, value=%s",
					cdata->kvp[i].key,
					(cdata->kvp[i].nbytes ? "0x" : "null"));
			for (j = 0; j < cdata->kvp[i].nbytes; j++) {
				pos += scnprintf(buf + pos, buflen - pos,
						"%02X",
						cdata->kvp[i].value[j]);
				if (j + 1 < cdata->kvp[i].nbytes)
					pos += scnprintf(buf + pos,
							buflen - pos, " ");
			}
			pos += scnprintf(buf + pos, buflen - pos, "]");
			if (prev_valid + 1 < valid_count)
				pos += scnprintf(buf + pos, buflen - pos, ", ");
			prev_valid++;
		}
	}

	pos += scnprintf(buf + pos, buflen - pos, "\n");
	pr_info("request info %s\n", buf);
}

static int msm_rpm_send_data(struct msm_rpm_request *cdata,
		int msg_type, bool noack)
{
	uint8_t *tmpbuff;
	int ret;
	uint32_t i;
	uint32_t msg_size;
	int msg_hdr_sz, req_hdr_sz;
	uint32_t data_len = get_data_len(cdata->client_buf);
	uint32_t set = get_set_type(cdata->client_buf);
	uint32_t msg_id;

	if (probe_status) {
		pr_err("probe failed\n");
		return probe_status;
	}
	if (!data_len) {
		pr_err("no data len\n");
		return 1;
	}

	msg_hdr_sz = rpm_msg_fmt_ver ? sizeof(struct rpm_message_header_v1) :
			sizeof(struct rpm_message_header_v0);

	req_hdr_sz = RPM_HDR_SIZE;
	set_msg_type(cdata->client_buf, msg_type);

	set_req_len(cdata->client_buf, data_len + msg_hdr_sz - req_hdr_sz);
	msg_size = get_req_len(cdata->client_buf) + req_hdr_sz;

	/* populate data_len */
	if (msg_size > cdata->numbytes) {
		kfree(cdata->buf);
		cdata->numbytes = msg_size;
		cdata->buf = kzalloc(msg_size, GFP_NOIO);
	}

	if (!cdata->buf) {
		pr_err("Failed malloc\n");
		return 0;
	}

	tmpbuff = cdata->buf;

	tmpbuff += msg_hdr_sz;
	for (i = 0; (i < cdata->write_idx); i++) {
		/* Sanity check */
		WARN_ON((tmpbuff - cdata->buf) > cdata->numbytes);

		if (!cdata->kvp[i].valid)
			continue;

		memcpy(tmpbuff, &cdata->kvp[i].key, sizeof(uint32_t));
		tmpbuff += sizeof(uint32_t);

		memcpy(tmpbuff, &cdata->kvp[i].nbytes, sizeof(uint32_t));
		tmpbuff += sizeof(uint32_t);

		memcpy(tmpbuff, cdata->kvp[i].value, cdata->kvp[i].nbytes);
		tmpbuff += cdata->kvp[i].nbytes;

	}

	memcpy(cdata->buf, cdata->client_buf, msg_hdr_sz);
	if ((set == MSM_RPM_CTX_SLEEP_SET) &&
		!msm_rpm_smd_buffer_request(cdata, msg_size, GFP_NOIO)) {
		return 1;
	}

	msg_id = msm_rpm_get_next_msg_id();
	/* Set the version bit for new protocol */
	set_msg_ver(cdata->buf, rpm_msg_fmt_ver);
	set_msg_id(cdata->buf, msg_id);
	set_msg_id(cdata->client_buf, msg_id);

	if (msm_rpm_debug_mask
	    & (MSM_RPM_LOG_REQUEST_PRETTY | MSM_RPM_LOG_REQUEST_RAW))
		msm_rpm_log_request(cdata);

	if (standalone) {
		for (i = 0; (i < cdata->write_idx); i++)
			cdata->kvp[i].valid = false;

		set_data_len(cdata->client_buf, 0);
		ret = msg_id;
		return ret;
	}

	msm_rpm_add_wait_list(msg_id, noack);

	ret = rpmsg_send(rpm->rpm_channel, &cdata->buf[0], msg_size);

	if (!ret) {
		for (i = 0; (i < cdata->write_idx); i++)
			cdata->kvp[i].valid = false;
		set_data_len(cdata->client_buf, 0);
		ret = msg_id;
		trace_rpm_smd_send_active_set(msg_id,
			get_rsc_type(cdata->client_buf),
			get_rsc_id(cdata->client_buf));
	} else if (ret < 0) {
		struct msm_rpm_wait_data *rc;

		ret = 0;
		pr_err("Failed to write data msg_size:%d ret:%d msg_id:%d\n",
				msg_size, ret, msg_id);
		rc = msm_rpm_get_entry_from_msg_id(msg_id);
		if (rc)
			msm_rpm_free_list_entry(rc);
	}
	return ret;
}

static int _msm_rpm_send_request(struct msm_rpm_request *handle, bool noack)
{
	int ret;
	static DEFINE_MUTEX(send_mtx);

	mutex_lock(&send_mtx);
	ret = msm_rpm_send_data(handle, MSM_RPM_MSG_REQUEST_TYPE, noack);
	mutex_unlock(&send_mtx);

	return ret;
}

int msm_rpm_send_request_noirq(struct msm_rpm_request *handle)
{
	return _msm_rpm_send_request(handle, false);
}
EXPORT_SYMBOL_GPL(msm_rpm_send_request_noirq);

int msm_rpm_send_request(struct msm_rpm_request *handle)
{
	return _msm_rpm_send_request(handle, false);
}
EXPORT_SYMBOL_GPL(msm_rpm_send_request);

void *msm_rpm_send_request_noack(struct msm_rpm_request *handle)
{
	int ret;

	ret = _msm_rpm_send_request(handle, true);

	return ret < 0 ? ERR_PTR(ret) : NULL;
}
EXPORT_SYMBOL_GPL(msm_rpm_send_request_noack);

int msm_rpm_wait_for_ack(uint32_t msg_id)
{
	struct msm_rpm_wait_data *elem;
	int rc = 0;

	if (!msg_id) {
		pr_err("Invalid msg id\n");
		return -ENOMEM;
	}

	if (msg_id == 1)
		return rc;

	if (standalone)
		return rc;

	elem = msm_rpm_get_entry_from_msg_id(msg_id);
	if (!elem)
		return rc;

	wait_for_completion(&elem->ack);
	trace_rpm_smd_ack_recvd(0, msg_id, 0xDEADFEED);

	rc = elem->errno;
	msm_rpm_free_list_entry(elem);

	return rc;
}
EXPORT_SYMBOL_GPL(msm_rpm_wait_for_ack);

int msm_rpm_wait_for_ack_noirq(uint32_t msg_id)
{
	return msm_rpm_wait_for_ack(msg_id);
}
EXPORT_SYMBOL_GPL(msm_rpm_wait_for_ack_noirq);

void *msm_rpm_send_message_noack(enum msm_rpm_set set, uint32_t rsc_type,
		uint32_t rsc_id, struct msm_rpm_kvp *kvp, int nelems)
{
	int i, rc;
	struct msm_rpm_request *req =
		msm_rpm_create_request_common(set, rsc_type, rsc_id, nelems);

	if (IS_ERR(req))
		return req;

	if (!req)
		return ERR_PTR(ENOMEM);

	for (i = 0; i < nelems; i++) {
		rc = msm_rpm_add_kvp_data(req, kvp[i].key,
				kvp[i].data, kvp[i].length);
		if (rc)
			goto bail;
	}

	rc = PTR_ERR(msm_rpm_send_request_noack(req));
bail:
	msm_rpm_free_request(req);
	return rc < 0 ? ERR_PTR(rc) : NULL;
}
EXPORT_SYMBOL_GPL(msm_rpm_send_message_noack);

int msm_rpm_send_message(enum msm_rpm_set set, uint32_t rsc_type,
		uint32_t rsc_id, struct msm_rpm_kvp *kvp, int nelems)
{
	int i, rc;
	struct msm_rpm_request *req =
		msm_rpm_create_request(set, rsc_type, rsc_id, nelems);

	if (IS_ERR(req))
		return PTR_ERR(req);

	if (!req)
		return -ENOMEM;

	for (i = 0; i < nelems; i++) {
		rc = msm_rpm_add_kvp_data(req, kvp[i].key,
				kvp[i].data, kvp[i].length);
		if (rc)
			goto bail;
	}

	rc = msm_rpm_wait_for_ack(msm_rpm_send_request(req));
bail:
	msm_rpm_free_request(req);
	return rc;
}
EXPORT_SYMBOL_GPL(msm_rpm_send_message);

int msm_rpm_send_message_noirq(enum msm_rpm_set set, uint32_t rsc_type,
			uint32_t rsc_id, struct msm_rpm_kvp *kvp, int nelems)
{

	return msm_rpm_send_message(set, rsc_type, rsc_id, kvp, nelems);
}
EXPORT_SYMBOL_GPL(msm_rpm_send_message_noirq);

static int smd_mask_receive_interrupt(bool mask,
		const struct cpumask *cpumask)
{
	struct irq_chip *irq_chip;
	struct irq_data *irq_data;

	irq_data = irq_get_irq_data(rpm->irq);
	if (!irq_data)
		return -ENODEV;

	irq_chip = irq_data->chip;
	if (!irq_chip)
		return -ENODEV;

	if (mask) {
		irq_chip->irq_mask(irq_data);
		if (cpumask && irq_chip->irq_set_affinity)
			irq_chip->irq_set_affinity(irq_data, cpumask, true);
	} else {
		irq_chip->irq_unmask(irq_data);
	}

	return 0;
}

/**
 * During power collapse, the rpm driver disables the SMD interrupts to make
 * sure that the interrupt doesn't wakes us from sleep.
 */
int msm_rpm_enter_sleep(struct cpumask *cpumask)
{
	int ret = 0;

	if (standalone)
		return 0;

	if (probe_status)
		return 0;

	if (cpumask == NULL)
		return -EINVAL;

	ret = smd_mask_receive_interrupt(true, cpumask);
	if (!ret) {
		ret = msm_rpm_flush_requests();
		if (ret)
			smd_mask_receive_interrupt(false, NULL);
	}

	return msm_mpm_enter_sleep(cpumask);
}
EXPORT_SYMBOL_GPL(msm_rpm_enter_sleep);

/**
 * When the system resumes from power collapse, the SMD interrupt disabled by
 * enter function has to reenabled to continue processing SMD message.
 */
void msm_rpm_exit_sleep(void)
{
	if (standalone)
		return;

	if (probe_status)
		return;

	smd_mask_receive_interrupt(false, NULL);
}
EXPORT_SYMBOL_GPL(msm_rpm_exit_sleep);

static int rpm_smd_power_cb(struct notifier_block *nb, unsigned long action, void *d)
{
	struct cpumask cpumask;
	unsigned int cpu = 0;

	switch (action) {
	case GENPD_NOTIFY_OFF:
		if (msm_rpm_waiting_for_ack())
			return NOTIFY_BAD;
		cpumask_copy(&cpumask, cpumask_of(cpu));
		if (msm_rpm_enter_sleep(&cpumask))
			return NOTIFY_BAD;

		break;
	case GENPD_NOTIFY_ON:
		msm_rpm_exit_sleep();
		break;
	}

	return NOTIFY_OK;
}

static int rpm_smd_pm_notifier(struct notifier_block *nb, unsigned long event, void *unused)
{
	int ret;

	if (event == PM_SUSPEND_PREPARE) {
		ret = msm_rpm_flush_requests();
		pr_debug("ret = %d\n", ret);
	}

	/* continue to suspend */
	return NOTIFY_OK;
}

static struct notifier_block rpm_smd_pm_nb = {
	.notifier_call = rpm_smd_pm_notifier,
};

static int qcom_smd_rpm_callback(struct rpmsg_device *rpdev, void *ptr,
				int size, void *priv, u32 addr)
{
	uint32_t msg_id;
	int errno;
	char buf[MAX_ERR_BUFFER_SIZE] = {0};
	struct msm_rpm_wait_data *elem;
	static DEFINE_SPINLOCK(rx_notify_lock);
	unsigned long flags;

	if (!size)
		return -EINVAL;

	WARN_ON(size > MAX_ERR_BUFFER_SIZE);

	spin_lock_irqsave(&rx_notify_lock, flags);
	memcpy(buf, ptr, size);
	msg_id = msm_rpm_get_msg_id_from_ack(buf);
	errno = msm_rpm_get_error_from_ack(buf);
	elem = msm_rpm_get_entry_from_msg_id(msg_id);

	/*
	 * It is applicable for sleep set requests
	 * Sleep set requests are not added to the
	 * wait queue list. Without this check we
	 * run into NULL pointer deferrence issue.
	 */
	if (!elem) {
		spin_unlock_irqrestore(&rx_notify_lock, flags);
		return 0;
	}

	msm_rpm_process_ack(msg_id, errno);
	spin_unlock_irqrestore(&rx_notify_lock, flags);

	return 0;
}

static int qcom_smd_rpm_probe(struct rpmsg_device *rpdev)
{
	char *key = NULL;
	struct device_node *p;
	struct platform_device *rpm_device;
	int ret = 0;
	int irq;
	void __iomem *reg_base;
	uint64_t version = V0_PROTOCOL_VERSION; /* set to default v0 format */

	p = of_find_compatible_node(NULL, NULL, "qcom,rpm-smd");
	if (!p) {
		pr_err("Unable to find rpm-smd\n");
		probe_status = -ENODEV;
		goto fail;
	}

	rpm_device = of_find_device_by_node(p);
	if (!rpm_device) {
		probe_status = -ENODEV;
		pr_err(" Unable to get rpm device structure\n");
		goto fail;
	}

	key = "rpm-standalone";
	standalone = of_property_read_bool(p, key);
	if (standalone) {
		probe_status = ret;
		pr_info("RPM running in standalone mode\n");
		return ret;
	}

	reg_base = of_iomap(p, 0);
	if (reg_base) {
		version = readq_relaxed(reg_base);
		iounmap(reg_base);
	}

	if (version == V1_PROTOCOL_VERSION)
		rpm_msg_fmt_ver = RPM_MSG_V1_FMT;

	pr_info("RPM-SMD running version %d\n", rpm_msg_fmt_ver);

	irq = of_irq_get(p, 0);
	if (!irq) {
		pr_err("Unable to get rpm-smd interrupt number\n");
		probe_status = -ENODEV;
		goto fail;
	}

	rpm = devm_kzalloc(&rpdev->dev, sizeof(*rpm), GFP_KERNEL);
	if (!rpm) {
		probe_status = -ENOMEM;
		goto fail;
	}

	ret = register_pm_notifier(&rpm_smd_pm_nb);
	if (ret) {
		pr_err("%s: power state notif error %d\n", __func__, ret);
		probe_status = -ENODEV;
		goto fail;
	}

	rpm->dev = &rpdev->dev;
	rpm->rpm_channel = rpdev->ept;
	dev_set_drvdata(&rpdev->dev, rpm);
	priv_rpm = *rpm;
	rpm->irq = irq;

	if (of_find_property(p, "power-domains", NULL)) {
		pm_runtime_enable(&rpm_device->dev);
		rpm->genpd_nb.notifier_call = rpm_smd_power_cb;
		ret = dev_pm_genpd_add_notifier(&rpm_device->dev, &rpm->genpd_nb);
		if (ret) {
			pm_runtime_disable(&rpm_device->dev);
			probe_status = ret;
			goto fail;
		}
	}

	key = "qcom,use-rpmsg-no-sleep";
	rpm->use_rpmsg_no_sleep = of_property_read_bool(p, key);

	mutex_init(&rpm->lock);
	init_completion(&rpm->ack);
	probe_status = 0;

fail:
	return probe_status;
}

static struct rpmsg_device_id rpmsg_driver_rpm_id_table[] = {
	{ .name	= "rpm_requests" },
	{ },
};

static struct rpmsg_driver qcom_smd_rpm_driver = {
	.probe = qcom_smd_rpm_probe,
	.callback = qcom_smd_rpm_callback,
	.id_table = rpmsg_driver_rpm_id_table,
	.drv  = {
		.name  = "qcom_rpm_smd",
		.owner = THIS_MODULE,
	},
};

static int rpm_driver_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *p = pdev->dev.of_node;

	ret = of_platform_populate(p, NULL, NULL, &pdev->dev);
	if (ret)
		return ret;

	ret = register_rpmsg_driver(&qcom_smd_rpm_driver);
	if (ret) {
		of_platform_depopulate(&pdev->dev);
		pr_err("register_rpmsg_driver: failed with err %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id rpm_of_match[] = {
	{ .compatible = "qcom,rpm-smd" },
	{},
};

struct platform_driver rpm_driver = {
	.probe = rpm_driver_probe,
	.driver  = {
		.name   = "rpm-smd",
		.of_match_table = rpm_of_match,
		.suppress_bind_attrs = true,
	},
};

int __init msm_rpm_driver_init(void)
{
	return platform_driver_register(&rpm_driver);
}

#ifdef MODULE
module_init(msm_rpm_driver_init);
#else
postcore_initcall_sync(msm_rpm_driver_init);
#endif
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. RPM-SMD Driver");
MODULE_LICENSE("GPL");
