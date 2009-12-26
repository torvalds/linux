/* arch/arm/mach-msm/smd_rpcrouter.c
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2007-2009 QUALCOMM Incorporated.
 * Author: San Mehat <san@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 */

/* TODO: handle cases where smd_write() will tempfail due to full fifo */
/* TODO: thread priority? schedule a work to bump it? */
/* TODO: maybe make server_list_lock a mutex */
/* TODO: pool fragments to avoid kmalloc/kfree churn */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/cdev.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>

#include <asm/byteorder.h>

#include <mach/msm_smd.h>
#include "smd_rpcrouter.h"

#define TRACE_R2R_MSG 0
#define TRACE_R2R_RAW 0
#define TRACE_RPC_MSG 0
#define TRACE_NOTIFY_MSG 0

#define MSM_RPCROUTER_DEBUG 0
#define MSM_RPCROUTER_DEBUG_PKT 0
#define MSM_RPCROUTER_R2R_DEBUG 0
#define DUMP_ALL_RECEIVED_HEADERS 0

#define DIAG(x...) printk("[RR] ERROR " x)

#if MSM_RPCROUTER_DEBUG
#define D(x...) printk(x)
#else
#define D(x...) do {} while (0)
#endif

#if TRACE_R2R_MSG
#define RR(x...) printk("[RR] "x)
#else
#define RR(x...) do {} while (0)
#endif

#if TRACE_RPC_MSG
#define IO(x...) printk("[RPC] "x)
#else
#define IO(x...) do {} while (0)
#endif

#if TRACE_NOTIFY_MSG
#define NTFY(x...) printk(KERN_ERR "[NOTIFY] "x)
#else
#define NTFY(x...) do {} while (0)
#endif

static LIST_HEAD(local_endpoints);
static LIST_HEAD(remote_endpoints);

static LIST_HEAD(server_list);

static smd_channel_t *smd_channel;
static int initialized;
static wait_queue_head_t newserver_wait;
static wait_queue_head_t smd_wait;

static DEFINE_SPINLOCK(local_endpoints_lock);
static DEFINE_SPINLOCK(remote_endpoints_lock);
static DEFINE_SPINLOCK(server_list_lock);
static DEFINE_SPINLOCK(smd_lock);

static struct workqueue_struct *rpcrouter_workqueue;
static int rpcrouter_need_len;

static atomic_t next_xid = ATOMIC_INIT(1);
static uint8_t next_pacmarkid;

static void do_read_data(struct work_struct *work);
static void do_create_pdevs(struct work_struct *work);
static void do_create_rpcrouter_pdev(struct work_struct *work);

static DECLARE_WORK(work_read_data, do_read_data);
static DECLARE_WORK(work_create_pdevs, do_create_pdevs);
static DECLARE_WORK(work_create_rpcrouter_pdev, do_create_rpcrouter_pdev);

#define RR_STATE_IDLE    0
#define RR_STATE_HEADER  1
#define RR_STATE_BODY    2
#define RR_STATE_ERROR   3

struct rr_context {
	struct rr_packet *pkt;
	uint8_t *ptr;
	uint32_t state; /* current assembly state */
	uint32_t count; /* bytes needed in this state */
};

static struct rr_context the_rr_context;

static struct platform_device rpcrouter_pdev = {
	.name		= "oncrpc_router",
	.id		= -1,
};


static int rpcrouter_send_control_msg(union rr_control_msg *msg)
{
	struct rr_header hdr;
	unsigned long flags;
	int need;

	if (!(msg->cmd == RPCROUTER_CTRL_CMD_HELLO) && !initialized) {
		printk(KERN_ERR "rpcrouter_send_control_msg(): Warning, "
		       "router not initialized\n");
		return -EINVAL;
	}

	hdr.version = RPCROUTER_VERSION;
	hdr.type = msg->cmd;
	hdr.src_pid = RPCROUTER_PID_LOCAL;
	hdr.src_cid = RPCROUTER_ROUTER_ADDRESS;
	hdr.confirm_rx = 0;
	hdr.size = sizeof(*msg);
	hdr.dst_pid = 0;
	hdr.dst_cid = RPCROUTER_ROUTER_ADDRESS;

	/* TODO: what if channel is full? */

	need = sizeof(hdr) + hdr.size;
	spin_lock_irqsave(&smd_lock, flags);
	while (smd_write_avail(smd_channel) < need) {
		spin_unlock_irqrestore(&smd_lock, flags);
		msleep(250);
		spin_lock_irqsave(&smd_lock, flags);
	}
	smd_write(smd_channel, &hdr, sizeof(hdr));
	smd_write(smd_channel, msg, hdr.size);
	spin_unlock_irqrestore(&smd_lock, flags);
	return 0;
}

static struct rr_server *rpcrouter_create_server(uint32_t pid,
							uint32_t cid,
							uint32_t prog,
							uint32_t ver)
{
	struct rr_server *server;
	unsigned long flags;
	int rc;

	server = kmalloc(sizeof(struct rr_server), GFP_KERNEL);
	if (!server)
		return ERR_PTR(-ENOMEM);

	memset(server, 0, sizeof(struct rr_server));
	server->pid = pid;
	server->cid = cid;
	server->prog = prog;
	server->vers = ver;

	spin_lock_irqsave(&server_list_lock, flags);
	list_add_tail(&server->list, &server_list);
	spin_unlock_irqrestore(&server_list_lock, flags);

	if (pid == RPCROUTER_PID_REMOTE) {
		rc = msm_rpcrouter_create_server_cdev(server);
		if (rc < 0)
			goto out_fail;
	}
	return server;
out_fail:
	spin_lock_irqsave(&server_list_lock, flags);
	list_del(&server->list);
	spin_unlock_irqrestore(&server_list_lock, flags);
	kfree(server);
	return ERR_PTR(rc);
}

static void rpcrouter_destroy_server(struct rr_server *server)
{
	unsigned long flags;

	spin_lock_irqsave(&server_list_lock, flags);
	list_del(&server->list);
	spin_unlock_irqrestore(&server_list_lock, flags);
	device_destroy(msm_rpcrouter_class, server->device_number);
	kfree(server);
}

static struct rr_server *rpcrouter_lookup_server(uint32_t prog, uint32_t ver)
{
	struct rr_server *server;
	unsigned long flags;

	spin_lock_irqsave(&server_list_lock, flags);
	list_for_each_entry(server, &server_list, list) {
		if (server->prog == prog
		 && server->vers == ver) {
			spin_unlock_irqrestore(&server_list_lock, flags);
			return server;
		}
	}
	spin_unlock_irqrestore(&server_list_lock, flags);
	return NULL;
}

static struct rr_server *rpcrouter_lookup_server_by_dev(dev_t dev)
{
	struct rr_server *server;
	unsigned long flags;

	spin_lock_irqsave(&server_list_lock, flags);
	list_for_each_entry(server, &server_list, list) {
		if (server->device_number == dev) {
			spin_unlock_irqrestore(&server_list_lock, flags);
			return server;
		}
	}
	spin_unlock_irqrestore(&server_list_lock, flags);
	return NULL;
}

struct msm_rpc_endpoint *msm_rpcrouter_create_local_endpoint(dev_t dev)
{
	struct msm_rpc_endpoint *ept;
	unsigned long flags;

	ept = kmalloc(sizeof(struct msm_rpc_endpoint), GFP_KERNEL);
	if (!ept)
		return NULL;
	memset(ept, 0, sizeof(struct msm_rpc_endpoint));

	/* mark no reply outstanding */
	ept->reply_pid = 0xffffffff;

	ept->cid = (uint32_t) ept;
	ept->pid = RPCROUTER_PID_LOCAL;
	ept->dev = dev;

	if ((dev != msm_rpcrouter_devno) && (dev != MKDEV(0, 0))) {
		struct rr_server *srv;
		/*
		 * This is a userspace client which opened
		 * a program/ver devicenode. Bind the client
		 * to that destination
		 */
		srv = rpcrouter_lookup_server_by_dev(dev);
		/* TODO: bug? really? */
		BUG_ON(!srv);

		ept->dst_pid = srv->pid;
		ept->dst_cid = srv->cid;
		ept->dst_prog = cpu_to_be32(srv->prog);
		ept->dst_vers = cpu_to_be32(srv->vers);

		D("Creating local ept %p @ %08x:%08x\n", ept, srv->prog, srv->vers);
	} else {
		/* mark not connected */
		ept->dst_pid = 0xffffffff;
		D("Creating a master local ept %p\n", ept);
	}

	init_waitqueue_head(&ept->wait_q);
	INIT_LIST_HEAD(&ept->read_q);
	spin_lock_init(&ept->read_q_lock);
	INIT_LIST_HEAD(&ept->incomplete);

	spin_lock_irqsave(&local_endpoints_lock, flags);
	list_add_tail(&ept->list, &local_endpoints);
	spin_unlock_irqrestore(&local_endpoints_lock, flags);
	return ept;
}

int msm_rpcrouter_destroy_local_endpoint(struct msm_rpc_endpoint *ept)
{
	int rc;
	union rr_control_msg msg;

	msg.cmd = RPCROUTER_CTRL_CMD_REMOVE_CLIENT;
	msg.cli.pid = ept->pid;
	msg.cli.cid = ept->cid;

	RR("x REMOVE_CLIENT id=%d:%08x\n", ept->pid, ept->cid);
	rc = rpcrouter_send_control_msg(&msg);
	if (rc < 0)
		return rc;

	list_del(&ept->list);
	kfree(ept);
	return 0;
}

static int rpcrouter_create_remote_endpoint(uint32_t cid)
{
	struct rr_remote_endpoint *new_c;
	unsigned long flags;

	new_c = kmalloc(sizeof(struct rr_remote_endpoint), GFP_KERNEL);
	if (!new_c)
		return -ENOMEM;
	memset(new_c, 0, sizeof(struct rr_remote_endpoint));

	new_c->cid = cid;
	new_c->pid = RPCROUTER_PID_REMOTE;
	init_waitqueue_head(&new_c->quota_wait);
	spin_lock_init(&new_c->quota_lock);

	spin_lock_irqsave(&remote_endpoints_lock, flags);
	list_add_tail(&new_c->list, &remote_endpoints);
	spin_unlock_irqrestore(&remote_endpoints_lock, flags);
	return 0;
}

static struct msm_rpc_endpoint *rpcrouter_lookup_local_endpoint(uint32_t cid)
{
	struct msm_rpc_endpoint *ept;
	unsigned long flags;

	spin_lock_irqsave(&local_endpoints_lock, flags);
	list_for_each_entry(ept, &local_endpoints, list) {
		if (ept->cid == cid) {
			spin_unlock_irqrestore(&local_endpoints_lock, flags);
			return ept;
		}
	}
	spin_unlock_irqrestore(&local_endpoints_lock, flags);
	return NULL;
}

static struct rr_remote_endpoint *rpcrouter_lookup_remote_endpoint(uint32_t cid)
{
	struct rr_remote_endpoint *ept;
	unsigned long flags;

	spin_lock_irqsave(&remote_endpoints_lock, flags);
	list_for_each_entry(ept, &remote_endpoints, list) {
		if (ept->cid == cid) {
			spin_unlock_irqrestore(&remote_endpoints_lock, flags);
			return ept;
		}
	}
	spin_unlock_irqrestore(&remote_endpoints_lock, flags);
	return NULL;
}

static int process_control_msg(union rr_control_msg *msg, int len)
{
	union rr_control_msg ctl;
	struct rr_server *server;
	struct rr_remote_endpoint *r_ept;
	int rc = 0;
	unsigned long flags;

	if (len != sizeof(*msg)) {
		printk(KERN_ERR "rpcrouter: r2r msg size %d != %d\n",
		       len, sizeof(*msg));
		return -EINVAL;
	}

	switch (msg->cmd) {
	case RPCROUTER_CTRL_CMD_HELLO:
		RR("o HELLO\n");

		RR("x HELLO\n");
		memset(&ctl, 0, sizeof(ctl));
		ctl.cmd = RPCROUTER_CTRL_CMD_HELLO;
		rpcrouter_send_control_msg(&ctl);

		initialized = 1;

		/* Send list of servers one at a time */
		ctl.cmd = RPCROUTER_CTRL_CMD_NEW_SERVER;

		/* TODO: long time to hold a spinlock... */
		spin_lock_irqsave(&server_list_lock, flags);
		list_for_each_entry(server, &server_list, list) {
			ctl.srv.pid = server->pid;
			ctl.srv.cid = server->cid;
			ctl.srv.prog = server->prog;
			ctl.srv.vers = server->vers;

			RR("x NEW_SERVER id=%d:%08x prog=%08x:%08x\n",
			   server->pid, server->cid,
			   server->prog, server->vers);

			rpcrouter_send_control_msg(&ctl);
		}
		spin_unlock_irqrestore(&server_list_lock, flags);

		queue_work(rpcrouter_workqueue, &work_create_rpcrouter_pdev);
		break;

	case RPCROUTER_CTRL_CMD_RESUME_TX:
		RR("o RESUME_TX id=%d:%08x\n", msg->cli.pid, msg->cli.cid);

		r_ept = rpcrouter_lookup_remote_endpoint(msg->cli.cid);
		if (!r_ept) {
			printk(KERN_ERR
			       "rpcrouter: Unable to resume client\n");
			break;
		}
		spin_lock_irqsave(&r_ept->quota_lock, flags);
		r_ept->tx_quota_cntr = 0;
		spin_unlock_irqrestore(&r_ept->quota_lock, flags);
		wake_up(&r_ept->quota_wait);
		break;

	case RPCROUTER_CTRL_CMD_NEW_SERVER:
		RR("o NEW_SERVER id=%d:%08x prog=%08x:%08x\n",
		   msg->srv.pid, msg->srv.cid, msg->srv.prog, msg->srv.vers);

		server = rpcrouter_lookup_server(msg->srv.prog, msg->srv.vers);

		if (!server) {
			server = rpcrouter_create_server(
				msg->srv.pid, msg->srv.cid,
				msg->srv.prog, msg->srv.vers);
			if (!server)
				return -ENOMEM;
			/*
			 * XXX: Verify that its okay to add the
			 * client to our remote client list
			 * if we get a NEW_SERVER notification
			 */
			if (!rpcrouter_lookup_remote_endpoint(msg->srv.cid)) {
				rc = rpcrouter_create_remote_endpoint(
					msg->srv.cid);
				if (rc < 0)
					printk(KERN_ERR
						"rpcrouter:Client create"
						"error (%d)\n", rc);
			}
			schedule_work(&work_create_pdevs);
			wake_up(&newserver_wait);
		} else {
			if ((server->pid == msg->srv.pid) &&
			    (server->cid == msg->srv.cid)) {
				printk(KERN_ERR "rpcrouter: Duplicate svr\n");
			} else {
				server->pid = msg->srv.pid;
				server->cid = msg->srv.cid;
			}
		}
		break;

	case RPCROUTER_CTRL_CMD_REMOVE_SERVER:
		RR("o REMOVE_SERVER prog=%08x:%d\n",
		   msg->srv.prog, msg->srv.vers);
		server = rpcrouter_lookup_server(msg->srv.prog, msg->srv.vers);
		if (server)
			rpcrouter_destroy_server(server);
		break;

	case RPCROUTER_CTRL_CMD_REMOVE_CLIENT:
		RR("o REMOVE_CLIENT id=%d:%08x\n", msg->cli.pid, msg->cli.cid);
		if (msg->cli.pid != RPCROUTER_PID_REMOTE) {
			printk(KERN_ERR
			       "rpcrouter: Denying remote removal of "
			       "local client\n");
			break;
		}
		r_ept = rpcrouter_lookup_remote_endpoint(msg->cli.cid);
		if (r_ept) {
			spin_lock_irqsave(&remote_endpoints_lock, flags);
			list_del(&r_ept->list);
			spin_unlock_irqrestore(&remote_endpoints_lock, flags);
			kfree(r_ept);
		}

		/* Notify local clients of this event */
		printk(KERN_ERR "rpcrouter: LOCAL NOTIFICATION NOT IMP\n");
		rc = -ENOSYS;

		break;
	default:
		RR("o UNKNOWN(%08x)\n", msg->cmd);
		rc = -ENOSYS;
	}

	return rc;
}

static void do_create_rpcrouter_pdev(struct work_struct *work)
{
	platform_device_register(&rpcrouter_pdev);
}

static void do_create_pdevs(struct work_struct *work)
{
	unsigned long flags;
	struct rr_server *server;

	/* TODO: race if destroyed while being registered */
	spin_lock_irqsave(&server_list_lock, flags);
	list_for_each_entry(server, &server_list, list) {
		if (server->pid == RPCROUTER_PID_REMOTE) {
			if (server->pdev_name[0] == 0) {
				spin_unlock_irqrestore(&server_list_lock,
						       flags);
				msm_rpcrouter_create_server_pdev(server);
				schedule_work(&work_create_pdevs);
				return;
			}
		}
	}
	spin_unlock_irqrestore(&server_list_lock, flags);
}

static void rpcrouter_smdnotify(void *_dev, unsigned event)
{
	if (event != SMD_EVENT_DATA)
		return;

	wake_up(&smd_wait);
}

static void *rr_malloc(unsigned sz)
{
	void *ptr = kmalloc(sz, GFP_KERNEL);
	if (ptr)
		return ptr;

	printk(KERN_ERR "rpcrouter: kmalloc of %d failed, retrying...\n", sz);
	do {
		ptr = kmalloc(sz, GFP_KERNEL);
	} while (!ptr);

	return ptr;
}

/* TODO: deal with channel teardown / restore */
static int rr_read(void *data, int len)
{
	int rc;
	unsigned long flags;
//	printk("rr_read() %d\n", len);
	for(;;) {
		spin_lock_irqsave(&smd_lock, flags);
		if (smd_read_avail(smd_channel) >= len) {
			rc = smd_read(smd_channel, data, len);
			spin_unlock_irqrestore(&smd_lock, flags);
			if (rc == len)
				return 0;
			else
				return -EIO;
		}
		rpcrouter_need_len = len;
		spin_unlock_irqrestore(&smd_lock, flags);

//		printk("rr_read: waiting (%d)\n", len);
		wait_event(smd_wait, smd_read_avail(smd_channel) >= len);
	}
	return 0;
}

static uint32_t r2r_buf[RPCROUTER_MSGSIZE_MAX];

static void do_read_data(struct work_struct *work)
{
	struct rr_header hdr;
	struct rr_packet *pkt;
	struct rr_fragment *frag;
	struct msm_rpc_endpoint *ept;
	uint32_t pm, mid;
	unsigned long flags;

	if (rr_read(&hdr, sizeof(hdr)))
		goto fail_io;

#if TRACE_R2R_RAW
	RR("- ver=%d type=%d src=%d:%08x crx=%d siz=%d dst=%d:%08x\n",
	   hdr.version, hdr.type, hdr.src_pid, hdr.src_cid,
	   hdr.confirm_rx, hdr.size, hdr.dst_pid, hdr.dst_cid);
#endif

	if (hdr.version != RPCROUTER_VERSION) {
		DIAG("version %d != %d\n", hdr.version, RPCROUTER_VERSION);
		goto fail_data;
	}
	if (hdr.size > RPCROUTER_MSGSIZE_MAX) {
		DIAG("msg size %d > max %d\n", hdr.size, RPCROUTER_MSGSIZE_MAX);
		goto fail_data;
	}

	if (hdr.dst_cid == RPCROUTER_ROUTER_ADDRESS) {
		if (rr_read(r2r_buf, hdr.size))
			goto fail_io;
		process_control_msg((void*) r2r_buf, hdr.size);
		goto done;
	}

	if (hdr.size < sizeof(pm)) {
		DIAG("runt packet (no pacmark)\n");
		goto fail_data;
	}
	if (rr_read(&pm, sizeof(pm)))
		goto fail_io;

	hdr.size -= sizeof(pm);

	frag = rr_malloc(hdr.size + sizeof(*frag));
	frag->next = NULL;
	frag->length = hdr.size;
	if (rr_read(frag->data, hdr.size))
		goto fail_io;

	ept = rpcrouter_lookup_local_endpoint(hdr.dst_cid);
	if (!ept) {
		DIAG("no local ept for cid %08x\n", hdr.dst_cid);
		kfree(frag);
		goto done;
	}

	/* See if there is already a partial packet that matches our mid
	 * and if so, append this fragment to that packet.
	 */
	mid = PACMARK_MID(pm);
	list_for_each_entry(pkt, &ept->incomplete, list) {
		if (pkt->mid == mid) {
			pkt->last->next = frag;
			pkt->last = frag;
			pkt->length += frag->length;
			if (PACMARK_LAST(pm)) {
				list_del(&pkt->list);
				goto packet_complete;
			}
			goto done;
		}
	}
	/* This mid is new -- create a packet for it, and put it on
	 * the incomplete list if this fragment is not a last fragment,
	 * otherwise put it on the read queue.
	 */
	pkt = rr_malloc(sizeof(struct rr_packet));
	pkt->first = frag;
	pkt->last = frag;
	memcpy(&pkt->hdr, &hdr, sizeof(hdr));
	pkt->mid = mid;
	pkt->length = frag->length;
	if (!PACMARK_LAST(pm)) {
		list_add_tail(&pkt->list, &ept->incomplete);
		goto done;
	}

packet_complete:
	spin_lock_irqsave(&ept->read_q_lock, flags);
	list_add_tail(&pkt->list, &ept->read_q);
	wake_up(&ept->wait_q);
	spin_unlock_irqrestore(&ept->read_q_lock, flags);
done:

	if (hdr.confirm_rx) {
		union rr_control_msg msg;

		msg.cmd = RPCROUTER_CTRL_CMD_RESUME_TX;
		msg.cli.pid = hdr.dst_pid;
		msg.cli.cid = hdr.dst_cid;

		RR("x RESUME_TX id=%d:%08x\n", msg.cli.pid, msg.cli.cid);
		rpcrouter_send_control_msg(&msg);
	}

	queue_work(rpcrouter_workqueue, &work_read_data);
	return;

fail_io:
fail_data:
	printk(KERN_ERR "rpc_router has died\n");
}

void msm_rpc_setup_req(struct rpc_request_hdr *hdr, uint32_t prog,
		       uint32_t vers, uint32_t proc)
{
	memset(hdr, 0, sizeof(struct rpc_request_hdr));
	hdr->xid = cpu_to_be32(atomic_add_return(1, &next_xid));
	hdr->rpc_vers = cpu_to_be32(2);
	hdr->prog = cpu_to_be32(prog);
	hdr->vers = cpu_to_be32(vers);
	hdr->procedure = cpu_to_be32(proc);
}

struct msm_rpc_endpoint *msm_rpc_open(void)
{
	struct msm_rpc_endpoint *ept;

	ept = msm_rpcrouter_create_local_endpoint(MKDEV(0, 0));
	if (ept == NULL)
		return ERR_PTR(-ENOMEM);

	return ept;
}

int msm_rpc_close(struct msm_rpc_endpoint *ept)
{
	return msm_rpcrouter_destroy_local_endpoint(ept);
}
EXPORT_SYMBOL(msm_rpc_close);

int msm_rpc_write(struct msm_rpc_endpoint *ept, void *buffer, int count)
{
	struct rr_header hdr;
	uint32_t pacmark;
	struct rpc_request_hdr *rq = buffer;
	struct rr_remote_endpoint *r_ept;
	unsigned long flags;
	int needed;
	DEFINE_WAIT(__wait);

	/* TODO: fragmentation for large outbound packets */
	if (count > (RPCROUTER_MSGSIZE_MAX - sizeof(uint32_t)) || !count)
		return -EINVAL;

	/* snoop the RPC packet and enforce permissions */

	/* has to have at least the xid and type fields */
	if (count < (sizeof(uint32_t) * 2)) {
		printk(KERN_ERR "rr_write: rejecting runt packet\n");
		return -EINVAL;
	}

	if (rq->type == 0) {
		/* RPC CALL */
		if (count < (sizeof(uint32_t) * 6)) {
			printk(KERN_ERR
			       "rr_write: rejecting runt call packet\n");
			return -EINVAL;
		}
		if (ept->dst_pid == 0xffffffff) {
			printk(KERN_ERR "rr_write: not connected\n");
			return -ENOTCONN;
		}

#if CONFIG_MSM_AMSS_VERSION >= 6350
		if ((ept->dst_prog != rq->prog) ||
			!msm_rpc_is_compatible_version(
					be32_to_cpu(ept->dst_vers),
					be32_to_cpu(rq->vers))) {
#else
		if (ept->dst_prog != rq->prog || ept->dst_vers != rq->vers) {
#endif
			printk(KERN_ERR
			       "rr_write: cannot write to %08x:%d "
			       "(bound to %08x:%d)\n",
			       be32_to_cpu(rq->prog), be32_to_cpu(rq->vers),
			       be32_to_cpu(ept->dst_prog),
			       be32_to_cpu(ept->dst_vers));
			return -EINVAL;
		}
		hdr.dst_pid = ept->dst_pid;
		hdr.dst_cid = ept->dst_cid;
		IO("CALL on ept %p to %08x:%08x @ %d:%08x (%d bytes) (xid %x proc %x)\n",
		   ept,
		   be32_to_cpu(rq->prog), be32_to_cpu(rq->vers),
		   ept->dst_pid, ept->dst_cid, count,
		   be32_to_cpu(rq->xid), be32_to_cpu(rq->procedure));
	} else {
		/* RPC REPLY */
		/* TODO: locking */
		if (ept->reply_pid == 0xffffffff) {
			printk(KERN_ERR
			       "rr_write: rejecting unexpected reply\n");
			return -EINVAL;
		}
		if (ept->reply_xid != rq->xid) {
			printk(KERN_ERR
			       "rr_write: rejecting packet w/ bad xid\n");
			return -EINVAL;
		}

		hdr.dst_pid = ept->reply_pid;
		hdr.dst_cid = ept->reply_cid;

		/* consume this reply */
		ept->reply_pid = 0xffffffff;

		IO("REPLY on ept %p to xid=%d @ %d:%08x (%d bytes)\n",
		   ept,
		   be32_to_cpu(rq->xid), hdr.dst_pid, hdr.dst_cid, count);
	}

	r_ept = rpcrouter_lookup_remote_endpoint(hdr.dst_cid);

	if (!r_ept) {
		printk(KERN_ERR
			"msm_rpc_write(): No route to ept "
			"[PID %x CID %x]\n", hdr.dst_pid, hdr.dst_cid);
		return -EHOSTUNREACH;
	}

	/* Create routing header */
	hdr.type = RPCROUTER_CTRL_CMD_DATA;
	hdr.version = RPCROUTER_VERSION;
	hdr.src_pid = ept->pid;
	hdr.src_cid = ept->cid;
	hdr.confirm_rx = 0;
	hdr.size = count + sizeof(uint32_t);

	for (;;) {
		prepare_to_wait(&r_ept->quota_wait, &__wait,
				TASK_INTERRUPTIBLE);
		spin_lock_irqsave(&r_ept->quota_lock, flags);
		if (r_ept->tx_quota_cntr < RPCROUTER_DEFAULT_RX_QUOTA)
			break;
		if (signal_pending(current) &&
		    (!(ept->flags & MSM_RPC_UNINTERRUPTIBLE)))
			break;
		spin_unlock_irqrestore(&r_ept->quota_lock, flags);
		schedule();
	}
	finish_wait(&r_ept->quota_wait, &__wait);

	if (signal_pending(current) &&
	    (!(ept->flags & MSM_RPC_UNINTERRUPTIBLE))) {
		spin_unlock_irqrestore(&r_ept->quota_lock, flags);
		return -ERESTARTSYS;
	}
	r_ept->tx_quota_cntr++;
	if (r_ept->tx_quota_cntr == RPCROUTER_DEFAULT_RX_QUOTA)
		hdr.confirm_rx = 1;

	/* bump pacmark while interrupts disabled to avoid race
	 * probably should be atomic op instead
	 */
	pacmark = PACMARK(count, ++next_pacmarkid, 0, 1);

	spin_unlock_irqrestore(&r_ept->quota_lock, flags);

	spin_lock_irqsave(&smd_lock, flags);

	needed = sizeof(hdr) + hdr.size;
	while (smd_write_avail(smd_channel) < needed) {
		spin_unlock_irqrestore(&smd_lock, flags);
		msleep(250);
		spin_lock_irqsave(&smd_lock, flags);
	}

	/* TODO: deal with full fifo */
	smd_write(smd_channel, &hdr, sizeof(hdr));
	smd_write(smd_channel, &pacmark, sizeof(pacmark));
	smd_write(smd_channel, buffer, count);

	spin_unlock_irqrestore(&smd_lock, flags);

	return count;
}
EXPORT_SYMBOL(msm_rpc_write);

/*
 * NOTE: It is the responsibility of the caller to kfree buffer
 */
int msm_rpc_read(struct msm_rpc_endpoint *ept, void **buffer,
		 unsigned user_len, long timeout)
{
	struct rr_fragment *frag, *next;
	char *buf;
	int rc;

	rc = __msm_rpc_read(ept, &frag, user_len, timeout);
	if (rc <= 0)
		return rc;

	/* single-fragment messages conveniently can be
	 * returned as-is (the buffer is at the front)
	 */
	if (frag->next == 0) {
		*buffer = (void*) frag;
		return rc;
	}

	/* multi-fragment messages, we have to do it the
	 * hard way, which is rather disgusting right now
	 */
	buf = rr_malloc(rc);
	*buffer = buf;

	while (frag != NULL) {
		memcpy(buf, frag->data, frag->length);
		next = frag->next;
		buf += frag->length;
		kfree(frag);
		frag = next;
	}

	return rc;
}

int msm_rpc_call(struct msm_rpc_endpoint *ept, uint32_t proc,
		 void *_request, int request_size,
		 long timeout)
{
	return msm_rpc_call_reply(ept, proc,
				  _request, request_size,
				  NULL, 0, timeout);
}
EXPORT_SYMBOL(msm_rpc_call);

int msm_rpc_call_reply(struct msm_rpc_endpoint *ept, uint32_t proc,
		       void *_request, int request_size,
		       void *_reply, int reply_size,
		       long timeout)
{
	struct rpc_request_hdr *req = _request;
	struct rpc_reply_hdr *reply;
	int rc;

	if (request_size < sizeof(*req))
		return -ETOOSMALL;

	if (ept->dst_pid == 0xffffffff)
		return -ENOTCONN;

	/* We can't use msm_rpc_setup_req() here, because dst_prog and
	 * dst_vers here are already in BE.
	 */
	memset(req, 0, sizeof(*req));
	req->xid = cpu_to_be32(atomic_add_return(1, &next_xid));
	req->rpc_vers = cpu_to_be32(2);
	req->prog = ept->dst_prog;
	req->vers = ept->dst_vers;
	req->procedure = cpu_to_be32(proc);

	rc = msm_rpc_write(ept, req, request_size);
	if (rc < 0)
		return rc;

	for (;;) {
		rc = msm_rpc_read(ept, (void*) &reply, -1, timeout);
		if (rc < 0)
			return rc;
		if (rc < (3 * sizeof(uint32_t))) {
			rc = -EIO;
			break;
		}
		/* we should not get CALL packets -- ignore them */
		if (reply->type == 0) {
			kfree(reply);
			continue;
		}
		/* If an earlier call timed out, we could get the (no
		 * longer wanted) reply for it.  Ignore replies that
		 * we don't expect.
		 */
		if (reply->xid != req->xid) {
			kfree(reply);
			continue;
		}
		if (reply->reply_stat != 0) {
			rc = -EPERM;
			break;
		}
		if (reply->data.acc_hdr.accept_stat != 0) {
			rc = -EINVAL;
			break;
		}
		if (_reply == NULL) {
			rc = 0;
			break;
		}
		if (rc > reply_size) {
			rc = -ENOMEM;
		} else {
			memcpy(_reply, reply, rc);
		}
		break;
	}
	kfree(reply);
	return rc;
}
EXPORT_SYMBOL(msm_rpc_call_reply);


static inline int ept_packet_available(struct msm_rpc_endpoint *ept)
{
	unsigned long flags;
	int ret;
	spin_lock_irqsave(&ept->read_q_lock, flags);
	ret = !list_empty(&ept->read_q);
	spin_unlock_irqrestore(&ept->read_q_lock, flags);
	return ret;
}

int __msm_rpc_read(struct msm_rpc_endpoint *ept,
		   struct rr_fragment **frag_ret,
		   unsigned len, long timeout)
{
	struct rr_packet *pkt;
	struct rpc_request_hdr *rq;
	DEFINE_WAIT(__wait);
	unsigned long flags;
	int rc;

	IO("READ on ept %p\n", ept);

	if (ept->flags & MSM_RPC_UNINTERRUPTIBLE) {
		if (timeout < 0) {
			wait_event(ept->wait_q, ept_packet_available(ept));
		} else {
			rc = wait_event_timeout(
				ept->wait_q, ept_packet_available(ept),
				timeout);
			if (rc == 0)
				return -ETIMEDOUT;
		}
	} else {
		if (timeout < 0) {
			rc = wait_event_interruptible(
				ept->wait_q, ept_packet_available(ept));
			if (rc < 0)
				return rc;
		} else {
			rc = wait_event_interruptible_timeout(
				ept->wait_q, ept_packet_available(ept),
				timeout);
			if (rc == 0)
				return -ETIMEDOUT;
		}
	}

	spin_lock_irqsave(&ept->read_q_lock, flags);
	if (list_empty(&ept->read_q)) {
		spin_unlock_irqrestore(&ept->read_q_lock, flags);
		return -EAGAIN;
	}
	pkt = list_first_entry(&ept->read_q, struct rr_packet, list);
	if (pkt->length > len) {
		spin_unlock_irqrestore(&ept->read_q_lock, flags);
		return -ETOOSMALL;
	}
	list_del(&pkt->list);
	spin_unlock_irqrestore(&ept->read_q_lock, flags);

	rc = pkt->length;

	*frag_ret = pkt->first;
	rq = (void*) pkt->first->data;
	if ((rc >= (sizeof(uint32_t) * 3)) && (rq->type == 0)) {
		IO("READ on ept %p is a CALL on %08x:%08x proc %d xid %d\n",
			ept, be32_to_cpu(rq->prog), be32_to_cpu(rq->vers),
			be32_to_cpu(rq->procedure),
			be32_to_cpu(rq->xid));
		/* RPC CALL */
		if (ept->reply_pid != 0xffffffff) {
			printk(KERN_WARNING
			       "rr_read: lost previous reply xid...\n");
		}
		/* TODO: locking? */
		ept->reply_pid = pkt->hdr.src_pid;
		ept->reply_cid = pkt->hdr.src_cid;
		ept->reply_xid = rq->xid;
	}
#if TRACE_RPC_MSG
	else if ((rc >= (sizeof(uint32_t) * 3)) && (rq->type == 1))
		IO("READ on ept %p is a REPLY\n", ept);
	else IO("READ on ept %p (%d bytes)\n", ept, rc);
#endif

	kfree(pkt);
	return rc;
}

#if CONFIG_MSM_AMSS_VERSION >= 6350
int msm_rpc_is_compatible_version(uint32_t server_version,
				  uint32_t client_version)
{
	if ((server_version & RPC_VERSION_MODE_MASK) !=
	    (client_version & RPC_VERSION_MODE_MASK))
		return 0;

	if (server_version & RPC_VERSION_MODE_MASK)
		return server_version == client_version;

	return ((server_version & RPC_VERSION_MAJOR_MASK) ==
		(client_version & RPC_VERSION_MAJOR_MASK)) &&
		((server_version & RPC_VERSION_MINOR_MASK) >=
		(client_version & RPC_VERSION_MINOR_MASK));
}
EXPORT_SYMBOL(msm_rpc_is_compatible_version);

static int msm_rpc_get_compatible_server(uint32_t prog,
					uint32_t ver,
					uint32_t *found_vers)
{
	struct rr_server *server;
	unsigned long     flags;
	if (found_vers == NULL)
		return 0;

	spin_lock_irqsave(&server_list_lock, flags);
	list_for_each_entry(server, &server_list, list) {
		if ((server->prog == prog) &&
		    msm_rpc_is_compatible_version(server->vers, ver)) {
			*found_vers = server->vers;
			spin_unlock_irqrestore(&server_list_lock, flags);
			return 0;
		}
	}
	spin_unlock_irqrestore(&server_list_lock, flags);
	return -1;
}
#endif

struct msm_rpc_endpoint *msm_rpc_connect(uint32_t prog, uint32_t vers, unsigned flags)
{
	struct msm_rpc_endpoint *ept;
	struct rr_server *server;

#if CONFIG_MSM_AMSS_VERSION >= 6350
	if (!(vers & RPC_VERSION_MODE_MASK)) {
		uint32_t found_vers;
		if (msm_rpc_get_compatible_server(prog, vers, &found_vers) < 0)
			return ERR_PTR(-EHOSTUNREACH);
		if (found_vers != vers) {
			D("RPC using new version %08x:{%08x --> %08x}\n",
			 	prog, vers, found_vers);
			vers = found_vers;
		}
	}
#endif

	server = rpcrouter_lookup_server(prog, vers);
	if (!server)
		return ERR_PTR(-EHOSTUNREACH);

	ept = msm_rpc_open();
	if (IS_ERR(ept))
		return ept;

	ept->flags = flags;
	ept->dst_pid = server->pid;
	ept->dst_cid = server->cid;
	ept->dst_prog = cpu_to_be32(prog);
	ept->dst_vers = cpu_to_be32(vers);

	return ept;
}
EXPORT_SYMBOL(msm_rpc_connect);

uint32_t msm_rpc_get_vers(struct msm_rpc_endpoint *ept)
{
	return be32_to_cpu(ept->dst_vers);
}
EXPORT_SYMBOL(msm_rpc_get_vers);

/* TODO: permission check? */
int msm_rpc_register_server(struct msm_rpc_endpoint *ept,
			    uint32_t prog, uint32_t vers)
{
	int rc;
	union rr_control_msg msg;
	struct rr_server *server;

	server = rpcrouter_create_server(ept->pid, ept->cid,
					 prog, vers);
	if (!server)
		return -ENODEV;

	msg.srv.cmd = RPCROUTER_CTRL_CMD_NEW_SERVER;
	msg.srv.pid = ept->pid;
	msg.srv.cid = ept->cid;
	msg.srv.prog = prog;
	msg.srv.vers = vers;

	RR("x NEW_SERVER id=%d:%08x prog=%08x:%08x\n",
	   ept->pid, ept->cid, prog, vers);

	rc = rpcrouter_send_control_msg(&msg);
	if (rc < 0)
		return rc;

	return 0;
}

/* TODO: permission check -- disallow unreg of somebody else's server */
int msm_rpc_unregister_server(struct msm_rpc_endpoint *ept,
			      uint32_t prog, uint32_t vers)
{
	struct rr_server *server;
	server = rpcrouter_lookup_server(prog, vers);

	if (!server)
		return -ENOENT;
	rpcrouter_destroy_server(server);
	return 0;
}

static int msm_rpcrouter_probe(struct platform_device *pdev)
{
	int rc;

	/* Initialize what we need to start processing */
	INIT_LIST_HEAD(&local_endpoints);
	INIT_LIST_HEAD(&remote_endpoints);

	init_waitqueue_head(&newserver_wait);
	init_waitqueue_head(&smd_wait);

	rpcrouter_workqueue = create_singlethread_workqueue("rpcrouter");
	if (!rpcrouter_workqueue)
		return -ENOMEM;

	rc = msm_rpcrouter_init_devices();
	if (rc < 0)
		goto fail_destroy_workqueue;

	/* Open up SMD channel 2 */
	initialized = 0;
	rc = smd_open("SMD_RPCCALL", &smd_channel, NULL, rpcrouter_smdnotify);
	if (rc < 0)
		goto fail_remove_devices;

	queue_work(rpcrouter_workqueue, &work_read_data);
	return 0;

 fail_remove_devices:
	msm_rpcrouter_exit_devices();
 fail_destroy_workqueue:
	destroy_workqueue(rpcrouter_workqueue);
	return rc;
}

static struct platform_driver msm_smd_channel2_driver = {
	.probe		= msm_rpcrouter_probe,
	.driver		= {
			.name	= "SMD_RPCCALL",
			.owner	= THIS_MODULE,
	},
};

static int __init rpcrouter_init(void)
{
	return platform_driver_register(&msm_smd_channel2_driver);
}

module_init(rpcrouter_init);
MODULE_DESCRIPTION("MSM RPC Router");
MODULE_AUTHOR("San Mehat <san@android.com>");
MODULE_LICENSE("GPL");
