/*
 * Copyright (C) 2010 Google, Inc.
 * Author: Dima Zavin <dima@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/irq.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/rbtree.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/tegra_rpc.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

#include <mach/clk.h>
#include <mach/io.h>
#include <mach/iomap.h>
#include <mach/nvmap.h>

#include "../../../../video/tegra/nvmap/nvmap.h"

#include "headavp.h"
#include "avp_msg.h"
#include "trpc.h"
#include "avp.h"

enum {
	AVP_DBG_TRACE_XPC	= 1U << 0,
	AVP_DBG_TRACE_XPC_IRQ	= 1U << 1,
	AVP_DBG_TRACE_XPC_MSG	= 1U << 2,
	AVP_DBG_TRACE_XPC_CONN	= 1U << 3,
	AVP_DBG_TRACE_TRPC_MSG	= 1U << 4,
	AVP_DBG_TRACE_TRPC_CONN	= 1U << 5,
	AVP_DBG_TRACE_LIB	= 1U << 6,
};

static u32 avp_debug_mask = 0;
module_param_named(debug_mask, avp_debug_mask, uint, S_IWUSR | S_IRUGO);

#define DBG(flag, args...) \
	do { if (unlikely(avp_debug_mask & (flag))) pr_info(args); } while (0)

#define TEGRA_AVP_NAME			"tegra-avp"

#define TEGRA_AVP_KERNEL_FW		"nvrm_avp.bin"

#define TEGRA_AVP_RESET_VECTOR_ADDR	\
		(IO_ADDRESS(TEGRA_EXCEPTION_VECTORS_BASE) + 0x200)

#define TEGRA_AVP_RESUME_ADDR		IO_ADDRESS(TEGRA_IRAM_BASE)

#define FLOW_CTRL_HALT_COP_EVENTS	IO_ADDRESS(TEGRA_FLOW_CTRL_BASE + 0x4)
#define FLOW_MODE_STOP			(0x2 << 29)
#define FLOW_MODE_NONE			0x0

#define MBOX_FROM_AVP			IO_ADDRESS(TEGRA_RES_SEMA_BASE + 0x10)
#define MBOX_TO_AVP			IO_ADDRESS(TEGRA_RES_SEMA_BASE + 0x20)

/* Layout of the mailbox registers:
 * bit 31	- pending message interrupt enable (mailbox full, i.e. valid=1)
 * bit 30	- message cleared interrupt enable (mailbox empty, i.e. valid=0)
 * bit 29	- message valid. peer clears this bit after reading msg
 * bits 27:0	- message data
 */
#define MBOX_MSG_PENDING_INT_EN		(1 << 31)
#define MBOX_MSG_READ_INT_EN		(1 << 30)
#define MBOX_MSG_VALID			(1 << 29)

#define AVP_MSG_MAX_CMD_LEN		16
#define AVP_MSG_AREA_SIZE	(AVP_MSG_MAX_CMD_LEN + TEGRA_RPC_MAX_MSG_LEN)

struct avp_info {
	struct clk			*cop_clk;

	int				mbox_from_avp_pend_irq;

	dma_addr_t			msg_area_addr;
	u32				msg;
	void				*msg_to_avp;
	void				*msg_from_avp;
	struct mutex			to_avp_lock;
	struct mutex			from_avp_lock;

	struct work_struct		recv_work;
	struct workqueue_struct		*recv_wq;

	struct trpc_node		*rpc_node;
	struct miscdevice		misc_dev;
	bool				opened;
	struct mutex			open_lock;

	spinlock_t			state_lock;
	bool				initialized;
	bool				shutdown;
	bool				suspending;
	bool				defer_remote;

	struct mutex			libs_lock;
	struct list_head		libs;
	struct nvmap_client		*nvmap_libs;

	/* client for driver allocations, persistent */
	struct nvmap_client		*nvmap_drv;
	struct nvmap_handle_ref		*kernel_handle;
	void				*kernel_data;
	unsigned long			kernel_phys;

	struct nvmap_handle_ref		*iram_backup_handle;
	void				*iram_backup_data;
	unsigned long			iram_backup_phys;
	unsigned long			resume_addr;

	struct trpc_endpoint		*avp_ep;
	struct rb_root			endpoints;

	struct avp_svc_info		*avp_svc;
};

struct remote_info {
	u32				loc_id;
	u32				rem_id;
	struct kref			ref;

	struct trpc_endpoint		*trpc_ep;
	struct rb_node			rb_node;
};

struct lib_item {
	struct list_head		list;
	u32				handle;
	char				name[TEGRA_AVP_LIB_MAX_NAME];
};

static struct avp_info *tegra_avp;

static int avp_trpc_send(struct trpc_endpoint *ep, void *buf, size_t len);
static void avp_trpc_close(struct trpc_endpoint *ep);
static void avp_trpc_show(struct seq_file *s, struct trpc_endpoint *ep);
static void libs_cleanup(struct avp_info *avp);

static struct trpc_ep_ops remote_ep_ops = {
	.send	= avp_trpc_send,
	.close	= avp_trpc_close,
	.show	= avp_trpc_show,
};

static struct remote_info *rinfo_alloc(struct avp_info *avp)
{
	struct remote_info *rinfo;

	rinfo = kzalloc(sizeof(struct remote_info), GFP_KERNEL);
	if (!rinfo)
		return NULL;
	kref_init(&rinfo->ref);
	return rinfo;
}

static void _rinfo_release(struct kref *ref)
{
	struct remote_info *rinfo = container_of(ref, struct remote_info, ref);
	kfree(rinfo);
}

static inline void rinfo_get(struct remote_info *rinfo)
{
	kref_get(&rinfo->ref);
}

static inline void rinfo_put(struct remote_info *rinfo)
{
	kref_put(&rinfo->ref, _rinfo_release);
}

static int remote_insert(struct avp_info *avp, struct remote_info *rinfo)
{
	struct rb_node **p;
	struct rb_node *parent;
	struct remote_info *tmp;

	p = &avp->endpoints.rb_node;
	parent = NULL;
	while (*p) {
		parent = *p;
		tmp = rb_entry(parent, struct remote_info, rb_node);

		if (rinfo->loc_id < tmp->loc_id)
			p = &(*p)->rb_left;
		else if (rinfo->loc_id > tmp->loc_id)
			p = &(*p)->rb_right;
		else {
			pr_info("%s: avp endpoint id=%x (%s) already exists\n",
				__func__, rinfo->loc_id,
				trpc_name(rinfo->trpc_ep));
			return -EEXIST;
		}
	}
	rb_link_node(&rinfo->rb_node, parent, p);
	rb_insert_color(&rinfo->rb_node, &avp->endpoints);
	rinfo_get(rinfo);
	return 0;
}

static struct remote_info *remote_find(struct avp_info *avp, u32 local_id)
{
	struct rb_node *n = avp->endpoints.rb_node;
	struct remote_info *rinfo;

	while (n) {
		rinfo = rb_entry(n, struct remote_info, rb_node);

		if (local_id < rinfo->loc_id)
			n = n->rb_left;
		else if (local_id > rinfo->loc_id)
			n = n->rb_right;
		else
			return rinfo;
	}
	return NULL;
}

static void remote_remove(struct avp_info *avp, struct remote_info *rinfo)
{
	rb_erase(&rinfo->rb_node, &avp->endpoints);
	rinfo_put(rinfo);
}

/* test whether or not the trpc endpoint provided is a valid AVP node
 * endpoint */
static struct remote_info *validate_trpc_ep(struct avp_info *avp,
					    struct trpc_endpoint *ep)
{
	struct remote_info *tmp = trpc_priv(ep);
	struct remote_info *rinfo;

	if (!tmp)
		return NULL;
	rinfo = remote_find(avp, tmp->loc_id);
	if (rinfo && rinfo == tmp && rinfo->trpc_ep == ep)
		return rinfo;
	return NULL;
}

static void avp_trpc_show(struct seq_file *s, struct trpc_endpoint *ep)
{
	struct avp_info *avp = tegra_avp;
	struct remote_info *rinfo;
	unsigned long flags;

	spin_lock_irqsave(&avp->state_lock, flags);
	rinfo = validate_trpc_ep(avp, ep);
	if (!rinfo) {
		seq_printf(s, "    <unknown>\n");
		goto out;
	}
	seq_printf(s, "    loc_id:0x%x\n    rem_id:0x%x\n",
		   rinfo->loc_id, rinfo->rem_id);
out:
	spin_unlock_irqrestore(&avp->state_lock, flags);
}

static inline void mbox_writel(u32 val, void __iomem *mbox)
{
	writel(val, mbox);
}

static inline u32 mbox_readl(void __iomem *mbox)
{
	return readl(mbox);
}

static inline void msg_ack_remote(struct avp_info *avp, u32 cmd, u32 arg)
{
	struct msg_ack *ack = avp->msg_from_avp;

	/* must make sure the arg is there first */
	ack->arg = arg;
	wmb();
	ack->cmd = cmd;
	wmb();
}

static inline u32 msg_recv_get_cmd(struct avp_info *avp)
{
	volatile u32 *cmd = avp->msg_from_avp;
	rmb();
	return *cmd;
}

static inline int __msg_write(struct avp_info *avp, void *hdr, size_t hdr_len,
			      void *buf, size_t len)
{
	memcpy(avp->msg_to_avp, hdr, hdr_len);
	if (buf && len)
		memcpy(avp->msg_to_avp + hdr_len, buf, len);
	mbox_writel(avp->msg, MBOX_TO_AVP);
	return 0;
}

static inline int msg_write(struct avp_info *avp, void *hdr, size_t hdr_len,
			    void *buf, size_t len)
{
	/* rem_ack is a pointer into shared memory that the AVP modifies */
	volatile u32 *rem_ack = avp->msg_to_avp;
	unsigned long endtime = jiffies + HZ;

	/* the other side ack's the message by clearing the first word,
	 * wait for it to do so */
	rmb();
	while (*rem_ack != 0 && time_before(jiffies, endtime)) {
		usleep_range(100, 2000);
		rmb();
	}
	if (*rem_ack != 0)
		return -ETIMEDOUT;
	__msg_write(avp, hdr, hdr_len, buf, len);
	return 0;
}

static inline int msg_check_ack(struct avp_info *avp, u32 cmd, u32 *arg)
{
	struct msg_ack ack;

	rmb();
	memcpy(&ack, avp->msg_to_avp, sizeof(ack));
	if (ack.cmd != cmd)
		return -ENOENT;
	if (arg)
		*arg = ack.arg;
	return 0;
}

/* XXX: add timeout */
static int msg_wait_ack_locked(struct avp_info *avp, u32 cmd, u32 *arg)
{
	/* rem_ack is a pointer into shared memory that the AVP modifies */
	volatile u32 *rem_ack = avp->msg_to_avp;
	unsigned long endtime = jiffies + HZ / 5;
	int ret;

	do {
		ret = msg_check_ack(avp, cmd, arg);
		usleep_range(1000, 5000);
	} while (ret && time_before(jiffies, endtime));

	/* if we timed out, try one more time */
	if (ret)
		ret = msg_check_ack(avp, cmd, arg);

	/* clear out the ack */
	*rem_ack = 0;
	wmb();
	return ret;
}

static int avp_trpc_send(struct trpc_endpoint *ep, void *buf, size_t len)
{
	struct avp_info *avp = tegra_avp;
	struct remote_info *rinfo;
	struct msg_port_data msg;
	int ret;
	unsigned long flags;

	DBG(AVP_DBG_TRACE_TRPC_MSG, "%s: ep=%p priv=%p buf=%p len=%d\n",
	    __func__, ep, trpc_priv(ep), buf, len);

	spin_lock_irqsave(&avp->state_lock, flags);
	if (unlikely(avp->suspending && trpc_peer(ep) != avp->avp_ep)) {
		ret = -EBUSY;
		goto err_state_locked;
	} else if (avp->shutdown) {
		ret = -ENODEV;
		goto err_state_locked;
	}
	rinfo = validate_trpc_ep(avp, ep);
	if (!rinfo) {
		ret = -ENOTTY;
		goto err_state_locked;
	}
	rinfo_get(rinfo);
	spin_unlock_irqrestore(&avp->state_lock, flags);

	msg.cmd = CMD_MESSAGE;
	msg.port_id = rinfo->rem_id;
	msg.msg_len = len;

	mutex_lock(&avp->to_avp_lock);
	ret = msg_write(avp, &msg, sizeof(msg), buf, len);
	mutex_unlock(&avp->to_avp_lock);

	DBG(AVP_DBG_TRACE_TRPC_MSG, "%s: msg sent for %s (%x->%x) (%d)\n",
	    __func__, trpc_name(ep), rinfo->loc_id, rinfo->rem_id, ret);
	rinfo_put(rinfo);
	return ret;

err_state_locked:
	spin_unlock_irqrestore(&avp->state_lock, flags);
	return ret;
}

static int _send_disconnect(struct avp_info *avp, u32 port_id)
{
	struct msg_disconnect msg;
	int ret;

	msg.cmd = CMD_DISCONNECT;
	msg.port_id = port_id;

	mutex_lock(&avp->to_avp_lock);
	ret = msg_write(avp, &msg, sizeof(msg), NULL, 0);
	if (ret) {
		pr_err("%s: remote has not acked last message (%x)\n", __func__,
		       port_id);
		goto err_msg_write;
	}

	ret = msg_wait_ack_locked(avp, CMD_ACK, NULL);
	if (ret) {
		pr_err("%s: remote end won't respond for %x\n", __func__,
		       port_id);
		goto err_wait_ack;
	}

	DBG(AVP_DBG_TRACE_XPC_CONN, "%s: sent disconnect msg for %x\n",
	    __func__, port_id);

err_wait_ack:
err_msg_write:
	mutex_unlock(&avp->to_avp_lock);
	return ret;
}

/* Note: Assumes that the rinfo was previously successfully added to the
 * endpoints rb_tree. The initial refcnt of 1 is inherited by the port when the
 * trpc endpoint is created with thi trpc_xxx functions. Thus, on close,
 * we must drop that reference here.
 * The avp->endpoints rb_tree keeps its own reference on rinfo objects.
 *
 * The try_connect function does not use this on error because it needs to
 * split the close of trpc_ep port and the put.
 */
static inline void remote_close(struct remote_info *rinfo)
{
	trpc_close(rinfo->trpc_ep);
	rinfo_put(rinfo);
}

static void avp_trpc_close(struct trpc_endpoint *ep)
{
	struct avp_info *avp = tegra_avp;
	struct remote_info *rinfo;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&avp->state_lock, flags);
	if (avp->shutdown) {
		spin_unlock_irqrestore(&avp->state_lock, flags);
		return;
	}

	rinfo = validate_trpc_ep(avp, ep);
	if (!rinfo) {
		pr_err("%s: tried to close invalid port '%s' endpoint (%p)\n",
		       __func__, trpc_name(ep), ep);
		spin_unlock_irqrestore(&avp->state_lock, flags);
		return;
	}
	rinfo_get(rinfo);
	remote_remove(avp, rinfo);
	spin_unlock_irqrestore(&avp->state_lock, flags);

	DBG(AVP_DBG_TRACE_TRPC_CONN, "%s: closing '%s' (%x)\n", __func__,
	    trpc_name(ep), rinfo->rem_id);

	ret = _send_disconnect(avp, rinfo->rem_id);
	if (ret)
		pr_err("%s: error while closing remote port '%s' (%x)\n",
		       __func__, trpc_name(ep), rinfo->rem_id);
	remote_close(rinfo);
	rinfo_put(rinfo);
}

/* takes and holds avp->from_avp_lock */
static void recv_msg_lock(struct avp_info *avp)
{
	unsigned long flags;

	mutex_lock(&avp->from_avp_lock);
	spin_lock_irqsave(&avp->state_lock, flags);
	avp->defer_remote = true;
	spin_unlock_irqrestore(&avp->state_lock, flags);
}

/* MUST be called with avp->from_avp_lock held */
static void recv_msg_unlock(struct avp_info *avp)
{
	unsigned long flags;

	spin_lock_irqsave(&avp->state_lock, flags);
	avp->defer_remote = false;
	spin_unlock_irqrestore(&avp->state_lock, flags);
	mutex_unlock(&avp->from_avp_lock);
}

static int avp_node_try_connect(struct trpc_node *node,
				struct trpc_node *src_node,
				struct trpc_endpoint *from)
{
	struct avp_info *avp = tegra_avp;
	const char *port_name = trpc_name(from);
	struct remote_info *rinfo;
	struct msg_connect msg;
	int ret;
	unsigned long flags;
	int len;

	DBG(AVP_DBG_TRACE_TRPC_CONN, "%s: trying connect from %s\n", __func__,
	    port_name);

	if (node != avp->rpc_node || node->priv != avp)
		return -ENODEV;

	len = strlen(port_name);
	if (len > XPC_PORT_NAME_LEN) {
		pr_err("%s: port name (%s) to long\n", __func__, port_name);
		return -EINVAL;
	}

	ret = 0;
	spin_lock_irqsave(&avp->state_lock, flags);
	if (avp->suspending) {
		ret = -EBUSY;
	} else if (likely(src_node != avp->rpc_node)) {
		/* only check for initialized when the source is not ourselves
		 * since we'll end up calling into here during initialization */
		if (!avp->initialized)
			ret = -ENODEV;
	} else if (strncmp(port_name, "RPC_AVP_PORT", XPC_PORT_NAME_LEN)) {
		/* we only allow connections to ourselves for the cpu-to-avp
		   port */
		ret = -EINVAL;
	}
	spin_unlock_irqrestore(&avp->state_lock, flags);
	if (ret)
		return ret;

	rinfo = rinfo_alloc(avp);
	if (!rinfo) {
		pr_err("%s: cannot alloc mem for rinfo\n", __func__);
		ret = -ENOMEM;
		goto err_alloc_rinfo;
	}
	rinfo->loc_id = (u32)rinfo;

	msg.cmd = CMD_CONNECT;
	msg.port_id = rinfo->loc_id;
	memcpy(msg.name, port_name, len);
	memset(msg.name + len, 0, XPC_PORT_NAME_LEN - len);

	/* when trying to connect to remote, we need to block remote
	 * messages until we get our ack and can insert it into our lists.
	 * Otherwise, we can get a message from the other side for a port
	 * that we haven't finished setting up.
	 *
	 * 'defer_remote' will force the irq handler to not process messages
	 * at irq context but to schedule work to do so. The work function will
	 * take the from_avp_lock and everything should stay consistent.
	 */
	recv_msg_lock(avp);
	mutex_lock(&avp->to_avp_lock);
	ret = msg_write(avp, &msg, sizeof(msg), NULL, 0);
	if (ret) {
		pr_err("%s: remote has not acked last message (%s)\n", __func__,
		       port_name);
		mutex_unlock(&avp->to_avp_lock);
		goto err_msg_write;
	}
	ret = msg_wait_ack_locked(avp, CMD_RESPONSE, &rinfo->rem_id);
	mutex_unlock(&avp->to_avp_lock);

	if (ret) {
		pr_err("%s: remote end won't respond for '%s'\n", __func__,
		       port_name);
		goto err_wait_ack;
	}
	if (!rinfo->rem_id) {
		pr_err("%s: can't connect to '%s'\n", __func__, port_name);
		ret = -ECONNREFUSED;
		goto err_nack;
	}

	DBG(AVP_DBG_TRACE_TRPC_CONN, "%s: got conn ack '%s' (%x <-> %x)\n",
	    __func__, port_name, rinfo->loc_id, rinfo->rem_id);

	rinfo->trpc_ep = trpc_create_peer(node, from, &remote_ep_ops,
					       rinfo);
	if (!rinfo->trpc_ep) {
		pr_err("%s: cannot create peer for %s\n", __func__, port_name);
		ret = -EINVAL;
		goto err_create_peer;
	}

	spin_lock_irqsave(&avp->state_lock, flags);
	ret = remote_insert(avp, rinfo);
	spin_unlock_irqrestore(&avp->state_lock, flags);
	if (ret)
		goto err_ep_insert;

	recv_msg_unlock(avp);
	return 0;

err_ep_insert:
	trpc_close(rinfo->trpc_ep);
err_create_peer:
	_send_disconnect(avp, rinfo->rem_id);
err_nack:
err_wait_ack:
err_msg_write:
	recv_msg_unlock(avp);
	rinfo_put(rinfo);
err_alloc_rinfo:
	return ret;
}

static void process_disconnect_locked(struct avp_info *avp,
				      struct msg_data *raw_msg)
{
	struct msg_disconnect *disconn_msg = (struct msg_disconnect *)raw_msg;
	unsigned long flags;
	struct remote_info *rinfo;

	DBG(AVP_DBG_TRACE_XPC_CONN, "%s: got disconnect (%x)\n", __func__,
	    disconn_msg->port_id);

	if (avp_debug_mask & AVP_DBG_TRACE_XPC_MSG)
		print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, disconn_msg,
				     sizeof(struct msg_disconnect));

	spin_lock_irqsave(&avp->state_lock, flags);
	rinfo = remote_find(avp, disconn_msg->port_id);
	if (!rinfo) {
		spin_unlock_irqrestore(&avp->state_lock, flags);
		pr_warning("%s: got disconnect for unknown port %x\n",
			   __func__, disconn_msg->port_id);
		goto ack;
	}
	rinfo_get(rinfo);
	remote_remove(avp, rinfo);
	spin_unlock_irqrestore(&avp->state_lock, flags);

	remote_close(rinfo);
	rinfo_put(rinfo);
ack:
	msg_ack_remote(avp, CMD_ACK, 0);
}

static void process_connect_locked(struct avp_info *avp,
				   struct msg_data *raw_msg)
{
	struct msg_connect *conn_msg = (struct msg_connect *)raw_msg;
	struct trpc_endpoint *trpc_ep;
	struct remote_info *rinfo;
	char name[XPC_PORT_NAME_LEN + 1];
	int ret;
	u32 local_port_id = 0;
	unsigned long flags;

	DBG(AVP_DBG_TRACE_XPC_CONN, "%s: got connect (%x)\n", __func__,
	    conn_msg->port_id);
	if (avp_debug_mask & AVP_DBG_TRACE_XPC_MSG)
		print_hex_dump_bytes("", DUMP_PREFIX_OFFSET,
				     conn_msg, sizeof(struct msg_connect));

	rinfo = rinfo_alloc(avp);
	if (!rinfo) {
		pr_err("%s: cannot alloc mem for rinfo\n", __func__);
		ret = -ENOMEM;
		goto ack;
	}
	rinfo->loc_id = (u32)rinfo;
	rinfo->rem_id = conn_msg->port_id;

	memcpy(name, conn_msg->name, XPC_PORT_NAME_LEN);
	name[XPC_PORT_NAME_LEN] = '\0';
	trpc_ep = trpc_create_connect(avp->rpc_node, name, &remote_ep_ops,
				      rinfo, 0);
	if (IS_ERR(trpc_ep)) {
		pr_err("%s: remote requested unknown port '%s' (%d)\n",
		       __func__, name, (int)PTR_ERR(trpc_ep));
		goto nack;
	}
	rinfo->trpc_ep = trpc_ep;

	spin_lock_irqsave(&avp->state_lock, flags);
	ret = remote_insert(avp, rinfo);
	spin_unlock_irqrestore(&avp->state_lock, flags);
	if (ret)
		goto err_ep_insert;

	local_port_id = rinfo->loc_id;
	goto ack;

err_ep_insert:
	trpc_close(trpc_ep);
nack:
	rinfo_put(rinfo);
	local_port_id = 0;
ack:
	msg_ack_remote(avp, CMD_RESPONSE, local_port_id);
}

static int process_message(struct avp_info *avp, struct msg_data *raw_msg,
			    gfp_t gfp_flags)
{
	struct msg_port_data *port_msg = (struct msg_port_data *)raw_msg;
	struct remote_info *rinfo;
	unsigned long flags;
	int len;
	int ret;

	len = min(port_msg->msg_len, (u32)TEGRA_RPC_MAX_MSG_LEN);

	if (avp_debug_mask & AVP_DBG_TRACE_XPC_MSG) {
		pr_info("%s: got message cmd=%x port=%x len=%d\n", __func__,
			port_msg->cmd, port_msg->port_id, port_msg->msg_len);
		print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, port_msg,
				     sizeof(struct msg_port_data) + len);
	}

	if (len != port_msg->msg_len)
		pr_err("%s: message sent is too long (%d bytes)\n", __func__,
		       port_msg->msg_len);

	spin_lock_irqsave(&avp->state_lock, flags);
	rinfo = remote_find(avp, port_msg->port_id);
	if (rinfo) {
		rinfo_get(rinfo);
		trpc_get(rinfo->trpc_ep);
	} else {
		pr_err("%s: port %x not found\n", __func__, port_msg->port_id);
		spin_unlock_irqrestore(&avp->state_lock, flags);
		ret = -ENOENT;
		goto ack;
	}
	spin_unlock_irqrestore(&avp->state_lock, flags);

	ret = trpc_send_msg(avp->rpc_node, rinfo->trpc_ep, port_msg->data,
				 len, gfp_flags);
	if (ret == -ENOMEM) {
		trpc_put(rinfo->trpc_ep);
		rinfo_put(rinfo);
		goto no_ack;
	} else if (ret) {
		pr_err("%s: cannot queue message for port %s/%x (%d)\n",
		       __func__, trpc_name(rinfo->trpc_ep), rinfo->loc_id,
		       ret);
	} else {
		DBG(AVP_DBG_TRACE_XPC_MSG, "%s: msg queued\n", __func__);
	}

	trpc_put(rinfo->trpc_ep);
	rinfo_put(rinfo);
ack:
	msg_ack_remote(avp, CMD_ACK, 0);
no_ack:
	return ret;
}

static void process_avp_message(struct work_struct *work)
{
	struct avp_info *avp = container_of(work, struct avp_info, recv_work);
	struct msg_data *msg = avp->msg_from_avp;

	mutex_lock(&avp->from_avp_lock);
	rmb();
	switch (msg->cmd) {
	case CMD_CONNECT:
		process_connect_locked(avp, msg);
		break;
	case CMD_DISCONNECT:
		process_disconnect_locked(avp, msg);
		break;
	case CMD_MESSAGE:
		process_message(avp, msg, GFP_KERNEL);
		break;
	default:
		pr_err("%s: unknown cmd (%x) received\n", __func__, msg->cmd);
		break;
	}
	mutex_unlock(&avp->from_avp_lock);
}

static irqreturn_t avp_mbox_pending_isr(int irq, void *data)
{
	struct avp_info *avp = data;
	struct msg_data *msg = avp->msg_from_avp;
	u32 mbox_msg;
	unsigned long flags;
	int ret;

	mbox_msg = mbox_readl(MBOX_FROM_AVP);
	mbox_writel(0, MBOX_FROM_AVP);

	DBG(AVP_DBG_TRACE_XPC_IRQ, "%s: got msg %x\n", __func__, mbox_msg);

	/* XXX: re-use previous message? */
	if (!(mbox_msg & MBOX_MSG_VALID)) {
		WARN_ON(1);
		goto done;
	}

	mbox_msg <<= 4;
	if (mbox_msg == 0x2f00bad0UL) {
		pr_info("%s: petting watchdog\n", __func__);
		goto done;
	}

	spin_lock_irqsave(&avp->state_lock, flags);
	if (avp->shutdown) {
		spin_unlock_irqrestore(&avp->state_lock, flags);
		goto done;
	} else if (avp->defer_remote) {
		spin_unlock_irqrestore(&avp->state_lock, flags);
		goto defer;
	}
	spin_unlock_irqrestore(&avp->state_lock, flags);

	rmb();
	if (msg->cmd == CMD_MESSAGE) {
		ret = process_message(avp, msg, GFP_ATOMIC);
		if (ret != -ENOMEM)
			goto done;
		pr_info("%s: deferring message (%d)\n", __func__, ret);
	}
defer:
	queue_work(avp->recv_wq, &avp->recv_work);
done:
	return IRQ_HANDLED;
}

static int avp_reset(struct avp_info *avp, unsigned long reset_addr)
{
	unsigned long stub_code_phys = virt_to_phys(_tegra_avp_boot_stub);
	dma_addr_t stub_data_phys;
	unsigned long timeout;
	int ret = 0;

	writel(FLOW_MODE_STOP, FLOW_CTRL_HALT_COP_EVENTS);

	_tegra_avp_boot_stub_data.map_phys_addr = avp->kernel_phys;
	_tegra_avp_boot_stub_data.jump_addr = reset_addr;
	wmb();
	stub_data_phys = dma_map_single(NULL, &_tegra_avp_boot_stub_data,
					sizeof(_tegra_avp_boot_stub_data),
					DMA_TO_DEVICE);

	writel(stub_code_phys, TEGRA_AVP_RESET_VECTOR_ADDR);

	tegra_periph_reset_assert(avp->cop_clk);
	udelay(10);
	tegra_periph_reset_deassert(avp->cop_clk);

	writel(FLOW_MODE_NONE, FLOW_CTRL_HALT_COP_EVENTS);

	/* the AVP firmware will reprogram its reset vector as the kernel
	 * starts, so a dead kernel can be detected by polling this value */
	timeout = jiffies + msecs_to_jiffies(2000);
	while (time_before(jiffies, timeout)) {
		if (readl(TEGRA_AVP_RESET_VECTOR_ADDR) != stub_code_phys)
			break;
		cpu_relax();
	}
	if (readl(TEGRA_AVP_RESET_VECTOR_ADDR) == stub_code_phys)
		ret = -EINVAL;
	WARN_ON(ret);
	dma_unmap_single(NULL, stub_data_phys,
			 sizeof(_tegra_avp_boot_stub_data),
			 DMA_TO_DEVICE);
	return ret;
}

static void avp_halt(struct avp_info *avp)
{
	/* ensure the AVP is halted */
	writel(FLOW_MODE_STOP, FLOW_CTRL_HALT_COP_EVENTS);
	tegra_periph_reset_assert(avp->cop_clk);

	/* set up the initial memory areas and mailbox contents */
	*((u32 *)avp->msg_from_avp) = 0;
	*((u32 *)avp->msg_to_avp) = 0xfeedf00d;
	mbox_writel(0, MBOX_FROM_AVP);
	mbox_writel(0, MBOX_TO_AVP);
}

/* Note: CPU_PORT server and AVP_PORT client are registered with the avp
 * node, but are actually meant to be processed on our side (either
 * by the svc thread for processing remote calls or by the client
 * of the char dev for receiving replies for managing remote
 * libraries/modules. */

static int avp_init(struct avp_info *avp, const char *fw_file)
{
	const struct firmware *avp_fw;
	int ret;
	struct trpc_endpoint *ep;

	avp->nvmap_libs = nvmap_create_client(nvmap_dev, "avp_libs");
	if (IS_ERR(avp->nvmap_libs)) {
		pr_err("%s: cannot create libs nvmap client\n", __func__);
		ret = PTR_ERR(avp->nvmap_libs);
		goto err_nvmap_create_libs_client;
	}

	/* put the address of the shared mem area into the mailbox for AVP
	 * to read out when its kernel boots. */
	mbox_writel(avp->msg, MBOX_TO_AVP);

	ret = request_firmware(&avp_fw, fw_file, avp->misc_dev.this_device);
	if (ret) {
		pr_err("%s: Cannot read firmware '%s'\n", __func__, fw_file);
		goto err_req_fw;
	}
	pr_info("%s: read firmware from '%s' (%d bytes)\n", __func__,
		fw_file, avp_fw->size);
	memcpy(avp->kernel_data, avp_fw->data, avp_fw->size);
	memset(avp->kernel_data + avp_fw->size, 0, SZ_1M - avp_fw->size);
	wmb();
	release_firmware(avp_fw);

	ret = avp_reset(avp, AVP_KERNEL_VIRT_BASE);
	if (ret) {
		pr_err("%s: cannot reset the AVP.. aborting..\n", __func__);
		goto err_reset;
	}

	enable_irq(avp->mbox_from_avp_pend_irq);
	/* Initialize the avp_svc *first*. This creates RPC_CPU_PORT to be
	 * ready for remote commands. Then, connect to the
	 * remote RPC_AVP_PORT to be able to send library load/unload and
	 * suspend commands to it */
	ret = avp_svc_start(avp->avp_svc);
	if (ret)
		goto err_avp_svc_start;

	ep = trpc_create_connect(avp->rpc_node, "RPC_AVP_PORT", NULL,
				      NULL, -1);
	if (IS_ERR(ep)) {
		pr_err("%s: can't connect to RPC_AVP_PORT server\n", __func__);
		ret = PTR_ERR(ep);
		goto err_rpc_avp_port;
	}
	avp->avp_ep = ep;

	avp->initialized = true;
	smp_wmb();
	pr_info("%s: avp init done\n", __func__);
	return 0;

err_rpc_avp_port:
	avp_svc_stop(avp->avp_svc);
err_avp_svc_start:
	disable_irq(avp->mbox_from_avp_pend_irq);
err_reset:
	avp_halt(avp);
err_req_fw:
	nvmap_client_put(avp->nvmap_libs);
err_nvmap_create_libs_client:
	avp->nvmap_libs = NULL;
	return ret;
}

static void avp_uninit(struct avp_info *avp)
{
	unsigned long flags;
	struct rb_node *n;
	struct remote_info *rinfo;

	spin_lock_irqsave(&avp->state_lock, flags);
	avp->initialized = false;
	avp->shutdown = true;
	spin_unlock_irqrestore(&avp->state_lock, flags);

	disable_irq(avp->mbox_from_avp_pend_irq);
	cancel_work_sync(&avp->recv_work);

	avp_halt(avp);

	spin_lock_irqsave(&avp->state_lock, flags);
	while ((n = rb_first(&avp->endpoints)) != NULL) {
		rinfo = rb_entry(n, struct remote_info, rb_node);
		rinfo_get(rinfo);
		remote_remove(avp, rinfo);
		spin_unlock_irqrestore(&avp->state_lock, flags);

		remote_close(rinfo);
		rinfo_put(rinfo);

		spin_lock_irqsave(&avp->state_lock, flags);
	}
	spin_unlock_irqrestore(&avp->state_lock, flags);

	avp_svc_stop(avp->avp_svc);

	if (avp->avp_ep) {
		trpc_close(avp->avp_ep);
		avp->avp_ep = NULL;
	}

	libs_cleanup(avp);

	avp->shutdown = false;
	smp_wmb();
	pr_info("%s: avp teardown done\n", __func__);
}

/* returns the remote lib handle in lib->handle */
static int _load_lib(struct avp_info *avp, struct tegra_avp_lib *lib)
{
	struct svc_lib_attach svc;
	struct svc_lib_attach_resp resp;
	const struct firmware *fw;
	void *args;
	struct nvmap_handle_ref *lib_handle;
	void *lib_data;
	unsigned long lib_phys;
	int ret;

	DBG(AVP_DBG_TRACE_LIB, "avp_lib: loading library '%s'\n", lib->name);

	args = kmalloc(lib->args_len, GFP_KERNEL);
	if (!args) {
		pr_err("avp_lib: can't alloc mem for args (%d)\n",
			lib->args_len);
		return -ENOMEM;
	}
	if (copy_from_user(args, lib->args, lib->args_len)) {
		pr_err("avp_lib: can't copy lib args\n");
		ret = -EFAULT;
		goto err_cp_args;
	}

	ret = request_firmware(&fw, lib->name, avp->misc_dev.this_device);
	if (ret) {
		pr_err("avp_lib: Cannot read firmware '%s'\n", lib->name);
		goto err_req_fw;
	}

	lib_handle = nvmap_alloc(avp->nvmap_libs, fw->size, L1_CACHE_BYTES,
				 NVMAP_HANDLE_WRITE_COMBINE);
	if (IS_ERR(lib_handle)) {
		pr_err("avp_lib: can't nvmap alloc for lib '%s'\n", lib->name);
		ret = PTR_ERR(lib_handle);
		goto err_nvmap_alloc;
	}

	lib_data = nvmap_mmap(lib_handle);
	if (!lib_data) {
		pr_err("avp_lib: can't nvmap map for lib '%s'\n", lib->name);
		ret = -ENOMEM;
		goto err_nvmap_mmap;
	}

	lib_phys = nvmap_pin(avp->nvmap_libs, lib_handle);
	if (IS_ERR((void *)lib_phys)) {
		pr_err("avp_lib: can't nvmap pin for lib '%s'\n", lib->name);
		ret = PTR_ERR(lib_handle);
		goto err_nvmap_pin;
	}

	memcpy(lib_data, fw->data, fw->size);

	svc.svc_id = SVC_LIBRARY_ATTACH;
	svc.address = lib_phys;
	svc.args_len = lib->args_len;
	svc.lib_size = fw->size;
	svc.reason = lib->greedy ? AVP_LIB_REASON_ATTACH_GREEDY :
		AVP_LIB_REASON_ATTACH;
	memcpy(svc.args, args, lib->args_len);
	wmb();

	/* send message, wait for reply */
	ret = trpc_send_msg(avp->rpc_node, avp->avp_ep, &svc, sizeof(svc),
				 GFP_KERNEL);
	if (ret)
		goto err_send_msg;

	ret = trpc_recv_msg(avp->rpc_node, avp->avp_ep, &resp,
				 sizeof(resp), -1);
	if (ret != sizeof(resp)) {
		pr_err("avp_lib: Couldn't get lib load reply (%d)\n", ret);
		goto err_recv_msg;
	} else if (resp.err) {
		pr_err("avp_lib: got remote error (%d) while loading lib %s\n",
		       resp.err, lib->name);
		ret = -EPROTO;
		goto err_recv_msg;
	}
	lib->handle = resp.lib_id;
	ret = 0;
	DBG(AVP_DBG_TRACE_LIB,
	    "avp_lib: Successfully loaded library %s (lib_id=%x)\n",
	    lib->name, resp.lib_id);

	/* We free the memory here because by this point the AVP has already
	 * requested memory for the library for all the sections since it does
	 * it's own relocation and memory management. So, our allocations were
	 * temporary to hand the library code over to the AVP.
	 */

err_recv_msg:
err_send_msg:
	nvmap_unpin(avp->nvmap_libs, lib_handle);
err_nvmap_pin:
	nvmap_munmap(lib_handle, lib_data);
err_nvmap_mmap:
	nvmap_free(avp->nvmap_libs, lib_handle);
err_nvmap_alloc:
	release_firmware(fw);
err_req_fw:
err_cp_args:
	kfree(args);
	return ret;
}

static int send_unload_lib_msg(struct avp_info *avp, u32 handle,
			       const char *name)
{
	struct svc_lib_detach svc;
	struct svc_lib_detach_resp resp;
	int ret;

	svc.svc_id = SVC_LIBRARY_DETACH;
	svc.reason = AVP_LIB_REASON_DETACH;
	svc.lib_id = handle;

	ret = trpc_send_msg(avp->rpc_node, avp->avp_ep, &svc, sizeof(svc),
				 GFP_KERNEL);
	if (ret) {
		pr_err("avp_lib: can't send unload message to avp for '%s'\n",
		       name);
		goto err;
	}

	ret = trpc_recv_msg(avp->rpc_node, avp->avp_ep, &resp,
				 sizeof(resp), -1);
	if (ret != sizeof(resp)) {
		pr_err("avp_lib: Couldn't get unload reply for '%s' (%d)\n",
		       name, ret);
	} else if (resp.err) {
		pr_err("avp_lib: remote error (%d) while unloading lib %s\n",
		       resp.err, name);
		ret = -EPROTO;
	} else
		ret = 0;
err:
	return ret;
}

static struct lib_item *_find_lib_locked(struct avp_info *avp, u32 handle)
{
	struct lib_item *item;

	list_for_each_entry(item, &avp->libs, list) {
		if (item->handle == handle)
			return item;
	}
	return NULL;
}

static int _insert_lib_locked(struct avp_info *avp, u32 handle, char *name)
{
	struct lib_item *item;

	item = kzalloc(sizeof(struct lib_item), GFP_KERNEL);
	if (!item)
		return -ENOMEM;
	item->handle = handle;
	strlcpy(item->name, name, TEGRA_AVP_LIB_MAX_NAME);
	list_add_tail(&item->list, &avp->libs);
	return 0;
}

static void _delete_lib_locked(struct avp_info *avp, struct lib_item *item)
{
	list_del(&item->list);
	kfree(item);
}

static int handle_load_lib_ioctl(struct avp_info *avp, unsigned long arg)
{
	struct tegra_avp_lib lib;
	int ret;

	if (copy_from_user(&lib, (void __user *)arg, sizeof(lib)))
		return -EFAULT;
	lib.name[TEGRA_AVP_LIB_MAX_NAME - 1] = '\0';

	if (lib.args_len > TEGRA_AVP_LIB_MAX_ARGS) {
		pr_err("%s: library args too long (%d)\n", __func__,
			lib.args_len);
		return -E2BIG;
	}

	mutex_lock(&avp->libs_lock);
	ret = _load_lib(avp, &lib);
	if (ret)
		goto err_load_lib;

	if (copy_to_user((void __user *)arg, &lib, sizeof(lib))) {
		/* TODO: probably need to free the library from remote
		 * we just loaded */
		ret = -EFAULT;
		goto err_copy_to_user;
	}
	ret = _insert_lib_locked(avp, lib.handle, lib.name);
	if (ret) {
		pr_err("%s: can't insert lib (%d)\n", __func__, ret);
		goto err_insert_lib;
	}

	mutex_unlock(&avp->libs_lock);
	return 0;

err_insert_lib:
err_copy_to_user:
	send_unload_lib_msg(avp, lib.handle, lib.name);
err_load_lib:
	mutex_unlock(&avp->libs_lock);
	return ret;
}

static int handle_unload_lib_ioctl(struct avp_info *avp, unsigned long arg)
{
	struct lib_item *item;
	int ret;

	mutex_lock(&avp->libs_lock);
	item = _find_lib_locked(avp, (u32)arg);
	if (!item) {
		pr_err("avp_lib: avp lib with handle 0x%x not found\n",
		       (u32)arg);
		ret = -ENOENT;
		goto err_find;
	}
	ret = send_unload_lib_msg(avp, item->handle, item->name);
	if (!ret)
		DBG(AVP_DBG_TRACE_LIB, "avp_lib: unloaded '%s'\n", item->name);
	else
		pr_err("avp_lib: can't unload lib '%s'/0x%x (%d)\n", item->name,
		       item->handle, ret);
	_delete_lib_locked(avp, item);

err_find:
	mutex_unlock(&avp->libs_lock);
	return ret;
}

static void libs_cleanup(struct avp_info *avp)
{
	struct lib_item *lib;
	struct lib_item *lib_tmp;

	mutex_lock(&avp->libs_lock);
	list_for_each_entry_safe(lib, lib_tmp, &avp->libs, list) {
		_delete_lib_locked(avp, lib);
	}

	nvmap_client_put(avp->nvmap_libs);
	avp->nvmap_libs = NULL;
	mutex_unlock(&avp->libs_lock);
}

static long tegra_avp_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct avp_info *avp = tegra_avp;
	int ret;

	if (_IOC_TYPE(cmd) != TEGRA_AVP_IOCTL_MAGIC ||
	    _IOC_NR(cmd) < TEGRA_AVP_IOCTL_MIN_NR ||
	    _IOC_NR(cmd) > TEGRA_AVP_IOCTL_MAX_NR)
		return -ENOTTY;

	switch (cmd) {
	case TEGRA_AVP_IOCTL_LOAD_LIB:
		ret = handle_load_lib_ioctl(avp, arg);
		break;
	case TEGRA_AVP_IOCTL_UNLOAD_LIB:
		ret = handle_unload_lib_ioctl(avp, arg);
		break;
	default:
		pr_err("avp_lib: Unknown tegra_avp ioctl 0x%x\n", _IOC_NR(cmd));
		ret = -ENOTTY;
		break;
	}
	return ret;
}

static int tegra_avp_open(struct inode *inode, struct file *file)
{
	struct avp_info *avp = tegra_avp;
	int ret = 0;

	nonseekable_open(inode, file);

	mutex_lock(&avp->open_lock);
	/* only one userspace client at a time */
	if (avp->opened) {
		pr_err("%s: already have client, aborting\n", __func__);
		ret = -EBUSY;
		goto out;
	}

	ret = avp_init(avp, TEGRA_AVP_KERNEL_FW);
	avp->opened = !ret;
out:
	mutex_unlock(&avp->open_lock);
	return ret;
}

static int tegra_avp_release(struct inode *inode, struct file *file)
{
	struct avp_info *avp = tegra_avp;
	int ret = 0;

	pr_info("%s: release\n", __func__);
	mutex_lock(&avp->open_lock);
	if (!avp->opened) {
		pr_err("%s: releasing while in invalid state\n", __func__);
		ret = -EINVAL;
		goto out;
	}

	avp_uninit(avp);

	avp->opened = false;
out:
	mutex_unlock(&avp->open_lock);
	return ret;
}

static int avp_enter_lp0(struct avp_info *avp)
{
	volatile u32 *avp_suspend_done =
		avp->iram_backup_data + TEGRA_IRAM_SIZE;
	struct svc_enter_lp0 svc;
	unsigned long endtime;
	int ret;

	svc.svc_id = SVC_ENTER_LP0;
	svc.src_addr = (u32)TEGRA_IRAM_BASE;
	svc.buf_addr = (u32)avp->iram_backup_phys;
	svc.buf_size = TEGRA_IRAM_SIZE;

	*avp_suspend_done = 0;
	wmb();

	ret = trpc_send_msg(avp->rpc_node, avp->avp_ep, &svc, sizeof(svc),
				 GFP_KERNEL);
	if (ret) {
		pr_err("%s: cannot send AVP suspend message\n", __func__);
		return ret;
	}

	endtime = jiffies + msecs_to_jiffies(1000);
	rmb();
	while ((*avp_suspend_done == 0) && time_before(jiffies, endtime)) {
		udelay(10);
		rmb();
	}

	rmb();
	if (*avp_suspend_done == 0) {
		pr_err("%s: AVP failed to suspend\n", __func__);
		ret = -ETIMEDOUT;
		goto err;
	}

	return 0;

err:
	return ret;
}

static int tegra_avp_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct avp_info *avp = tegra_avp;
	unsigned long flags;
	int ret;

	pr_info("%s()+\n", __func__);
	spin_lock_irqsave(&avp->state_lock, flags);
	if (!avp->initialized) {
		spin_unlock_irqrestore(&avp->state_lock, flags);
		return 0;
	}
	avp->suspending = true;
	spin_unlock_irqrestore(&avp->state_lock, flags);

	ret = avp_enter_lp0(avp);
	if (ret)
		goto err;

	avp->resume_addr = readl(TEGRA_AVP_RESUME_ADDR);
	if (!avp->resume_addr) {
		pr_err("%s: AVP failed to set it's resume address\n", __func__);
		ret = -EINVAL;
		goto err;
	}

	disable_irq(avp->mbox_from_avp_pend_irq);

	pr_info("avp_suspend: resume_addr=%lx\n", avp->resume_addr);
	avp->resume_addr &= 0xfffffffeUL;
	pr_info("%s()-\n", __func__);

	return 0;

err:
	/* TODO: we need to kill the AVP so that when we come back
	 * it could be reinitialized.. We'd probably need to kill
	 * the users of it so they don't have the wrong state.
	 */
	return ret;
}

static int tegra_avp_resume(struct platform_device *pdev)
{
	struct avp_info *avp = tegra_avp;
	int ret = 0;

	pr_info("%s()+\n", __func__);
	smp_rmb();
	if (!avp->initialized)
		goto out;

	BUG_ON(!avp->resume_addr);

	avp_reset(avp, avp->resume_addr);
	avp->resume_addr = 0;
	avp->suspending = false;
	smp_wmb();
	enable_irq(avp->mbox_from_avp_pend_irq);

	pr_info("%s()-\n", __func__);

out:
	return ret;
}

static const struct file_operations tegra_avp_fops = {
	.owner		= THIS_MODULE,
	.open		= tegra_avp_open,
	.release	= tegra_avp_release,
	.unlocked_ioctl	= tegra_avp_ioctl,
};

static struct trpc_node avp_trpc_node = {
	.name		= "avp-remote",
	.type		= TRPC_NODE_REMOTE,
	.try_connect	= avp_node_try_connect,
};

static int tegra_avp_probe(struct platform_device *pdev)
{
	void *msg_area;
	struct avp_info *avp;
	int ret = 0;
	int irq;

	irq = platform_get_irq_byname(pdev, "mbox_from_avp_pending");
	if (irq < 0) {
		pr_err("%s: invalid platform data\n", __func__);
		return -EINVAL;
	}

	avp = kzalloc(sizeof(struct avp_info), GFP_KERNEL);
	if (!avp) {
		pr_err("%s: cannot allocate avp_info\n", __func__);
		return -ENOMEM;
	}

	avp->nvmap_drv = nvmap_create_client(nvmap_dev, "avp_core");
	if (IS_ERR(avp->nvmap_drv)) {
		pr_err("%s: cannot create drv nvmap client\n", __func__);
		ret = PTR_ERR(avp->nvmap_drv);
		goto err_nvmap_create_drv_client;
	}

	avp->kernel_handle = nvmap_alloc(avp->nvmap_drv, SZ_1M, SZ_1M,
					 NVMAP_HANDLE_WRITE_COMBINE);
	if (IS_ERR(avp->kernel_handle)) {
		pr_err("%s: cannot create handle\n", __func__);
		ret = PTR_ERR(avp->kernel_handle);
		goto err_nvmap_alloc;
	}

	avp->kernel_data = nvmap_mmap(avp->kernel_handle);
	if (!avp->kernel_data) {
		pr_err("%s: cannot map kernel handle\n", __func__);
		ret = -ENOMEM;
		goto err_nvmap_mmap;
	}

	avp->kernel_phys = nvmap_pin(avp->nvmap_drv, avp->kernel_handle);
	if (IS_ERR((void *)avp->kernel_phys)) {
		pr_err("%s: cannot pin kernel handle\n", __func__);
		ret = PTR_ERR((void *)avp->kernel_phys);
		goto err_nvmap_pin;
	}

	/* allocate an extra 4 bytes at the end which AVP uses to signal to
	 * us that it is done suspending.
	 */
	avp->iram_backup_handle =
		nvmap_alloc(avp->nvmap_drv, TEGRA_IRAM_SIZE + 4,
			    L1_CACHE_BYTES, NVMAP_HANDLE_WRITE_COMBINE);
	if (IS_ERR(avp->iram_backup_handle)) {
		pr_err("%s: cannot create handle for iram backup\n", __func__);
		ret = PTR_ERR(avp->iram_backup_handle);
		goto err_iram_nvmap_alloc;
	}
	avp->iram_backup_data = nvmap_mmap(avp->iram_backup_handle);
	if (!avp->iram_backup_data) {
		pr_err("%s: cannot map iram backup handle\n", __func__);
		ret = -ENOMEM;
		goto err_iram_nvmap_mmap;
	}
	avp->iram_backup_phys = nvmap_pin(avp->nvmap_drv,
					  avp->iram_backup_handle);
	if (IS_ERR((void *)avp->iram_backup_phys)) {
		pr_err("%s: cannot pin iram backup handle\n", __func__);
		ret = PTR_ERR((void *)avp->iram_backup_phys);
		goto err_iram_nvmap_pin;
	}

	avp->mbox_from_avp_pend_irq = irq;
	avp->endpoints = RB_ROOT;
	spin_lock_init(&avp->state_lock);
	mutex_init(&avp->open_lock);
	mutex_init(&avp->to_avp_lock);
	mutex_init(&avp->from_avp_lock);
	INIT_WORK(&avp->recv_work, process_avp_message);

	mutex_init(&avp->libs_lock);
	INIT_LIST_HEAD(&avp->libs);

	avp->recv_wq = alloc_workqueue("avp-msg-recv",
				       WQ_NON_REENTRANT | WQ_HIGHPRI, 1);
	if (!avp->recv_wq) {
		pr_err("%s: can't create recve workqueue\n", __func__);
		ret = -ENOMEM;
		goto err_create_wq;
	}

	avp->cop_clk = clk_get(&pdev->dev, "cop");
	if (IS_ERR(avp->cop_clk)) {
		pr_err("%s: Couldn't get cop clock\n", TEGRA_AVP_NAME);
		ret = -ENOENT;
		goto err_get_cop_clk;
	}

	msg_area = dma_alloc_coherent(&pdev->dev, AVP_MSG_AREA_SIZE * 2,
				      &avp->msg_area_addr, GFP_KERNEL);
	if (!msg_area) {
		pr_err("%s: cannot allocate msg_area\n", __func__);
		ret = -ENOMEM;
		goto err_alloc_msg_area;
	}
	memset(msg_area, 0, AVP_MSG_AREA_SIZE * 2);
	avp->msg = ((avp->msg_area_addr >> 4) |
			MBOX_MSG_VALID | MBOX_MSG_PENDING_INT_EN);
	avp->msg_to_avp = msg_area;
	avp->msg_from_avp = msg_area + AVP_MSG_AREA_SIZE;

	avp_halt(avp);

	avp_trpc_node.priv = avp;
	ret = trpc_node_register(&avp_trpc_node);
	if (ret) {
		pr_err("%s: Can't register avp rpc node\n", __func__);
		goto err_node_reg;
	}
	avp->rpc_node = &avp_trpc_node;

	avp->avp_svc = avp_svc_init(pdev, avp->rpc_node);
	if (IS_ERR(avp->avp_svc)) {
		pr_err("%s: Cannot initialize avp_svc\n", __func__);
		ret = PTR_ERR(avp->avp_svc);
		goto err_avp_svc_init;
	}

	avp->misc_dev.minor = MISC_DYNAMIC_MINOR;
	avp->misc_dev.name = "tegra_avp";
	avp->misc_dev.fops = &tegra_avp_fops;

	ret = misc_register(&avp->misc_dev);
	if (ret) {
		pr_err("%s: Unable to register misc device!\n", TEGRA_AVP_NAME);
		goto err_misc_reg;
	}

	ret = request_irq(irq, avp_mbox_pending_isr, 0, TEGRA_AVP_NAME, avp);
	if (ret) {
		pr_err("%s: cannot register irq handler\n", __func__);
		goto err_req_irq_pend;
	}
	disable_irq(avp->mbox_from_avp_pend_irq);

	tegra_avp = avp;

	pr_info("%s: driver registered, kernel %lx(%p), msg area %lx/%lx\n",
		__func__, avp->kernel_phys, avp->kernel_data,
		(unsigned long)avp->msg_area_addr,
		(unsigned long)avp->msg_area_addr + AVP_MSG_AREA_SIZE);

	return 0;

err_req_irq_pend:
	misc_deregister(&avp->misc_dev);
err_misc_reg:
	avp_svc_destroy(avp->avp_svc);
err_avp_svc_init:
	trpc_node_unregister(avp->rpc_node);
err_node_reg:
	dma_free_coherent(&pdev->dev, AVP_MSG_AREA_SIZE * 2, msg_area,
			  avp->msg_area_addr);
err_alloc_msg_area:
	clk_put(avp->cop_clk);
err_get_cop_clk:
	destroy_workqueue(avp->recv_wq);
err_create_wq:
	nvmap_unpin(avp->nvmap_drv, avp->iram_backup_handle);
err_iram_nvmap_pin:
	nvmap_munmap(avp->iram_backup_handle, avp->iram_backup_data);
err_iram_nvmap_mmap:
	nvmap_free(avp->nvmap_drv, avp->iram_backup_handle);
err_iram_nvmap_alloc:
	nvmap_unpin(avp->nvmap_drv, avp->kernel_handle);
err_nvmap_pin:
	nvmap_munmap(avp->kernel_handle, avp->kernel_data);
err_nvmap_mmap:
	nvmap_free(avp->nvmap_drv, avp->kernel_handle);
err_nvmap_alloc:
	nvmap_client_put(avp->nvmap_drv);
err_nvmap_create_drv_client:
	kfree(avp);
	tegra_avp = NULL;
	return ret;
}

static int tegra_avp_remove(struct platform_device *pdev)
{
	struct avp_info *avp = tegra_avp;

	if (!avp)
		return 0;

	mutex_lock(&avp->open_lock);
	if (avp->opened) {
		mutex_unlock(&avp->open_lock);
		return -EBUSY;
	}
	/* ensure that noone can open while we tear down */
	avp->opened = true;
	mutex_unlock(&avp->open_lock);

	misc_deregister(&avp->misc_dev);

	avp_halt(avp);

	avp_svc_destroy(avp->avp_svc);
	trpc_node_unregister(avp->rpc_node);
	dma_free_coherent(&pdev->dev, AVP_MSG_AREA_SIZE * 2, avp->msg_to_avp,
			  avp->msg_area_addr);
	clk_put(avp->cop_clk);
	destroy_workqueue(avp->recv_wq);
	nvmap_unpin(avp->nvmap_drv, avp->iram_backup_handle);
	nvmap_munmap(avp->iram_backup_handle, avp->iram_backup_data);
	nvmap_free(avp->nvmap_drv, avp->iram_backup_handle);
	nvmap_unpin(avp->nvmap_drv, avp->kernel_handle);
	nvmap_munmap(avp->kernel_handle, avp->kernel_data);
	nvmap_free(avp->nvmap_drv, avp->kernel_handle);
	nvmap_client_put(avp->nvmap_drv);
	kfree(avp);
	tegra_avp = NULL;
	return 0;
}

static struct platform_driver tegra_avp_driver = {
	.probe		= tegra_avp_probe,
	.remove		= tegra_avp_remove,
	.suspend	= tegra_avp_suspend,
	.resume		= tegra_avp_resume,
	.driver	= {
		.name	= TEGRA_AVP_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init tegra_avp_init(void)
{
	return platform_driver_register(&tegra_avp_driver);
}

static void __exit tegra_avp_exit(void)
{
	platform_driver_unregister(&tegra_avp_driver);
}

module_init(tegra_avp_init);
module_exit(tegra_avp_exit);
