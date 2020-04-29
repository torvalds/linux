// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 Avago Technologies.  All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/parser.h>
#include <uapi/scsi/fc/fc_fs.h>

#include "../host/nvme.h"
#include "../target/nvmet.h"
#include <linux/nvme-fc-driver.h>
#include <linux/nvme-fc.h>


enum {
	NVMF_OPT_ERR		= 0,
	NVMF_OPT_WWNN		= 1 << 0,
	NVMF_OPT_WWPN		= 1 << 1,
	NVMF_OPT_ROLES		= 1 << 2,
	NVMF_OPT_FCADDR		= 1 << 3,
	NVMF_OPT_LPWWNN		= 1 << 4,
	NVMF_OPT_LPWWPN		= 1 << 5,
};

struct fcloop_ctrl_options {
	int			mask;
	u64			wwnn;
	u64			wwpn;
	u32			roles;
	u32			fcaddr;
	u64			lpwwnn;
	u64			lpwwpn;
};

static const match_table_t opt_tokens = {
	{ NVMF_OPT_WWNN,	"wwnn=%s"	},
	{ NVMF_OPT_WWPN,	"wwpn=%s"	},
	{ NVMF_OPT_ROLES,	"roles=%d"	},
	{ NVMF_OPT_FCADDR,	"fcaddr=%x"	},
	{ NVMF_OPT_LPWWNN,	"lpwwnn=%s"	},
	{ NVMF_OPT_LPWWPN,	"lpwwpn=%s"	},
	{ NVMF_OPT_ERR,		NULL		}
};

static int
fcloop_parse_options(struct fcloop_ctrl_options *opts,
		const char *buf)
{
	substring_t args[MAX_OPT_ARGS];
	char *options, *o, *p;
	int token, ret = 0;
	u64 token64;

	options = o = kstrdup(buf, GFP_KERNEL);
	if (!options)
		return -ENOMEM;

	while ((p = strsep(&o, ",\n")) != NULL) {
		if (!*p)
			continue;

		token = match_token(p, opt_tokens, args);
		opts->mask |= token;
		switch (token) {
		case NVMF_OPT_WWNN:
			if (match_u64(args, &token64)) {
				ret = -EINVAL;
				goto out_free_options;
			}
			opts->wwnn = token64;
			break;
		case NVMF_OPT_WWPN:
			if (match_u64(args, &token64)) {
				ret = -EINVAL;
				goto out_free_options;
			}
			opts->wwpn = token64;
			break;
		case NVMF_OPT_ROLES:
			if (match_int(args, &token)) {
				ret = -EINVAL;
				goto out_free_options;
			}
			opts->roles = token;
			break;
		case NVMF_OPT_FCADDR:
			if (match_hex(args, &token)) {
				ret = -EINVAL;
				goto out_free_options;
			}
			opts->fcaddr = token;
			break;
		case NVMF_OPT_LPWWNN:
			if (match_u64(args, &token64)) {
				ret = -EINVAL;
				goto out_free_options;
			}
			opts->lpwwnn = token64;
			break;
		case NVMF_OPT_LPWWPN:
			if (match_u64(args, &token64)) {
				ret = -EINVAL;
				goto out_free_options;
			}
			opts->lpwwpn = token64;
			break;
		default:
			pr_warn("unknown parameter or missing value '%s'\n", p);
			ret = -EINVAL;
			goto out_free_options;
		}
	}

out_free_options:
	kfree(options);
	return ret;
}


static int
fcloop_parse_nm_options(struct device *dev, u64 *nname, u64 *pname,
		const char *buf)
{
	substring_t args[MAX_OPT_ARGS];
	char *options, *o, *p;
	int token, ret = 0;
	u64 token64;

	*nname = -1;
	*pname = -1;

	options = o = kstrdup(buf, GFP_KERNEL);
	if (!options)
		return -ENOMEM;

	while ((p = strsep(&o, ",\n")) != NULL) {
		if (!*p)
			continue;

		token = match_token(p, opt_tokens, args);
		switch (token) {
		case NVMF_OPT_WWNN:
			if (match_u64(args, &token64)) {
				ret = -EINVAL;
				goto out_free_options;
			}
			*nname = token64;
			break;
		case NVMF_OPT_WWPN:
			if (match_u64(args, &token64)) {
				ret = -EINVAL;
				goto out_free_options;
			}
			*pname = token64;
			break;
		default:
			pr_warn("unknown parameter or missing value '%s'\n", p);
			ret = -EINVAL;
			goto out_free_options;
		}
	}

out_free_options:
	kfree(options);

	if (!ret) {
		if (*nname == -1)
			return -EINVAL;
		if (*pname == -1)
			return -EINVAL;
	}

	return ret;
}


#define LPORT_OPTS	(NVMF_OPT_WWNN | NVMF_OPT_WWPN)

#define RPORT_OPTS	(NVMF_OPT_WWNN | NVMF_OPT_WWPN |  \
			 NVMF_OPT_LPWWNN | NVMF_OPT_LPWWPN)

#define TGTPORT_OPTS	(NVMF_OPT_WWNN | NVMF_OPT_WWPN)


static DEFINE_SPINLOCK(fcloop_lock);
static LIST_HEAD(fcloop_lports);
static LIST_HEAD(fcloop_nports);

struct fcloop_lport {
	struct nvme_fc_local_port *localport;
	struct list_head lport_list;
	struct completion unreg_done;
};

struct fcloop_lport_priv {
	struct fcloop_lport *lport;
};

struct fcloop_rport {
	struct nvme_fc_remote_port	*remoteport;
	struct nvmet_fc_target_port	*targetport;
	struct fcloop_nport		*nport;
	struct fcloop_lport		*lport;
	spinlock_t			lock;
	struct list_head		ls_list;
	struct work_struct		ls_work;
};

struct fcloop_tport {
	struct nvmet_fc_target_port *targetport;
	struct nvme_fc_remote_port *remoteport;
	struct fcloop_nport *nport;
	struct fcloop_lport *lport;
};

struct fcloop_nport {
	struct fcloop_rport *rport;
	struct fcloop_tport *tport;
	struct fcloop_lport *lport;
	struct list_head nport_list;
	struct kref ref;
	u64 node_name;
	u64 port_name;
	u32 port_role;
	u32 port_id;
};

struct fcloop_lsreq {
	struct nvmefc_ls_req		*lsreq;
	struct nvmefc_tgt_ls_req	tgt_ls_req;
	int				status;
	struct list_head		ls_list; /* fcloop_rport->ls_list */
};

struct fcloop_rscn {
	struct fcloop_tport		*tport;
	struct work_struct		work;
};

enum {
	INI_IO_START		= 0,
	INI_IO_ACTIVE		= 1,
	INI_IO_ABORTED		= 2,
	INI_IO_COMPLETED	= 3,
};

struct fcloop_fcpreq {
	struct fcloop_tport		*tport;
	struct nvmefc_fcp_req		*fcpreq;
	spinlock_t			reqlock;
	u16				status;
	u32				inistate;
	bool				active;
	bool				aborted;
	struct kref			ref;
	struct work_struct		fcp_rcv_work;
	struct work_struct		abort_rcv_work;
	struct work_struct		tio_done_work;
	struct nvmefc_tgt_fcp_req	tgt_fcp_req;
};

struct fcloop_ini_fcpreq {
	struct nvmefc_fcp_req		*fcpreq;
	struct fcloop_fcpreq		*tfcp_req;
	spinlock_t			inilock;
};

static inline struct fcloop_lsreq *
tgt_ls_req_to_lsreq(struct nvmefc_tgt_ls_req *tgt_lsreq)
{
	return container_of(tgt_lsreq, struct fcloop_lsreq, tgt_ls_req);
}

static inline struct fcloop_fcpreq *
tgt_fcp_req_to_fcpreq(struct nvmefc_tgt_fcp_req *tgt_fcpreq)
{
	return container_of(tgt_fcpreq, struct fcloop_fcpreq, tgt_fcp_req);
}


static int
fcloop_create_queue(struct nvme_fc_local_port *localport,
			unsigned int qidx, u16 qsize,
			void **handle)
{
	*handle = localport;
	return 0;
}

static void
fcloop_delete_queue(struct nvme_fc_local_port *localport,
			unsigned int idx, void *handle)
{
}

static void
fcloop_rport_lsrqst_work(struct work_struct *work)
{
	struct fcloop_rport *rport =
		container_of(work, struct fcloop_rport, ls_work);
	struct fcloop_lsreq *tls_req;

	spin_lock(&rport->lock);
	for (;;) {
		tls_req = list_first_entry_or_null(&rport->ls_list,
				struct fcloop_lsreq, ls_list);
		if (!tls_req)
			break;

		list_del(&tls_req->ls_list);
		spin_unlock(&rport->lock);

		tls_req->lsreq->done(tls_req->lsreq, tls_req->status);
		/*
		 * callee may free memory containing tls_req.
		 * do not reference lsreq after this.
		 */

		spin_lock(&rport->lock);
	}
	spin_unlock(&rport->lock);
}

static int
fcloop_ls_req(struct nvme_fc_local_port *localport,
			struct nvme_fc_remote_port *remoteport,
			struct nvmefc_ls_req *lsreq)
{
	struct fcloop_lsreq *tls_req = lsreq->private;
	struct fcloop_rport *rport = remoteport->private;
	int ret = 0;

	tls_req->lsreq = lsreq;
	INIT_LIST_HEAD(&tls_req->ls_list);

	if (!rport->targetport) {
		tls_req->status = -ECONNREFUSED;
		spin_lock(&rport->lock);
		list_add_tail(&rport->ls_list, &tls_req->ls_list);
		spin_unlock(&rport->lock);
		schedule_work(&rport->ls_work);
		return ret;
	}

	tls_req->status = 0;
	ret = nvmet_fc_rcv_ls_req(rport->targetport, &tls_req->tgt_ls_req,
				 lsreq->rqstaddr, lsreq->rqstlen);

	return ret;
}

static int
fcloop_xmt_ls_rsp(struct nvmet_fc_target_port *targetport,
			struct nvmefc_tgt_ls_req *tgt_lsreq)
{
	struct fcloop_lsreq *tls_req = tgt_ls_req_to_lsreq(tgt_lsreq);
	struct nvmefc_ls_req *lsreq = tls_req->lsreq;
	struct fcloop_tport *tport = targetport->private;
	struct nvme_fc_remote_port *remoteport = tport->remoteport;
	struct fcloop_rport *rport;

	memcpy(lsreq->rspaddr, tgt_lsreq->rspbuf,
		((lsreq->rsplen < tgt_lsreq->rsplen) ?
				lsreq->rsplen : tgt_lsreq->rsplen));

	tgt_lsreq->done(tgt_lsreq);

	if (remoteport) {
		rport = remoteport->private;
		spin_lock(&rport->lock);
		list_add_tail(&rport->ls_list, &tls_req->ls_list);
		spin_unlock(&rport->lock);
		schedule_work(&rport->ls_work);
	}

	return 0;
}

/*
 * Simulate reception of RSCN and converting it to a initiator transport
 * call to rescan a remote port.
 */
static void
fcloop_tgt_rscn_work(struct work_struct *work)
{
	struct fcloop_rscn *tgt_rscn =
		container_of(work, struct fcloop_rscn, work);
	struct fcloop_tport *tport = tgt_rscn->tport;

	if (tport->remoteport)
		nvme_fc_rescan_remoteport(tport->remoteport);
	kfree(tgt_rscn);
}

static void
fcloop_tgt_discovery_evt(struct nvmet_fc_target_port *tgtport)
{
	struct fcloop_rscn *tgt_rscn;

	tgt_rscn = kzalloc(sizeof(*tgt_rscn), GFP_KERNEL);
	if (!tgt_rscn)
		return;

	tgt_rscn->tport = tgtport->private;
	INIT_WORK(&tgt_rscn->work, fcloop_tgt_rscn_work);

	schedule_work(&tgt_rscn->work);
}

static void
fcloop_tfcp_req_free(struct kref *ref)
{
	struct fcloop_fcpreq *tfcp_req =
		container_of(ref, struct fcloop_fcpreq, ref);

	kfree(tfcp_req);
}

static void
fcloop_tfcp_req_put(struct fcloop_fcpreq *tfcp_req)
{
	kref_put(&tfcp_req->ref, fcloop_tfcp_req_free);
}

static int
fcloop_tfcp_req_get(struct fcloop_fcpreq *tfcp_req)
{
	return kref_get_unless_zero(&tfcp_req->ref);
}

static void
fcloop_call_host_done(struct nvmefc_fcp_req *fcpreq,
			struct fcloop_fcpreq *tfcp_req, int status)
{
	struct fcloop_ini_fcpreq *inireq = NULL;

	if (fcpreq) {
		inireq = fcpreq->private;
		spin_lock(&inireq->inilock);
		inireq->tfcp_req = NULL;
		spin_unlock(&inireq->inilock);

		fcpreq->status = status;
		fcpreq->done(fcpreq);
	}

	/* release original io reference on tgt struct */
	fcloop_tfcp_req_put(tfcp_req);
}

static void
fcloop_fcp_recv_work(struct work_struct *work)
{
	struct fcloop_fcpreq *tfcp_req =
		container_of(work, struct fcloop_fcpreq, fcp_rcv_work);
	struct nvmefc_fcp_req *fcpreq = tfcp_req->fcpreq;
	int ret = 0;
	bool aborted = false;

	spin_lock_irq(&tfcp_req->reqlock);
	switch (tfcp_req->inistate) {
	case INI_IO_START:
		tfcp_req->inistate = INI_IO_ACTIVE;
		break;
	case INI_IO_ABORTED:
		aborted = true;
		break;
	default:
		spin_unlock_irq(&tfcp_req->reqlock);
		WARN_ON(1);
		return;
	}
	spin_unlock_irq(&tfcp_req->reqlock);

	if (unlikely(aborted))
		ret = -ECANCELED;
	else
		ret = nvmet_fc_rcv_fcp_req(tfcp_req->tport->targetport,
				&tfcp_req->tgt_fcp_req,
				fcpreq->cmdaddr, fcpreq->cmdlen);
	if (ret)
		fcloop_call_host_done(fcpreq, tfcp_req, ret);

	return;
}

static void
fcloop_fcp_abort_recv_work(struct work_struct *work)
{
	struct fcloop_fcpreq *tfcp_req =
		container_of(work, struct fcloop_fcpreq, abort_rcv_work);
	struct nvmefc_fcp_req *fcpreq;
	bool completed = false;

	spin_lock_irq(&tfcp_req->reqlock);
	fcpreq = tfcp_req->fcpreq;
	switch (tfcp_req->inistate) {
	case INI_IO_ABORTED:
		break;
	case INI_IO_COMPLETED:
		completed = true;
		break;
	default:
		spin_unlock_irq(&tfcp_req->reqlock);
		WARN_ON(1);
		return;
	}
	spin_unlock_irq(&tfcp_req->reqlock);

	if (unlikely(completed)) {
		/* remove reference taken in original abort downcall */
		fcloop_tfcp_req_put(tfcp_req);
		return;
	}

	if (tfcp_req->tport->targetport)
		nvmet_fc_rcv_fcp_abort(tfcp_req->tport->targetport,
					&tfcp_req->tgt_fcp_req);

	spin_lock_irq(&tfcp_req->reqlock);
	tfcp_req->fcpreq = NULL;
	spin_unlock_irq(&tfcp_req->reqlock);

	fcloop_call_host_done(fcpreq, tfcp_req, -ECANCELED);
	/* call_host_done releases reference for abort downcall */
}

/*
 * FCP IO operation done by target completion.
 * call back up initiator "done" flows.
 */
static void
fcloop_tgt_fcprqst_done_work(struct work_struct *work)
{
	struct fcloop_fcpreq *tfcp_req =
		container_of(work, struct fcloop_fcpreq, tio_done_work);
	struct nvmefc_fcp_req *fcpreq;

	spin_lock_irq(&tfcp_req->reqlock);
	fcpreq = tfcp_req->fcpreq;
	tfcp_req->inistate = INI_IO_COMPLETED;
	spin_unlock_irq(&tfcp_req->reqlock);

	fcloop_call_host_done(fcpreq, tfcp_req, tfcp_req->status);
}


static int
fcloop_fcp_req(struct nvme_fc_local_port *localport,
			struct nvme_fc_remote_port *remoteport,
			void *hw_queue_handle,
			struct nvmefc_fcp_req *fcpreq)
{
	struct fcloop_rport *rport = remoteport->private;
	struct fcloop_ini_fcpreq *inireq = fcpreq->private;
	struct fcloop_fcpreq *tfcp_req;

	if (!rport->targetport)
		return -ECONNREFUSED;

	tfcp_req = kzalloc(sizeof(*tfcp_req), GFP_ATOMIC);
	if (!tfcp_req)
		return -ENOMEM;

	inireq->fcpreq = fcpreq;
	inireq->tfcp_req = tfcp_req;
	spin_lock_init(&inireq->inilock);

	tfcp_req->fcpreq = fcpreq;
	tfcp_req->tport = rport->targetport->private;
	tfcp_req->inistate = INI_IO_START;
	spin_lock_init(&tfcp_req->reqlock);
	INIT_WORK(&tfcp_req->fcp_rcv_work, fcloop_fcp_recv_work);
	INIT_WORK(&tfcp_req->abort_rcv_work, fcloop_fcp_abort_recv_work);
	INIT_WORK(&tfcp_req->tio_done_work, fcloop_tgt_fcprqst_done_work);
	kref_init(&tfcp_req->ref);

	schedule_work(&tfcp_req->fcp_rcv_work);

	return 0;
}

static void
fcloop_fcp_copy_data(u8 op, struct scatterlist *data_sg,
			struct scatterlist *io_sg, u32 offset, u32 length)
{
	void *data_p, *io_p;
	u32 data_len, io_len, tlen;

	io_p = sg_virt(io_sg);
	io_len = io_sg->length;

	for ( ; offset; ) {
		tlen = min_t(u32, offset, io_len);
		offset -= tlen;
		io_len -= tlen;
		if (!io_len) {
			io_sg = sg_next(io_sg);
			io_p = sg_virt(io_sg);
			io_len = io_sg->length;
		} else
			io_p += tlen;
	}

	data_p = sg_virt(data_sg);
	data_len = data_sg->length;

	for ( ; length; ) {
		tlen = min_t(u32, io_len, data_len);
		tlen = min_t(u32, tlen, length);

		if (op == NVMET_FCOP_WRITEDATA)
			memcpy(data_p, io_p, tlen);
		else
			memcpy(io_p, data_p, tlen);

		length -= tlen;

		io_len -= tlen;
		if ((!io_len) && (length)) {
			io_sg = sg_next(io_sg);
			io_p = sg_virt(io_sg);
			io_len = io_sg->length;
		} else
			io_p += tlen;

		data_len -= tlen;
		if ((!data_len) && (length)) {
			data_sg = sg_next(data_sg);
			data_p = sg_virt(data_sg);
			data_len = data_sg->length;
		} else
			data_p += tlen;
	}
}

static int
fcloop_fcp_op(struct nvmet_fc_target_port *tgtport,
			struct nvmefc_tgt_fcp_req *tgt_fcpreq)
{
	struct fcloop_fcpreq *tfcp_req = tgt_fcp_req_to_fcpreq(tgt_fcpreq);
	struct nvmefc_fcp_req *fcpreq;
	u32 rsplen = 0, xfrlen = 0;
	int fcp_err = 0, active, aborted;
	u8 op = tgt_fcpreq->op;

	spin_lock_irq(&tfcp_req->reqlock);
	fcpreq = tfcp_req->fcpreq;
	active = tfcp_req->active;
	aborted = tfcp_req->aborted;
	tfcp_req->active = true;
	spin_unlock_irq(&tfcp_req->reqlock);

	if (unlikely(active))
		/* illegal - call while i/o active */
		return -EALREADY;

	if (unlikely(aborted)) {
		/* target transport has aborted i/o prior */
		spin_lock_irq(&tfcp_req->reqlock);
		tfcp_req->active = false;
		spin_unlock_irq(&tfcp_req->reqlock);
		tgt_fcpreq->transferred_length = 0;
		tgt_fcpreq->fcp_error = -ECANCELED;
		tgt_fcpreq->done(tgt_fcpreq);
		return 0;
	}

	/*
	 * if fcpreq is NULL, the I/O has been aborted (from
	 * initiator side). For the target side, act as if all is well
	 * but don't actually move data.
	 */

	switch (op) {
	case NVMET_FCOP_WRITEDATA:
		xfrlen = tgt_fcpreq->transfer_length;
		if (fcpreq) {
			fcloop_fcp_copy_data(op, tgt_fcpreq->sg,
					fcpreq->first_sgl, tgt_fcpreq->offset,
					xfrlen);
			fcpreq->transferred_length += xfrlen;
		}
		break;

	case NVMET_FCOP_READDATA:
	case NVMET_FCOP_READDATA_RSP:
		xfrlen = tgt_fcpreq->transfer_length;
		if (fcpreq) {
			fcloop_fcp_copy_data(op, tgt_fcpreq->sg,
					fcpreq->first_sgl, tgt_fcpreq->offset,
					xfrlen);
			fcpreq->transferred_length += xfrlen;
		}
		if (op == NVMET_FCOP_READDATA)
			break;

		/* Fall-Thru to RSP handling */
		/* FALLTHRU */

	case NVMET_FCOP_RSP:
		if (fcpreq) {
			rsplen = ((fcpreq->rsplen < tgt_fcpreq->rsplen) ?
					fcpreq->rsplen : tgt_fcpreq->rsplen);
			memcpy(fcpreq->rspaddr, tgt_fcpreq->rspaddr, rsplen);
			if (rsplen < tgt_fcpreq->rsplen)
				fcp_err = -E2BIG;
			fcpreq->rcv_rsplen = rsplen;
			fcpreq->status = 0;
		}
		tfcp_req->status = 0;
		break;

	default:
		fcp_err = -EINVAL;
		break;
	}

	spin_lock_irq(&tfcp_req->reqlock);
	tfcp_req->active = false;
	spin_unlock_irq(&tfcp_req->reqlock);

	tgt_fcpreq->transferred_length = xfrlen;
	tgt_fcpreq->fcp_error = fcp_err;
	tgt_fcpreq->done(tgt_fcpreq);

	return 0;
}

static void
fcloop_tgt_fcp_abort(struct nvmet_fc_target_port *tgtport,
			struct nvmefc_tgt_fcp_req *tgt_fcpreq)
{
	struct fcloop_fcpreq *tfcp_req = tgt_fcp_req_to_fcpreq(tgt_fcpreq);

	/*
	 * mark aborted only in case there were 2 threads in transport
	 * (one doing io, other doing abort) and only kills ops posted
	 * after the abort request
	 */
	spin_lock_irq(&tfcp_req->reqlock);
	tfcp_req->aborted = true;
	spin_unlock_irq(&tfcp_req->reqlock);

	tfcp_req->status = NVME_SC_INTERNAL;

	/*
	 * nothing more to do. If io wasn't active, the transport should
	 * immediately call the req_release. If it was active, the op
	 * will complete, and the lldd should call req_release.
	 */
}

static void
fcloop_fcp_req_release(struct nvmet_fc_target_port *tgtport,
			struct nvmefc_tgt_fcp_req *tgt_fcpreq)
{
	struct fcloop_fcpreq *tfcp_req = tgt_fcp_req_to_fcpreq(tgt_fcpreq);

	schedule_work(&tfcp_req->tio_done_work);
}

static void
fcloop_ls_abort(struct nvme_fc_local_port *localport,
			struct nvme_fc_remote_port *remoteport,
				struct nvmefc_ls_req *lsreq)
{
}

static void
fcloop_fcp_abort(struct nvme_fc_local_port *localport,
			struct nvme_fc_remote_port *remoteport,
			void *hw_queue_handle,
			struct nvmefc_fcp_req *fcpreq)
{
	struct fcloop_ini_fcpreq *inireq = fcpreq->private;
	struct fcloop_fcpreq *tfcp_req;
	bool abortio = true;

	spin_lock(&inireq->inilock);
	tfcp_req = inireq->tfcp_req;
	if (tfcp_req)
		fcloop_tfcp_req_get(tfcp_req);
	spin_unlock(&inireq->inilock);

	if (!tfcp_req)
		/* abort has already been called */
		return;

	/* break initiator/target relationship for io */
	spin_lock_irq(&tfcp_req->reqlock);
	switch (tfcp_req->inistate) {
	case INI_IO_START:
	case INI_IO_ACTIVE:
		tfcp_req->inistate = INI_IO_ABORTED;
		break;
	case INI_IO_COMPLETED:
		abortio = false;
		break;
	default:
		spin_unlock_irq(&tfcp_req->reqlock);
		WARN_ON(1);
		return;
	}
	spin_unlock_irq(&tfcp_req->reqlock);

	if (abortio)
		/* leave the reference while the work item is scheduled */
		WARN_ON(!schedule_work(&tfcp_req->abort_rcv_work));
	else  {
		/*
		 * as the io has already had the done callback made,
		 * nothing more to do. So release the reference taken above
		 */
		fcloop_tfcp_req_put(tfcp_req);
	}
}

static void
fcloop_nport_free(struct kref *ref)
{
	struct fcloop_nport *nport =
		container_of(ref, struct fcloop_nport, ref);
	unsigned long flags;

	spin_lock_irqsave(&fcloop_lock, flags);
	list_del(&nport->nport_list);
	spin_unlock_irqrestore(&fcloop_lock, flags);

	kfree(nport);
}

static void
fcloop_nport_put(struct fcloop_nport *nport)
{
	kref_put(&nport->ref, fcloop_nport_free);
}

static int
fcloop_nport_get(struct fcloop_nport *nport)
{
	return kref_get_unless_zero(&nport->ref);
}

static void
fcloop_localport_delete(struct nvme_fc_local_port *localport)
{
	struct fcloop_lport_priv *lport_priv = localport->private;
	struct fcloop_lport *lport = lport_priv->lport;

	/* release any threads waiting for the unreg to complete */
	complete(&lport->unreg_done);
}

static void
fcloop_remoteport_delete(struct nvme_fc_remote_port *remoteport)
{
	struct fcloop_rport *rport = remoteport->private;

	flush_work(&rport->ls_work);
	fcloop_nport_put(rport->nport);
}

static void
fcloop_targetport_delete(struct nvmet_fc_target_port *targetport)
{
	struct fcloop_tport *tport = targetport->private;

	fcloop_nport_put(tport->nport);
}

#define	FCLOOP_HW_QUEUES		4
#define	FCLOOP_SGL_SEGS			256
#define FCLOOP_DMABOUND_4G		0xFFFFFFFF

static struct nvme_fc_port_template fctemplate = {
	.localport_delete	= fcloop_localport_delete,
	.remoteport_delete	= fcloop_remoteport_delete,
	.create_queue		= fcloop_create_queue,
	.delete_queue		= fcloop_delete_queue,
	.ls_req			= fcloop_ls_req,
	.fcp_io			= fcloop_fcp_req,
	.ls_abort		= fcloop_ls_abort,
	.fcp_abort		= fcloop_fcp_abort,
	.max_hw_queues		= FCLOOP_HW_QUEUES,
	.max_sgl_segments	= FCLOOP_SGL_SEGS,
	.max_dif_sgl_segments	= FCLOOP_SGL_SEGS,
	.dma_boundary		= FCLOOP_DMABOUND_4G,
	/* sizes of additional private data for data structures */
	.local_priv_sz		= sizeof(struct fcloop_lport_priv),
	.remote_priv_sz		= sizeof(struct fcloop_rport),
	.lsrqst_priv_sz		= sizeof(struct fcloop_lsreq),
	.fcprqst_priv_sz	= sizeof(struct fcloop_ini_fcpreq),
};

static struct nvmet_fc_target_template tgttemplate = {
	.targetport_delete	= fcloop_targetport_delete,
	.xmt_ls_rsp		= fcloop_xmt_ls_rsp,
	.fcp_op			= fcloop_fcp_op,
	.fcp_abort		= fcloop_tgt_fcp_abort,
	.fcp_req_release	= fcloop_fcp_req_release,
	.discovery_event	= fcloop_tgt_discovery_evt,
	.max_hw_queues		= FCLOOP_HW_QUEUES,
	.max_sgl_segments	= FCLOOP_SGL_SEGS,
	.max_dif_sgl_segments	= FCLOOP_SGL_SEGS,
	.dma_boundary		= FCLOOP_DMABOUND_4G,
	/* optional features */
	.target_features	= 0,
	/* sizes of additional private data for data structures */
	.target_priv_sz		= sizeof(struct fcloop_tport),
};

static ssize_t
fcloop_create_local_port(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct nvme_fc_port_info pinfo;
	struct fcloop_ctrl_options *opts;
	struct nvme_fc_local_port *localport;
	struct fcloop_lport *lport;
	struct fcloop_lport_priv *lport_priv;
	unsigned long flags;
	int ret = -ENOMEM;

	lport = kzalloc(sizeof(*lport), GFP_KERNEL);
	if (!lport)
		return -ENOMEM;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		goto out_free_lport;

	ret = fcloop_parse_options(opts, buf);
	if (ret)
		goto out_free_opts;

	/* everything there ? */
	if ((opts->mask & LPORT_OPTS) != LPORT_OPTS) {
		ret = -EINVAL;
		goto out_free_opts;
	}

	memset(&pinfo, 0, sizeof(pinfo));
	pinfo.node_name = opts->wwnn;
	pinfo.port_name = opts->wwpn;
	pinfo.port_role = opts->roles;
	pinfo.port_id = opts->fcaddr;

	ret = nvme_fc_register_localport(&pinfo, &fctemplate, NULL, &localport);
	if (!ret) {
		/* success */
		lport_priv = localport->private;
		lport_priv->lport = lport;

		lport->localport = localport;
		INIT_LIST_HEAD(&lport->lport_list);

		spin_lock_irqsave(&fcloop_lock, flags);
		list_add_tail(&lport->lport_list, &fcloop_lports);
		spin_unlock_irqrestore(&fcloop_lock, flags);
	}

out_free_opts:
	kfree(opts);
out_free_lport:
	/* free only if we're going to fail */
	if (ret)
		kfree(lport);

	return ret ? ret : count;
}


static void
__unlink_local_port(struct fcloop_lport *lport)
{
	list_del(&lport->lport_list);
}

static int
__wait_localport_unreg(struct fcloop_lport *lport)
{
	int ret;

	init_completion(&lport->unreg_done);

	ret = nvme_fc_unregister_localport(lport->localport);

	wait_for_completion(&lport->unreg_done);

	kfree(lport);

	return ret;
}


static ssize_t
fcloop_delete_local_port(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct fcloop_lport *tlport, *lport = NULL;
	u64 nodename, portname;
	unsigned long flags;
	int ret;

	ret = fcloop_parse_nm_options(dev, &nodename, &portname, buf);
	if (ret)
		return ret;

	spin_lock_irqsave(&fcloop_lock, flags);

	list_for_each_entry(tlport, &fcloop_lports, lport_list) {
		if (tlport->localport->node_name == nodename &&
		    tlport->localport->port_name == portname) {
			lport = tlport;
			__unlink_local_port(lport);
			break;
		}
	}
	spin_unlock_irqrestore(&fcloop_lock, flags);

	if (!lport)
		return -ENOENT;

	ret = __wait_localport_unreg(lport);

	return ret ? ret : count;
}

static struct fcloop_nport *
fcloop_alloc_nport(const char *buf, size_t count, bool remoteport)
{
	struct fcloop_nport *newnport, *nport = NULL;
	struct fcloop_lport *tmplport, *lport = NULL;
	struct fcloop_ctrl_options *opts;
	unsigned long flags;
	u32 opts_mask = (remoteport) ? RPORT_OPTS : TGTPORT_OPTS;
	int ret;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return NULL;

	ret = fcloop_parse_options(opts, buf);
	if (ret)
		goto out_free_opts;

	/* everything there ? */
	if ((opts->mask & opts_mask) != opts_mask) {
		ret = -EINVAL;
		goto out_free_opts;
	}

	newnport = kzalloc(sizeof(*newnport), GFP_KERNEL);
	if (!newnport)
		goto out_free_opts;

	INIT_LIST_HEAD(&newnport->nport_list);
	newnport->node_name = opts->wwnn;
	newnport->port_name = opts->wwpn;
	if (opts->mask & NVMF_OPT_ROLES)
		newnport->port_role = opts->roles;
	if (opts->mask & NVMF_OPT_FCADDR)
		newnport->port_id = opts->fcaddr;
	kref_init(&newnport->ref);

	spin_lock_irqsave(&fcloop_lock, flags);

	list_for_each_entry(tmplport, &fcloop_lports, lport_list) {
		if (tmplport->localport->node_name == opts->wwnn &&
		    tmplport->localport->port_name == opts->wwpn)
			goto out_invalid_opts;

		if (tmplport->localport->node_name == opts->lpwwnn &&
		    tmplport->localport->port_name == opts->lpwwpn)
			lport = tmplport;
	}

	if (remoteport) {
		if (!lport)
			goto out_invalid_opts;
		newnport->lport = lport;
	}

	list_for_each_entry(nport, &fcloop_nports, nport_list) {
		if (nport->node_name == opts->wwnn &&
		    nport->port_name == opts->wwpn) {
			if ((remoteport && nport->rport) ||
			    (!remoteport && nport->tport)) {
				nport = NULL;
				goto out_invalid_opts;
			}

			fcloop_nport_get(nport);

			spin_unlock_irqrestore(&fcloop_lock, flags);

			if (remoteport)
				nport->lport = lport;
			if (opts->mask & NVMF_OPT_ROLES)
				nport->port_role = opts->roles;
			if (opts->mask & NVMF_OPT_FCADDR)
				nport->port_id = opts->fcaddr;
			goto out_free_newnport;
		}
	}

	list_add_tail(&newnport->nport_list, &fcloop_nports);

	spin_unlock_irqrestore(&fcloop_lock, flags);

	kfree(opts);
	return newnport;

out_invalid_opts:
	spin_unlock_irqrestore(&fcloop_lock, flags);
out_free_newnport:
	kfree(newnport);
out_free_opts:
	kfree(opts);
	return nport;
}

static ssize_t
fcloop_create_remote_port(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct nvme_fc_remote_port *remoteport;
	struct fcloop_nport *nport;
	struct fcloop_rport *rport;
	struct nvme_fc_port_info pinfo;
	int ret;

	nport = fcloop_alloc_nport(buf, count, true);
	if (!nport)
		return -EIO;

	memset(&pinfo, 0, sizeof(pinfo));
	pinfo.node_name = nport->node_name;
	pinfo.port_name = nport->port_name;
	pinfo.port_role = nport->port_role;
	pinfo.port_id = nport->port_id;

	ret = nvme_fc_register_remoteport(nport->lport->localport,
						&pinfo, &remoteport);
	if (ret || !remoteport) {
		fcloop_nport_put(nport);
		return ret;
	}

	/* success */
	rport = remoteport->private;
	rport->remoteport = remoteport;
	rport->targetport = (nport->tport) ?  nport->tport->targetport : NULL;
	if (nport->tport) {
		nport->tport->remoteport = remoteport;
		nport->tport->lport = nport->lport;
	}
	rport->nport = nport;
	rport->lport = nport->lport;
	nport->rport = rport;
	spin_lock_init(&rport->lock);
	INIT_WORK(&rport->ls_work, fcloop_rport_lsrqst_work);
	INIT_LIST_HEAD(&rport->ls_list);

	return count;
}


static struct fcloop_rport *
__unlink_remote_port(struct fcloop_nport *nport)
{
	struct fcloop_rport *rport = nport->rport;

	if (rport && nport->tport)
		nport->tport->remoteport = NULL;
	nport->rport = NULL;

	return rport;
}

static int
__remoteport_unreg(struct fcloop_nport *nport, struct fcloop_rport *rport)
{
	if (!rport)
		return -EALREADY;

	return nvme_fc_unregister_remoteport(rport->remoteport);
}

static ssize_t
fcloop_delete_remote_port(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct fcloop_nport *nport = NULL, *tmpport;
	static struct fcloop_rport *rport;
	u64 nodename, portname;
	unsigned long flags;
	int ret;

	ret = fcloop_parse_nm_options(dev, &nodename, &portname, buf);
	if (ret)
		return ret;

	spin_lock_irqsave(&fcloop_lock, flags);

	list_for_each_entry(tmpport, &fcloop_nports, nport_list) {
		if (tmpport->node_name == nodename &&
		    tmpport->port_name == portname && tmpport->rport) {
			nport = tmpport;
			rport = __unlink_remote_port(nport);
			break;
		}
	}

	spin_unlock_irqrestore(&fcloop_lock, flags);

	if (!nport)
		return -ENOENT;

	ret = __remoteport_unreg(nport, rport);

	return ret ? ret : count;
}

static ssize_t
fcloop_create_target_port(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct nvmet_fc_target_port *targetport;
	struct fcloop_nport *nport;
	struct fcloop_tport *tport;
	struct nvmet_fc_port_info tinfo;
	int ret;

	nport = fcloop_alloc_nport(buf, count, false);
	if (!nport)
		return -EIO;

	tinfo.node_name = nport->node_name;
	tinfo.port_name = nport->port_name;
	tinfo.port_id = nport->port_id;

	ret = nvmet_fc_register_targetport(&tinfo, &tgttemplate, NULL,
						&targetport);
	if (ret) {
		fcloop_nport_put(nport);
		return ret;
	}

	/* success */
	tport = targetport->private;
	tport->targetport = targetport;
	tport->remoteport = (nport->rport) ?  nport->rport->remoteport : NULL;
	if (nport->rport)
		nport->rport->targetport = targetport;
	tport->nport = nport;
	tport->lport = nport->lport;
	nport->tport = tport;

	return count;
}


static struct fcloop_tport *
__unlink_target_port(struct fcloop_nport *nport)
{
	struct fcloop_tport *tport = nport->tport;

	if (tport && nport->rport)
		nport->rport->targetport = NULL;
	nport->tport = NULL;

	return tport;
}

static int
__targetport_unreg(struct fcloop_nport *nport, struct fcloop_tport *tport)
{
	if (!tport)
		return -EALREADY;

	return nvmet_fc_unregister_targetport(tport->targetport);
}

static ssize_t
fcloop_delete_target_port(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct fcloop_nport *nport = NULL, *tmpport;
	struct fcloop_tport *tport = NULL;
	u64 nodename, portname;
	unsigned long flags;
	int ret;

	ret = fcloop_parse_nm_options(dev, &nodename, &portname, buf);
	if (ret)
		return ret;

	spin_lock_irqsave(&fcloop_lock, flags);

	list_for_each_entry(tmpport, &fcloop_nports, nport_list) {
		if (tmpport->node_name == nodename &&
		    tmpport->port_name == portname && tmpport->tport) {
			nport = tmpport;
			tport = __unlink_target_port(nport);
			break;
		}
	}

	spin_unlock_irqrestore(&fcloop_lock, flags);

	if (!nport)
		return -ENOENT;

	ret = __targetport_unreg(nport, tport);

	return ret ? ret : count;
}


static DEVICE_ATTR(add_local_port, 0200, NULL, fcloop_create_local_port);
static DEVICE_ATTR(del_local_port, 0200, NULL, fcloop_delete_local_port);
static DEVICE_ATTR(add_remote_port, 0200, NULL, fcloop_create_remote_port);
static DEVICE_ATTR(del_remote_port, 0200, NULL, fcloop_delete_remote_port);
static DEVICE_ATTR(add_target_port, 0200, NULL, fcloop_create_target_port);
static DEVICE_ATTR(del_target_port, 0200, NULL, fcloop_delete_target_port);

static struct attribute *fcloop_dev_attrs[] = {
	&dev_attr_add_local_port.attr,
	&dev_attr_del_local_port.attr,
	&dev_attr_add_remote_port.attr,
	&dev_attr_del_remote_port.attr,
	&dev_attr_add_target_port.attr,
	&dev_attr_del_target_port.attr,
	NULL
};

static struct attribute_group fclopp_dev_attrs_group = {
	.attrs		= fcloop_dev_attrs,
};

static const struct attribute_group *fcloop_dev_attr_groups[] = {
	&fclopp_dev_attrs_group,
	NULL,
};

static struct class *fcloop_class;
static struct device *fcloop_device;


static int __init fcloop_init(void)
{
	int ret;

	fcloop_class = class_create(THIS_MODULE, "fcloop");
	if (IS_ERR(fcloop_class)) {
		pr_err("couldn't register class fcloop\n");
		ret = PTR_ERR(fcloop_class);
		return ret;
	}

	fcloop_device = device_create_with_groups(
				fcloop_class, NULL, MKDEV(0, 0), NULL,
				fcloop_dev_attr_groups, "ctl");
	if (IS_ERR(fcloop_device)) {
		pr_err("couldn't create ctl device!\n");
		ret = PTR_ERR(fcloop_device);
		goto out_destroy_class;
	}

	get_device(fcloop_device);

	return 0;

out_destroy_class:
	class_destroy(fcloop_class);
	return ret;
}

static void __exit fcloop_exit(void)
{
	struct fcloop_lport *lport;
	struct fcloop_nport *nport;
	struct fcloop_tport *tport;
	struct fcloop_rport *rport;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&fcloop_lock, flags);

	for (;;) {
		nport = list_first_entry_or_null(&fcloop_nports,
						typeof(*nport), nport_list);
		if (!nport)
			break;

		tport = __unlink_target_port(nport);
		rport = __unlink_remote_port(nport);

		spin_unlock_irqrestore(&fcloop_lock, flags);

		ret = __targetport_unreg(nport, tport);
		if (ret)
			pr_warn("%s: Failed deleting target port\n", __func__);

		ret = __remoteport_unreg(nport, rport);
		if (ret)
			pr_warn("%s: Failed deleting remote port\n", __func__);

		spin_lock_irqsave(&fcloop_lock, flags);
	}

	for (;;) {
		lport = list_first_entry_or_null(&fcloop_lports,
						typeof(*lport), lport_list);
		if (!lport)
			break;

		__unlink_local_port(lport);

		spin_unlock_irqrestore(&fcloop_lock, flags);

		ret = __wait_localport_unreg(lport);
		if (ret)
			pr_warn("%s: Failed deleting local port\n", __func__);

		spin_lock_irqsave(&fcloop_lock, flags);
	}

	spin_unlock_irqrestore(&fcloop_lock, flags);

	put_device(fcloop_device);

	device_destroy(fcloop_class, MKDEV(0, 0));
	class_destroy(fcloop_class);
}

module_init(fcloop_init);
module_exit(fcloop_exit);

MODULE_LICENSE("GPL v2");
