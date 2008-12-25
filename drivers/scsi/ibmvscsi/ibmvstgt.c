/*
 * IBM eServer i/pSeries Virtual SCSI Target Driver
 * Copyright (C) 2003-2005 Dave Boutcher (boutcher@us.ibm.com) IBM Corp.
 *			   Santiago Leon (santil@us.ibm.com) IBM Corp.
 *			   Linda Xie (lxie@us.ibm.com) IBM Corp.
 *
 * Copyright (C) 2005-2006 FUJITA Tomonori <tomof@acm.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <linux/interrupt.h>
#include <linux/module.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_srp.h>
#include <scsi/scsi_tgt.h>
#include <scsi/libsrp.h>
#include <asm/hvcall.h>
#include <asm/iommu.h>
#include <asm/prom.h>
#include <asm/vio.h>

#include "ibmvscsi.h"

#define	INITIAL_SRP_LIMIT	16
#define	DEFAULT_MAX_SECTORS	256

#define	TGT_NAME	"ibmvstgt"

/*
 * Hypervisor calls.
 */
#define h_copy_rdma(l, sa, sb, da, db) \
			plpar_hcall_norets(H_COPY_RDMA, l, sa, sb, da, db)
#define h_send_crq(ua, l, h) \
			plpar_hcall_norets(H_SEND_CRQ, ua, l, h)
#define h_reg_crq(ua, tok, sz)\
			plpar_hcall_norets(H_REG_CRQ, ua, tok, sz);
#define h_free_crq(ua) \
			plpar_hcall_norets(H_FREE_CRQ, ua);

/* tmp - will replace with SCSI logging stuff */
#define eprintk(fmt, args...)					\
do {								\
	printk("%s(%d) " fmt, __func__, __LINE__, ##args);	\
} while (0)
/* #define dprintk eprintk */
#define dprintk(fmt, args...)

struct vio_port {
	struct vio_dev *dma_dev;

	struct crq_queue crq_queue;
	struct work_struct crq_work;

	unsigned long liobn;
	unsigned long riobn;
	struct srp_target *target;

	struct srp_rport *rport;
};

static struct workqueue_struct *vtgtd;
static struct scsi_transport_template *ibmvstgt_transport_template;

/*
 * These are fixed for the system and come from the Open Firmware device tree.
 * We just store them here to save getting them every time.
 */
static char system_id[64] = "";
static char partition_name[97] = "UNKNOWN";
static unsigned int partition_number = -1;

static struct vio_port *target_to_port(struct srp_target *target)
{
	return (struct vio_port *) target->ldata;
}

static inline union viosrp_iu *vio_iu(struct iu_entry *iue)
{
	return (union viosrp_iu *) (iue->sbuf->buf);
}

static int send_iu(struct iu_entry *iue, uint64_t length, uint8_t format)
{
	struct srp_target *target = iue->target;
	struct vio_port *vport = target_to_port(target);
	long rc, rc1;
	union {
		struct viosrp_crq cooked;
		uint64_t raw[2];
	} crq;

	/* First copy the SRP */
	rc = h_copy_rdma(length, vport->liobn, iue->sbuf->dma,
			 vport->riobn, iue->remote_token);

	if (rc)
		eprintk("Error %ld transferring data\n", rc);

	crq.cooked.valid = 0x80;
	crq.cooked.format = format;
	crq.cooked.reserved = 0x00;
	crq.cooked.timeout = 0x00;
	crq.cooked.IU_length = length;
	crq.cooked.IU_data_ptr = vio_iu(iue)->srp.rsp.tag;

	if (rc == 0)
		crq.cooked.status = 0x99;	/* Just needs to be non-zero */
	else
		crq.cooked.status = 0x00;

	rc1 = h_send_crq(vport->dma_dev->unit_address, crq.raw[0], crq.raw[1]);

	if (rc1) {
		eprintk("%ld sending response\n", rc1);
		return rc1;
	}

	return rc;
}

#define SRP_RSP_SENSE_DATA_LEN	18

static int send_rsp(struct iu_entry *iue, struct scsi_cmnd *sc,
		    unsigned char status, unsigned char asc)
{
	union viosrp_iu *iu = vio_iu(iue);
	uint64_t tag = iu->srp.rsp.tag;

	/* If the linked bit is on and status is good */
	if (test_bit(V_LINKED, &iue->flags) && (status == NO_SENSE))
		status = 0x10;

	memset(iu, 0, sizeof(struct srp_rsp));
	iu->srp.rsp.opcode = SRP_RSP;
	iu->srp.rsp.req_lim_delta = 1;
	iu->srp.rsp.tag = tag;

	if (test_bit(V_DIOVER, &iue->flags))
		iu->srp.rsp.flags |= SRP_RSP_FLAG_DIOVER;

	iu->srp.rsp.data_in_res_cnt = 0;
	iu->srp.rsp.data_out_res_cnt = 0;

	iu->srp.rsp.flags &= ~SRP_RSP_FLAG_RSPVALID;

	iu->srp.rsp.resp_data_len = 0;
	iu->srp.rsp.status = status;
	if (status) {
		uint8_t *sense = iu->srp.rsp.data;

		if (sc) {
			iu->srp.rsp.flags |= SRP_RSP_FLAG_SNSVALID;
			iu->srp.rsp.sense_data_len = SCSI_SENSE_BUFFERSIZE;
			memcpy(sense, sc->sense_buffer, SCSI_SENSE_BUFFERSIZE);
		} else {
			iu->srp.rsp.status = SAM_STAT_CHECK_CONDITION;
			iu->srp.rsp.flags |= SRP_RSP_FLAG_SNSVALID;
			iu->srp.rsp.sense_data_len = SRP_RSP_SENSE_DATA_LEN;

			/* Valid bit and 'current errors' */
			sense[0] = (0x1 << 7 | 0x70);
			/* Sense key */
			sense[2] = status;
			/* Additional sense length */
			sense[7] = 0xa;	/* 10 bytes */
			/* Additional sense code */
			sense[12] = asc;
		}
	}

	send_iu(iue, sizeof(iu->srp.rsp) + SRP_RSP_SENSE_DATA_LEN,
		VIOSRP_SRP_FORMAT);

	return 0;
}

static void handle_cmd_queue(struct srp_target *target)
{
	struct Scsi_Host *shost = target->shost;
	struct srp_rport *rport = target_to_port(target)->rport;
	struct iu_entry *iue;
	struct srp_cmd *cmd;
	unsigned long flags;
	int err;

retry:
	spin_lock_irqsave(&target->lock, flags);

	list_for_each_entry(iue, &target->cmd_queue, ilist) {
		if (!test_and_set_bit(V_FLYING, &iue->flags)) {
			spin_unlock_irqrestore(&target->lock, flags);
			cmd = iue->sbuf->buf;
			err = srp_cmd_queue(shost, cmd, iue,
					    (unsigned long)rport, 0);
			if (err) {
				eprintk("cannot queue cmd %p %d\n", cmd, err);
				srp_iu_put(iue);
			}
			goto retry;
		}
	}

	spin_unlock_irqrestore(&target->lock, flags);
}

static int ibmvstgt_rdma(struct scsi_cmnd *sc, struct scatterlist *sg, int nsg,
			 struct srp_direct_buf *md, int nmd,
			 enum dma_data_direction dir, unsigned int rest)
{
	struct iu_entry *iue = (struct iu_entry *) sc->SCp.ptr;
	struct srp_target *target = iue->target;
	struct vio_port *vport = target_to_port(target);
	dma_addr_t token;
	long err;
	unsigned int done = 0;
	int i, sidx, soff;

	sidx = soff = 0;
	token = sg_dma_address(sg + sidx);

	for (i = 0; i < nmd && rest; i++) {
		unsigned int mdone, mlen;

		mlen = min(rest, md[i].len);
		for (mdone = 0; mlen;) {
			int slen = min(sg_dma_len(sg + sidx) - soff, mlen);

			if (dir == DMA_TO_DEVICE)
				err = h_copy_rdma(slen,
						  vport->riobn,
						  md[i].va + mdone,
						  vport->liobn,
						  token + soff);
			else
				err = h_copy_rdma(slen,
						  vport->liobn,
						  token + soff,
						  vport->riobn,
						  md[i].va + mdone);

			if (err != H_SUCCESS) {
				eprintk("rdma error %d %d %ld\n", dir, slen, err);
				return -EIO;
			}

			mlen -= slen;
			mdone += slen;
			soff += slen;
			done += slen;

			if (soff == sg_dma_len(sg + sidx)) {
				sidx++;
				soff = 0;
				token = sg_dma_address(sg + sidx);

				if (sidx > nsg) {
					eprintk("out of sg %p %d %d\n",
						iue, sidx, nsg);
					return -EIO;
				}
			}
		};

		rest -= mlen;
	}
	return 0;
}

static int ibmvstgt_cmd_done(struct scsi_cmnd *sc,
			     void (*done)(struct scsi_cmnd *))
{
	unsigned long flags;
	struct iu_entry *iue = (struct iu_entry *) sc->SCp.ptr;
	struct srp_target *target = iue->target;
	int err = 0;

	dprintk("%p %p %x %u\n", iue, target, vio_iu(iue)->srp.cmd.cdb[0],
		scsi_sg_count(sc));

	if (scsi_sg_count(sc))
		err = srp_transfer_data(sc, &vio_iu(iue)->srp.cmd, ibmvstgt_rdma, 1, 1);

	spin_lock_irqsave(&target->lock, flags);
	list_del(&iue->ilist);
	spin_unlock_irqrestore(&target->lock, flags);

	if (err|| sc->result != SAM_STAT_GOOD) {
		eprintk("operation failed %p %d %x\n",
			iue, sc->result, vio_iu(iue)->srp.cmd.cdb[0]);
		send_rsp(iue, sc, HARDWARE_ERROR, 0x00);
	} else
		send_rsp(iue, sc, NO_SENSE, 0x00);

	done(sc);
	srp_iu_put(iue);
	return 0;
}

int send_adapter_info(struct iu_entry *iue,
		      dma_addr_t remote_buffer, uint16_t length)
{
	struct srp_target *target = iue->target;
	struct vio_port *vport = target_to_port(target);
	struct Scsi_Host *shost = target->shost;
	dma_addr_t data_token;
	struct mad_adapter_info_data *info;
	int err;

	info = dma_alloc_coherent(target->dev, sizeof(*info), &data_token,
				  GFP_KERNEL);
	if (!info) {
		eprintk("bad dma_alloc_coherent %p\n", target);
		return 1;
	}

	/* Get remote info */
	err = h_copy_rdma(sizeof(*info), vport->riobn, remote_buffer,
			  vport->liobn, data_token);
	if (err == H_SUCCESS) {
		dprintk("Client connect: %s (%d)\n",
			info->partition_name, info->partition_number);
	}

	memset(info, 0, sizeof(*info));

	strcpy(info->srp_version, "16.a");
	strncpy(info->partition_name, partition_name,
		sizeof(info->partition_name));
	info->partition_number = partition_number;
	info->mad_version = 1;
	info->os_type = 2;
	info->port_max_txu[0] = shost->hostt->max_sectors << 9;

	/* Send our info to remote */
	err = h_copy_rdma(sizeof(*info), vport->liobn, data_token,
			  vport->riobn, remote_buffer);

	dma_free_coherent(target->dev, sizeof(*info), info, data_token);

	if (err != H_SUCCESS) {
		eprintk("Error sending adapter info %d\n", err);
		return 1;
	}

	return 0;
}

static void process_login(struct iu_entry *iue)
{
	union viosrp_iu *iu = vio_iu(iue);
	struct srp_login_rsp *rsp = &iu->srp.login_rsp;
	uint64_t tag = iu->srp.rsp.tag;
	struct Scsi_Host *shost = iue->target->shost;
	struct srp_target *target = host_to_srp_target(shost);
	struct vio_port *vport = target_to_port(target);
	struct srp_rport_identifiers ids;

	memset(&ids, 0, sizeof(ids));
	sprintf(ids.port_id, "%x", vport->dma_dev->unit_address);
	ids.roles = SRP_RPORT_ROLE_INITIATOR;
	if (!vport->rport)
		vport->rport = srp_rport_add(shost, &ids);

	/* TODO handle case that requested size is wrong and
	 * buffer format is wrong
	 */
	memset(iu, 0, sizeof(struct srp_login_rsp));
	rsp->opcode = SRP_LOGIN_RSP;
	rsp->req_lim_delta = INITIAL_SRP_LIMIT;
	rsp->tag = tag;
	rsp->max_it_iu_len = sizeof(union srp_iu);
	rsp->max_ti_iu_len = sizeof(union srp_iu);
	/* direct and indirect */
	rsp->buf_fmt = SRP_BUF_FORMAT_DIRECT | SRP_BUF_FORMAT_INDIRECT;

	send_iu(iue, sizeof(*rsp), VIOSRP_SRP_FORMAT);
}

static inline void queue_cmd(struct iu_entry *iue)
{
	struct srp_target *target = iue->target;
	unsigned long flags;

	spin_lock_irqsave(&target->lock, flags);
	list_add_tail(&iue->ilist, &target->cmd_queue);
	spin_unlock_irqrestore(&target->lock, flags);
}

static int process_tsk_mgmt(struct iu_entry *iue)
{
	union viosrp_iu *iu = vio_iu(iue);
	int fn;

	dprintk("%p %u\n", iue, iu->srp.tsk_mgmt.tsk_mgmt_func);

	switch (iu->srp.tsk_mgmt.tsk_mgmt_func) {
	case SRP_TSK_ABORT_TASK:
		fn = ABORT_TASK;
		break;
	case SRP_TSK_ABORT_TASK_SET:
		fn = ABORT_TASK_SET;
		break;
	case SRP_TSK_CLEAR_TASK_SET:
		fn = CLEAR_TASK_SET;
		break;
	case SRP_TSK_LUN_RESET:
		fn = LOGICAL_UNIT_RESET;
		break;
	case SRP_TSK_CLEAR_ACA:
		fn = CLEAR_ACA;
		break;
	default:
		fn = 0;
	}
	if (fn)
		scsi_tgt_tsk_mgmt_request(iue->target->shost,
					  (unsigned long)iue->target->shost,
					  fn,
					  iu->srp.tsk_mgmt.task_tag,
					  (struct scsi_lun *) &iu->srp.tsk_mgmt.lun,
					  iue);
	else
		send_rsp(iue, NULL, ILLEGAL_REQUEST, 0x20);

	return !fn;
}

static int process_mad_iu(struct iu_entry *iue)
{
	union viosrp_iu *iu = vio_iu(iue);
	struct viosrp_adapter_info *info;
	struct viosrp_host_config *conf;

	switch (iu->mad.empty_iu.common.type) {
	case VIOSRP_EMPTY_IU_TYPE:
		eprintk("%s\n", "Unsupported EMPTY MAD IU");
		break;
	case VIOSRP_ERROR_LOG_TYPE:
		eprintk("%s\n", "Unsupported ERROR LOG MAD IU");
		iu->mad.error_log.common.status = 1;
		send_iu(iue, sizeof(iu->mad.error_log),	VIOSRP_MAD_FORMAT);
		break;
	case VIOSRP_ADAPTER_INFO_TYPE:
		info = &iu->mad.adapter_info;
		info->common.status = send_adapter_info(iue, info->buffer,
							info->common.length);
		send_iu(iue, sizeof(*info), VIOSRP_MAD_FORMAT);
		break;
	case VIOSRP_HOST_CONFIG_TYPE:
		conf = &iu->mad.host_config;
		conf->common.status = 1;
		send_iu(iue, sizeof(*conf), VIOSRP_MAD_FORMAT);
		break;
	default:
		eprintk("Unknown type %u\n", iu->srp.rsp.opcode);
	}

	return 1;
}

static int process_srp_iu(struct iu_entry *iue)
{
	union viosrp_iu *iu = vio_iu(iue);
	int done = 1;
	u8 opcode = iu->srp.rsp.opcode;

	switch (opcode) {
	case SRP_LOGIN_REQ:
		process_login(iue);
		break;
	case SRP_TSK_MGMT:
		done = process_tsk_mgmt(iue);
		break;
	case SRP_CMD:
		queue_cmd(iue);
		done = 0;
		break;
	case SRP_LOGIN_RSP:
	case SRP_I_LOGOUT:
	case SRP_T_LOGOUT:
	case SRP_RSP:
	case SRP_CRED_REQ:
	case SRP_CRED_RSP:
	case SRP_AER_REQ:
	case SRP_AER_RSP:
		eprintk("Unsupported type %u\n", opcode);
		break;
	default:
		eprintk("Unknown type %u\n", opcode);
	}

	return done;
}

static void process_iu(struct viosrp_crq *crq, struct srp_target *target)
{
	struct vio_port *vport = target_to_port(target);
	struct iu_entry *iue;
	long err;
	int done = 1;

	iue = srp_iu_get(target);
	if (!iue) {
		eprintk("Error getting IU from pool, %p\n", target);
		return;
	}

	iue->remote_token = crq->IU_data_ptr;

	err = h_copy_rdma(crq->IU_length, vport->riobn,
			  iue->remote_token, vport->liobn, iue->sbuf->dma);

	if (err != H_SUCCESS) {
		eprintk("%ld transferring data error %p\n", err, iue);
		goto out;
	}

	if (crq->format == VIOSRP_MAD_FORMAT)
		done = process_mad_iu(iue);
	else
		done = process_srp_iu(iue);
out:
	if (done)
		srp_iu_put(iue);
}

static irqreturn_t ibmvstgt_interrupt(int dummy, void *data)
{
	struct srp_target *target = data;
	struct vio_port *vport = target_to_port(target);

	vio_disable_interrupts(vport->dma_dev);
	queue_work(vtgtd, &vport->crq_work);

	return IRQ_HANDLED;
}

static int crq_queue_create(struct crq_queue *queue, struct srp_target *target)
{
	int err;
	struct vio_port *vport = target_to_port(target);

	queue->msgs = (struct viosrp_crq *) get_zeroed_page(GFP_KERNEL);
	if (!queue->msgs)
		goto malloc_failed;
	queue->size = PAGE_SIZE / sizeof(*queue->msgs);

	queue->msg_token = dma_map_single(target->dev, queue->msgs,
					  queue->size * sizeof(*queue->msgs),
					  DMA_BIDIRECTIONAL);

	if (dma_mapping_error(target->dev, queue->msg_token))
		goto map_failed;

	err = h_reg_crq(vport->dma_dev->unit_address, queue->msg_token,
			PAGE_SIZE);

	/* If the adapter was left active for some reason (like kexec)
	 * try freeing and re-registering
	 */
	if (err == H_RESOURCE) {
	    do {
		err = h_free_crq(vport->dma_dev->unit_address);
	    } while (err == H_BUSY || H_IS_LONG_BUSY(err));

	    err = h_reg_crq(vport->dma_dev->unit_address, queue->msg_token,
			    PAGE_SIZE);
	}

	if (err != H_SUCCESS && err != 2) {
		eprintk("Error 0x%x opening virtual adapter\n", err);
		goto reg_crq_failed;
	}

	err = request_irq(vport->dma_dev->irq, &ibmvstgt_interrupt,
			  IRQF_DISABLED, "ibmvstgt", target);
	if (err)
		goto req_irq_failed;

	vio_enable_interrupts(vport->dma_dev);

	h_send_crq(vport->dma_dev->unit_address, 0xC001000000000000, 0);

	queue->cur = 0;
	spin_lock_init(&queue->lock);

	return 0;

req_irq_failed:
	do {
		err = h_free_crq(vport->dma_dev->unit_address);
	} while (err == H_BUSY || H_IS_LONG_BUSY(err));

reg_crq_failed:
	dma_unmap_single(target->dev, queue->msg_token,
			 queue->size * sizeof(*queue->msgs), DMA_BIDIRECTIONAL);
map_failed:
	free_page((unsigned long) queue->msgs);

malloc_failed:
	return -ENOMEM;
}

static void crq_queue_destroy(struct srp_target *target)
{
	struct vio_port *vport = target_to_port(target);
	struct crq_queue *queue = &vport->crq_queue;
	int err;

	free_irq(vport->dma_dev->irq, target);
	do {
		err = h_free_crq(vport->dma_dev->unit_address);
	} while (err == H_BUSY || H_IS_LONG_BUSY(err));

	dma_unmap_single(target->dev, queue->msg_token,
			 queue->size * sizeof(*queue->msgs), DMA_BIDIRECTIONAL);

	free_page((unsigned long) queue->msgs);
}

static void process_crq(struct viosrp_crq *crq,	struct srp_target *target)
{
	struct vio_port *vport = target_to_port(target);
	dprintk("%x %x\n", crq->valid, crq->format);

	switch (crq->valid) {
	case 0xC0:
		/* initialization */
		switch (crq->format) {
		case 0x01:
			h_send_crq(vport->dma_dev->unit_address,
				   0xC002000000000000, 0);
			break;
		case 0x02:
			break;
		default:
			eprintk("Unknown format %u\n", crq->format);
		}
		break;
	case 0xFF:
		/* transport event */
		break;
	case 0x80:
		/* real payload */
		switch (crq->format) {
		case VIOSRP_SRP_FORMAT:
		case VIOSRP_MAD_FORMAT:
			process_iu(crq, target);
			break;
		case VIOSRP_OS400_FORMAT:
		case VIOSRP_AIX_FORMAT:
		case VIOSRP_LINUX_FORMAT:
		case VIOSRP_INLINE_FORMAT:
			eprintk("Unsupported format %u\n", crq->format);
			break;
		default:
			eprintk("Unknown format %u\n", crq->format);
		}
		break;
	default:
		eprintk("unknown message type 0x%02x!?\n", crq->valid);
	}
}

static inline struct viosrp_crq *next_crq(struct crq_queue *queue)
{
	struct viosrp_crq *crq;
	unsigned long flags;

	spin_lock_irqsave(&queue->lock, flags);
	crq = &queue->msgs[queue->cur];
	if (crq->valid & 0x80) {
		if (++queue->cur == queue->size)
			queue->cur = 0;
	} else
		crq = NULL;
	spin_unlock_irqrestore(&queue->lock, flags);

	return crq;
}

static void handle_crq(struct work_struct *work)
{
	struct vio_port *vport = container_of(work, struct vio_port, crq_work);
	struct srp_target *target = vport->target;
	struct viosrp_crq *crq;
	int done = 0;

	while (!done) {
		while ((crq = next_crq(&vport->crq_queue)) != NULL) {
			process_crq(crq, target);
			crq->valid = 0x00;
		}

		vio_enable_interrupts(vport->dma_dev);

		crq = next_crq(&vport->crq_queue);
		if (crq) {
			vio_disable_interrupts(vport->dma_dev);
			process_crq(crq, target);
			crq->valid = 0x00;
		} else
			done = 1;
	}

	handle_cmd_queue(target);
}


static int ibmvstgt_eh_abort_handler(struct scsi_cmnd *sc)
{
	unsigned long flags;
	struct iu_entry *iue = (struct iu_entry *) sc->SCp.ptr;
	struct srp_target *target = iue->target;

	dprintk("%p %p %x\n", iue, target, vio_iu(iue)->srp.cmd.cdb[0]);

	spin_lock_irqsave(&target->lock, flags);
	list_del(&iue->ilist);
	spin_unlock_irqrestore(&target->lock, flags);

	srp_iu_put(iue);

	return 0;
}

static int ibmvstgt_tsk_mgmt_response(struct Scsi_Host *shost,
				      u64 itn_id, u64 mid, int result)
{
	struct iu_entry *iue = (struct iu_entry *) ((void *) mid);
	union viosrp_iu *iu = vio_iu(iue);
	unsigned char status, asc;

	eprintk("%p %d\n", iue, result);
	status = NO_SENSE;
	asc = 0;

	switch (iu->srp.tsk_mgmt.tsk_mgmt_func) {
	case SRP_TSK_ABORT_TASK:
		asc = 0x14;
		if (result)
			status = ABORTED_COMMAND;
		break;
	default:
		break;
	}

	send_rsp(iue, NULL, status, asc);
	srp_iu_put(iue);

	return 0;
}

static int ibmvstgt_it_nexus_response(struct Scsi_Host *shost, u64 itn_id,
				      int result)
{
	struct srp_target *target = host_to_srp_target(shost);
	struct vio_port *vport = target_to_port(target);

	if (result) {
		eprintk("%p %d\n", shost, result);
		srp_rport_del(vport->rport);
		vport->rport = NULL;
	}
	return 0;
}

static ssize_t system_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", system_id);
}

static ssize_t partition_number_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%x\n", partition_number);
}

static ssize_t unit_address_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct srp_target *target = host_to_srp_target(shost);
	struct vio_port *vport = target_to_port(target);
	return snprintf(buf, PAGE_SIZE, "%x\n", vport->dma_dev->unit_address);
}

static DEVICE_ATTR(system_id, S_IRUGO, system_id_show, NULL);
static DEVICE_ATTR(partition_number, S_IRUGO, partition_number_show, NULL);
static DEVICE_ATTR(unit_address, S_IRUGO, unit_address_show, NULL);

static struct device_attribute *ibmvstgt_attrs[] = {
	&dev_attr_system_id,
	&dev_attr_partition_number,
	&dev_attr_unit_address,
	NULL,
};

static struct scsi_host_template ibmvstgt_sht = {
	.name			= TGT_NAME,
	.module			= THIS_MODULE,
	.can_queue		= INITIAL_SRP_LIMIT,
	.sg_tablesize		= SG_ALL,
	.use_clustering		= DISABLE_CLUSTERING,
	.max_sectors		= DEFAULT_MAX_SECTORS,
	.transfer_response	= ibmvstgt_cmd_done,
	.eh_abort_handler	= ibmvstgt_eh_abort_handler,
	.shost_attrs		= ibmvstgt_attrs,
	.proc_name		= TGT_NAME,
	.supported_mode		= MODE_TARGET,
};

static int ibmvstgt_probe(struct vio_dev *dev, const struct vio_device_id *id)
{
	struct Scsi_Host *shost;
	struct srp_target *target;
	struct vio_port *vport;
	unsigned int *dma, dma_size;
	int err = -ENOMEM;

	vport = kzalloc(sizeof(struct vio_port), GFP_KERNEL);
	if (!vport)
		return err;
	shost = scsi_host_alloc(&ibmvstgt_sht, sizeof(struct srp_target));
	if (!shost)
		goto free_vport;
	shost->transportt = ibmvstgt_transport_template;

	target = host_to_srp_target(shost);
	target->shost = shost;
	vport->dma_dev = dev;
	target->ldata = vport;
	vport->target = target;
	err = srp_target_alloc(target, &dev->dev, INITIAL_SRP_LIMIT,
			       SRP_MAX_IU_LEN);
	if (err)
		goto put_host;

	dma = (unsigned int *) vio_get_attribute(dev, "ibm,my-dma-window",
						 &dma_size);
	if (!dma || dma_size != 40) {
		eprintk("Couldn't get window property %d\n", dma_size);
		err = -EIO;
		goto free_srp_target;
	}
	vport->liobn = dma[0];
	vport->riobn = dma[5];

	INIT_WORK(&vport->crq_work, handle_crq);

	err = scsi_add_host(shost, target->dev);
	if (err)
		goto free_srp_target;

	err = scsi_tgt_alloc_queue(shost);
	if (err)
		goto remove_host;

	err = crq_queue_create(&vport->crq_queue, target);
	if (err)
		goto free_queue;

	return 0;
free_queue:
	scsi_tgt_free_queue(shost);
remove_host:
	scsi_remove_host(shost);
free_srp_target:
	srp_target_free(target);
put_host:
	scsi_host_put(shost);
free_vport:
	kfree(vport);
	return err;
}

static int ibmvstgt_remove(struct vio_dev *dev)
{
	struct srp_target *target = (struct srp_target *) dev->dev.driver_data;
	struct Scsi_Host *shost = target->shost;
	struct vio_port *vport = target->ldata;

	crq_queue_destroy(target);
	srp_remove_host(shost);
	scsi_remove_host(shost);
	scsi_tgt_free_queue(shost);
	srp_target_free(target);
	kfree(vport);
	scsi_host_put(shost);
	return 0;
}

static struct vio_device_id ibmvstgt_device_table[] __devinitdata = {
	{"v-scsi-host", "IBM,v-scsi-host"},
	{"",""}
};

MODULE_DEVICE_TABLE(vio, ibmvstgt_device_table);

static struct vio_driver ibmvstgt_driver = {
	.id_table = ibmvstgt_device_table,
	.probe = ibmvstgt_probe,
	.remove = ibmvstgt_remove,
	.driver = {
		.name = "ibmvscsis",
		.owner = THIS_MODULE,
	}
};

static int get_system_info(void)
{
	struct device_node *rootdn;
	const char *id, *model, *name;
	const unsigned int *num;

	rootdn = of_find_node_by_path("/");
	if (!rootdn)
		return -ENOENT;

	model = of_get_property(rootdn, "model", NULL);
	id = of_get_property(rootdn, "system-id", NULL);
	if (model && id)
		snprintf(system_id, sizeof(system_id), "%s-%s", model, id);

	name = of_get_property(rootdn, "ibm,partition-name", NULL);
	if (name)
		strncpy(partition_name, name, sizeof(partition_name));

	num = of_get_property(rootdn, "ibm,partition-no", NULL);
	if (num)
		partition_number = *num;

	of_node_put(rootdn);
	return 0;
}

static struct srp_function_template ibmvstgt_transport_functions = {
	.tsk_mgmt_response = ibmvstgt_tsk_mgmt_response,
	.it_nexus_response = ibmvstgt_it_nexus_response,
};

static int ibmvstgt_init(void)
{
	int err = -ENOMEM;

	printk("IBM eServer i/pSeries Virtual SCSI Target Driver\n");

	ibmvstgt_transport_template =
		srp_attach_transport(&ibmvstgt_transport_functions);
	if (!ibmvstgt_transport_template)
		return err;

	vtgtd = create_workqueue("ibmvtgtd");
	if (!vtgtd)
		goto release_transport;

	err = get_system_info();
	if (err)
		goto destroy_wq;

	err = vio_register_driver(&ibmvstgt_driver);
	if (err)
		goto destroy_wq;

	return 0;
destroy_wq:
	destroy_workqueue(vtgtd);
release_transport:
	srp_release_transport(ibmvstgt_transport_template);
	return err;
}

static void ibmvstgt_exit(void)
{
	printk("Unregister IBM virtual SCSI driver\n");

	destroy_workqueue(vtgtd);
	vio_unregister_driver(&ibmvstgt_driver);
	srp_release_transport(ibmvstgt_transport_template);
}

MODULE_DESCRIPTION("IBM Virtual SCSI Target");
MODULE_AUTHOR("Santiago Leon");
MODULE_LICENSE("GPL");

module_init(ibmvstgt_init);
module_exit(ibmvstgt_exit);
