/* ds.c: Domain Services driver for Logical Domains
 *
 * Copyright (C) 2007 David S. Miller <davem@davemloft.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include <asm/ldc.h>
#include <asm/vio.h>
#include <asm/power.h>

#define DRV_MODULE_NAME		"ds"
#define PFX DRV_MODULE_NAME	": "
#define DRV_MODULE_VERSION	"1.0"
#define DRV_MODULE_RELDATE	"Jul 11, 2007"

static char version[] __devinitdata =
	DRV_MODULE_NAME ".c:v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";
MODULE_AUTHOR("David S. Miller (davem@davemloft.net)");
MODULE_DESCRIPTION("Sun LDOM domain services driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

struct ds_msg_tag {
	__u32			type;
#define DS_INIT_REQ		0x00
#define DS_INIT_ACK		0x01
#define DS_INIT_NACK		0x02
#define DS_REG_REQ		0x03
#define DS_REG_ACK		0x04
#define DS_REG_NACK		0x05
#define DS_UNREG_REQ		0x06
#define DS_UNREG_ACK		0x07
#define DS_UNREG_NACK		0x08
#define DS_DATA			0x09
#define DS_NACK			0x0a

	__u32			len;
};

/* Result codes */
#define DS_OK			0x00
#define DS_REG_VER_NACK		0x01
#define DS_REG_DUP		0x02
#define DS_INV_HDL		0x03
#define DS_TYPE_UNKNOWN		0x04

struct ds_version {
	__u16			major;
	__u16			minor;
};

struct ds_ver_req {
	struct ds_msg_tag	tag;
	struct ds_version	ver;
};

struct ds_ver_ack {
	struct ds_msg_tag	tag;
	__u16			minor;
};

struct ds_ver_nack {
	struct ds_msg_tag	tag;
	__u16			major;
};

struct ds_reg_req {
	struct ds_msg_tag	tag;
	__u64			handle;
	__u16			major;
	__u16			minor;
	char			svc_id[0];
};

struct ds_reg_ack {
	struct ds_msg_tag	tag;
	__u64			handle;
	__u16			minor;
};

struct ds_reg_nack {
	struct ds_msg_tag	tag;
	__u64			handle;
	__u16			major;
};

struct ds_unreg_req {
	struct ds_msg_tag	tag;
	__u64			handle;
};

struct ds_unreg_ack {
	struct ds_msg_tag	tag;
	__u64			handle;
};

struct ds_unreg_nack {
	struct ds_msg_tag	tag;
	__u64			handle;
};

struct ds_data {
	struct ds_msg_tag	tag;
	__u64			handle;
};

struct ds_data_nack {
	struct ds_msg_tag	tag;
	__u64			handle;
	__u64			result;
};

struct ds_cap_state {
	__u64			handle;

	void			(*data)(struct ldc_channel *lp,
					struct ds_cap_state *dp,
					void *buf, int len);

	const char		*service_id;

	u8			state;
#define CAP_STATE_UNKNOWN	0x00
#define CAP_STATE_REG_SENT	0x01
#define CAP_STATE_REGISTERED	0x02
};

static int ds_send(struct ldc_channel *lp, void *data, int len)
{
	int err, limit = 1000;

	err = -EINVAL;
	while (limit-- > 0) {
		err = ldc_write(lp, data, len);
		if (!err || (err != -EAGAIN))
			break;
		udelay(1);
	}

	return err;
}

struct ds_md_update_req {
	__u64				req_num;
};

struct ds_md_update_res {
	__u64				req_num;
	__u32				result;
};

static void md_update_data(struct ldc_channel *lp,
			   struct ds_cap_state *dp,
			   void *buf, int len)
{
	struct ds_data *dpkt = buf;
	struct ds_md_update_req *rp;
	struct {
		struct ds_data		data;
		struct ds_md_update_res	res;
	} pkt;

	rp = (struct ds_md_update_req *) (dpkt + 1);

	printk(KERN_ERR PFX "MD update REQ [%lx] len=%d\n",
	       rp->req_num, len);

	memset(&pkt, 0, sizeof(pkt));
	pkt.data.tag.type = DS_DATA;
	pkt.data.tag.len = sizeof(pkt) - sizeof(struct ds_msg_tag);
	pkt.data.handle = dp->handle;
	pkt.res.req_num = rp->req_num;
	pkt.res.result = DS_OK;

	ds_send(lp, &pkt, sizeof(pkt));
}

struct ds_shutdown_req {
	__u64				req_num;
	__u32				ms_delay;
};

struct ds_shutdown_res {
	__u64				req_num;
	__u32				result;
	char				reason[1];
};

static void domain_shutdown_data(struct ldc_channel *lp,
				 struct ds_cap_state *dp,
				 void *buf, int len)
{
	struct ds_data *dpkt = buf;
	struct ds_shutdown_req *rp;
	struct {
		struct ds_data		data;
		struct ds_shutdown_res	res;
	} pkt;

	rp = (struct ds_shutdown_req *) (dpkt + 1);

	printk(KERN_ALERT PFX "Shutdown request from "
	       "LDOM manager received.\n");

	memset(&pkt, 0, sizeof(pkt));
	pkt.data.tag.type = DS_DATA;
	pkt.data.tag.len = sizeof(pkt) - sizeof(struct ds_msg_tag);
	pkt.data.handle = dp->handle;
	pkt.res.req_num = rp->req_num;
	pkt.res.result = DS_OK;
	pkt.res.reason[0] = 0;

	ds_send(lp, &pkt, sizeof(pkt));

	wake_up_powerd();
}

struct ds_panic_req {
	__u64				req_num;
};

struct ds_panic_res {
	__u64				req_num;
	__u32				result;
	char				reason[1];
};

static void domain_panic_data(struct ldc_channel *lp,
			      struct ds_cap_state *dp,
			      void *buf, int len)
{
	struct ds_data *dpkt = buf;
	struct ds_panic_req *rp;
	struct {
		struct ds_data		data;
		struct ds_panic_res	res;
	} pkt;

	rp = (struct ds_panic_req *) (dpkt + 1);

	printk(KERN_ERR PFX "Panic REQ [%lx], len=%d\n",
	       rp->req_num, len);

	memset(&pkt, 0, sizeof(pkt));
	pkt.data.tag.type = DS_DATA;
	pkt.data.tag.len = sizeof(pkt) - sizeof(struct ds_msg_tag);
	pkt.data.handle = dp->handle;
	pkt.res.req_num = rp->req_num;
	pkt.res.result = DS_OK;
	pkt.res.reason[0] = 0;

	ds_send(lp, &pkt, sizeof(pkt));

	panic("PANIC requested by LDOM manager.");
}

struct ds_cpu_tag {
	__u64				req_num;
	__u32				type;
#define DS_CPU_CONFIGURE		0x43
#define DS_CPU_UNCONFIGURE		0x55
#define DS_CPU_FORCE_UNCONFIGURE	0x46
#define DS_CPU_STATUS			0x53

/* Responses */
#define DS_CPU_OK			0x6f
#define DS_CPU_ERROR			0x65

	__u32				num_records;
};

struct ds_cpu_record {
	__u32				cpu_id;
};

static void dr_cpu_data(struct ldc_channel *lp,
			struct ds_cap_state *dp,
			void *buf, int len)
{
	struct ds_data *dpkt = buf;
	struct ds_cpu_tag *rp;

	rp = (struct ds_cpu_tag *) (dpkt + 1);

	printk(KERN_ERR PFX "CPU REQ [%lx:%x], len=%d\n",
	       rp->req_num, rp->type, len);
}

struct ds_pri_msg {
	__u64				req_num;
	__u64				type;
#define DS_PRI_REQUEST			0x00
#define DS_PRI_DATA			0x01
#define DS_PRI_UPDATE			0x02
};

static void ds_pri_data(struct ldc_channel *lp,
			struct ds_cap_state *dp,
			void *buf, int len)
{
	struct ds_data *dpkt = buf;
	struct ds_pri_msg *rp;

	rp = (struct ds_pri_msg *) (dpkt + 1);

	printk(KERN_ERR PFX "PRI REQ [%lx:%lx], len=%d\n",
	       rp->req_num, rp->type, len);
}

struct ds_cap_state ds_states[] = {
	{
		.service_id	= "md-update",
		.data		= md_update_data,
	},
	{
		.service_id	= "domain-shutdown",
		.data		= domain_shutdown_data,
	},
	{
		.service_id	= "domain-panic",
		.data		= domain_panic_data,
	},
	{
		.service_id	= "dr-cpu",
		.data		= dr_cpu_data,
	},
	{
		.service_id	= "pri",
		.data		= ds_pri_data,
	},
};

static struct ds_cap_state *find_cap(u64 handle)
{
	unsigned int index = handle >> 32;

	if (index >= ARRAY_SIZE(ds_states))
		return NULL;
	return &ds_states[index];
}

static DEFINE_SPINLOCK(ds_lock);

struct ds_info {
	struct ldc_channel	*lp;
	u8			hs_state;
#define DS_HS_START		0x01
#define DS_HS_DONE		0x02

	void			*rcv_buf;
	int			rcv_buf_len;
};

static void ds_conn_reset(struct ds_info *dp)
{
	printk(KERN_ERR PFX "ds_conn_reset() from %p\n",
	       __builtin_return_address(0));
}

static int register_services(struct ds_info *dp)
{
	struct ldc_channel *lp = dp->lp;
	int i;

	for (i = 0; i < ARRAY_SIZE(ds_states); i++) {
		struct {
			struct ds_reg_req req;
			u8 id_buf[256];
		} pbuf;
		struct ds_cap_state *cp = &ds_states[i];
		int err, msg_len;
		u64 new_count;

		if (cp->state == CAP_STATE_REGISTERED)
			continue;

		new_count = sched_clock() & 0xffffffff;
		cp->handle = ((u64) i << 32) | new_count;

		msg_len = (sizeof(struct ds_reg_req) +
			   strlen(cp->service_id));

		memset(&pbuf, 0, sizeof(pbuf));
		pbuf.req.tag.type = DS_REG_REQ;
		pbuf.req.tag.len = (msg_len - sizeof(struct ds_msg_tag));
		pbuf.req.handle = cp->handle;
		pbuf.req.major = 1;
		pbuf.req.minor = 0;
		strcpy(pbuf.req.svc_id, cp->service_id);

		err = ds_send(lp, &pbuf, msg_len);
		if (err > 0)
			cp->state = CAP_STATE_REG_SENT;
	}
	return 0;
}

static int ds_handshake(struct ds_info *dp, struct ds_msg_tag *pkt)
{

	if (dp->hs_state == DS_HS_START) {
		if (pkt->type != DS_INIT_ACK)
			goto conn_reset;

		dp->hs_state = DS_HS_DONE;

		return register_services(dp);
	}

	if (dp->hs_state != DS_HS_DONE)
		goto conn_reset;

	if (pkt->type == DS_REG_ACK) {
		struct ds_reg_ack *ap = (struct ds_reg_ack *) pkt;
		struct ds_cap_state *cp = find_cap(ap->handle);

		if (!cp) {
			printk(KERN_ERR PFX "REG ACK for unknown handle %lx\n",
			       ap->handle);
			return 0;
		}
		printk(KERN_INFO PFX "Registered %s service.\n",
		       cp->service_id);
		cp->state = CAP_STATE_REGISTERED;
	} else if (pkt->type == DS_REG_NACK) {
		struct ds_reg_nack *np = (struct ds_reg_nack *) pkt;
		struct ds_cap_state *cp = find_cap(np->handle);

		if (!cp) {
			printk(KERN_ERR PFX "REG NACK for "
			       "unknown handle %lx\n",
			       np->handle);
			return 0;
		}
		printk(KERN_ERR PFX "Could not register %s service\n",
		       cp->service_id);
		cp->state = CAP_STATE_UNKNOWN;
	}

	return 0;

conn_reset:
	ds_conn_reset(dp);
	return -ECONNRESET;
}

static int ds_data(struct ds_info *dp, struct ds_msg_tag *pkt, int len)
{
	struct ds_data *dpkt = (struct ds_data *) pkt;
	struct ds_cap_state *cp = find_cap(dpkt->handle);

	if (!cp) {
		struct ds_data_nack nack = {
			.tag = {
				.type = DS_NACK,
				.len = (sizeof(struct ds_data_nack) -
					sizeof(struct ds_msg_tag)),
			},
			.handle = dpkt->handle,
			.result = DS_INV_HDL,
		};

		printk(KERN_ERR PFX "Data for unknown handle %lu\n",
		       dpkt->handle);
		ds_send(dp->lp, &nack, sizeof(nack));
	} else {
		cp->data(dp->lp, cp, dpkt, len);
	}
	return 0;
}

static void ds_up(struct ds_info *dp)
{
	struct ldc_channel *lp = dp->lp;
	struct ds_ver_req req;
	int err;

	req.tag.type = DS_INIT_REQ;
	req.tag.len = sizeof(req) - sizeof(struct ds_msg_tag);
	req.ver.major = 1;
	req.ver.minor = 0;

	err = ds_send(lp, &req, sizeof(req));
	if (err > 0)
		dp->hs_state = DS_HS_START;
}

static void ds_event(void *arg, int event)
{
	struct ds_info *dp = arg;
	struct ldc_channel *lp = dp->lp;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&ds_lock, flags);

	if (event == LDC_EVENT_UP) {
		ds_up(dp);
		spin_unlock_irqrestore(&ds_lock, flags);
		return;
	}

	if (event != LDC_EVENT_DATA_READY) {
		printk(KERN_WARNING PFX "Unexpected LDC event %d\n", event);
		spin_unlock_irqrestore(&ds_lock, flags);
		return;
	}

	err = 0;
	while (1) {
		struct ds_msg_tag *tag;

		err = ldc_read(lp, dp->rcv_buf, sizeof(*tag));

		if (unlikely(err < 0)) {
			if (err == -ECONNRESET)
				ds_conn_reset(dp);
			break;
		}
		if (err == 0)
			break;

		tag = dp->rcv_buf;
		err = ldc_read(lp, tag + 1, tag->len);

		if (unlikely(err < 0)) {
			if (err == -ECONNRESET)
				ds_conn_reset(dp);
			break;
		}
		if (err < tag->len)
			break;

		if (tag->type < DS_DATA)
			err = ds_handshake(dp, dp->rcv_buf);
		else
			err = ds_data(dp, dp->rcv_buf,
				      sizeof(*tag) + err);
		if (err == -ECONNRESET)
			break;
	}

	spin_unlock_irqrestore(&ds_lock, flags);
}

static int __devinit ds_probe(struct vio_dev *vdev,
			      const struct vio_device_id *id)
{
	static int ds_version_printed;
	struct mdesc_node *endp;
	struct ldc_channel_config ds_cfg = {
		.event		= ds_event,
		.mtu		= 4096,
		.mode		= LDC_MODE_STREAM,
	};
	struct ldc_channel *lp;
	struct ds_info *dp;
	const u64 *chan_id;
	int err;

	if (ds_version_printed++ == 0)
		printk(KERN_INFO "%s", version);

	endp = vio_find_endpoint(vdev);
	if (!endp)
		return -ENODEV;

	chan_id = md_get_property(endp, "id", NULL);
	if (!chan_id)
		return -ENODEV;

	dp = kzalloc(sizeof(*dp), GFP_KERNEL);
	err = -ENOMEM;
	if (!dp)
		goto out_err;

	dp->rcv_buf = kzalloc(4096, GFP_KERNEL);
	if (!dp->rcv_buf)
		goto out_free_dp;

	dp->rcv_buf_len = 4096;

	ds_cfg.tx_irq = endp->irqs[0];
	ds_cfg.rx_irq = endp->irqs[1];

	lp = ldc_alloc(*chan_id, &ds_cfg, dp);
	if (IS_ERR(lp)) {
		err = PTR_ERR(lp);
		goto out_free_rcv_buf;
	}
	dp->lp = lp;

	err = ldc_bind(lp);
	if (err)
		goto out_free_ldc;

	start_powerd();

	return err;

out_free_ldc:
	ldc_free(dp->lp);

out_free_rcv_buf:
	kfree(dp->rcv_buf);

out_free_dp:
	kfree(dp);

out_err:
	return err;
}

static int ds_remove(struct vio_dev *vdev)
{
	return 0;
}

static struct vio_device_id ds_match[] = {
	{
		.type = "domain-services-port",
	},
	{},
};

static struct vio_driver ds_driver = {
	.id_table	= ds_match,
	.probe		= ds_probe,
	.remove		= ds_remove,
	.driver		= {
		.name	= "ds",
		.owner	= THIS_MODULE,
	}
};

static int __init ds_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ds_states); i++)
		ds_states[i].handle = ((u64)i << 32);

	return vio_register_driver(&ds_driver);
}

subsys_initcall(ds_init);
