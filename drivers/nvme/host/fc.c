// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 Avago Technologies.  All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/parser.h>
#include <uapi/scsi/fc/fc_fs.h>
#include <uapi/scsi/fc/fc_els.h>
#include <linux/delay.h>
#include <linux/overflow.h>
#include <linux/blk-cgroup.h>
#include "nvme.h"
#include "fabrics.h"
#include <linux/nvme-fc-driver.h>
#include <linux/nvme-fc.h>
#include "fc.h"
#include <scsi/scsi_transport_fc.h>
#include <linux/blk-mq-pci.h>

/* *************************** Data Structures/Defines ****************** */


enum nvme_fc_queue_flags {
	NVME_FC_Q_CONNECTED = 0,
	NVME_FC_Q_LIVE,
};

#define NVME_FC_DEFAULT_DEV_LOSS_TMO	60	/* seconds */
#define NVME_FC_DEFAULT_RECONNECT_TMO	2	/* delay between reconnects
						 * when connected and a
						 * connection failure.
						 */

struct nvme_fc_queue {
	struct nvme_fc_ctrl	*ctrl;
	struct device		*dev;
	struct blk_mq_hw_ctx	*hctx;
	void			*lldd_handle;
	size_t			cmnd_capsule_len;
	u32			qnum;
	u32			rqcnt;
	u32			seqno;

	u64			connection_id;
	atomic_t		csn;

	unsigned long		flags;
} __aligned(sizeof(u64));	/* alignment for other things alloc'd with */

enum nvme_fcop_flags {
	FCOP_FLAGS_TERMIO	= (1 << 0),
	FCOP_FLAGS_AEN		= (1 << 1),
};

struct nvmefc_ls_req_op {
	struct nvmefc_ls_req	ls_req;

	struct nvme_fc_rport	*rport;
	struct nvme_fc_queue	*queue;
	struct request		*rq;
	u32			flags;

	int			ls_error;
	struct completion	ls_done;
	struct list_head	lsreq_list;	/* rport->ls_req_list */
	bool			req_queued;
};

struct nvmefc_ls_rcv_op {
	struct nvme_fc_rport		*rport;
	struct nvmefc_ls_rsp		*lsrsp;
	union nvmefc_ls_requests	*rqstbuf;
	union nvmefc_ls_responses	*rspbuf;
	u16				rqstdatalen;
	bool				handled;
	dma_addr_t			rspdma;
	struct list_head		lsrcv_list;	/* rport->ls_rcv_list */
} __aligned(sizeof(u64));	/* alignment for other things alloc'd with */

enum nvme_fcpop_state {
	FCPOP_STATE_UNINIT	= 0,
	FCPOP_STATE_IDLE	= 1,
	FCPOP_STATE_ACTIVE	= 2,
	FCPOP_STATE_ABORTED	= 3,
	FCPOP_STATE_COMPLETE	= 4,
};

struct nvme_fc_fcp_op {
	struct nvme_request	nreq;		/*
						 * nvme/host/core.c
						 * requires this to be
						 * the 1st element in the
						 * private structure
						 * associated with the
						 * request.
						 */
	struct nvmefc_fcp_req	fcp_req;

	struct nvme_fc_ctrl	*ctrl;
	struct nvme_fc_queue	*queue;
	struct request		*rq;

	atomic_t		state;
	u32			flags;
	u32			rqno;
	u32			nents;

	struct nvme_fc_cmd_iu	cmd_iu;
	struct nvme_fc_ersp_iu	rsp_iu;
};

struct nvme_fcp_op_w_sgl {
	struct nvme_fc_fcp_op	op;
	struct scatterlist	sgl[NVME_INLINE_SG_CNT];
	uint8_t			priv[];
};

struct nvme_fc_lport {
	struct nvme_fc_local_port	localport;

	struct ida			endp_cnt;
	struct list_head		port_list;	/* nvme_fc_port_list */
	struct list_head		endp_list;
	struct device			*dev;	/* physical device for dma */
	struct nvme_fc_port_template	*ops;
	struct kref			ref;
	atomic_t                        act_rport_cnt;
} __aligned(sizeof(u64));	/* alignment for other things alloc'd with */

struct nvme_fc_rport {
	struct nvme_fc_remote_port	remoteport;

	struct list_head		endp_list; /* for lport->endp_list */
	struct list_head		ctrl_list;
	struct list_head		ls_req_list;
	struct list_head		ls_rcv_list;
	struct list_head		disc_list;
	struct device			*dev;	/* physical device for dma */
	struct nvme_fc_lport		*lport;
	spinlock_t			lock;
	struct kref			ref;
	atomic_t                        act_ctrl_cnt;
	unsigned long			dev_loss_end;
	struct work_struct		lsrcv_work;
} __aligned(sizeof(u64));	/* alignment for other things alloc'd with */

/* fc_ctrl flags values - specified as bit positions */
#define ASSOC_ACTIVE		0
#define ASSOC_FAILED		1
#define FCCTRL_TERMIO		2

struct nvme_fc_ctrl {
	spinlock_t		lock;
	struct nvme_fc_queue	*queues;
	struct device		*dev;
	struct nvme_fc_lport	*lport;
	struct nvme_fc_rport	*rport;
	u32			cnum;

	bool			ioq_live;
	u64			association_id;
	struct nvmefc_ls_rcv_op	*rcv_disconn;

	struct list_head	ctrl_list;	/* rport->ctrl_list */

	struct blk_mq_tag_set	admin_tag_set;
	struct blk_mq_tag_set	tag_set;

	struct work_struct	ioerr_work;
	struct delayed_work	connect_work;

	struct kref		ref;
	unsigned long		flags;
	u32			iocnt;
	wait_queue_head_t	ioabort_wait;

	struct nvme_fc_fcp_op	aen_ops[NVME_NR_AEN_COMMANDS];

	struct nvme_ctrl	ctrl;
};

static inline struct nvme_fc_ctrl *
to_fc_ctrl(struct nvme_ctrl *ctrl)
{
	return container_of(ctrl, struct nvme_fc_ctrl, ctrl);
}

static inline struct nvme_fc_lport *
localport_to_lport(struct nvme_fc_local_port *portptr)
{
	return container_of(portptr, struct nvme_fc_lport, localport);
}

static inline struct nvme_fc_rport *
remoteport_to_rport(struct nvme_fc_remote_port *portptr)
{
	return container_of(portptr, struct nvme_fc_rport, remoteport);
}

static inline struct nvmefc_ls_req_op *
ls_req_to_lsop(struct nvmefc_ls_req *lsreq)
{
	return container_of(lsreq, struct nvmefc_ls_req_op, ls_req);
}

static inline struct nvme_fc_fcp_op *
fcp_req_to_fcp_op(struct nvmefc_fcp_req *fcpreq)
{
	return container_of(fcpreq, struct nvme_fc_fcp_op, fcp_req);
}



/* *************************** Globals **************************** */


static DEFINE_SPINLOCK(nvme_fc_lock);

static LIST_HEAD(nvme_fc_lport_list);
static DEFINE_IDA(nvme_fc_local_port_cnt);
static DEFINE_IDA(nvme_fc_ctrl_cnt);

static struct workqueue_struct *nvme_fc_wq;

static bool nvme_fc_waiting_to_unload;
static DECLARE_COMPLETION(nvme_fc_unload_proceed);

/*
 * These items are short-term. They will eventually be moved into
 * a generic FC class. See comments in module init.
 */
static struct device *fc_udev_device;

static void nvme_fc_complete_rq(struct request *rq);

/* *********************** FC-NVME Port Management ************************ */

static void __nvme_fc_delete_hw_queue(struct nvme_fc_ctrl *,
			struct nvme_fc_queue *, unsigned int);

static void nvme_fc_handle_ls_rqst_work(struct work_struct *work);


static void
nvme_fc_free_lport(struct kref *ref)
{
	struct nvme_fc_lport *lport =
		container_of(ref, struct nvme_fc_lport, ref);
	unsigned long flags;

	WARN_ON(lport->localport.port_state != FC_OBJSTATE_DELETED);
	WARN_ON(!list_empty(&lport->endp_list));

	/* remove from transport list */
	spin_lock_irqsave(&nvme_fc_lock, flags);
	list_del(&lport->port_list);
	if (nvme_fc_waiting_to_unload && list_empty(&nvme_fc_lport_list))
		complete(&nvme_fc_unload_proceed);
	spin_unlock_irqrestore(&nvme_fc_lock, flags);

	ida_free(&nvme_fc_local_port_cnt, lport->localport.port_num);
	ida_destroy(&lport->endp_cnt);

	put_device(lport->dev);

	kfree(lport);
}

static void
nvme_fc_lport_put(struct nvme_fc_lport *lport)
{
	kref_put(&lport->ref, nvme_fc_free_lport);
}

static int
nvme_fc_lport_get(struct nvme_fc_lport *lport)
{
	return kref_get_unless_zero(&lport->ref);
}


static struct nvme_fc_lport *
nvme_fc_attach_to_unreg_lport(struct nvme_fc_port_info *pinfo,
			struct nvme_fc_port_template *ops,
			struct device *dev)
{
	struct nvme_fc_lport *lport;
	unsigned long flags;

	spin_lock_irqsave(&nvme_fc_lock, flags);

	list_for_each_entry(lport, &nvme_fc_lport_list, port_list) {
		if (lport->localport.node_name != pinfo->node_name ||
		    lport->localport.port_name != pinfo->port_name)
			continue;

		if (lport->dev != dev) {
			lport = ERR_PTR(-EXDEV);
			goto out_done;
		}

		if (lport->localport.port_state != FC_OBJSTATE_DELETED) {
			lport = ERR_PTR(-EEXIST);
			goto out_done;
		}

		if (!nvme_fc_lport_get(lport)) {
			/*
			 * fails if ref cnt already 0. If so,
			 * act as if lport already deleted
			 */
			lport = NULL;
			goto out_done;
		}

		/* resume the lport */

		lport->ops = ops;
		lport->localport.port_role = pinfo->port_role;
		lport->localport.port_id = pinfo->port_id;
		lport->localport.port_state = FC_OBJSTATE_ONLINE;

		spin_unlock_irqrestore(&nvme_fc_lock, flags);

		return lport;
	}

	lport = NULL;

out_done:
	spin_unlock_irqrestore(&nvme_fc_lock, flags);

	return lport;
}

/**
 * nvme_fc_register_localport - transport entry point called by an
 *                              LLDD to register the existence of a NVME
 *                              host FC port.
 * @pinfo:     pointer to information about the port to be registered
 * @template:  LLDD entrypoints and operational parameters for the port
 * @dev:       physical hardware device node port corresponds to. Will be
 *             used for DMA mappings
 * @portptr:   pointer to a local port pointer. Upon success, the routine
 *             will allocate a nvme_fc_local_port structure and place its
 *             address in the local port pointer. Upon failure, local port
 *             pointer will be set to 0.
 *
 * Returns:
 * a completion status. Must be 0 upon success; a negative errno
 * (ex: -ENXIO) upon failure.
 */
int
nvme_fc_register_localport(struct nvme_fc_port_info *pinfo,
			struct nvme_fc_port_template *template,
			struct device *dev,
			struct nvme_fc_local_port **portptr)
{
	struct nvme_fc_lport *newrec;
	unsigned long flags;
	int ret, idx;

	if (!template->localport_delete || !template->remoteport_delete ||
	    !template->ls_req || !template->fcp_io ||
	    !template->ls_abort || !template->fcp_abort ||
	    !template->max_hw_queues || !template->max_sgl_segments ||
	    !template->max_dif_sgl_segments || !template->dma_boundary) {
		ret = -EINVAL;
		goto out_reghost_failed;
	}

	/*
	 * look to see if there is already a localport that had been
	 * deregistered and in the process of waiting for all the
	 * references to fully be removed.  If the references haven't
	 * expired, we can simply re-enable the localport. Remoteports
	 * and controller reconnections should resume naturally.
	 */
	newrec = nvme_fc_attach_to_unreg_lport(pinfo, template, dev);

	/* found an lport, but something about its state is bad */
	if (IS_ERR(newrec)) {
		ret = PTR_ERR(newrec);
		goto out_reghost_failed;

	/* found existing lport, which was resumed */
	} else if (newrec) {
		*portptr = &newrec->localport;
		return 0;
	}

	/* nothing found - allocate a new localport struct */

	newrec = kmalloc((sizeof(*newrec) + template->local_priv_sz),
			 GFP_KERNEL);
	if (!newrec) {
		ret = -ENOMEM;
		goto out_reghost_failed;
	}

	idx = ida_alloc(&nvme_fc_local_port_cnt, GFP_KERNEL);
	if (idx < 0) {
		ret = -ENOSPC;
		goto out_fail_kfree;
	}

	if (!get_device(dev) && dev) {
		ret = -ENODEV;
		goto out_ida_put;
	}

	INIT_LIST_HEAD(&newrec->port_list);
	INIT_LIST_HEAD(&newrec->endp_list);
	kref_init(&newrec->ref);
	atomic_set(&newrec->act_rport_cnt, 0);
	newrec->ops = template;
	newrec->dev = dev;
	ida_init(&newrec->endp_cnt);
	if (template->local_priv_sz)
		newrec->localport.private = &newrec[1];
	else
		newrec->localport.private = NULL;
	newrec->localport.node_name = pinfo->node_name;
	newrec->localport.port_name = pinfo->port_name;
	newrec->localport.port_role = pinfo->port_role;
	newrec->localport.port_id = pinfo->port_id;
	newrec->localport.port_state = FC_OBJSTATE_ONLINE;
	newrec->localport.port_num = idx;

	spin_lock_irqsave(&nvme_fc_lock, flags);
	list_add_tail(&newrec->port_list, &nvme_fc_lport_list);
	spin_unlock_irqrestore(&nvme_fc_lock, flags);

	if (dev)
		dma_set_seg_boundary(dev, template->dma_boundary);

	*portptr = &newrec->localport;
	return 0;

out_ida_put:
	ida_free(&nvme_fc_local_port_cnt, idx);
out_fail_kfree:
	kfree(newrec);
out_reghost_failed:
	*portptr = NULL;

	return ret;
}
EXPORT_SYMBOL_GPL(nvme_fc_register_localport);

/**
 * nvme_fc_unregister_localport - transport entry point called by an
 *                              LLDD to deregister/remove a previously
 *                              registered a NVME host FC port.
 * @portptr: pointer to the (registered) local port that is to be deregistered.
 *
 * Returns:
 * a completion status. Must be 0 upon success; a negative errno
 * (ex: -ENXIO) upon failure.
 */
int
nvme_fc_unregister_localport(struct nvme_fc_local_port *portptr)
{
	struct nvme_fc_lport *lport = localport_to_lport(portptr);
	unsigned long flags;

	if (!portptr)
		return -EINVAL;

	spin_lock_irqsave(&nvme_fc_lock, flags);

	if (portptr->port_state != FC_OBJSTATE_ONLINE) {
		spin_unlock_irqrestore(&nvme_fc_lock, flags);
		return -EINVAL;
	}
	portptr->port_state = FC_OBJSTATE_DELETED;

	spin_unlock_irqrestore(&nvme_fc_lock, flags);

	if (atomic_read(&lport->act_rport_cnt) == 0)
		lport->ops->localport_delete(&lport->localport);

	nvme_fc_lport_put(lport);

	return 0;
}
EXPORT_SYMBOL_GPL(nvme_fc_unregister_localport);

/*
 * TRADDR strings, per FC-NVME are fixed format:
 *   "nn-0x<16hexdigits>:pn-0x<16hexdigits>" - 43 characters
 * udev event will only differ by prefix of what field is
 * being specified:
 *    "NVMEFC_HOST_TRADDR=" or "NVMEFC_TRADDR=" - 19 max characters
 *  19 + 43 + null_fudge = 64 characters
 */
#define FCNVME_TRADDR_LENGTH		64

static void
nvme_fc_signal_discovery_scan(struct nvme_fc_lport *lport,
		struct nvme_fc_rport *rport)
{
	char hostaddr[FCNVME_TRADDR_LENGTH];	/* NVMEFC_HOST_TRADDR=...*/
	char tgtaddr[FCNVME_TRADDR_LENGTH];	/* NVMEFC_TRADDR=...*/
	char *envp[4] = { "FC_EVENT=nvmediscovery", hostaddr, tgtaddr, NULL };

	if (!(rport->remoteport.port_role & FC_PORT_ROLE_NVME_DISCOVERY))
		return;

	snprintf(hostaddr, sizeof(hostaddr),
		"NVMEFC_HOST_TRADDR=nn-0x%016llx:pn-0x%016llx",
		lport->localport.node_name, lport->localport.port_name);
	snprintf(tgtaddr, sizeof(tgtaddr),
		"NVMEFC_TRADDR=nn-0x%016llx:pn-0x%016llx",
		rport->remoteport.node_name, rport->remoteport.port_name);
	kobject_uevent_env(&fc_udev_device->kobj, KOBJ_CHANGE, envp);
}

static void
nvme_fc_free_rport(struct kref *ref)
{
	struct nvme_fc_rport *rport =
		container_of(ref, struct nvme_fc_rport, ref);
	struct nvme_fc_lport *lport =
			localport_to_lport(rport->remoteport.localport);
	unsigned long flags;

	WARN_ON(rport->remoteport.port_state != FC_OBJSTATE_DELETED);
	WARN_ON(!list_empty(&rport->ctrl_list));

	/* remove from lport list */
	spin_lock_irqsave(&nvme_fc_lock, flags);
	list_del(&rport->endp_list);
	spin_unlock_irqrestore(&nvme_fc_lock, flags);

	WARN_ON(!list_empty(&rport->disc_list));
	ida_free(&lport->endp_cnt, rport->remoteport.port_num);

	kfree(rport);

	nvme_fc_lport_put(lport);
}

static void
nvme_fc_rport_put(struct nvme_fc_rport *rport)
{
	kref_put(&rport->ref, nvme_fc_free_rport);
}

static int
nvme_fc_rport_get(struct nvme_fc_rport *rport)
{
	return kref_get_unless_zero(&rport->ref);
}

static void
nvme_fc_resume_controller(struct nvme_fc_ctrl *ctrl)
{
	switch (nvme_ctrl_state(&ctrl->ctrl)) {
	case NVME_CTRL_NEW:
	case NVME_CTRL_CONNECTING:
		/*
		 * As all reconnects were suppressed, schedule a
		 * connect.
		 */
		dev_info(ctrl->ctrl.device,
			"NVME-FC{%d}: connectivity re-established. "
			"Attempting reconnect\n", ctrl->cnum);

		queue_delayed_work(nvme_wq, &ctrl->connect_work, 0);
		break;

	case NVME_CTRL_RESETTING:
		/*
		 * Controller is already in the process of terminating the
		 * association. No need to do anything further. The reconnect
		 * step will naturally occur after the reset completes.
		 */
		break;

	default:
		/* no action to take - let it delete */
		break;
	}
}

static struct nvme_fc_rport *
nvme_fc_attach_to_suspended_rport(struct nvme_fc_lport *lport,
				struct nvme_fc_port_info *pinfo)
{
	struct nvme_fc_rport *rport;
	struct nvme_fc_ctrl *ctrl;
	unsigned long flags;

	spin_lock_irqsave(&nvme_fc_lock, flags);

	list_for_each_entry(rport, &lport->endp_list, endp_list) {
		if (rport->remoteport.node_name != pinfo->node_name ||
		    rport->remoteport.port_name != pinfo->port_name)
			continue;

		if (!nvme_fc_rport_get(rport)) {
			rport = ERR_PTR(-ENOLCK);
			goto out_done;
		}

		spin_unlock_irqrestore(&nvme_fc_lock, flags);

		spin_lock_irqsave(&rport->lock, flags);

		/* has it been unregistered */
		if (rport->remoteport.port_state != FC_OBJSTATE_DELETED) {
			/* means lldd called us twice */
			spin_unlock_irqrestore(&rport->lock, flags);
			nvme_fc_rport_put(rport);
			return ERR_PTR(-ESTALE);
		}

		rport->remoteport.port_role = pinfo->port_role;
		rport->remoteport.port_id = pinfo->port_id;
		rport->remoteport.port_state = FC_OBJSTATE_ONLINE;
		rport->dev_loss_end = 0;

		/*
		 * kick off a reconnect attempt on all associations to the
		 * remote port. A successful reconnects will resume i/o.
		 */
		list_for_each_entry(ctrl, &rport->ctrl_list, ctrl_list)
			nvme_fc_resume_controller(ctrl);

		spin_unlock_irqrestore(&rport->lock, flags);

		return rport;
	}

	rport = NULL;

out_done:
	spin_unlock_irqrestore(&nvme_fc_lock, flags);

	return rport;
}

static inline void
__nvme_fc_set_dev_loss_tmo(struct nvme_fc_rport *rport,
			struct nvme_fc_port_info *pinfo)
{
	if (pinfo->dev_loss_tmo)
		rport->remoteport.dev_loss_tmo = pinfo->dev_loss_tmo;
	else
		rport->remoteport.dev_loss_tmo = NVME_FC_DEFAULT_DEV_LOSS_TMO;
}

/**
 * nvme_fc_register_remoteport - transport entry point called by an
 *                              LLDD to register the existence of a NVME
 *                              subsystem FC port on its fabric.
 * @localport: pointer to the (registered) local port that the remote
 *             subsystem port is connected to.
 * @pinfo:     pointer to information about the port to be registered
 * @portptr:   pointer to a remote port pointer. Upon success, the routine
 *             will allocate a nvme_fc_remote_port structure and place its
 *             address in the remote port pointer. Upon failure, remote port
 *             pointer will be set to 0.
 *
 * Returns:
 * a completion status. Must be 0 upon success; a negative errno
 * (ex: -ENXIO) upon failure.
 */
int
nvme_fc_register_remoteport(struct nvme_fc_local_port *localport,
				struct nvme_fc_port_info *pinfo,
				struct nvme_fc_remote_port **portptr)
{
	struct nvme_fc_lport *lport = localport_to_lport(localport);
	struct nvme_fc_rport *newrec;
	unsigned long flags;
	int ret, idx;

	if (!nvme_fc_lport_get(lport)) {
		ret = -ESHUTDOWN;
		goto out_reghost_failed;
	}

	/*
	 * look to see if there is already a remoteport that is waiting
	 * for a reconnect (within dev_loss_tmo) with the same WWN's.
	 * If so, transition to it and reconnect.
	 */
	newrec = nvme_fc_attach_to_suspended_rport(lport, pinfo);

	/* found an rport, but something about its state is bad */
	if (IS_ERR(newrec)) {
		ret = PTR_ERR(newrec);
		goto out_lport_put;

	/* found existing rport, which was resumed */
	} else if (newrec) {
		nvme_fc_lport_put(lport);
		__nvme_fc_set_dev_loss_tmo(newrec, pinfo);
		nvme_fc_signal_discovery_scan(lport, newrec);
		*portptr = &newrec->remoteport;
		return 0;
	}

	/* nothing found - allocate a new remoteport struct */

	newrec = kmalloc((sizeof(*newrec) + lport->ops->remote_priv_sz),
			 GFP_KERNEL);
	if (!newrec) {
		ret = -ENOMEM;
		goto out_lport_put;
	}

	idx = ida_alloc(&lport->endp_cnt, GFP_KERNEL);
	if (idx < 0) {
		ret = -ENOSPC;
		goto out_kfree_rport;
	}

	INIT_LIST_HEAD(&newrec->endp_list);
	INIT_LIST_HEAD(&newrec->ctrl_list);
	INIT_LIST_HEAD(&newrec->ls_req_list);
	INIT_LIST_HEAD(&newrec->disc_list);
	kref_init(&newrec->ref);
	atomic_set(&newrec->act_ctrl_cnt, 0);
	spin_lock_init(&newrec->lock);
	newrec->remoteport.localport = &lport->localport;
	INIT_LIST_HEAD(&newrec->ls_rcv_list);
	newrec->dev = lport->dev;
	newrec->lport = lport;
	if (lport->ops->remote_priv_sz)
		newrec->remoteport.private = &newrec[1];
	else
		newrec->remoteport.private = NULL;
	newrec->remoteport.port_role = pinfo->port_role;
	newrec->remoteport.node_name = pinfo->node_name;
	newrec->remoteport.port_name = pinfo->port_name;
	newrec->remoteport.port_id = pinfo->port_id;
	newrec->remoteport.port_state = FC_OBJSTATE_ONLINE;
	newrec->remoteport.port_num = idx;
	__nvme_fc_set_dev_loss_tmo(newrec, pinfo);
	INIT_WORK(&newrec->lsrcv_work, nvme_fc_handle_ls_rqst_work);

	spin_lock_irqsave(&nvme_fc_lock, flags);
	list_add_tail(&newrec->endp_list, &lport->endp_list);
	spin_unlock_irqrestore(&nvme_fc_lock, flags);

	nvme_fc_signal_discovery_scan(lport, newrec);

	*portptr = &newrec->remoteport;
	return 0;

out_kfree_rport:
	kfree(newrec);
out_lport_put:
	nvme_fc_lport_put(lport);
out_reghost_failed:
	*portptr = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_fc_register_remoteport);

static int
nvme_fc_abort_lsops(struct nvme_fc_rport *rport)
{
	struct nvmefc_ls_req_op *lsop;
	unsigned long flags;

restart:
	spin_lock_irqsave(&rport->lock, flags);

	list_for_each_entry(lsop, &rport->ls_req_list, lsreq_list) {
		if (!(lsop->flags & FCOP_FLAGS_TERMIO)) {
			lsop->flags |= FCOP_FLAGS_TERMIO;
			spin_unlock_irqrestore(&rport->lock, flags);
			rport->lport->ops->ls_abort(&rport->lport->localport,
						&rport->remoteport,
						&lsop->ls_req);
			goto restart;
		}
	}
	spin_unlock_irqrestore(&rport->lock, flags);

	return 0;
}

static void
nvme_fc_ctrl_connectivity_loss(struct nvme_fc_ctrl *ctrl)
{
	dev_info(ctrl->ctrl.device,
		"NVME-FC{%d}: controller connectivity lost. Awaiting "
		"Reconnect", ctrl->cnum);

	switch (nvme_ctrl_state(&ctrl->ctrl)) {
	case NVME_CTRL_NEW:
	case NVME_CTRL_LIVE:
		/*
		 * Schedule a controller reset. The reset will terminate the
		 * association and schedule the reconnect timer.  Reconnects
		 * will be attempted until either the ctlr_loss_tmo
		 * (max_retries * connect_delay) expires or the remoteport's
		 * dev_loss_tmo expires.
		 */
		if (nvme_reset_ctrl(&ctrl->ctrl)) {
			dev_warn(ctrl->ctrl.device,
				"NVME-FC{%d}: Couldn't schedule reset.\n",
				ctrl->cnum);
			nvme_delete_ctrl(&ctrl->ctrl);
		}
		break;

	case NVME_CTRL_CONNECTING:
		/*
		 * The association has already been terminated and the
		 * controller is attempting reconnects.  No need to do anything
		 * futher.  Reconnects will be attempted until either the
		 * ctlr_loss_tmo (max_retries * connect_delay) expires or the
		 * remoteport's dev_loss_tmo expires.
		 */
		break;

	case NVME_CTRL_RESETTING:
		/*
		 * Controller is already in the process of terminating the
		 * association.  No need to do anything further. The reconnect
		 * step will kick in naturally after the association is
		 * terminated.
		 */
		break;

	case NVME_CTRL_DELETING:
	case NVME_CTRL_DELETING_NOIO:
	default:
		/* no action to take - let it delete */
		break;
	}
}

/**
 * nvme_fc_unregister_remoteport - transport entry point called by an
 *                              LLDD to deregister/remove a previously
 *                              registered a NVME subsystem FC port.
 * @portptr: pointer to the (registered) remote port that is to be
 *           deregistered.
 *
 * Returns:
 * a completion status. Must be 0 upon success; a negative errno
 * (ex: -ENXIO) upon failure.
 */
int
nvme_fc_unregister_remoteport(struct nvme_fc_remote_port *portptr)
{
	struct nvme_fc_rport *rport = remoteport_to_rport(portptr);
	struct nvme_fc_ctrl *ctrl;
	unsigned long flags;

	if (!portptr)
		return -EINVAL;

	spin_lock_irqsave(&rport->lock, flags);

	if (portptr->port_state != FC_OBJSTATE_ONLINE) {
		spin_unlock_irqrestore(&rport->lock, flags);
		return -EINVAL;
	}
	portptr->port_state = FC_OBJSTATE_DELETED;

	rport->dev_loss_end = jiffies + (portptr->dev_loss_tmo * HZ);

	list_for_each_entry(ctrl, &rport->ctrl_list, ctrl_list) {
		/* if dev_loss_tmo==0, dev loss is immediate */
		if (!portptr->dev_loss_tmo) {
			dev_warn(ctrl->ctrl.device,
				"NVME-FC{%d}: controller connectivity lost.\n",
				ctrl->cnum);
			nvme_delete_ctrl(&ctrl->ctrl);
		} else
			nvme_fc_ctrl_connectivity_loss(ctrl);
	}

	spin_unlock_irqrestore(&rport->lock, flags);

	nvme_fc_abort_lsops(rport);

	if (atomic_read(&rport->act_ctrl_cnt) == 0)
		rport->lport->ops->remoteport_delete(portptr);

	/*
	 * release the reference, which will allow, if all controllers
	 * go away, which should only occur after dev_loss_tmo occurs,
	 * for the rport to be torn down.
	 */
	nvme_fc_rport_put(rport);

	return 0;
}
EXPORT_SYMBOL_GPL(nvme_fc_unregister_remoteport);

/**
 * nvme_fc_rescan_remoteport - transport entry point called by an
 *                              LLDD to request a nvme device rescan.
 * @remoteport: pointer to the (registered) remote port that is to be
 *              rescanned.
 *
 * Returns: N/A
 */
void
nvme_fc_rescan_remoteport(struct nvme_fc_remote_port *remoteport)
{
	struct nvme_fc_rport *rport = remoteport_to_rport(remoteport);

	nvme_fc_signal_discovery_scan(rport->lport, rport);
}
EXPORT_SYMBOL_GPL(nvme_fc_rescan_remoteport);

int
nvme_fc_set_remoteport_devloss(struct nvme_fc_remote_port *portptr,
			u32 dev_loss_tmo)
{
	struct nvme_fc_rport *rport = remoteport_to_rport(portptr);
	unsigned long flags;

	spin_lock_irqsave(&rport->lock, flags);

	if (portptr->port_state != FC_OBJSTATE_ONLINE) {
		spin_unlock_irqrestore(&rport->lock, flags);
		return -EINVAL;
	}

	/* a dev_loss_tmo of 0 (immediate) is allowed to be set */
	rport->remoteport.dev_loss_tmo = dev_loss_tmo;

	spin_unlock_irqrestore(&rport->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(nvme_fc_set_remoteport_devloss);


/* *********************** FC-NVME DMA Handling **************************** */

/*
 * The fcloop device passes in a NULL device pointer. Real LLD's will
 * pass in a valid device pointer. If NULL is passed to the dma mapping
 * routines, depending on the platform, it may or may not succeed, and
 * may crash.
 *
 * As such:
 * Wrapper all the dma routines and check the dev pointer.
 *
 * If simple mappings (return just a dma address, we'll noop them,
 * returning a dma address of 0.
 *
 * On more complex mappings (dma_map_sg), a pseudo routine fills
 * in the scatter list, setting all dma addresses to 0.
 */

static inline dma_addr_t
fc_dma_map_single(struct device *dev, void *ptr, size_t size,
		enum dma_data_direction dir)
{
	return dev ? dma_map_single(dev, ptr, size, dir) : (dma_addr_t)0L;
}

static inline int
fc_dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return dev ? dma_mapping_error(dev, dma_addr) : 0;
}

static inline void
fc_dma_unmap_single(struct device *dev, dma_addr_t addr, size_t size,
	enum dma_data_direction dir)
{
	if (dev)
		dma_unmap_single(dev, addr, size, dir);
}

static inline void
fc_dma_sync_single_for_cpu(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir)
{
	if (dev)
		dma_sync_single_for_cpu(dev, addr, size, dir);
}

static inline void
fc_dma_sync_single_for_device(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir)
{
	if (dev)
		dma_sync_single_for_device(dev, addr, size, dir);
}

/* pseudo dma_map_sg call */
static int
fc_map_sg(struct scatterlist *sg, int nents)
{
	struct scatterlist *s;
	int i;

	WARN_ON(nents == 0 || sg[0].length == 0);

	for_each_sg(sg, s, nents, i) {
		s->dma_address = 0L;
#ifdef CONFIG_NEED_SG_DMA_LENGTH
		s->dma_length = s->length;
#endif
	}
	return nents;
}

static inline int
fc_dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction dir)
{
	return dev ? dma_map_sg(dev, sg, nents, dir) : fc_map_sg(sg, nents);
}

static inline void
fc_dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction dir)
{
	if (dev)
		dma_unmap_sg(dev, sg, nents, dir);
}

/* *********************** FC-NVME LS Handling **************************** */

static void nvme_fc_ctrl_put(struct nvme_fc_ctrl *);
static int nvme_fc_ctrl_get(struct nvme_fc_ctrl *);

static void nvme_fc_error_recovery(struct nvme_fc_ctrl *ctrl, char *errmsg);

static void
__nvme_fc_finish_ls_req(struct nvmefc_ls_req_op *lsop)
{
	struct nvme_fc_rport *rport = lsop->rport;
	struct nvmefc_ls_req *lsreq = &lsop->ls_req;
	unsigned long flags;

	spin_lock_irqsave(&rport->lock, flags);

	if (!lsop->req_queued) {
		spin_unlock_irqrestore(&rport->lock, flags);
		return;
	}

	list_del(&lsop->lsreq_list);

	lsop->req_queued = false;

	spin_unlock_irqrestore(&rport->lock, flags);

	fc_dma_unmap_single(rport->dev, lsreq->rqstdma,
				  (lsreq->rqstlen + lsreq->rsplen),
				  DMA_BIDIRECTIONAL);

	nvme_fc_rport_put(rport);
}

static int
__nvme_fc_send_ls_req(struct nvme_fc_rport *rport,
		struct nvmefc_ls_req_op *lsop,
		void (*done)(struct nvmefc_ls_req *req, int status))
{
	struct nvmefc_ls_req *lsreq = &lsop->ls_req;
	unsigned long flags;
	int ret = 0;

	if (rport->remoteport.port_state != FC_OBJSTATE_ONLINE)
		return -ECONNREFUSED;

	if (!nvme_fc_rport_get(rport))
		return -ESHUTDOWN;

	lsreq->done = done;
	lsop->rport = rport;
	lsop->req_queued = false;
	INIT_LIST_HEAD(&lsop->lsreq_list);
	init_completion(&lsop->ls_done);

	lsreq->rqstdma = fc_dma_map_single(rport->dev, lsreq->rqstaddr,
				  lsreq->rqstlen + lsreq->rsplen,
				  DMA_BIDIRECTIONAL);
	if (fc_dma_mapping_error(rport->dev, lsreq->rqstdma)) {
		ret = -EFAULT;
		goto out_putrport;
	}
	lsreq->rspdma = lsreq->rqstdma + lsreq->rqstlen;

	spin_lock_irqsave(&rport->lock, flags);

	list_add_tail(&lsop->lsreq_list, &rport->ls_req_list);

	lsop->req_queued = true;

	spin_unlock_irqrestore(&rport->lock, flags);

	ret = rport->lport->ops->ls_req(&rport->lport->localport,
					&rport->remoteport, lsreq);
	if (ret)
		goto out_unlink;

	return 0;

out_unlink:
	lsop->ls_error = ret;
	spin_lock_irqsave(&rport->lock, flags);
	lsop->req_queued = false;
	list_del(&lsop->lsreq_list);
	spin_unlock_irqrestore(&rport->lock, flags);
	fc_dma_unmap_single(rport->dev, lsreq->rqstdma,
				  (lsreq->rqstlen + lsreq->rsplen),
				  DMA_BIDIRECTIONAL);
out_putrport:
	nvme_fc_rport_put(rport);

	return ret;
}

static void
nvme_fc_send_ls_req_done(struct nvmefc_ls_req *lsreq, int status)
{
	struct nvmefc_ls_req_op *lsop = ls_req_to_lsop(lsreq);

	lsop->ls_error = status;
	complete(&lsop->ls_done);
}

static int
nvme_fc_send_ls_req(struct nvme_fc_rport *rport, struct nvmefc_ls_req_op *lsop)
{
	struct nvmefc_ls_req *lsreq = &lsop->ls_req;
	struct fcnvme_ls_rjt *rjt = lsreq->rspaddr;
	int ret;

	ret = __nvme_fc_send_ls_req(rport, lsop, nvme_fc_send_ls_req_done);

	if (!ret) {
		/*
		 * No timeout/not interruptible as we need the struct
		 * to exist until the lldd calls us back. Thus mandate
		 * wait until driver calls back. lldd responsible for
		 * the timeout action
		 */
		wait_for_completion(&lsop->ls_done);

		__nvme_fc_finish_ls_req(lsop);

		ret = lsop->ls_error;
	}

	if (ret)
		return ret;

	/* ACC or RJT payload ? */
	if (rjt->w0.ls_cmd == FCNVME_LS_RJT)
		return -ENXIO;

	return 0;
}

static int
nvme_fc_send_ls_req_async(struct nvme_fc_rport *rport,
		struct nvmefc_ls_req_op *lsop,
		void (*done)(struct nvmefc_ls_req *req, int status))
{
	/* don't wait for completion */

	return __nvme_fc_send_ls_req(rport, lsop, done);
}

static int
nvme_fc_connect_admin_queue(struct nvme_fc_ctrl *ctrl,
	struct nvme_fc_queue *queue, u16 qsize, u16 ersp_ratio)
{
	struct nvmefc_ls_req_op *lsop;
	struct nvmefc_ls_req *lsreq;
	struct fcnvme_ls_cr_assoc_rqst *assoc_rqst;
	struct fcnvme_ls_cr_assoc_acc *assoc_acc;
	unsigned long flags;
	int ret, fcret = 0;

	lsop = kzalloc((sizeof(*lsop) +
			 sizeof(*assoc_rqst) + sizeof(*assoc_acc) +
			 ctrl->lport->ops->lsrqst_priv_sz), GFP_KERNEL);
	if (!lsop) {
		dev_info(ctrl->ctrl.device,
			"NVME-FC{%d}: send Create Association failed: ENOMEM\n",
			ctrl->cnum);
		ret = -ENOMEM;
		goto out_no_memory;
	}

	assoc_rqst = (struct fcnvme_ls_cr_assoc_rqst *)&lsop[1];
	assoc_acc = (struct fcnvme_ls_cr_assoc_acc *)&assoc_rqst[1];
	lsreq = &lsop->ls_req;
	if (ctrl->lport->ops->lsrqst_priv_sz)
		lsreq->private = &assoc_acc[1];
	else
		lsreq->private = NULL;

	assoc_rqst->w0.ls_cmd = FCNVME_LS_CREATE_ASSOCIATION;
	assoc_rqst->desc_list_len =
			cpu_to_be32(sizeof(struct fcnvme_lsdesc_cr_assoc_cmd));

	assoc_rqst->assoc_cmd.desc_tag =
			cpu_to_be32(FCNVME_LSDESC_CREATE_ASSOC_CMD);
	assoc_rqst->assoc_cmd.desc_len =
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_lsdesc_cr_assoc_cmd));

	assoc_rqst->assoc_cmd.ersp_ratio = cpu_to_be16(ersp_ratio);
	assoc_rqst->assoc_cmd.sqsize = cpu_to_be16(qsize - 1);
	/* Linux supports only Dynamic controllers */
	assoc_rqst->assoc_cmd.cntlid = cpu_to_be16(0xffff);
	uuid_copy(&assoc_rqst->assoc_cmd.hostid, &ctrl->ctrl.opts->host->id);
	strncpy(assoc_rqst->assoc_cmd.hostnqn, ctrl->ctrl.opts->host->nqn,
		min(FCNVME_ASSOC_HOSTNQN_LEN, NVMF_NQN_SIZE));
	strncpy(assoc_rqst->assoc_cmd.subnqn, ctrl->ctrl.opts->subsysnqn,
		min(FCNVME_ASSOC_SUBNQN_LEN, NVMF_NQN_SIZE));

	lsop->queue = queue;
	lsreq->rqstaddr = assoc_rqst;
	lsreq->rqstlen = sizeof(*assoc_rqst);
	lsreq->rspaddr = assoc_acc;
	lsreq->rsplen = sizeof(*assoc_acc);
	lsreq->timeout = NVME_FC_LS_TIMEOUT_SEC;

	ret = nvme_fc_send_ls_req(ctrl->rport, lsop);
	if (ret)
		goto out_free_buffer;

	/* process connect LS completion */

	/* validate the ACC response */
	if (assoc_acc->hdr.w0.ls_cmd != FCNVME_LS_ACC)
		fcret = VERR_LSACC;
	else if (assoc_acc->hdr.desc_list_len !=
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_ls_cr_assoc_acc)))
		fcret = VERR_CR_ASSOC_ACC_LEN;
	else if (assoc_acc->hdr.rqst.desc_tag !=
			cpu_to_be32(FCNVME_LSDESC_RQST))
		fcret = VERR_LSDESC_RQST;
	else if (assoc_acc->hdr.rqst.desc_len !=
			fcnvme_lsdesc_len(sizeof(struct fcnvme_lsdesc_rqst)))
		fcret = VERR_LSDESC_RQST_LEN;
	else if (assoc_acc->hdr.rqst.w0.ls_cmd != FCNVME_LS_CREATE_ASSOCIATION)
		fcret = VERR_CR_ASSOC;
	else if (assoc_acc->associd.desc_tag !=
			cpu_to_be32(FCNVME_LSDESC_ASSOC_ID))
		fcret = VERR_ASSOC_ID;
	else if (assoc_acc->associd.desc_len !=
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_lsdesc_assoc_id)))
		fcret = VERR_ASSOC_ID_LEN;
	else if (assoc_acc->connectid.desc_tag !=
			cpu_to_be32(FCNVME_LSDESC_CONN_ID))
		fcret = VERR_CONN_ID;
	else if (assoc_acc->connectid.desc_len !=
			fcnvme_lsdesc_len(sizeof(struct fcnvme_lsdesc_conn_id)))
		fcret = VERR_CONN_ID_LEN;

	if (fcret) {
		ret = -EBADF;
		dev_err(ctrl->dev,
			"q %d Create Association LS failed: %s\n",
			queue->qnum, validation_errors[fcret]);
	} else {
		spin_lock_irqsave(&ctrl->lock, flags);
		ctrl->association_id =
			be64_to_cpu(assoc_acc->associd.association_id);
		queue->connection_id =
			be64_to_cpu(assoc_acc->connectid.connection_id);
		set_bit(NVME_FC_Q_CONNECTED, &queue->flags);
		spin_unlock_irqrestore(&ctrl->lock, flags);
	}

out_free_buffer:
	kfree(lsop);
out_no_memory:
	if (ret)
		dev_err(ctrl->dev,
			"queue %d connect admin queue failed (%d).\n",
			queue->qnum, ret);
	return ret;
}

static int
nvme_fc_connect_queue(struct nvme_fc_ctrl *ctrl, struct nvme_fc_queue *queue,
			u16 qsize, u16 ersp_ratio)
{
	struct nvmefc_ls_req_op *lsop;
	struct nvmefc_ls_req *lsreq;
	struct fcnvme_ls_cr_conn_rqst *conn_rqst;
	struct fcnvme_ls_cr_conn_acc *conn_acc;
	int ret, fcret = 0;

	lsop = kzalloc((sizeof(*lsop) +
			 sizeof(*conn_rqst) + sizeof(*conn_acc) +
			 ctrl->lport->ops->lsrqst_priv_sz), GFP_KERNEL);
	if (!lsop) {
		dev_info(ctrl->ctrl.device,
			"NVME-FC{%d}: send Create Connection failed: ENOMEM\n",
			ctrl->cnum);
		ret = -ENOMEM;
		goto out_no_memory;
	}

	conn_rqst = (struct fcnvme_ls_cr_conn_rqst *)&lsop[1];
	conn_acc = (struct fcnvme_ls_cr_conn_acc *)&conn_rqst[1];
	lsreq = &lsop->ls_req;
	if (ctrl->lport->ops->lsrqst_priv_sz)
		lsreq->private = (void *)&conn_acc[1];
	else
		lsreq->private = NULL;

	conn_rqst->w0.ls_cmd = FCNVME_LS_CREATE_CONNECTION;
	conn_rqst->desc_list_len = cpu_to_be32(
				sizeof(struct fcnvme_lsdesc_assoc_id) +
				sizeof(struct fcnvme_lsdesc_cr_conn_cmd));

	conn_rqst->associd.desc_tag = cpu_to_be32(FCNVME_LSDESC_ASSOC_ID);
	conn_rqst->associd.desc_len =
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_lsdesc_assoc_id));
	conn_rqst->associd.association_id = cpu_to_be64(ctrl->association_id);
	conn_rqst->connect_cmd.desc_tag =
			cpu_to_be32(FCNVME_LSDESC_CREATE_CONN_CMD);
	conn_rqst->connect_cmd.desc_len =
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_lsdesc_cr_conn_cmd));
	conn_rqst->connect_cmd.ersp_ratio = cpu_to_be16(ersp_ratio);
	conn_rqst->connect_cmd.qid  = cpu_to_be16(queue->qnum);
	conn_rqst->connect_cmd.sqsize = cpu_to_be16(qsize - 1);

	lsop->queue = queue;
	lsreq->rqstaddr = conn_rqst;
	lsreq->rqstlen = sizeof(*conn_rqst);
	lsreq->rspaddr = conn_acc;
	lsreq->rsplen = sizeof(*conn_acc);
	lsreq->timeout = NVME_FC_LS_TIMEOUT_SEC;

	ret = nvme_fc_send_ls_req(ctrl->rport, lsop);
	if (ret)
		goto out_free_buffer;

	/* process connect LS completion */

	/* validate the ACC response */
	if (conn_acc->hdr.w0.ls_cmd != FCNVME_LS_ACC)
		fcret = VERR_LSACC;
	else if (conn_acc->hdr.desc_list_len !=
			fcnvme_lsdesc_len(sizeof(struct fcnvme_ls_cr_conn_acc)))
		fcret = VERR_CR_CONN_ACC_LEN;
	else if (conn_acc->hdr.rqst.desc_tag != cpu_to_be32(FCNVME_LSDESC_RQST))
		fcret = VERR_LSDESC_RQST;
	else if (conn_acc->hdr.rqst.desc_len !=
			fcnvme_lsdesc_len(sizeof(struct fcnvme_lsdesc_rqst)))
		fcret = VERR_LSDESC_RQST_LEN;
	else if (conn_acc->hdr.rqst.w0.ls_cmd != FCNVME_LS_CREATE_CONNECTION)
		fcret = VERR_CR_CONN;
	else if (conn_acc->connectid.desc_tag !=
			cpu_to_be32(FCNVME_LSDESC_CONN_ID))
		fcret = VERR_CONN_ID;
	else if (conn_acc->connectid.desc_len !=
			fcnvme_lsdesc_len(sizeof(struct fcnvme_lsdesc_conn_id)))
		fcret = VERR_CONN_ID_LEN;

	if (fcret) {
		ret = -EBADF;
		dev_err(ctrl->dev,
			"q %d Create I/O Connection LS failed: %s\n",
			queue->qnum, validation_errors[fcret]);
	} else {
		queue->connection_id =
			be64_to_cpu(conn_acc->connectid.connection_id);
		set_bit(NVME_FC_Q_CONNECTED, &queue->flags);
	}

out_free_buffer:
	kfree(lsop);
out_no_memory:
	if (ret)
		dev_err(ctrl->dev,
			"queue %d connect I/O queue failed (%d).\n",
			queue->qnum, ret);
	return ret;
}

static void
nvme_fc_disconnect_assoc_done(struct nvmefc_ls_req *lsreq, int status)
{
	struct nvmefc_ls_req_op *lsop = ls_req_to_lsop(lsreq);

	__nvme_fc_finish_ls_req(lsop);

	/* fc-nvme initiator doesn't care about success or failure of cmd */

	kfree(lsop);
}

/*
 * This routine sends a FC-NVME LS to disconnect (aka terminate)
 * the FC-NVME Association.  Terminating the association also
 * terminates the FC-NVME connections (per queue, both admin and io
 * queues) that are part of the association. E.g. things are torn
 * down, and the related FC-NVME Association ID and Connection IDs
 * become invalid.
 *
 * The behavior of the fc-nvme initiator is such that it's
 * understanding of the association and connections will implicitly
 * be torn down. The action is implicit as it may be due to a loss of
 * connectivity with the fc-nvme target, so you may never get a
 * response even if you tried.  As such, the action of this routine
 * is to asynchronously send the LS, ignore any results of the LS, and
 * continue on with terminating the association. If the fc-nvme target
 * is present and receives the LS, it too can tear down.
 */
static void
nvme_fc_xmt_disconnect_assoc(struct nvme_fc_ctrl *ctrl)
{
	struct fcnvme_ls_disconnect_assoc_rqst *discon_rqst;
	struct fcnvme_ls_disconnect_assoc_acc *discon_acc;
	struct nvmefc_ls_req_op *lsop;
	struct nvmefc_ls_req *lsreq;
	int ret;

	lsop = kzalloc((sizeof(*lsop) +
			sizeof(*discon_rqst) + sizeof(*discon_acc) +
			ctrl->lport->ops->lsrqst_priv_sz), GFP_KERNEL);
	if (!lsop) {
		dev_info(ctrl->ctrl.device,
			"NVME-FC{%d}: send Disconnect Association "
			"failed: ENOMEM\n",
			ctrl->cnum);
		return;
	}

	discon_rqst = (struct fcnvme_ls_disconnect_assoc_rqst *)&lsop[1];
	discon_acc = (struct fcnvme_ls_disconnect_assoc_acc *)&discon_rqst[1];
	lsreq = &lsop->ls_req;
	if (ctrl->lport->ops->lsrqst_priv_sz)
		lsreq->private = (void *)&discon_acc[1];
	else
		lsreq->private = NULL;

	nvmefc_fmt_lsreq_discon_assoc(lsreq, discon_rqst, discon_acc,
				ctrl->association_id);

	ret = nvme_fc_send_ls_req_async(ctrl->rport, lsop,
				nvme_fc_disconnect_assoc_done);
	if (ret)
		kfree(lsop);
}

static void
nvme_fc_xmt_ls_rsp_done(struct nvmefc_ls_rsp *lsrsp)
{
	struct nvmefc_ls_rcv_op *lsop = lsrsp->nvme_fc_private;
	struct nvme_fc_rport *rport = lsop->rport;
	struct nvme_fc_lport *lport = rport->lport;
	unsigned long flags;

	spin_lock_irqsave(&rport->lock, flags);
	list_del(&lsop->lsrcv_list);
	spin_unlock_irqrestore(&rport->lock, flags);

	fc_dma_sync_single_for_cpu(lport->dev, lsop->rspdma,
				sizeof(*lsop->rspbuf), DMA_TO_DEVICE);
	fc_dma_unmap_single(lport->dev, lsop->rspdma,
			sizeof(*lsop->rspbuf), DMA_TO_DEVICE);

	kfree(lsop->rspbuf);
	kfree(lsop->rqstbuf);
	kfree(lsop);

	nvme_fc_rport_put(rport);
}

static void
nvme_fc_xmt_ls_rsp(struct nvmefc_ls_rcv_op *lsop)
{
	struct nvme_fc_rport *rport = lsop->rport;
	struct nvme_fc_lport *lport = rport->lport;
	struct fcnvme_ls_rqst_w0 *w0 = &lsop->rqstbuf->w0;
	int ret;

	fc_dma_sync_single_for_device(lport->dev, lsop->rspdma,
				  sizeof(*lsop->rspbuf), DMA_TO_DEVICE);

	ret = lport->ops->xmt_ls_rsp(&lport->localport, &rport->remoteport,
				     lsop->lsrsp);
	if (ret) {
		dev_warn(lport->dev,
			"LLDD rejected LS RSP xmt: LS %d status %d\n",
			w0->ls_cmd, ret);
		nvme_fc_xmt_ls_rsp_done(lsop->lsrsp);
		return;
	}
}

static struct nvme_fc_ctrl *
nvme_fc_match_disconn_ls(struct nvme_fc_rport *rport,
		      struct nvmefc_ls_rcv_op *lsop)
{
	struct fcnvme_ls_disconnect_assoc_rqst *rqst =
					&lsop->rqstbuf->rq_dis_assoc;
	struct nvme_fc_ctrl *ctrl, *ret = NULL;
	struct nvmefc_ls_rcv_op *oldls = NULL;
	u64 association_id = be64_to_cpu(rqst->associd.association_id);
	unsigned long flags;

	spin_lock_irqsave(&rport->lock, flags);

	list_for_each_entry(ctrl, &rport->ctrl_list, ctrl_list) {
		if (!nvme_fc_ctrl_get(ctrl))
			continue;
		spin_lock(&ctrl->lock);
		if (association_id == ctrl->association_id) {
			oldls = ctrl->rcv_disconn;
			ctrl->rcv_disconn = lsop;
			ret = ctrl;
		}
		spin_unlock(&ctrl->lock);
		if (ret)
			/* leave the ctrl get reference */
			break;
		nvme_fc_ctrl_put(ctrl);
	}

	spin_unlock_irqrestore(&rport->lock, flags);

	/* transmit a response for anything that was pending */
	if (oldls) {
		dev_info(rport->lport->dev,
			"NVME-FC{%d}: Multiple Disconnect Association "
			"LS's received\n", ctrl->cnum);
		/* overwrite good response with bogus failure */
		oldls->lsrsp->rsplen = nvme_fc_format_rjt(oldls->rspbuf,
						sizeof(*oldls->rspbuf),
						rqst->w0.ls_cmd,
						FCNVME_RJT_RC_UNAB,
						FCNVME_RJT_EXP_NONE, 0);
		nvme_fc_xmt_ls_rsp(oldls);
	}

	return ret;
}

/*
 * returns true to mean LS handled and ls_rsp can be sent
 * returns false to defer ls_rsp xmt (will be done as part of
 *     association termination)
 */
static bool
nvme_fc_ls_disconnect_assoc(struct nvmefc_ls_rcv_op *lsop)
{
	struct nvme_fc_rport *rport = lsop->rport;
	struct fcnvme_ls_disconnect_assoc_rqst *rqst =
					&lsop->rqstbuf->rq_dis_assoc;
	struct fcnvme_ls_disconnect_assoc_acc *acc =
					&lsop->rspbuf->rsp_dis_assoc;
	struct nvme_fc_ctrl *ctrl = NULL;
	int ret = 0;

	memset(acc, 0, sizeof(*acc));

	ret = nvmefc_vldt_lsreq_discon_assoc(lsop->rqstdatalen, rqst);
	if (!ret) {
		/* match an active association */
		ctrl = nvme_fc_match_disconn_ls(rport, lsop);
		if (!ctrl)
			ret = VERR_NO_ASSOC;
	}

	if (ret) {
		dev_info(rport->lport->dev,
			"Disconnect LS failed: %s\n",
			validation_errors[ret]);
		lsop->lsrsp->rsplen = nvme_fc_format_rjt(acc,
					sizeof(*acc), rqst->w0.ls_cmd,
					(ret == VERR_NO_ASSOC) ?
						FCNVME_RJT_RC_INV_ASSOC :
						FCNVME_RJT_RC_LOGIC,
					FCNVME_RJT_EXP_NONE, 0);
		return true;
	}

	/* format an ACCept response */

	lsop->lsrsp->rsplen = sizeof(*acc);

	nvme_fc_format_rsp_hdr(acc, FCNVME_LS_ACC,
			fcnvme_lsdesc_len(
				sizeof(struct fcnvme_ls_disconnect_assoc_acc)),
			FCNVME_LS_DISCONNECT_ASSOC);

	/*
	 * the transmit of the response will occur after the exchanges
	 * for the association have been ABTS'd by
	 * nvme_fc_delete_association().
	 */

	/* fail the association */
	nvme_fc_error_recovery(ctrl, "Disconnect Association LS received");

	/* release the reference taken by nvme_fc_match_disconn_ls() */
	nvme_fc_ctrl_put(ctrl);

	return false;
}

/*
 * Actual Processing routine for received FC-NVME LS Requests from the LLD
 * returns true if a response should be sent afterward, false if rsp will
 * be sent asynchronously.
 */
static bool
nvme_fc_handle_ls_rqst(struct nvmefc_ls_rcv_op *lsop)
{
	struct fcnvme_ls_rqst_w0 *w0 = &lsop->rqstbuf->w0;
	bool ret = true;

	lsop->lsrsp->nvme_fc_private = lsop;
	lsop->lsrsp->rspbuf = lsop->rspbuf;
	lsop->lsrsp->rspdma = lsop->rspdma;
	lsop->lsrsp->done = nvme_fc_xmt_ls_rsp_done;
	/* Be preventative. handlers will later set to valid length */
	lsop->lsrsp->rsplen = 0;

	/*
	 * handlers:
	 *   parse request input, execute the request, and format the
	 *   LS response
	 */
	switch (w0->ls_cmd) {
	case FCNVME_LS_DISCONNECT_ASSOC:
		ret = nvme_fc_ls_disconnect_assoc(lsop);
		break;
	case FCNVME_LS_DISCONNECT_CONN:
		lsop->lsrsp->rsplen = nvme_fc_format_rjt(lsop->rspbuf,
				sizeof(*lsop->rspbuf), w0->ls_cmd,
				FCNVME_RJT_RC_UNSUP, FCNVME_RJT_EXP_NONE, 0);
		break;
	case FCNVME_LS_CREATE_ASSOCIATION:
	case FCNVME_LS_CREATE_CONNECTION:
		lsop->lsrsp->rsplen = nvme_fc_format_rjt(lsop->rspbuf,
				sizeof(*lsop->rspbuf), w0->ls_cmd,
				FCNVME_RJT_RC_LOGIC, FCNVME_RJT_EXP_NONE, 0);
		break;
	default:
		lsop->lsrsp->rsplen = nvme_fc_format_rjt(lsop->rspbuf,
				sizeof(*lsop->rspbuf), w0->ls_cmd,
				FCNVME_RJT_RC_INVAL, FCNVME_RJT_EXP_NONE, 0);
		break;
	}

	return(ret);
}

static void
nvme_fc_handle_ls_rqst_work(struct work_struct *work)
{
	struct nvme_fc_rport *rport =
		container_of(work, struct nvme_fc_rport, lsrcv_work);
	struct fcnvme_ls_rqst_w0 *w0;
	struct nvmefc_ls_rcv_op *lsop;
	unsigned long flags;
	bool sendrsp;

restart:
	sendrsp = true;
	spin_lock_irqsave(&rport->lock, flags);
	list_for_each_entry(lsop, &rport->ls_rcv_list, lsrcv_list) {
		if (lsop->handled)
			continue;

		lsop->handled = true;
		if (rport->remoteport.port_state == FC_OBJSTATE_ONLINE) {
			spin_unlock_irqrestore(&rport->lock, flags);
			sendrsp = nvme_fc_handle_ls_rqst(lsop);
		} else {
			spin_unlock_irqrestore(&rport->lock, flags);
			w0 = &lsop->rqstbuf->w0;
			lsop->lsrsp->rsplen = nvme_fc_format_rjt(
						lsop->rspbuf,
						sizeof(*lsop->rspbuf),
						w0->ls_cmd,
						FCNVME_RJT_RC_UNAB,
						FCNVME_RJT_EXP_NONE, 0);
		}
		if (sendrsp)
			nvme_fc_xmt_ls_rsp(lsop);
		goto restart;
	}
	spin_unlock_irqrestore(&rport->lock, flags);
}

static
void nvme_fc_rcv_ls_req_err_msg(struct nvme_fc_lport *lport,
				struct fcnvme_ls_rqst_w0 *w0)
{
	dev_info(lport->dev, "RCV %s LS failed: No memory\n",
		(w0->ls_cmd <= NVME_FC_LAST_LS_CMD_VALUE) ?
			nvmefc_ls_names[w0->ls_cmd] : "");
}

/**
 * nvme_fc_rcv_ls_req - transport entry point called by an LLDD
 *                       upon the reception of a NVME LS request.
 *
 * The nvme-fc layer will copy payload to an internal structure for
 * processing.  As such, upon completion of the routine, the LLDD may
 * immediately free/reuse the LS request buffer passed in the call.
 *
 * If this routine returns error, the LLDD should abort the exchange.
 *
 * @portptr:    pointer to the (registered) remote port that the LS
 *              was received from. The remoteport is associated with
 *              a specific localport.
 * @lsrsp:      pointer to a nvmefc_ls_rsp response structure to be
 *              used to reference the exchange corresponding to the LS
 *              when issuing an ls response.
 * @lsreqbuf:   pointer to the buffer containing the LS Request
 * @lsreqbuf_len: length, in bytes, of the received LS request
 */
int
nvme_fc_rcv_ls_req(struct nvme_fc_remote_port *portptr,
			struct nvmefc_ls_rsp *lsrsp,
			void *lsreqbuf, u32 lsreqbuf_len)
{
	struct nvme_fc_rport *rport = remoteport_to_rport(portptr);
	struct nvme_fc_lport *lport = rport->lport;
	struct fcnvme_ls_rqst_w0 *w0 = (struct fcnvme_ls_rqst_w0 *)lsreqbuf;
	struct nvmefc_ls_rcv_op *lsop;
	unsigned long flags;
	int ret;

	nvme_fc_rport_get(rport);

	/* validate there's a routine to transmit a response */
	if (!lport->ops->xmt_ls_rsp) {
		dev_info(lport->dev,
			"RCV %s LS failed: no LLDD xmt_ls_rsp\n",
			(w0->ls_cmd <= NVME_FC_LAST_LS_CMD_VALUE) ?
				nvmefc_ls_names[w0->ls_cmd] : "");
		ret = -EINVAL;
		goto out_put;
	}

	if (lsreqbuf_len > sizeof(union nvmefc_ls_requests)) {
		dev_info(lport->dev,
			"RCV %s LS failed: payload too large\n",
			(w0->ls_cmd <= NVME_FC_LAST_LS_CMD_VALUE) ?
				nvmefc_ls_names[w0->ls_cmd] : "");
		ret = -E2BIG;
		goto out_put;
	}

	lsop = kzalloc(sizeof(*lsop), GFP_KERNEL);
	if (!lsop) {
		nvme_fc_rcv_ls_req_err_msg(lport, w0);
		ret = -ENOMEM;
		goto out_put;
	}

	lsop->rqstbuf = kzalloc(sizeof(*lsop->rqstbuf), GFP_KERNEL);
	lsop->rspbuf = kzalloc(sizeof(*lsop->rspbuf), GFP_KERNEL);
	if (!lsop->rqstbuf || !lsop->rspbuf) {
		nvme_fc_rcv_ls_req_err_msg(lport, w0);
		ret = -ENOMEM;
		goto out_free;
	}

	lsop->rspdma = fc_dma_map_single(lport->dev, lsop->rspbuf,
					sizeof(*lsop->rspbuf),
					DMA_TO_DEVICE);
	if (fc_dma_mapping_error(lport->dev, lsop->rspdma)) {
		dev_info(lport->dev,
			"RCV %s LS failed: DMA mapping failure\n",
			(w0->ls_cmd <= NVME_FC_LAST_LS_CMD_VALUE) ?
				nvmefc_ls_names[w0->ls_cmd] : "");
		ret = -EFAULT;
		goto out_free;
	}

	lsop->rport = rport;
	lsop->lsrsp = lsrsp;

	memcpy(lsop->rqstbuf, lsreqbuf, lsreqbuf_len);
	lsop->rqstdatalen = lsreqbuf_len;

	spin_lock_irqsave(&rport->lock, flags);
	if (rport->remoteport.port_state != FC_OBJSTATE_ONLINE) {
		spin_unlock_irqrestore(&rport->lock, flags);
		ret = -ENOTCONN;
		goto out_unmap;
	}
	list_add_tail(&lsop->lsrcv_list, &rport->ls_rcv_list);
	spin_unlock_irqrestore(&rport->lock, flags);

	schedule_work(&rport->lsrcv_work);

	return 0;

out_unmap:
	fc_dma_unmap_single(lport->dev, lsop->rspdma,
			sizeof(*lsop->rspbuf), DMA_TO_DEVICE);
out_free:
	kfree(lsop->rspbuf);
	kfree(lsop->rqstbuf);
	kfree(lsop);
out_put:
	nvme_fc_rport_put(rport);
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_fc_rcv_ls_req);


/* *********************** NVME Ctrl Routines **************************** */

static void
__nvme_fc_exit_request(struct nvme_fc_ctrl *ctrl,
		struct nvme_fc_fcp_op *op)
{
	fc_dma_unmap_single(ctrl->lport->dev, op->fcp_req.rspdma,
				sizeof(op->rsp_iu), DMA_FROM_DEVICE);
	fc_dma_unmap_single(ctrl->lport->dev, op->fcp_req.cmddma,
				sizeof(op->cmd_iu), DMA_TO_DEVICE);

	atomic_set(&op->state, FCPOP_STATE_UNINIT);
}

static void
nvme_fc_exit_request(struct blk_mq_tag_set *set, struct request *rq,
		unsigned int hctx_idx)
{
	struct nvme_fc_fcp_op *op = blk_mq_rq_to_pdu(rq);

	return __nvme_fc_exit_request(to_fc_ctrl(set->driver_data), op);
}

static int
__nvme_fc_abort_op(struct nvme_fc_ctrl *ctrl, struct nvme_fc_fcp_op *op)
{
	unsigned long flags;
	int opstate;

	spin_lock_irqsave(&ctrl->lock, flags);
	opstate = atomic_xchg(&op->state, FCPOP_STATE_ABORTED);
	if (opstate != FCPOP_STATE_ACTIVE)
		atomic_set(&op->state, opstate);
	else if (test_bit(FCCTRL_TERMIO, &ctrl->flags)) {
		op->flags |= FCOP_FLAGS_TERMIO;
		ctrl->iocnt++;
	}
	spin_unlock_irqrestore(&ctrl->lock, flags);

	if (opstate != FCPOP_STATE_ACTIVE)
		return -ECANCELED;

	ctrl->lport->ops->fcp_abort(&ctrl->lport->localport,
					&ctrl->rport->remoteport,
					op->queue->lldd_handle,
					&op->fcp_req);

	return 0;
}

static void
nvme_fc_abort_aen_ops(struct nvme_fc_ctrl *ctrl)
{
	struct nvme_fc_fcp_op *aen_op = ctrl->aen_ops;
	int i;

	/* ensure we've initialized the ops once */
	if (!(aen_op->flags & FCOP_FLAGS_AEN))
		return;

	for (i = 0; i < NVME_NR_AEN_COMMANDS; i++, aen_op++)
		__nvme_fc_abort_op(ctrl, aen_op);
}

static inline void
__nvme_fc_fcpop_chk_teardowns(struct nvme_fc_ctrl *ctrl,
		struct nvme_fc_fcp_op *op, int opstate)
{
	unsigned long flags;

	if (opstate == FCPOP_STATE_ABORTED) {
		spin_lock_irqsave(&ctrl->lock, flags);
		if (test_bit(FCCTRL_TERMIO, &ctrl->flags) &&
		    op->flags & FCOP_FLAGS_TERMIO) {
			if (!--ctrl->iocnt)
				wake_up(&ctrl->ioabort_wait);
		}
		spin_unlock_irqrestore(&ctrl->lock, flags);
	}
}

static void
nvme_fc_ctrl_ioerr_work(struct work_struct *work)
{
	struct nvme_fc_ctrl *ctrl =
			container_of(work, struct nvme_fc_ctrl, ioerr_work);

	nvme_fc_error_recovery(ctrl, "transport detected io error");
}

/*
 * nvme_fc_io_getuuid - Routine called to get the appid field
 * associated with request by the lldd
 * @req:IO request from nvme fc to driver
 * Returns: UUID if there is an appid associated with VM or
 * NULL if the user/libvirt has not set the appid to VM
 */
char *nvme_fc_io_getuuid(struct nvmefc_fcp_req *req)
{
	struct nvme_fc_fcp_op *op = fcp_req_to_fcp_op(req);
	struct request *rq = op->rq;

	if (!IS_ENABLED(CONFIG_BLK_CGROUP_FC_APPID) || !rq || !rq->bio)
		return NULL;
	return blkcg_get_fc_appid(rq->bio);
}
EXPORT_SYMBOL_GPL(nvme_fc_io_getuuid);

static void
nvme_fc_fcpio_done(struct nvmefc_fcp_req *req)
{
	struct nvme_fc_fcp_op *op = fcp_req_to_fcp_op(req);
	struct request *rq = op->rq;
	struct nvmefc_fcp_req *freq = &op->fcp_req;
	struct nvme_fc_ctrl *ctrl = op->ctrl;
	struct nvme_fc_queue *queue = op->queue;
	struct nvme_completion *cqe = &op->rsp_iu.cqe;
	struct nvme_command *sqe = &op->cmd_iu.sqe;
	__le16 status = cpu_to_le16(NVME_SC_SUCCESS << 1);
	union nvme_result result;
	bool terminate_assoc = true;
	int opstate;

	/*
	 * WARNING:
	 * The current linux implementation of a nvme controller
	 * allocates a single tag set for all io queues and sizes
	 * the io queues to fully hold all possible tags. Thus, the
	 * implementation does not reference or care about the sqhd
	 * value as it never needs to use the sqhd/sqtail pointers
	 * for submission pacing.
	 *
	 * This affects the FC-NVME implementation in two ways:
	 * 1) As the value doesn't matter, we don't need to waste
	 *    cycles extracting it from ERSPs and stamping it in the
	 *    cases where the transport fabricates CQEs on successful
	 *    completions.
	 * 2) The FC-NVME implementation requires that delivery of
	 *    ERSP completions are to go back to the nvme layer in order
	 *    relative to the rsn, such that the sqhd value will always
	 *    be "in order" for the nvme layer. As the nvme layer in
	 *    linux doesn't care about sqhd, there's no need to return
	 *    them in order.
	 *
	 * Additionally:
	 * As the core nvme layer in linux currently does not look at
	 * every field in the cqe - in cases where the FC transport must
	 * fabricate a CQE, the following fields will not be set as they
	 * are not referenced:
	 *      cqe.sqid,  cqe.sqhd,  cqe.command_id
	 *
	 * Failure or error of an individual i/o, in a transport
	 * detected fashion unrelated to the nvme completion status,
	 * potentially cause the initiator and target sides to get out
	 * of sync on SQ head/tail (aka outstanding io count allowed).
	 * Per FC-NVME spec, failure of an individual command requires
	 * the connection to be terminated, which in turn requires the
	 * association to be terminated.
	 */

	opstate = atomic_xchg(&op->state, FCPOP_STATE_COMPLETE);

	fc_dma_sync_single_for_cpu(ctrl->lport->dev, op->fcp_req.rspdma,
				sizeof(op->rsp_iu), DMA_FROM_DEVICE);

	if (opstate == FCPOP_STATE_ABORTED)
		status = cpu_to_le16(NVME_SC_HOST_ABORTED_CMD << 1);
	else if (freq->status) {
		status = cpu_to_le16(NVME_SC_HOST_PATH_ERROR << 1);
		dev_info(ctrl->ctrl.device,
			"NVME-FC{%d}: io failed due to lldd error %d\n",
			ctrl->cnum, freq->status);
	}

	/*
	 * For the linux implementation, if we have an unsuccesful
	 * status, they blk-mq layer can typically be called with the
	 * non-zero status and the content of the cqe isn't important.
	 */
	if (status)
		goto done;

	/*
	 * command completed successfully relative to the wire
	 * protocol. However, validate anything received and
	 * extract the status and result from the cqe (create it
	 * where necessary).
	 */

	switch (freq->rcv_rsplen) {

	case 0:
	case NVME_FC_SIZEOF_ZEROS_RSP:
		/*
		 * No response payload or 12 bytes of payload (which
		 * should all be zeros) are considered successful and
		 * no payload in the CQE by the transport.
		 */
		if (freq->transferred_length !=
		    be32_to_cpu(op->cmd_iu.data_len)) {
			status = cpu_to_le16(NVME_SC_HOST_PATH_ERROR << 1);
			dev_info(ctrl->ctrl.device,
				"NVME-FC{%d}: io failed due to bad transfer "
				"length: %d vs expected %d\n",
				ctrl->cnum, freq->transferred_length,
				be32_to_cpu(op->cmd_iu.data_len));
			goto done;
		}
		result.u64 = 0;
		break;

	case sizeof(struct nvme_fc_ersp_iu):
		/*
		 * The ERSP IU contains a full completion with CQE.
		 * Validate ERSP IU and look at cqe.
		 */
		if (unlikely(be16_to_cpu(op->rsp_iu.iu_len) !=
					(freq->rcv_rsplen / 4) ||
			     be32_to_cpu(op->rsp_iu.xfrd_len) !=
					freq->transferred_length ||
			     op->rsp_iu.ersp_result ||
			     sqe->common.command_id != cqe->command_id)) {
			status = cpu_to_le16(NVME_SC_HOST_PATH_ERROR << 1);
			dev_info(ctrl->ctrl.device,
				"NVME-FC{%d}: io failed due to bad NVMe_ERSP: "
				"iu len %d, xfr len %d vs %d, status code "
				"%d, cmdid %d vs %d\n",
				ctrl->cnum, be16_to_cpu(op->rsp_iu.iu_len),
				be32_to_cpu(op->rsp_iu.xfrd_len),
				freq->transferred_length,
				op->rsp_iu.ersp_result,
				sqe->common.command_id,
				cqe->command_id);
			goto done;
		}
		result = cqe->result;
		status = cqe->status;
		break;

	default:
		status = cpu_to_le16(NVME_SC_HOST_PATH_ERROR << 1);
		dev_info(ctrl->ctrl.device,
			"NVME-FC{%d}: io failed due to odd NVMe_xRSP iu "
			"len %d\n",
			ctrl->cnum, freq->rcv_rsplen);
		goto done;
	}

	terminate_assoc = false;

done:
	if (op->flags & FCOP_FLAGS_AEN) {
		nvme_complete_async_event(&queue->ctrl->ctrl, status, &result);
		__nvme_fc_fcpop_chk_teardowns(ctrl, op, opstate);
		atomic_set(&op->state, FCPOP_STATE_IDLE);
		op->flags = FCOP_FLAGS_AEN;	/* clear other flags */
		nvme_fc_ctrl_put(ctrl);
		goto check_error;
	}

	__nvme_fc_fcpop_chk_teardowns(ctrl, op, opstate);
	if (!nvme_try_complete_req(rq, status, result))
		nvme_fc_complete_rq(rq);

check_error:
	if (terminate_assoc && ctrl->ctrl.state != NVME_CTRL_RESETTING)
		queue_work(nvme_reset_wq, &ctrl->ioerr_work);
}

static int
__nvme_fc_init_request(struct nvme_fc_ctrl *ctrl,
		struct nvme_fc_queue *queue, struct nvme_fc_fcp_op *op,
		struct request *rq, u32 rqno)
{
	struct nvme_fcp_op_w_sgl *op_w_sgl =
		container_of(op, typeof(*op_w_sgl), op);
	struct nvme_fc_cmd_iu *cmdiu = &op->cmd_iu;
	int ret = 0;

	memset(op, 0, sizeof(*op));
	op->fcp_req.cmdaddr = &op->cmd_iu;
	op->fcp_req.cmdlen = sizeof(op->cmd_iu);
	op->fcp_req.rspaddr = &op->rsp_iu;
	op->fcp_req.rsplen = sizeof(op->rsp_iu);
	op->fcp_req.done = nvme_fc_fcpio_done;
	op->ctrl = ctrl;
	op->queue = queue;
	op->rq = rq;
	op->rqno = rqno;

	cmdiu->format_id = NVME_CMD_FORMAT_ID;
	cmdiu->fc_id = NVME_CMD_FC_ID;
	cmdiu->iu_len = cpu_to_be16(sizeof(*cmdiu) / sizeof(u32));
	if (queue->qnum)
		cmdiu->rsv_cat = fccmnd_set_cat_css(0,
					(NVME_CC_CSS_NVM >> NVME_CC_CSS_SHIFT));
	else
		cmdiu->rsv_cat = fccmnd_set_cat_admin(0);

	op->fcp_req.cmddma = fc_dma_map_single(ctrl->lport->dev,
				&op->cmd_iu, sizeof(op->cmd_iu), DMA_TO_DEVICE);
	if (fc_dma_mapping_error(ctrl->lport->dev, op->fcp_req.cmddma)) {
		dev_err(ctrl->dev,
			"FCP Op failed - cmdiu dma mapping failed.\n");
		ret = -EFAULT;
		goto out_on_error;
	}

	op->fcp_req.rspdma = fc_dma_map_single(ctrl->lport->dev,
				&op->rsp_iu, sizeof(op->rsp_iu),
				DMA_FROM_DEVICE);
	if (fc_dma_mapping_error(ctrl->lport->dev, op->fcp_req.rspdma)) {
		dev_err(ctrl->dev,
			"FCP Op failed - rspiu dma mapping failed.\n");
		ret = -EFAULT;
	}

	atomic_set(&op->state, FCPOP_STATE_IDLE);
out_on_error:
	return ret;
}

static int
nvme_fc_init_request(struct blk_mq_tag_set *set, struct request *rq,
		unsigned int hctx_idx, unsigned int numa_node)
{
	struct nvme_fc_ctrl *ctrl = to_fc_ctrl(set->driver_data);
	struct nvme_fcp_op_w_sgl *op = blk_mq_rq_to_pdu(rq);
	int queue_idx = (set == &ctrl->tag_set) ? hctx_idx + 1 : 0;
	struct nvme_fc_queue *queue = &ctrl->queues[queue_idx];
	int res;

	res = __nvme_fc_init_request(ctrl, queue, &op->op, rq, queue->rqcnt++);
	if (res)
		return res;
	op->op.fcp_req.first_sgl = op->sgl;
	op->op.fcp_req.private = &op->priv[0];
	nvme_req(rq)->ctrl = &ctrl->ctrl;
	nvme_req(rq)->cmd = &op->op.cmd_iu.sqe;
	return res;
}

static int
nvme_fc_init_aen_ops(struct nvme_fc_ctrl *ctrl)
{
	struct nvme_fc_fcp_op *aen_op;
	struct nvme_fc_cmd_iu *cmdiu;
	struct nvme_command *sqe;
	void *private = NULL;
	int i, ret;

	aen_op = ctrl->aen_ops;
	for (i = 0; i < NVME_NR_AEN_COMMANDS; i++, aen_op++) {
		if (ctrl->lport->ops->fcprqst_priv_sz) {
			private = kzalloc(ctrl->lport->ops->fcprqst_priv_sz,
						GFP_KERNEL);
			if (!private)
				return -ENOMEM;
		}

		cmdiu = &aen_op->cmd_iu;
		sqe = &cmdiu->sqe;
		ret = __nvme_fc_init_request(ctrl, &ctrl->queues[0],
				aen_op, (struct request *)NULL,
				(NVME_AQ_BLK_MQ_DEPTH + i));
		if (ret) {
			kfree(private);
			return ret;
		}

		aen_op->flags = FCOP_FLAGS_AEN;
		aen_op->fcp_req.private = private;

		memset(sqe, 0, sizeof(*sqe));
		sqe->common.opcode = nvme_admin_async_event;
		/* Note: core layer may overwrite the sqe.command_id value */
		sqe->common.command_id = NVME_AQ_BLK_MQ_DEPTH + i;
	}
	return 0;
}

static void
nvme_fc_term_aen_ops(struct nvme_fc_ctrl *ctrl)
{
	struct nvme_fc_fcp_op *aen_op;
	int i;

	cancel_work_sync(&ctrl->ctrl.async_event_work);
	aen_op = ctrl->aen_ops;
	for (i = 0; i < NVME_NR_AEN_COMMANDS; i++, aen_op++) {
		__nvme_fc_exit_request(ctrl, aen_op);

		kfree(aen_op->fcp_req.private);
		aen_op->fcp_req.private = NULL;
	}
}

static inline int
__nvme_fc_init_hctx(struct blk_mq_hw_ctx *hctx, void *data, unsigned int qidx)
{
	struct nvme_fc_ctrl *ctrl = to_fc_ctrl(data);
	struct nvme_fc_queue *queue = &ctrl->queues[qidx];

	hctx->driver_data = queue;
	queue->hctx = hctx;
	return 0;
}

static int
nvme_fc_init_hctx(struct blk_mq_hw_ctx *hctx, void *data, unsigned int hctx_idx)
{
	return __nvme_fc_init_hctx(hctx, data, hctx_idx + 1);
}

static int
nvme_fc_init_admin_hctx(struct blk_mq_hw_ctx *hctx, void *data,
		unsigned int hctx_idx)
{
	return __nvme_fc_init_hctx(hctx, data, hctx_idx);
}

static void
nvme_fc_init_queue(struct nvme_fc_ctrl *ctrl, int idx)
{
	struct nvme_fc_queue *queue;

	queue = &ctrl->queues[idx];
	memset(queue, 0, sizeof(*queue));
	queue->ctrl = ctrl;
	queue->qnum = idx;
	atomic_set(&queue->csn, 0);
	queue->dev = ctrl->dev;

	if (idx > 0)
		queue->cmnd_capsule_len = ctrl->ctrl.ioccsz * 16;
	else
		queue->cmnd_capsule_len = sizeof(struct nvme_command);

	/*
	 * Considered whether we should allocate buffers for all SQEs
	 * and CQEs and dma map them - mapping their respective entries
	 * into the request structures (kernel vm addr and dma address)
	 * thus the driver could use the buffers/mappings directly.
	 * It only makes sense if the LLDD would use them for its
	 * messaging api. It's very unlikely most adapter api's would use
	 * a native NVME sqe/cqe. More reasonable if FC-NVME IU payload
	 * structures were used instead.
	 */
}

/*
 * This routine terminates a queue at the transport level.
 * The transport has already ensured that all outstanding ios on
 * the queue have been terminated.
 * The transport will send a Disconnect LS request to terminate
 * the queue's connection. Termination of the admin queue will also
 * terminate the association at the target.
 */
static void
nvme_fc_free_queue(struct nvme_fc_queue *queue)
{
	if (!test_and_clear_bit(NVME_FC_Q_CONNECTED, &queue->flags))
		return;

	clear_bit(NVME_FC_Q_LIVE, &queue->flags);
	/*
	 * Current implementation never disconnects a single queue.
	 * It always terminates a whole association. So there is never
	 * a disconnect(queue) LS sent to the target.
	 */

	queue->connection_id = 0;
	atomic_set(&queue->csn, 0);
}

static void
__nvme_fc_delete_hw_queue(struct nvme_fc_ctrl *ctrl,
	struct nvme_fc_queue *queue, unsigned int qidx)
{
	if (ctrl->lport->ops->delete_queue)
		ctrl->lport->ops->delete_queue(&ctrl->lport->localport, qidx,
				queue->lldd_handle);
	queue->lldd_handle = NULL;
}

static void
nvme_fc_free_io_queues(struct nvme_fc_ctrl *ctrl)
{
	int i;

	for (i = 1; i < ctrl->ctrl.queue_count; i++)
		nvme_fc_free_queue(&ctrl->queues[i]);
}

static int
__nvme_fc_create_hw_queue(struct nvme_fc_ctrl *ctrl,
	struct nvme_fc_queue *queue, unsigned int qidx, u16 qsize)
{
	int ret = 0;

	queue->lldd_handle = NULL;
	if (ctrl->lport->ops->create_queue)
		ret = ctrl->lport->ops->create_queue(&ctrl->lport->localport,
				qidx, qsize, &queue->lldd_handle);

	return ret;
}

static void
nvme_fc_delete_hw_io_queues(struct nvme_fc_ctrl *ctrl)
{
	struct nvme_fc_queue *queue = &ctrl->queues[ctrl->ctrl.queue_count - 1];
	int i;

	for (i = ctrl->ctrl.queue_count - 1; i >= 1; i--, queue--)
		__nvme_fc_delete_hw_queue(ctrl, queue, i);
}

static int
nvme_fc_create_hw_io_queues(struct nvme_fc_ctrl *ctrl, u16 qsize)
{
	struct nvme_fc_queue *queue = &ctrl->queues[1];
	int i, ret;

	for (i = 1; i < ctrl->ctrl.queue_count; i++, queue++) {
		ret = __nvme_fc_create_hw_queue(ctrl, queue, i, qsize);
		if (ret)
			goto delete_queues;
	}

	return 0;

delete_queues:
	for (; i > 0; i--)
		__nvme_fc_delete_hw_queue(ctrl, &ctrl->queues[i], i);
	return ret;
}

static int
nvme_fc_connect_io_queues(struct nvme_fc_ctrl *ctrl, u16 qsize)
{
	int i, ret = 0;

	for (i = 1; i < ctrl->ctrl.queue_count; i++) {
		ret = nvme_fc_connect_queue(ctrl, &ctrl->queues[i], qsize,
					(qsize / 5));
		if (ret)
			break;
		ret = nvmf_connect_io_queue(&ctrl->ctrl, i);
		if (ret)
			break;

		set_bit(NVME_FC_Q_LIVE, &ctrl->queues[i].flags);
	}

	return ret;
}

static void
nvme_fc_init_io_queues(struct nvme_fc_ctrl *ctrl)
{
	int i;

	for (i = 1; i < ctrl->ctrl.queue_count; i++)
		nvme_fc_init_queue(ctrl, i);
}

static void
nvme_fc_ctrl_free(struct kref *ref)
{
	struct nvme_fc_ctrl *ctrl =
		container_of(ref, struct nvme_fc_ctrl, ref);
	unsigned long flags;

	if (ctrl->ctrl.tagset)
		nvme_remove_io_tag_set(&ctrl->ctrl);

	/* remove from rport list */
	spin_lock_irqsave(&ctrl->rport->lock, flags);
	list_del(&ctrl->ctrl_list);
	spin_unlock_irqrestore(&ctrl->rport->lock, flags);

	nvme_unquiesce_admin_queue(&ctrl->ctrl);
	nvme_remove_admin_tag_set(&ctrl->ctrl);

	kfree(ctrl->queues);

	put_device(ctrl->dev);
	nvme_fc_rport_put(ctrl->rport);

	ida_free(&nvme_fc_ctrl_cnt, ctrl->cnum);
	if (ctrl->ctrl.opts)
		nvmf_free_options(ctrl->ctrl.opts);
	kfree(ctrl);
}

static void
nvme_fc_ctrl_put(struct nvme_fc_ctrl *ctrl)
{
	kref_put(&ctrl->ref, nvme_fc_ctrl_free);
}

static int
nvme_fc_ctrl_get(struct nvme_fc_ctrl *ctrl)
{
	return kref_get_unless_zero(&ctrl->ref);
}

/*
 * All accesses from nvme core layer done - can now free the
 * controller. Called after last nvme_put_ctrl() call
 */
static void
nvme_fc_nvme_ctrl_freed(struct nvme_ctrl *nctrl)
{
	struct nvme_fc_ctrl *ctrl = to_fc_ctrl(nctrl);

	WARN_ON(nctrl != &ctrl->ctrl);

	nvme_fc_ctrl_put(ctrl);
}

/*
 * This routine is used by the transport when it needs to find active
 * io on a queue that is to be terminated. The transport uses
 * blk_mq_tagset_busy_itr() to find the busy requests, which then invoke
 * this routine to kill them on a 1 by 1 basis.
 *
 * As FC allocates FC exchange for each io, the transport must contact
 * the LLDD to terminate the exchange, thus releasing the FC exchange.
 * After terminating the exchange the LLDD will call the transport's
 * normal io done path for the request, but it will have an aborted
 * status. The done path will return the io request back to the block
 * layer with an error status.
 */
static bool nvme_fc_terminate_exchange(struct request *req, void *data)
{
	struct nvme_ctrl *nctrl = data;
	struct nvme_fc_ctrl *ctrl = to_fc_ctrl(nctrl);
	struct nvme_fc_fcp_op *op = blk_mq_rq_to_pdu(req);

	op->nreq.flags |= NVME_REQ_CANCELLED;
	__nvme_fc_abort_op(ctrl, op);
	return true;
}

/*
 * This routine runs through all outstanding commands on the association
 * and aborts them.  This routine is typically be called by the
 * delete_association routine. It is also called due to an error during
 * reconnect. In that scenario, it is most likely a command that initializes
 * the controller, including fabric Connect commands on io queues, that
 * may have timed out or failed thus the io must be killed for the connect
 * thread to see the error.
 */
static void
__nvme_fc_abort_outstanding_ios(struct nvme_fc_ctrl *ctrl, bool start_queues)
{
	int q;

	/*
	 * if aborting io, the queues are no longer good, mark them
	 * all as not live.
	 */
	if (ctrl->ctrl.queue_count > 1) {
		for (q = 1; q < ctrl->ctrl.queue_count; q++)
			clear_bit(NVME_FC_Q_LIVE, &ctrl->queues[q].flags);
	}
	clear_bit(NVME_FC_Q_LIVE, &ctrl->queues[0].flags);

	/*
	 * If io queues are present, stop them and terminate all outstanding
	 * ios on them. As FC allocates FC exchange for each io, the
	 * transport must contact the LLDD to terminate the exchange,
	 * thus releasing the FC exchange. We use blk_mq_tagset_busy_itr()
	 * to tell us what io's are busy and invoke a transport routine
	 * to kill them with the LLDD.  After terminating the exchange
	 * the LLDD will call the transport's normal io done path, but it
	 * will have an aborted status. The done path will return the
	 * io requests back to the block layer as part of normal completions
	 * (but with error status).
	 */
	if (ctrl->ctrl.queue_count > 1) {
		nvme_quiesce_io_queues(&ctrl->ctrl);
		nvme_sync_io_queues(&ctrl->ctrl);
		blk_mq_tagset_busy_iter(&ctrl->tag_set,
				nvme_fc_terminate_exchange, &ctrl->ctrl);
		blk_mq_tagset_wait_completed_request(&ctrl->tag_set);
		if (start_queues)
			nvme_unquiesce_io_queues(&ctrl->ctrl);
	}

	/*
	 * Other transports, which don't have link-level contexts bound
	 * to sqe's, would try to gracefully shutdown the controller by
	 * writing the registers for shutdown and polling (call
	 * nvme_disable_ctrl()). Given a bunch of i/o was potentially
	 * just aborted and we will wait on those contexts, and given
	 * there was no indication of how live the controlelr is on the
	 * link, don't send more io to create more contexts for the
	 * shutdown. Let the controller fail via keepalive failure if
	 * its still present.
	 */

	/*
	 * clean up the admin queue. Same thing as above.
	 */
	nvme_quiesce_admin_queue(&ctrl->ctrl);
	blk_sync_queue(ctrl->ctrl.admin_q);
	blk_mq_tagset_busy_iter(&ctrl->admin_tag_set,
				nvme_fc_terminate_exchange, &ctrl->ctrl);
	blk_mq_tagset_wait_completed_request(&ctrl->admin_tag_set);
	if (start_queues)
		nvme_unquiesce_admin_queue(&ctrl->ctrl);
}

static void
nvme_fc_error_recovery(struct nvme_fc_ctrl *ctrl, char *errmsg)
{
	/*
	 * if an error (io timeout, etc) while (re)connecting, the remote
	 * port requested terminating of the association (disconnect_ls)
	 * or an error (timeout or abort) occurred on an io while creating
	 * the controller.  Abort any ios on the association and let the
	 * create_association error path resolve things.
	 */
	if (ctrl->ctrl.state == NVME_CTRL_CONNECTING) {
		__nvme_fc_abort_outstanding_ios(ctrl, true);
		set_bit(ASSOC_FAILED, &ctrl->flags);
		dev_warn(ctrl->ctrl.device,
			"NVME-FC{%d}: transport error during (re)connect\n",
			ctrl->cnum);
		return;
	}

	/* Otherwise, only proceed if in LIVE state - e.g. on first error */
	if (ctrl->ctrl.state != NVME_CTRL_LIVE)
		return;

	dev_warn(ctrl->ctrl.device,
		"NVME-FC{%d}: transport association event: %s\n",
		ctrl->cnum, errmsg);
	dev_warn(ctrl->ctrl.device,
		"NVME-FC{%d}: resetting controller\n", ctrl->cnum);

	nvme_reset_ctrl(&ctrl->ctrl);
}

static enum blk_eh_timer_return nvme_fc_timeout(struct request *rq)
{
	struct nvme_fc_fcp_op *op = blk_mq_rq_to_pdu(rq);
	struct nvme_fc_ctrl *ctrl = op->ctrl;
	struct nvme_fc_cmd_iu *cmdiu = &op->cmd_iu;
	struct nvme_command *sqe = &cmdiu->sqe;

	/*
	 * Attempt to abort the offending command. Command completion
	 * will detect the aborted io and will fail the connection.
	 */
	dev_info(ctrl->ctrl.device,
		"NVME-FC{%d.%d}: io timeout: opcode %d fctype %d w10/11: "
		"x%08x/x%08x\n",
		ctrl->cnum, op->queue->qnum, sqe->common.opcode,
		sqe->connect.fctype, sqe->common.cdw10, sqe->common.cdw11);
	if (__nvme_fc_abort_op(ctrl, op))
		nvme_fc_error_recovery(ctrl, "io timeout abort failed");

	/*
	 * the io abort has been initiated. Have the reset timer
	 * restarted and the abort completion will complete the io
	 * shortly. Avoids a synchronous wait while the abort finishes.
	 */
	return BLK_EH_RESET_TIMER;
}

static int
nvme_fc_map_data(struct nvme_fc_ctrl *ctrl, struct request *rq,
		struct nvme_fc_fcp_op *op)
{
	struct nvmefc_fcp_req *freq = &op->fcp_req;
	int ret;

	freq->sg_cnt = 0;

	if (!blk_rq_nr_phys_segments(rq))
		return 0;

	freq->sg_table.sgl = freq->first_sgl;
	ret = sg_alloc_table_chained(&freq->sg_table,
			blk_rq_nr_phys_segments(rq), freq->sg_table.sgl,
			NVME_INLINE_SG_CNT);
	if (ret)
		return -ENOMEM;

	op->nents = blk_rq_map_sg(rq->q, rq, freq->sg_table.sgl);
	WARN_ON(op->nents > blk_rq_nr_phys_segments(rq));
	freq->sg_cnt = fc_dma_map_sg(ctrl->lport->dev, freq->sg_table.sgl,
				op->nents, rq_dma_dir(rq));
	if (unlikely(freq->sg_cnt <= 0)) {
		sg_free_table_chained(&freq->sg_table, NVME_INLINE_SG_CNT);
		freq->sg_cnt = 0;
		return -EFAULT;
	}

	/*
	 * TODO: blk_integrity_rq(rq)  for DIF
	 */
	return 0;
}

static void
nvme_fc_unmap_data(struct nvme_fc_ctrl *ctrl, struct request *rq,
		struct nvme_fc_fcp_op *op)
{
	struct nvmefc_fcp_req *freq = &op->fcp_req;

	if (!freq->sg_cnt)
		return;

	fc_dma_unmap_sg(ctrl->lport->dev, freq->sg_table.sgl, op->nents,
			rq_dma_dir(rq));

	sg_free_table_chained(&freq->sg_table, NVME_INLINE_SG_CNT);

	freq->sg_cnt = 0;
}

/*
 * In FC, the queue is a logical thing. At transport connect, the target
 * creates its "queue" and returns a handle that is to be given to the
 * target whenever it posts something to the corresponding SQ.  When an
 * SQE is sent on a SQ, FC effectively considers the SQE, or rather the
 * command contained within the SQE, an io, and assigns a FC exchange
 * to it. The SQE and the associated SQ handle are sent in the initial
 * CMD IU sents on the exchange. All transfers relative to the io occur
 * as part of the exchange.  The CQE is the last thing for the io,
 * which is transferred (explicitly or implicitly) with the RSP IU
 * sent on the exchange. After the CQE is received, the FC exchange is
 * terminaed and the Exchange may be used on a different io.
 *
 * The transport to LLDD api has the transport making a request for a
 * new fcp io request to the LLDD. The LLDD then allocates a FC exchange
 * resource and transfers the command. The LLDD will then process all
 * steps to complete the io. Upon completion, the transport done routine
 * is called.
 *
 * So - while the operation is outstanding to the LLDD, there is a link
 * level FC exchange resource that is also outstanding. This must be
 * considered in all cleanup operations.
 */
static blk_status_t
nvme_fc_start_fcp_op(struct nvme_fc_ctrl *ctrl, struct nvme_fc_queue *queue,
	struct nvme_fc_fcp_op *op, u32 data_len,
	enum nvmefc_fcp_datadir	io_dir)
{
	struct nvme_fc_cmd_iu *cmdiu = &op->cmd_iu;
	struct nvme_command *sqe = &cmdiu->sqe;
	int ret, opstate;

	/*
	 * before attempting to send the io, check to see if we believe
	 * the target device is present
	 */
	if (ctrl->rport->remoteport.port_state != FC_OBJSTATE_ONLINE)
		return BLK_STS_RESOURCE;

	if (!nvme_fc_ctrl_get(ctrl))
		return BLK_STS_IOERR;

	/* format the FC-NVME CMD IU and fcp_req */
	cmdiu->connection_id = cpu_to_be64(queue->connection_id);
	cmdiu->data_len = cpu_to_be32(data_len);
	switch (io_dir) {
	case NVMEFC_FCP_WRITE:
		cmdiu->flags = FCNVME_CMD_FLAGS_WRITE;
		break;
	case NVMEFC_FCP_READ:
		cmdiu->flags = FCNVME_CMD_FLAGS_READ;
		break;
	case NVMEFC_FCP_NODATA:
		cmdiu->flags = 0;
		break;
	}
	op->fcp_req.payload_length = data_len;
	op->fcp_req.io_dir = io_dir;
	op->fcp_req.transferred_length = 0;
	op->fcp_req.rcv_rsplen = 0;
	op->fcp_req.status = NVME_SC_SUCCESS;
	op->fcp_req.sqid = cpu_to_le16(queue->qnum);

	/*
	 * validate per fabric rules, set fields mandated by fabric spec
	 * as well as those by FC-NVME spec.
	 */
	WARN_ON_ONCE(sqe->common.metadata);
	sqe->common.flags |= NVME_CMD_SGL_METABUF;

	/*
	 * format SQE DPTR field per FC-NVME rules:
	 *    type=0x5     Transport SGL Data Block Descriptor
	 *    subtype=0xA  Transport-specific value
	 *    address=0
	 *    length=length of the data series
	 */
	sqe->rw.dptr.sgl.type = (NVME_TRANSPORT_SGL_DATA_DESC << 4) |
					NVME_SGL_FMT_TRANSPORT_A;
	sqe->rw.dptr.sgl.length = cpu_to_le32(data_len);
	sqe->rw.dptr.sgl.addr = 0;

	if (!(op->flags & FCOP_FLAGS_AEN)) {
		ret = nvme_fc_map_data(ctrl, op->rq, op);
		if (ret < 0) {
			nvme_cleanup_cmd(op->rq);
			nvme_fc_ctrl_put(ctrl);
			if (ret == -ENOMEM || ret == -EAGAIN)
				return BLK_STS_RESOURCE;
			return BLK_STS_IOERR;
		}
	}

	fc_dma_sync_single_for_device(ctrl->lport->dev, op->fcp_req.cmddma,
				  sizeof(op->cmd_iu), DMA_TO_DEVICE);

	atomic_set(&op->state, FCPOP_STATE_ACTIVE);

	if (!(op->flags & FCOP_FLAGS_AEN))
		nvme_start_request(op->rq);

	cmdiu->csn = cpu_to_be32(atomic_inc_return(&queue->csn));
	ret = ctrl->lport->ops->fcp_io(&ctrl->lport->localport,
					&ctrl->rport->remoteport,
					queue->lldd_handle, &op->fcp_req);

	if (ret) {
		/*
		 * If the lld fails to send the command is there an issue with
		 * the csn value?  If the command that fails is the Connect,
		 * no - as the connection won't be live.  If it is a command
		 * post-connect, it's possible a gap in csn may be created.
		 * Does this matter?  As Linux initiators don't send fused
		 * commands, no.  The gap would exist, but as there's nothing
		 * that depends on csn order to be delivered on the target
		 * side, it shouldn't hurt.  It would be difficult for a
		 * target to even detect the csn gap as it has no idea when the
		 * cmd with the csn was supposed to arrive.
		 */
		opstate = atomic_xchg(&op->state, FCPOP_STATE_COMPLETE);
		__nvme_fc_fcpop_chk_teardowns(ctrl, op, opstate);

		if (!(op->flags & FCOP_FLAGS_AEN)) {
			nvme_fc_unmap_data(ctrl, op->rq, op);
			nvme_cleanup_cmd(op->rq);
		}

		nvme_fc_ctrl_put(ctrl);

		if (ctrl->rport->remoteport.port_state == FC_OBJSTATE_ONLINE &&
				ret != -EBUSY)
			return BLK_STS_IOERR;

		return BLK_STS_RESOURCE;
	}

	return BLK_STS_OK;
}

static blk_status_t
nvme_fc_queue_rq(struct blk_mq_hw_ctx *hctx,
			const struct blk_mq_queue_data *bd)
{
	struct nvme_ns *ns = hctx->queue->queuedata;
	struct nvme_fc_queue *queue = hctx->driver_data;
	struct nvme_fc_ctrl *ctrl = queue->ctrl;
	struct request *rq = bd->rq;
	struct nvme_fc_fcp_op *op = blk_mq_rq_to_pdu(rq);
	enum nvmefc_fcp_datadir	io_dir;
	bool queue_ready = test_bit(NVME_FC_Q_LIVE, &queue->flags);
	u32 data_len;
	blk_status_t ret;

	if (ctrl->rport->remoteport.port_state != FC_OBJSTATE_ONLINE ||
	    !nvme_check_ready(&queue->ctrl->ctrl, rq, queue_ready))
		return nvme_fail_nonready_command(&queue->ctrl->ctrl, rq);

	ret = nvme_setup_cmd(ns, rq);
	if (ret)
		return ret;

	/*
	 * nvme core doesn't quite treat the rq opaquely. Commands such
	 * as WRITE ZEROES will return a non-zero rq payload_bytes yet
	 * there is no actual payload to be transferred.
	 * To get it right, key data transmission on there being 1 or
	 * more physical segments in the sg list. If there is no
	 * physical segments, there is no payload.
	 */
	if (blk_rq_nr_phys_segments(rq)) {
		data_len = blk_rq_payload_bytes(rq);
		io_dir = ((rq_data_dir(rq) == WRITE) ?
					NVMEFC_FCP_WRITE : NVMEFC_FCP_READ);
	} else {
		data_len = 0;
		io_dir = NVMEFC_FCP_NODATA;
	}


	return nvme_fc_start_fcp_op(ctrl, queue, op, data_len, io_dir);
}

static void
nvme_fc_submit_async_event(struct nvme_ctrl *arg)
{
	struct nvme_fc_ctrl *ctrl = to_fc_ctrl(arg);
	struct nvme_fc_fcp_op *aen_op;
	blk_status_t ret;

	if (test_bit(FCCTRL_TERMIO, &ctrl->flags))
		return;

	aen_op = &ctrl->aen_ops[0];

	ret = nvme_fc_start_fcp_op(ctrl, aen_op->queue, aen_op, 0,
					NVMEFC_FCP_NODATA);
	if (ret)
		dev_err(ctrl->ctrl.device,
			"failed async event work\n");
}

static void
nvme_fc_complete_rq(struct request *rq)
{
	struct nvme_fc_fcp_op *op = blk_mq_rq_to_pdu(rq);
	struct nvme_fc_ctrl *ctrl = op->ctrl;

	atomic_set(&op->state, FCPOP_STATE_IDLE);
	op->flags &= ~FCOP_FLAGS_TERMIO;

	nvme_fc_unmap_data(ctrl, rq, op);
	nvme_complete_rq(rq);
	nvme_fc_ctrl_put(ctrl);
}

static void nvme_fc_map_queues(struct blk_mq_tag_set *set)
{
	struct nvme_fc_ctrl *ctrl = to_fc_ctrl(set->driver_data);
	int i;

	for (i = 0; i < set->nr_maps; i++) {
		struct blk_mq_queue_map *map = &set->map[i];

		if (!map->nr_queues) {
			WARN_ON(i == HCTX_TYPE_DEFAULT);
			continue;
		}

		/* Call LLDD map queue functionality if defined */
		if (ctrl->lport->ops->map_queues)
			ctrl->lport->ops->map_queues(&ctrl->lport->localport,
						     map);
		else
			blk_mq_map_queues(map);
	}
}

static const struct blk_mq_ops nvme_fc_mq_ops = {
	.queue_rq	= nvme_fc_queue_rq,
	.complete	= nvme_fc_complete_rq,
	.init_request	= nvme_fc_init_request,
	.exit_request	= nvme_fc_exit_request,
	.init_hctx	= nvme_fc_init_hctx,
	.timeout	= nvme_fc_timeout,
	.map_queues	= nvme_fc_map_queues,
};

static int
nvme_fc_create_io_queues(struct nvme_fc_ctrl *ctrl)
{
	struct nvmf_ctrl_options *opts = ctrl->ctrl.opts;
	unsigned int nr_io_queues;
	int ret;

	nr_io_queues = min(min(opts->nr_io_queues, num_online_cpus()),
				ctrl->lport->ops->max_hw_queues);
	ret = nvme_set_queue_count(&ctrl->ctrl, &nr_io_queues);
	if (ret) {
		dev_info(ctrl->ctrl.device,
			"set_queue_count failed: %d\n", ret);
		return ret;
	}

	ctrl->ctrl.queue_count = nr_io_queues + 1;
	if (!nr_io_queues)
		return 0;

	nvme_fc_init_io_queues(ctrl);

	ret = nvme_alloc_io_tag_set(&ctrl->ctrl, &ctrl->tag_set,
			&nvme_fc_mq_ops, 1,
			struct_size_t(struct nvme_fcp_op_w_sgl, priv,
				      ctrl->lport->ops->fcprqst_priv_sz));
	if (ret)
		return ret;

	ret = nvme_fc_create_hw_io_queues(ctrl, ctrl->ctrl.sqsize + 1);
	if (ret)
		goto out_cleanup_tagset;

	ret = nvme_fc_connect_io_queues(ctrl, ctrl->ctrl.sqsize + 1);
	if (ret)
		goto out_delete_hw_queues;

	ctrl->ioq_live = true;

	return 0;

out_delete_hw_queues:
	nvme_fc_delete_hw_io_queues(ctrl);
out_cleanup_tagset:
	nvme_remove_io_tag_set(&ctrl->ctrl);
	nvme_fc_free_io_queues(ctrl);

	/* force put free routine to ignore io queues */
	ctrl->ctrl.tagset = NULL;

	return ret;
}

static int
nvme_fc_recreate_io_queues(struct nvme_fc_ctrl *ctrl)
{
	struct nvmf_ctrl_options *opts = ctrl->ctrl.opts;
	u32 prior_ioq_cnt = ctrl->ctrl.queue_count - 1;
	unsigned int nr_io_queues;
	int ret;

	nr_io_queues = min(min(opts->nr_io_queues, num_online_cpus()),
				ctrl->lport->ops->max_hw_queues);
	ret = nvme_set_queue_count(&ctrl->ctrl, &nr_io_queues);
	if (ret) {
		dev_info(ctrl->ctrl.device,
			"set_queue_count failed: %d\n", ret);
		return ret;
	}

	if (!nr_io_queues && prior_ioq_cnt) {
		dev_info(ctrl->ctrl.device,
			"Fail Reconnect: At least 1 io queue "
			"required (was %d)\n", prior_ioq_cnt);
		return -ENOSPC;
	}

	ctrl->ctrl.queue_count = nr_io_queues + 1;
	/* check for io queues existing */
	if (ctrl->ctrl.queue_count == 1)
		return 0;

	if (prior_ioq_cnt != nr_io_queues) {
		dev_info(ctrl->ctrl.device,
			"reconnect: revising io queue count from %d to %d\n",
			prior_ioq_cnt, nr_io_queues);
		blk_mq_update_nr_hw_queues(&ctrl->tag_set, nr_io_queues);
	}

	ret = nvme_fc_create_hw_io_queues(ctrl, ctrl->ctrl.sqsize + 1);
	if (ret)
		goto out_free_io_queues;

	ret = nvme_fc_connect_io_queues(ctrl, ctrl->ctrl.sqsize + 1);
	if (ret)
		goto out_delete_hw_queues;

	return 0;

out_delete_hw_queues:
	nvme_fc_delete_hw_io_queues(ctrl);
out_free_io_queues:
	nvme_fc_free_io_queues(ctrl);
	return ret;
}

static void
nvme_fc_rport_active_on_lport(struct nvme_fc_rport *rport)
{
	struct nvme_fc_lport *lport = rport->lport;

	atomic_inc(&lport->act_rport_cnt);
}

static void
nvme_fc_rport_inactive_on_lport(struct nvme_fc_rport *rport)
{
	struct nvme_fc_lport *lport = rport->lport;
	u32 cnt;

	cnt = atomic_dec_return(&lport->act_rport_cnt);
	if (cnt == 0 && lport->localport.port_state == FC_OBJSTATE_DELETED)
		lport->ops->localport_delete(&lport->localport);
}

static int
nvme_fc_ctlr_active_on_rport(struct nvme_fc_ctrl *ctrl)
{
	struct nvme_fc_rport *rport = ctrl->rport;
	u32 cnt;

	if (test_and_set_bit(ASSOC_ACTIVE, &ctrl->flags))
		return 1;

	cnt = atomic_inc_return(&rport->act_ctrl_cnt);
	if (cnt == 1)
		nvme_fc_rport_active_on_lport(rport);

	return 0;
}

static int
nvme_fc_ctlr_inactive_on_rport(struct nvme_fc_ctrl *ctrl)
{
	struct nvme_fc_rport *rport = ctrl->rport;
	struct nvme_fc_lport *lport = rport->lport;
	u32 cnt;

	/* clearing of ctrl->flags ASSOC_ACTIVE bit is in association delete */

	cnt = atomic_dec_return(&rport->act_ctrl_cnt);
	if (cnt == 0) {
		if (rport->remoteport.port_state == FC_OBJSTATE_DELETED)
			lport->ops->remoteport_delete(&rport->remoteport);
		nvme_fc_rport_inactive_on_lport(rport);
	}

	return 0;
}

/*
 * This routine restarts the controller on the host side, and
 * on the link side, recreates the controller association.
 */
static int
nvme_fc_create_association(struct nvme_fc_ctrl *ctrl)
{
	struct nvmf_ctrl_options *opts = ctrl->ctrl.opts;
	struct nvmefc_ls_rcv_op *disls = NULL;
	unsigned long flags;
	int ret;
	bool changed;

	++ctrl->ctrl.nr_reconnects;

	if (ctrl->rport->remoteport.port_state != FC_OBJSTATE_ONLINE)
		return -ENODEV;

	if (nvme_fc_ctlr_active_on_rport(ctrl))
		return -ENOTUNIQ;

	dev_info(ctrl->ctrl.device,
		"NVME-FC{%d}: create association : host wwpn 0x%016llx "
		" rport wwpn 0x%016llx: NQN \"%s\"\n",
		ctrl->cnum, ctrl->lport->localport.port_name,
		ctrl->rport->remoteport.port_name, ctrl->ctrl.opts->subsysnqn);

	clear_bit(ASSOC_FAILED, &ctrl->flags);

	/*
	 * Create the admin queue
	 */

	ret = __nvme_fc_create_hw_queue(ctrl, &ctrl->queues[0], 0,
				NVME_AQ_DEPTH);
	if (ret)
		goto out_free_queue;

	ret = nvme_fc_connect_admin_queue(ctrl, &ctrl->queues[0],
				NVME_AQ_DEPTH, (NVME_AQ_DEPTH / 4));
	if (ret)
		goto out_delete_hw_queue;

	ret = nvmf_connect_admin_queue(&ctrl->ctrl);
	if (ret)
		goto out_disconnect_admin_queue;

	set_bit(NVME_FC_Q_LIVE, &ctrl->queues[0].flags);

	/*
	 * Check controller capabilities
	 *
	 * todo:- add code to check if ctrl attributes changed from
	 * prior connection values
	 */

	ret = nvme_enable_ctrl(&ctrl->ctrl);
	if (!ret && test_bit(ASSOC_FAILED, &ctrl->flags))
		ret = -EIO;
	if (ret)
		goto out_disconnect_admin_queue;

	ctrl->ctrl.max_segments = ctrl->lport->ops->max_sgl_segments;
	ctrl->ctrl.max_hw_sectors = ctrl->ctrl.max_segments <<
						(ilog2(SZ_4K) - 9);

	nvme_unquiesce_admin_queue(&ctrl->ctrl);

	ret = nvme_init_ctrl_finish(&ctrl->ctrl, false);
	if (ret)
		goto out_disconnect_admin_queue;
	if (test_bit(ASSOC_FAILED, &ctrl->flags)) {
		ret = -EIO;
		goto out_stop_keep_alive;
	}
	/* sanity checks */

	/* FC-NVME does not have other data in the capsule */
	if (ctrl->ctrl.icdoff) {
		dev_err(ctrl->ctrl.device, "icdoff %d is not supported!\n",
				ctrl->ctrl.icdoff);
		ret = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		goto out_stop_keep_alive;
	}

	/* FC-NVME supports normal SGL Data Block Descriptors */
	if (!nvme_ctrl_sgl_supported(&ctrl->ctrl)) {
		dev_err(ctrl->ctrl.device,
			"Mandatory sgls are not supported!\n");
		ret = NVME_SC_INVALID_FIELD | NVME_SC_DNR;
		goto out_stop_keep_alive;
	}

	if (opts->queue_size > ctrl->ctrl.maxcmd) {
		/* warn if maxcmd is lower than queue_size */
		dev_warn(ctrl->ctrl.device,
			"queue_size %zu > ctrl maxcmd %u, reducing "
			"to maxcmd\n",
			opts->queue_size, ctrl->ctrl.maxcmd);
		opts->queue_size = ctrl->ctrl.maxcmd;
		ctrl->ctrl.sqsize = opts->queue_size - 1;
	}

	ret = nvme_fc_init_aen_ops(ctrl);
	if (ret)
		goto out_term_aen_ops;

	/*
	 * Create the io queues
	 */

	if (ctrl->ctrl.queue_count > 1) {
		if (!ctrl->ioq_live)
			ret = nvme_fc_create_io_queues(ctrl);
		else
			ret = nvme_fc_recreate_io_queues(ctrl);
	}
	if (!ret && test_bit(ASSOC_FAILED, &ctrl->flags))
		ret = -EIO;
	if (ret)
		goto out_term_aen_ops;

	changed = nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_LIVE);

	ctrl->ctrl.nr_reconnects = 0;

	if (changed)
		nvme_start_ctrl(&ctrl->ctrl);

	return 0;	/* Success */

out_term_aen_ops:
	nvme_fc_term_aen_ops(ctrl);
out_stop_keep_alive:
	nvme_stop_keep_alive(&ctrl->ctrl);
out_disconnect_admin_queue:
	dev_warn(ctrl->ctrl.device,
		"NVME-FC{%d}: create_assoc failed, assoc_id %llx ret %d\n",
		ctrl->cnum, ctrl->association_id, ret);
	/* send a Disconnect(association) LS to fc-nvme target */
	nvme_fc_xmt_disconnect_assoc(ctrl);
	spin_lock_irqsave(&ctrl->lock, flags);
	ctrl->association_id = 0;
	disls = ctrl->rcv_disconn;
	ctrl->rcv_disconn = NULL;
	spin_unlock_irqrestore(&ctrl->lock, flags);
	if (disls)
		nvme_fc_xmt_ls_rsp(disls);
out_delete_hw_queue:
	__nvme_fc_delete_hw_queue(ctrl, &ctrl->queues[0], 0);
out_free_queue:
	nvme_fc_free_queue(&ctrl->queues[0]);
	clear_bit(ASSOC_ACTIVE, &ctrl->flags);
	nvme_fc_ctlr_inactive_on_rport(ctrl);

	return ret;
}


/*
 * This routine stops operation of the controller on the host side.
 * On the host os stack side: Admin and IO queues are stopped,
 *   outstanding ios on them terminated via FC ABTS.
 * On the link side: the association is terminated.
 */
static void
nvme_fc_delete_association(struct nvme_fc_ctrl *ctrl)
{
	struct nvmefc_ls_rcv_op *disls = NULL;
	unsigned long flags;

	if (!test_and_clear_bit(ASSOC_ACTIVE, &ctrl->flags))
		return;

	spin_lock_irqsave(&ctrl->lock, flags);
	set_bit(FCCTRL_TERMIO, &ctrl->flags);
	ctrl->iocnt = 0;
	spin_unlock_irqrestore(&ctrl->lock, flags);

	__nvme_fc_abort_outstanding_ios(ctrl, false);

	/* kill the aens as they are a separate path */
	nvme_fc_abort_aen_ops(ctrl);

	/* wait for all io that had to be aborted */
	spin_lock_irq(&ctrl->lock);
	wait_event_lock_irq(ctrl->ioabort_wait, ctrl->iocnt == 0, ctrl->lock);
	clear_bit(FCCTRL_TERMIO, &ctrl->flags);
	spin_unlock_irq(&ctrl->lock);

	nvme_fc_term_aen_ops(ctrl);

	/*
	 * send a Disconnect(association) LS to fc-nvme target
	 * Note: could have been sent at top of process, but
	 * cleaner on link traffic if after the aborts complete.
	 * Note: if association doesn't exist, association_id will be 0
	 */
	if (ctrl->association_id)
		nvme_fc_xmt_disconnect_assoc(ctrl);

	spin_lock_irqsave(&ctrl->lock, flags);
	ctrl->association_id = 0;
	disls = ctrl->rcv_disconn;
	ctrl->rcv_disconn = NULL;
	spin_unlock_irqrestore(&ctrl->lock, flags);
	if (disls)
		/*
		 * if a Disconnect Request was waiting for a response, send
		 * now that all ABTS's have been issued (and are complete).
		 */
		nvme_fc_xmt_ls_rsp(disls);

	if (ctrl->ctrl.tagset) {
		nvme_fc_delete_hw_io_queues(ctrl);
		nvme_fc_free_io_queues(ctrl);
	}

	__nvme_fc_delete_hw_queue(ctrl, &ctrl->queues[0], 0);
	nvme_fc_free_queue(&ctrl->queues[0]);

	/* re-enable the admin_q so anything new can fast fail */
	nvme_unquiesce_admin_queue(&ctrl->ctrl);

	/* resume the io queues so that things will fast fail */
	nvme_unquiesce_io_queues(&ctrl->ctrl);

	nvme_fc_ctlr_inactive_on_rport(ctrl);
}

static void
nvme_fc_delete_ctrl(struct nvme_ctrl *nctrl)
{
	struct nvme_fc_ctrl *ctrl = to_fc_ctrl(nctrl);

	cancel_work_sync(&ctrl->ioerr_work);
	cancel_delayed_work_sync(&ctrl->connect_work);
	/*
	 * kill the association on the link side.  this will block
	 * waiting for io to terminate
	 */
	nvme_fc_delete_association(ctrl);
}

static void
nvme_fc_reconnect_or_delete(struct nvme_fc_ctrl *ctrl, int status)
{
	struct nvme_fc_rport *rport = ctrl->rport;
	struct nvme_fc_remote_port *portptr = &rport->remoteport;
	unsigned long recon_delay = ctrl->ctrl.opts->reconnect_delay * HZ;
	bool recon = true;

	if (nvme_ctrl_state(&ctrl->ctrl) != NVME_CTRL_CONNECTING)
		return;

	if (portptr->port_state == FC_OBJSTATE_ONLINE) {
		dev_info(ctrl->ctrl.device,
			"NVME-FC{%d}: reset: Reconnect attempt failed (%d)\n",
			ctrl->cnum, status);
		if (status > 0 && (status & NVME_SC_DNR))
			recon = false;
	} else if (time_after_eq(jiffies, rport->dev_loss_end))
		recon = false;

	if (recon && nvmf_should_reconnect(&ctrl->ctrl)) {
		if (portptr->port_state == FC_OBJSTATE_ONLINE)
			dev_info(ctrl->ctrl.device,
				"NVME-FC{%d}: Reconnect attempt in %ld "
				"seconds\n",
				ctrl->cnum, recon_delay / HZ);
		else if (time_after(jiffies + recon_delay, rport->dev_loss_end))
			recon_delay = rport->dev_loss_end - jiffies;

		queue_delayed_work(nvme_wq, &ctrl->connect_work, recon_delay);
	} else {
		if (portptr->port_state == FC_OBJSTATE_ONLINE) {
			if (status > 0 && (status & NVME_SC_DNR))
				dev_warn(ctrl->ctrl.device,
					 "NVME-FC{%d}: reconnect failure\n",
					 ctrl->cnum);
			else
				dev_warn(ctrl->ctrl.device,
					 "NVME-FC{%d}: Max reconnect attempts "
					 "(%d) reached.\n",
					 ctrl->cnum, ctrl->ctrl.nr_reconnects);
		} else
			dev_warn(ctrl->ctrl.device,
				"NVME-FC{%d}: dev_loss_tmo (%d) expired "
				"while waiting for remoteport connectivity.\n",
				ctrl->cnum, min_t(int, portptr->dev_loss_tmo,
					(ctrl->ctrl.opts->max_reconnects *
					 ctrl->ctrl.opts->reconnect_delay)));
		WARN_ON(nvme_delete_ctrl(&ctrl->ctrl));
	}
}

static void
nvme_fc_reset_ctrl_work(struct work_struct *work)
{
	struct nvme_fc_ctrl *ctrl =
		container_of(work, struct nvme_fc_ctrl, ctrl.reset_work);

	nvme_stop_ctrl(&ctrl->ctrl);

	/* will block will waiting for io to terminate */
	nvme_fc_delete_association(ctrl);

	if (!nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_CONNECTING))
		dev_err(ctrl->ctrl.device,
			"NVME-FC{%d}: error_recovery: Couldn't change state "
			"to CONNECTING\n", ctrl->cnum);

	if (ctrl->rport->remoteport.port_state == FC_OBJSTATE_ONLINE) {
		if (!queue_delayed_work(nvme_wq, &ctrl->connect_work, 0)) {
			dev_err(ctrl->ctrl.device,
				"NVME-FC{%d}: failed to schedule connect "
				"after reset\n", ctrl->cnum);
		} else {
			flush_delayed_work(&ctrl->connect_work);
		}
	} else {
		nvme_fc_reconnect_or_delete(ctrl, -ENOTCONN);
	}
}


static const struct nvme_ctrl_ops nvme_fc_ctrl_ops = {
	.name			= "fc",
	.module			= THIS_MODULE,
	.flags			= NVME_F_FABRICS,
	.reg_read32		= nvmf_reg_read32,
	.reg_read64		= nvmf_reg_read64,
	.reg_write32		= nvmf_reg_write32,
	.free_ctrl		= nvme_fc_nvme_ctrl_freed,
	.submit_async_event	= nvme_fc_submit_async_event,
	.delete_ctrl		= nvme_fc_delete_ctrl,
	.get_address		= nvmf_get_address,
};

static void
nvme_fc_connect_ctrl_work(struct work_struct *work)
{
	int ret;

	struct nvme_fc_ctrl *ctrl =
			container_of(to_delayed_work(work),
				struct nvme_fc_ctrl, connect_work);

	ret = nvme_fc_create_association(ctrl);
	if (ret)
		nvme_fc_reconnect_or_delete(ctrl, ret);
	else
		dev_info(ctrl->ctrl.device,
			"NVME-FC{%d}: controller connect complete\n",
			ctrl->cnum);
}


static const struct blk_mq_ops nvme_fc_admin_mq_ops = {
	.queue_rq	= nvme_fc_queue_rq,
	.complete	= nvme_fc_complete_rq,
	.init_request	= nvme_fc_init_request,
	.exit_request	= nvme_fc_exit_request,
	.init_hctx	= nvme_fc_init_admin_hctx,
	.timeout	= nvme_fc_timeout,
};


/*
 * Fails a controller request if it matches an existing controller
 * (association) with the same tuple:
 * <Host NQN, Host ID, local FC port, remote FC port, SUBSYS NQN>
 *
 * The ports don't need to be compared as they are intrinsically
 * already matched by the port pointers supplied.
 */
static bool
nvme_fc_existing_controller(struct nvme_fc_rport *rport,
		struct nvmf_ctrl_options *opts)
{
	struct nvme_fc_ctrl *ctrl;
	unsigned long flags;
	bool found = false;

	spin_lock_irqsave(&rport->lock, flags);
	list_for_each_entry(ctrl, &rport->ctrl_list, ctrl_list) {
		found = nvmf_ctlr_matches_baseopts(&ctrl->ctrl, opts);
		if (found)
			break;
	}
	spin_unlock_irqrestore(&rport->lock, flags);

	return found;
}

static struct nvme_ctrl *
nvme_fc_init_ctrl(struct device *dev, struct nvmf_ctrl_options *opts,
	struct nvme_fc_lport *lport, struct nvme_fc_rport *rport)
{
	struct nvme_fc_ctrl *ctrl;
	unsigned long flags;
	int ret, idx, ctrl_loss_tmo;

	if (!(rport->remoteport.port_role &
	    (FC_PORT_ROLE_NVME_DISCOVERY | FC_PORT_ROLE_NVME_TARGET))) {
		ret = -EBADR;
		goto out_fail;
	}

	if (!opts->duplicate_connect &&
	    nvme_fc_existing_controller(rport, opts)) {
		ret = -EALREADY;
		goto out_fail;
	}

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl) {
		ret = -ENOMEM;
		goto out_fail;
	}

	idx = ida_alloc(&nvme_fc_ctrl_cnt, GFP_KERNEL);
	if (idx < 0) {
		ret = -ENOSPC;
		goto out_free_ctrl;
	}

	/*
	 * if ctrl_loss_tmo is being enforced and the default reconnect delay
	 * is being used, change to a shorter reconnect delay for FC.
	 */
	if (opts->max_reconnects != -1 &&
	    opts->reconnect_delay == NVMF_DEF_RECONNECT_DELAY &&
	    opts->reconnect_delay > NVME_FC_DEFAULT_RECONNECT_TMO) {
		ctrl_loss_tmo = opts->max_reconnects * opts->reconnect_delay;
		opts->reconnect_delay = NVME_FC_DEFAULT_RECONNECT_TMO;
		opts->max_reconnects = DIV_ROUND_UP(ctrl_loss_tmo,
						opts->reconnect_delay);
	}

	ctrl->ctrl.opts = opts;
	ctrl->ctrl.nr_reconnects = 0;
	if (lport->dev)
		ctrl->ctrl.numa_node = dev_to_node(lport->dev);
	else
		ctrl->ctrl.numa_node = NUMA_NO_NODE;
	INIT_LIST_HEAD(&ctrl->ctrl_list);
	ctrl->lport = lport;
	ctrl->rport = rport;
	ctrl->dev = lport->dev;
	ctrl->cnum = idx;
	ctrl->ioq_live = false;
	init_waitqueue_head(&ctrl->ioabort_wait);

	get_device(ctrl->dev);
	kref_init(&ctrl->ref);

	INIT_WORK(&ctrl->ctrl.reset_work, nvme_fc_reset_ctrl_work);
	INIT_DELAYED_WORK(&ctrl->connect_work, nvme_fc_connect_ctrl_work);
	INIT_WORK(&ctrl->ioerr_work, nvme_fc_ctrl_ioerr_work);
	spin_lock_init(&ctrl->lock);

	/* io queue count */
	ctrl->ctrl.queue_count = min_t(unsigned int,
				opts->nr_io_queues,
				lport->ops->max_hw_queues);
	ctrl->ctrl.queue_count++;	/* +1 for admin queue */

	ctrl->ctrl.sqsize = opts->queue_size - 1;
	ctrl->ctrl.kato = opts->kato;
	ctrl->ctrl.cntlid = 0xffff;

	ret = -ENOMEM;
	ctrl->queues = kcalloc(ctrl->ctrl.queue_count,
				sizeof(struct nvme_fc_queue), GFP_KERNEL);
	if (!ctrl->queues)
		goto out_free_ida;

	nvme_fc_init_queue(ctrl, 0);

	/*
	 * Would have been nice to init io queues tag set as well.
	 * However, we require interaction from the controller
	 * for max io queue count before we can do so.
	 * Defer this to the connect path.
	 */

	ret = nvme_init_ctrl(&ctrl->ctrl, dev, &nvme_fc_ctrl_ops, 0);
	if (ret)
		goto out_free_queues;

	/* at this point, teardown path changes to ref counting on nvme ctrl */

	ret = nvme_alloc_admin_tag_set(&ctrl->ctrl, &ctrl->admin_tag_set,
			&nvme_fc_admin_mq_ops,
			struct_size_t(struct nvme_fcp_op_w_sgl, priv,
				      ctrl->lport->ops->fcprqst_priv_sz));
	if (ret)
		goto fail_ctrl;

	spin_lock_irqsave(&rport->lock, flags);
	list_add_tail(&ctrl->ctrl_list, &rport->ctrl_list);
	spin_unlock_irqrestore(&rport->lock, flags);

	if (!nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_RESETTING) ||
	    !nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_CONNECTING)) {
		dev_err(ctrl->ctrl.device,
			"NVME-FC{%d}: failed to init ctrl state\n", ctrl->cnum);
		goto fail_ctrl;
	}

	if (!queue_delayed_work(nvme_wq, &ctrl->connect_work, 0)) {
		dev_err(ctrl->ctrl.device,
			"NVME-FC{%d}: failed to schedule initial connect\n",
			ctrl->cnum);
		goto fail_ctrl;
	}

	flush_delayed_work(&ctrl->connect_work);

	dev_info(ctrl->ctrl.device,
		"NVME-FC{%d}: new ctrl: NQN \"%s\"\n",
		ctrl->cnum, nvmf_ctrl_subsysnqn(&ctrl->ctrl));

	return &ctrl->ctrl;

fail_ctrl:
	nvme_change_ctrl_state(&ctrl->ctrl, NVME_CTRL_DELETING);
	cancel_work_sync(&ctrl->ioerr_work);
	cancel_work_sync(&ctrl->ctrl.reset_work);
	cancel_delayed_work_sync(&ctrl->connect_work);

	ctrl->ctrl.opts = NULL;

	/* initiate nvme ctrl ref counting teardown */
	nvme_uninit_ctrl(&ctrl->ctrl);

	/* Remove core ctrl ref. */
	nvme_put_ctrl(&ctrl->ctrl);

	/* as we're past the point where we transition to the ref
	 * counting teardown path, if we return a bad pointer here,
	 * the calling routine, thinking it's prior to the
	 * transition, will do an rport put. Since the teardown
	 * path also does a rport put, we do an extra get here to
	 * so proper order/teardown happens.
	 */
	nvme_fc_rport_get(rport);

	return ERR_PTR(-EIO);

out_free_queues:
	kfree(ctrl->queues);
out_free_ida:
	put_device(ctrl->dev);
	ida_free(&nvme_fc_ctrl_cnt, ctrl->cnum);
out_free_ctrl:
	kfree(ctrl);
out_fail:
	/* exit via here doesn't follow ctlr ref points */
	return ERR_PTR(ret);
}


struct nvmet_fc_traddr {
	u64	nn;
	u64	pn;
};

static int
__nvme_fc_parse_u64(substring_t *sstr, u64 *val)
{
	u64 token64;

	if (match_u64(sstr, &token64))
		return -EINVAL;
	*val = token64;

	return 0;
}

/*
 * This routine validates and extracts the WWN's from the TRADDR string.
 * As kernel parsers need the 0x to determine number base, universally
 * build string to parse with 0x prefix before parsing name strings.
 */
static int
nvme_fc_parse_traddr(struct nvmet_fc_traddr *traddr, char *buf, size_t blen)
{
	char name[2 + NVME_FC_TRADDR_HEXNAMELEN + 1];
	substring_t wwn = { name, &name[sizeof(name)-1] };
	int nnoffset, pnoffset;

	/* validate if string is one of the 2 allowed formats */
	if (strnlen(buf, blen) == NVME_FC_TRADDR_MAXLENGTH &&
			!strncmp(buf, "nn-0x", NVME_FC_TRADDR_OXNNLEN) &&
			!strncmp(&buf[NVME_FC_TRADDR_MAX_PN_OFFSET],
				"pn-0x", NVME_FC_TRADDR_OXNNLEN)) {
		nnoffset = NVME_FC_TRADDR_OXNNLEN;
		pnoffset = NVME_FC_TRADDR_MAX_PN_OFFSET +
						NVME_FC_TRADDR_OXNNLEN;
	} else if ((strnlen(buf, blen) == NVME_FC_TRADDR_MINLENGTH &&
			!strncmp(buf, "nn-", NVME_FC_TRADDR_NNLEN) &&
			!strncmp(&buf[NVME_FC_TRADDR_MIN_PN_OFFSET],
				"pn-", NVME_FC_TRADDR_NNLEN))) {
		nnoffset = NVME_FC_TRADDR_NNLEN;
		pnoffset = NVME_FC_TRADDR_MIN_PN_OFFSET + NVME_FC_TRADDR_NNLEN;
	} else
		goto out_einval;

	name[0] = '0';
	name[1] = 'x';
	name[2 + NVME_FC_TRADDR_HEXNAMELEN] = 0;

	memcpy(&name[2], &buf[nnoffset], NVME_FC_TRADDR_HEXNAMELEN);
	if (__nvme_fc_parse_u64(&wwn, &traddr->nn))
		goto out_einval;

	memcpy(&name[2], &buf[pnoffset], NVME_FC_TRADDR_HEXNAMELEN);
	if (__nvme_fc_parse_u64(&wwn, &traddr->pn))
		goto out_einval;

	return 0;

out_einval:
	pr_warn("%s: bad traddr string\n", __func__);
	return -EINVAL;
}

static struct nvme_ctrl *
nvme_fc_create_ctrl(struct device *dev, struct nvmf_ctrl_options *opts)
{
	struct nvme_fc_lport *lport;
	struct nvme_fc_rport *rport;
	struct nvme_ctrl *ctrl;
	struct nvmet_fc_traddr laddr = { 0L, 0L };
	struct nvmet_fc_traddr raddr = { 0L, 0L };
	unsigned long flags;
	int ret;

	ret = nvme_fc_parse_traddr(&raddr, opts->traddr, NVMF_TRADDR_SIZE);
	if (ret || !raddr.nn || !raddr.pn)
		return ERR_PTR(-EINVAL);

	ret = nvme_fc_parse_traddr(&laddr, opts->host_traddr, NVMF_TRADDR_SIZE);
	if (ret || !laddr.nn || !laddr.pn)
		return ERR_PTR(-EINVAL);

	/* find the host and remote ports to connect together */
	spin_lock_irqsave(&nvme_fc_lock, flags);
	list_for_each_entry(lport, &nvme_fc_lport_list, port_list) {
		if (lport->localport.node_name != laddr.nn ||
		    lport->localport.port_name != laddr.pn ||
		    lport->localport.port_state != FC_OBJSTATE_ONLINE)
			continue;

		list_for_each_entry(rport, &lport->endp_list, endp_list) {
			if (rport->remoteport.node_name != raddr.nn ||
			    rport->remoteport.port_name != raddr.pn ||
			    rport->remoteport.port_state != FC_OBJSTATE_ONLINE)
				continue;

			/* if fail to get reference fall through. Will error */
			if (!nvme_fc_rport_get(rport))
				break;

			spin_unlock_irqrestore(&nvme_fc_lock, flags);

			ctrl = nvme_fc_init_ctrl(dev, opts, lport, rport);
			if (IS_ERR(ctrl))
				nvme_fc_rport_put(rport);
			return ctrl;
		}
	}
	spin_unlock_irqrestore(&nvme_fc_lock, flags);

	pr_warn("%s: %s - %s combination not found\n",
		__func__, opts->traddr, opts->host_traddr);
	return ERR_PTR(-ENOENT);
}


static struct nvmf_transport_ops nvme_fc_transport = {
	.name		= "fc",
	.module		= THIS_MODULE,
	.required_opts	= NVMF_OPT_TRADDR | NVMF_OPT_HOST_TRADDR,
	.allowed_opts	= NVMF_OPT_RECONNECT_DELAY | NVMF_OPT_CTRL_LOSS_TMO,
	.create_ctrl	= nvme_fc_create_ctrl,
};

/* Arbitrary successive failures max. With lots of subsystems could be high */
#define DISCOVERY_MAX_FAIL	20

static ssize_t nvme_fc_nvme_discovery_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long flags;
	LIST_HEAD(local_disc_list);
	struct nvme_fc_lport *lport;
	struct nvme_fc_rport *rport;
	int failcnt = 0;

	spin_lock_irqsave(&nvme_fc_lock, flags);
restart:
	list_for_each_entry(lport, &nvme_fc_lport_list, port_list) {
		list_for_each_entry(rport, &lport->endp_list, endp_list) {
			if (!nvme_fc_lport_get(lport))
				continue;
			if (!nvme_fc_rport_get(rport)) {
				/*
				 * This is a temporary condition. Upon restart
				 * this rport will be gone from the list.
				 *
				 * Revert the lport put and retry.  Anything
				 * added to the list already will be skipped (as
				 * they are no longer list_empty).  Loops should
				 * resume at rports that were not yet seen.
				 */
				nvme_fc_lport_put(lport);

				if (failcnt++ < DISCOVERY_MAX_FAIL)
					goto restart;

				pr_err("nvme_discovery: too many reference "
				       "failures\n");
				goto process_local_list;
			}
			if (list_empty(&rport->disc_list))
				list_add_tail(&rport->disc_list,
					      &local_disc_list);
		}
	}

process_local_list:
	while (!list_empty(&local_disc_list)) {
		rport = list_first_entry(&local_disc_list,
					 struct nvme_fc_rport, disc_list);
		list_del_init(&rport->disc_list);
		spin_unlock_irqrestore(&nvme_fc_lock, flags);

		lport = rport->lport;
		/* signal discovery. Won't hurt if it repeats */
		nvme_fc_signal_discovery_scan(lport, rport);
		nvme_fc_rport_put(rport);
		nvme_fc_lport_put(lport);

		spin_lock_irqsave(&nvme_fc_lock, flags);
	}
	spin_unlock_irqrestore(&nvme_fc_lock, flags);

	return count;
}

static DEVICE_ATTR(nvme_discovery, 0200, NULL, nvme_fc_nvme_discovery_store);

#ifdef CONFIG_BLK_CGROUP_FC_APPID
/* Parse the cgroup id from a buf and return the length of cgrpid */
static int fc_parse_cgrpid(const char *buf, u64 *id)
{
	char cgrp_id[16+1];
	int cgrpid_len, j;

	memset(cgrp_id, 0x0, sizeof(cgrp_id));
	for (cgrpid_len = 0, j = 0; cgrpid_len < 17; cgrpid_len++) {
		if (buf[cgrpid_len] != ':')
			cgrp_id[cgrpid_len] = buf[cgrpid_len];
		else {
			j = 1;
			break;
		}
	}
	if (!j)
		return -EINVAL;
	if (kstrtou64(cgrp_id, 16, id) < 0)
		return -EINVAL;
	return cgrpid_len;
}

/*
 * Parse and update the appid in the blkcg associated with the cgroupid.
 */
static ssize_t fc_appid_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	size_t orig_count = count;
	u64 cgrp_id;
	int appid_len = 0;
	int cgrpid_len = 0;
	char app_id[FC_APPID_LEN];
	int ret = 0;

	if (buf[count-1] == '\n')
		count--;

	if ((count > (16+1+FC_APPID_LEN)) || (!strchr(buf, ':')))
		return -EINVAL;

	cgrpid_len = fc_parse_cgrpid(buf, &cgrp_id);
	if (cgrpid_len < 0)
		return -EINVAL;
	appid_len = count - cgrpid_len - 1;
	if (appid_len > FC_APPID_LEN)
		return -EINVAL;

	memset(app_id, 0x0, sizeof(app_id));
	memcpy(app_id, &buf[cgrpid_len+1], appid_len);
	ret = blkcg_set_fc_appid(app_id, cgrp_id, sizeof(app_id));
	if (ret < 0)
		return ret;
	return orig_count;
}
static DEVICE_ATTR(appid_store, 0200, NULL, fc_appid_store);
#endif /* CONFIG_BLK_CGROUP_FC_APPID */

static struct attribute *nvme_fc_attrs[] = {
	&dev_attr_nvme_discovery.attr,
#ifdef CONFIG_BLK_CGROUP_FC_APPID
	&dev_attr_appid_store.attr,
#endif
	NULL
};

static const struct attribute_group nvme_fc_attr_group = {
	.attrs = nvme_fc_attrs,
};

static const struct attribute_group *nvme_fc_attr_groups[] = {
	&nvme_fc_attr_group,
	NULL
};

static struct class fc_class = {
	.name = "fc",
	.dev_groups = nvme_fc_attr_groups,
};

static int __init nvme_fc_init_module(void)
{
	int ret;

	nvme_fc_wq = alloc_workqueue("nvme_fc_wq", WQ_MEM_RECLAIM, 0);
	if (!nvme_fc_wq)
		return -ENOMEM;

	/*
	 * NOTE:
	 * It is expected that in the future the kernel will combine
	 * the FC-isms that are currently under scsi and now being
	 * added to by NVME into a new standalone FC class. The SCSI
	 * and NVME protocols and their devices would be under this
	 * new FC class.
	 *
	 * As we need something to post FC-specific udev events to,
	 * specifically for nvme probe events, start by creating the
	 * new device class.  When the new standalone FC class is
	 * put in place, this code will move to a more generic
	 * location for the class.
	 */
	ret = class_register(&fc_class);
	if (ret) {
		pr_err("couldn't register class fc\n");
		goto out_destroy_wq;
	}

	/*
	 * Create a device for the FC-centric udev events
	 */
	fc_udev_device = device_create(&fc_class, NULL, MKDEV(0, 0), NULL,
				"fc_udev_device");
	if (IS_ERR(fc_udev_device)) {
		pr_err("couldn't create fc_udev device!\n");
		ret = PTR_ERR(fc_udev_device);
		goto out_destroy_class;
	}

	ret = nvmf_register_transport(&nvme_fc_transport);
	if (ret)
		goto out_destroy_device;

	return 0;

out_destroy_device:
	device_destroy(&fc_class, MKDEV(0, 0));
out_destroy_class:
	class_unregister(&fc_class);
out_destroy_wq:
	destroy_workqueue(nvme_fc_wq);

	return ret;
}

static void
nvme_fc_delete_controllers(struct nvme_fc_rport *rport)
{
	struct nvme_fc_ctrl *ctrl;

	spin_lock(&rport->lock);
	list_for_each_entry(ctrl, &rport->ctrl_list, ctrl_list) {
		dev_warn(ctrl->ctrl.device,
			"NVME-FC{%d}: transport unloading: deleting ctrl\n",
			ctrl->cnum);
		nvme_delete_ctrl(&ctrl->ctrl);
	}
	spin_unlock(&rport->lock);
}

static void
nvme_fc_cleanup_for_unload(void)
{
	struct nvme_fc_lport *lport;
	struct nvme_fc_rport *rport;

	list_for_each_entry(lport, &nvme_fc_lport_list, port_list) {
		list_for_each_entry(rport, &lport->endp_list, endp_list) {
			nvme_fc_delete_controllers(rport);
		}
	}
}

static void __exit nvme_fc_exit_module(void)
{
	unsigned long flags;
	bool need_cleanup = false;

	spin_lock_irqsave(&nvme_fc_lock, flags);
	nvme_fc_waiting_to_unload = true;
	if (!list_empty(&nvme_fc_lport_list)) {
		need_cleanup = true;
		nvme_fc_cleanup_for_unload();
	}
	spin_unlock_irqrestore(&nvme_fc_lock, flags);
	if (need_cleanup) {
		pr_info("%s: waiting for ctlr deletes\n", __func__);
		wait_for_completion(&nvme_fc_unload_proceed);
		pr_info("%s: ctrl deletes complete\n", __func__);
	}

	nvmf_unregister_transport(&nvme_fc_transport);

	ida_destroy(&nvme_fc_local_port_cnt);
	ida_destroy(&nvme_fc_ctrl_cnt);

	device_destroy(&fc_class, MKDEV(0, 0));
	class_unregister(&fc_class);
	destroy_workqueue(nvme_fc_wq);
}

module_init(nvme_fc_init_module);
module_exit(nvme_fc_exit_module);

MODULE_LICENSE("GPL v2");
