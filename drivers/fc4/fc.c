/* fc.c: Generic Fibre Channel and FC4 SCSI driver.
 *
 * Copyright (C) 1997,1998,1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1997,1998 Jirka Hanika (geo@ff.cuni.cz)
 *
 * There are two kinds of Fibre Channel adapters used in Linux. Either
 * the adapter is "smart" and does all FC bookkeeping by itself and
 * just presents a standard SCSI interface to the operating system
 * (that's e.g. the case with Qlogic FC cards), or leaves most of the FC
 * bookkeeping to the OS (e.g. soc, socal). Drivers for the former adapters
 * will look like normal SCSI drivers (with the exception of max_id will be
 * usually 127), the latter on the other side allows SCSI, IP over FC and other
 * protocols. This driver tree is for the latter adapters.
 *
 * This file should support both Point-to-Point and Arbitrated Loop topologies.
 *
 * Sources:
 *	Fibre Channel Physical & Signaling Interface (FC-PH), dpANS, 1994
 *	dpANS Fibre Channel Protocol for SCSI (X3.269-199X), Rev. 012, 1995
 *	Fibre Channel Arbitrated Loop (FC-AL), Rev. 4.5, 1995
 *	Fibre Channel Private Loop SCSI Direct Attach (FC-PLDA), Rev. 2.1, 1997
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>

#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/semaphore.h>
#include "fcp_impl.h"
#include <scsi/scsi_host.h>

/* #define FCDEBUG */

#define fc_printk printk ("%s: ", fc->name); printk 

#ifdef FCDEBUG
#define FCD(x)  fc_printk x;
#define FCND(x)	printk ("FC: "); printk x;
#else
#define FCD(x)
#define FCND(x)
#endif

#ifdef __sparc__
#define dma_alloc_consistent(d,s,p) sbus_alloc_consistent(d,s,p)
#define dma_free_consistent(d,s,v,h) sbus_free_consistent(d,s,v,h)
#define dma_map_single(d,v,s,dir) sbus_map_single(d,v,s,dir)
#define dma_unmap_single(d,h,s,dir) sbus_unmap_single(d,h,s,dir)
#define dma_map_sg(d,s,n,dir) sbus_map_sg(d,s,n,dir)
#define dma_unmap_sg(d,s,n,dir) sbus_unmap_sg(d,s,n,dir)
#else
#define dma_alloc_consistent(d,s,p) pci_alloc_consistent(d,s,p)
#define dma_free_consistent(d,s,v,h) pci_free_consistent(d,s,v,h)
#define dma_map_single(d,v,s,dir) pci_map_single(d,v,s,dir)
#define dma_unmap_single(d,h,s,dir) pci_unmap_single(d,h,s,dir)
#define dma_map_sg(d,s,n,dir) pci_map_sg(d,s,n,dir)
#define dma_unmap_sg(d,s,n,dir) pci_unmap_sg(d,s,n,dir)
#endif							       

#define FCP_CMND(SCpnt) ((fcp_cmnd *)&(SCpnt->SCp))
#define FC_SCMND(SCpnt) ((fc_channel *)(SCpnt->device->host->hostdata[0]))
#define SC_FCMND(fcmnd) ((Scsi_Cmnd *)((long)fcmnd - (long)&(((Scsi_Cmnd *)0)->SCp)))

static int fcp_scsi_queue_it(fc_channel *, Scsi_Cmnd *, fcp_cmnd *, int);
void fcp_queue_empty(fc_channel *);

static void fcp_scsi_insert_queue (fc_channel *fc, fcp_cmnd *fcmd)
{
	if (!fc->scsi_que) {
		fc->scsi_que = fcmd;
		fcmd->next = fcmd;
		fcmd->prev = fcmd;
	} else {
		fc->scsi_que->prev->next = fcmd;
		fcmd->prev = fc->scsi_que->prev;
		fc->scsi_que->prev = fcmd;
		fcmd->next = fc->scsi_que;
	}
}

static void fcp_scsi_remove_queue (fc_channel *fc, fcp_cmnd *fcmd)
{
	if (fcmd == fcmd->next) {
		fc->scsi_que = NULL;
		return;
	}
	if (fcmd == fc->scsi_que)
		fc->scsi_que = fcmd->next;
	fcmd->prev->next = fcmd->next;
	fcmd->next->prev = fcmd->prev;
}

fc_channel *fc_channels = NULL;

#define LSMAGIC	620829043
typedef struct {
	/* Must be first */
	struct semaphore sem;
	int magic;
	int count;
	logi *logi;
	fcp_cmnd *fcmds;
	atomic_t todo;
	struct timer_list timer;
	unsigned char grace[0];
} ls;

#define LSOMAGIC 654907799
typedef struct {
	/* Must be first */
	struct semaphore sem;
	int magic;
	int count;
	fcp_cmnd *fcmds;
	atomic_t todo;
	struct timer_list timer;
} lso;

#define LSEMAGIC 84482456
typedef struct {
	/* Must be first */
	struct semaphore sem;
	int magic;
	int status;
	struct timer_list timer;
} lse;

static void fcp_login_timeout(unsigned long data)
{
	ls *l = (ls *)data;
	FCND(("Login timeout\n"))
	up(&l->sem);
}

static void fcp_login_done(fc_channel *fc, int i, int status)
{
	fcp_cmnd *fcmd;
	logi *plogi;
	fc_hdr *fch;
	ls *l = (ls *)fc->ls;
	
	FCD(("Login done %d %d\n", i, status))
	if (i < l->count) {
		if (fc->state == FC_STATE_FPORT_OK) {
			FCD(("Additional FPORT_OK received with status %d\n", status))
			return;
		}
		switch (status) {
		case FC_STATUS_OK: /* Oh, we found a fabric */
		case FC_STATUS_P_RJT: /* Oh, we haven't found any */
			fc->state = FC_STATE_FPORT_OK;
			fcmd = l->fcmds + i;
			plogi = l->logi + 3 * i;
			dma_unmap_single (fc->dev, fcmd->cmd, 3 * sizeof(logi),
					  DMA_BIDIRECTIONAL);
			plogi->code = LS_PLOGI;
			memcpy (&plogi->nport_wwn, &fc->wwn_nport, sizeof(fc_wwn));
			memcpy (&plogi->node_wwn, &fc->wwn_node, sizeof(fc_wwn));
			memcpy (&plogi->common, fc->common_svc, sizeof(common_svc_parm));
			memcpy (&plogi->class1, fc->class_svcs, 3*sizeof(svc_parm));
			fch = &fcmd->fch;
			fcmd->token += l->count;
			FILL_FCHDR_RCTL_DID(fch, R_CTL_ELS_REQ, fc->did);
			FILL_FCHDR_SID(fch, fc->sid);
#ifdef FCDEBUG
			{
				int i;
				unsigned *x = (unsigned *)plogi;
				printk ("logi: ");
				for (i = 0; i < 21; i++)
					printk ("%08x ", x[i]);
				printk ("\n");
			}
#endif			
			fcmd->cmd = dma_map_single (fc->dev, plogi, 3 * sizeof(logi),
						    DMA_BIDIRECTIONAL);
			fcmd->rsp = fcmd->cmd + 2 * sizeof(logi);
			if (fc->hw_enque (fc, fcmd))
				printk ("FC: Cannot enque PLOGI packet on %s\n", fc->name);
			break;
		case FC_STATUS_ERR_OFFLINE:
			fc->state = FC_STATE_MAYBEOFFLINE;
			FCD (("FC is offline %d\n", l->grace[i]))
			break;
		default:
			printk ("FLOGI failed for %s with status %d\n", fc->name, status);
			/* Do some sort of error recovery here */
			break;
		}
	} else {
		i -= l->count;
		if (fc->state != FC_STATE_FPORT_OK) {
			FCD(("Unexpected N-PORT rsp received"))
			return;
		}
		switch (status) {
		case FC_STATUS_OK:
			plogi = l->logi + 3 * i;
			dma_unmap_single (fc->dev, l->fcmds[i].cmd, 3 * sizeof(logi),
					  DMA_BIDIRECTIONAL);
			if (!fc->wwn_dest.lo && !fc->wwn_dest.hi) {
				memcpy (&fc->wwn_dest, &plogi[1].node_wwn, sizeof(fc_wwn)); 
				FCD(("Dest WWN %08x%08x\n", *(u32 *)&fc->wwn_dest, fc->wwn_dest.lo))
			} else if (fc->wwn_dest.lo != plogi[1].node_wwn.lo ||
				   fc->wwn_dest.hi != plogi[1].node_wwn.hi) {
				printk ("%s: mismatch in wwns. Got %08x%08x, expected %08x%08x\n",
					fc->name,
					*(u32 *)&plogi[1].node_wwn, plogi[1].node_wwn.lo,
					*(u32 *)&fc->wwn_dest, fc->wwn_dest.lo);
			}
			fc->state = FC_STATE_ONLINE;
			printk ("%s: ONLINE\n", fc->name);
			if (atomic_dec_and_test (&l->todo))
				up(&l->sem);
			break;
		case FC_STATUS_ERR_OFFLINE:
			fc->state = FC_STATE_OFFLINE;
			dma_unmap_single (fc->dev, l->fcmds[i].cmd, 3 * sizeof(logi),
					  DMA_BIDIRECTIONAL);
			printk ("%s: FC is offline\n", fc->name);
			if (atomic_dec_and_test (&l->todo))
				up(&l->sem);
			break;
		default:
			printk ("PLOGI failed for %s with status %d\n", fc->name, status);
			/* Do some sort of error recovery here */
			break;
		}
	}
}

static void fcp_report_map_done(fc_channel *fc, int i, int status)
{
	fcp_cmnd *fcmd;
	fc_hdr *fch;
	unsigned char j;
	ls *l = (ls *)fc->ls;
	fc_al_posmap *p;
	
	FCD(("Report map done %d %d\n", i, status))
	switch (status) {
	case FC_STATUS_OK: /* Ok, let's have a fun on a loop */
		dma_unmap_single (fc->dev, l->fcmds[i].cmd, 3 * sizeof(logi),
				  DMA_BIDIRECTIONAL);
		p = (fc_al_posmap *)(l->logi + 3 * i);
#ifdef FCDEBUG
		{
		u32 *u = (u32 *)p;
		FCD(("%08x\n", u[0]))
		u ++;
		FCD(("%08x.%08x.%08x.%08x.%08x.%08x.%08x.%08x\n", u[0],u[1],u[2],u[3],u[4],u[5],u[6],u[7]))
		}
#endif		
		if ((p->magic & 0xffff0000) != FC_AL_LILP || !p->len) {
			printk ("FC: Bad magic from REPORT_AL_MAP on %s - %08x\n", fc->name, p->magic);
			fc->state = FC_STATE_OFFLINE;
		} else {
			fc->posmap = (fcp_posmap *)kmalloc(sizeof(fcp_posmap)+p->len, GFP_KERNEL);
			if (!fc->posmap) {
				printk("FC: Not enough memory, offlining channel\n");
				fc->state = FC_STATE_OFFLINE;
			} else {
				int k;
				memset(fc->posmap, 0, sizeof(fcp_posmap)+p->len);
				/* FIXME: This is where SOCAL transfers our AL-PA.
				   Keep it here till we found out what other cards do... */
				fc->sid = (p->magic & 0xff);
				for (i = 0; i < p->len; i++)
					if (p->alpa[i] == fc->sid)
						break;
				k = p->len;
				if (i == p->len)
					i = 0;
				else {
					p->len--;
					i++;
				}
				fc->posmap->len = p->len;
				for (j = 0; j < p->len; j++) {
					if (i == k) i = 0;
					fc->posmap->list[j] = p->alpa[i++];
				}
				fc->state = FC_STATE_ONLINE;
			}
		}
		printk ("%s: ONLINE\n", fc->name);
		if (atomic_dec_and_test (&l->todo))
			up(&l->sem);
		break;
	case FC_STATUS_POINTTOPOINT: /* We're Point-to-Point, no AL... */
		FCD(("SID %d DID %d\n", fc->sid, fc->did))
		fcmd = l->fcmds + i;
		dma_unmap_single(fc->dev, fcmd->cmd, 3 * sizeof(logi),
				 DMA_BIDIRECTIONAL);
		fch = &fcmd->fch;
		memset(l->logi + 3 * i, 0, 3 * sizeof(logi));
		FILL_FCHDR_RCTL_DID(fch, R_CTL_ELS_REQ, FS_FABRIC_F_PORT);
		FILL_FCHDR_SID(fch, 0);
		FILL_FCHDR_TYPE_FCTL(fch, TYPE_EXTENDED_LS, F_CTL_FIRST_SEQ | F_CTL_SEQ_INITIATIVE);
		FILL_FCHDR_SEQ_DF_SEQ(fch, 0, 0, 0);
		FILL_FCHDR_OXRX(fch, 0xffff, 0xffff);
		fch->param = 0;
		l->logi [3 * i].code = LS_FLOGI;
		fcmd->cmd = dma_map_single (fc->dev, l->logi + 3 * i, 3 * sizeof(logi),
					    DMA_BIDIRECTIONAL);
		fcmd->rsp = fcmd->cmd + sizeof(logi);
		fcmd->cmdlen = sizeof(logi);
		fcmd->rsplen = sizeof(logi);
		fcmd->data = (dma_addr_t)NULL;
		fcmd->class = FC_CLASS_SIMPLE;
		fcmd->proto = TYPE_EXTENDED_LS;
		if (fc->hw_enque (fc, fcmd))
			printk ("FC: Cannot enque FLOGI packet on %s\n", fc->name);
		break;
	case FC_STATUS_ERR_OFFLINE:
		fc->state = FC_STATE_MAYBEOFFLINE;
		FCD (("FC is offline %d\n", l->grace[i]))
		break;
	default:
		printk ("FLOGI failed for %s with status %d\n", fc->name, status);
		/* Do some sort of error recovery here */
		break;
	}
}

void fcp_register(fc_channel *fc, u8 type, int unregister)
{
	int size, i;
	int slots = (fc->can_queue * 3) >> 1;

	FCND(("Going to %sregister\n", unregister ? "un" : ""))

	if (type == TYPE_SCSI_FCP) {
		if (!unregister) {
			fc->scsi_cmd_pool = (fcp_cmd *)
				dma_alloc_consistent (fc->dev,
						      slots * (sizeof (fcp_cmd) + fc->rsp_size),
						      &fc->dma_scsi_cmd);
			fc->scsi_rsp_pool = (char *)(fc->scsi_cmd_pool + slots);
			fc->dma_scsi_rsp = fc->dma_scsi_cmd + slots * sizeof (fcp_cmd);
			fc->scsi_bitmap_end = (slots + 63) & ~63;
			size = fc->scsi_bitmap_end / 8;
			fc->scsi_bitmap = kmalloc (size, GFP_KERNEL);
			memset (fc->scsi_bitmap, 0, size);
			set_bit (0, fc->scsi_bitmap);
			for (i = fc->can_queue; i < fc->scsi_bitmap_end; i++)
				set_bit (i, fc->scsi_bitmap);
			fc->scsi_free = fc->can_queue;
			fc->cmd_slots = (fcp_cmnd **)kmalloc(slots * sizeof(fcp_cmnd*), GFP_KERNEL);
			memset(fc->cmd_slots, 0, slots * sizeof(fcp_cmnd*));
			fc->abort_count = 0;
		} else {
			fc->scsi_name[0] = 0;
			kfree (fc->scsi_bitmap);
			kfree (fc->cmd_slots);
			FCND(("Unregistering\n"));
			if (fc->rst_pkt) {
				if (fc->rst_pkt->eh_state == SCSI_STATE_UNUSED)
					kfree(fc->rst_pkt);
				else {
					/* Can't happen. Some memory would be lost. */
					printk("FC: Reset in progress. Now?!");
				}
			}
			FCND(("Unregistered\n"));
		}
	} else
		printk ("FC: %segistering unknown type %02x\n", unregister ? "Unr" : "R", type);
}

static void fcp_scsi_done(Scsi_Cmnd *SCpnt);

static inline void fcp_scsi_receive(fc_channel *fc, int token, int status, fc_hdr *fch)
{
	fcp_cmnd *fcmd;
	fcp_rsp  *rsp;
	int host_status;
	Scsi_Cmnd *SCpnt;
	int sense_len;
	int rsp_status;

	fcmd = fc->cmd_slots[token];
	if (!fcmd) return;
	rsp = (fcp_rsp *) (fc->scsi_rsp_pool + fc->rsp_size * token);
	SCpnt = SC_FCMND(fcmd);

	if (SCpnt->done != fcp_scsi_done)
		return;

	rsp_status = rsp->fcp_status;
	FCD(("rsp_status %08x status %08x\n", rsp_status, status))
	switch (status) {
	case FC_STATUS_OK:
		host_status=DID_OK;
		
		if (rsp_status & FCP_STATUS_RESID) {
#ifdef FCDEBUG
			FCD(("Resid %d\n", rsp->fcp_resid))
			{
				fcp_cmd *cmd = fc->scsi_cmd_pool + token;
				int i;
				
				printk ("Command ");
				for (i = 0; i < sizeof(fcp_cmd); i+=4)
					printk ("%08x ", *(u32 *)(((char *)cmd)+i));
				printk ("\nResponse ");
				for (i = 0; i < fc->rsp_size; i+=4)
					printk ("%08x ", *(u32 *)(((char *)rsp)+i));
				printk ("\n");
			}
#endif			
		}

		if (rsp_status & FCP_STATUS_SENSE_LEN) {
			sense_len = rsp->fcp_sense_len;
			if (sense_len > sizeof(SCpnt->sense_buffer)) sense_len = sizeof(SCpnt->sense_buffer);
			memcpy(SCpnt->sense_buffer, ((char *)(rsp+1)), sense_len);
		}
		
		if (fcmd->data) {
			if (SCpnt->use_sg)
				dma_unmap_sg(fc->dev, (struct scatterlist *)SCpnt->buffer,
						SCpnt->use_sg,
						SCpnt->sc_data_direction);
			else
				dma_unmap_single(fc->dev, fcmd->data, SCpnt->request_bufflen,
						 SCpnt->sc_data_direction);
		}
		break;
	default:
		host_status=DID_ERROR; /* FIXME */
		FCD(("Wrong FC status %d for token %d\n", status, token))
		break;
	}

	if (status_byte(rsp_status) == QUEUE_FULL) {
		printk ("%s: (%d,%d) Received rsp_status 0x%x\n", fc->name, SCpnt->device->channel, SCpnt->device->id, rsp_status);
	}	
	
	SCpnt->result = (host_status << 16) | (rsp_status & 0xff);
#ifdef FCDEBUG	
	if (host_status || SCpnt->result || rsp_status) printk("FC: host_status %d, packet status %d\n",
			host_status, SCpnt->result);
#endif
	SCpnt->done = fcmd->done;
	fcmd->done=NULL;
	clear_bit(token, fc->scsi_bitmap);
	fc->scsi_free++;
	FCD(("Calling scsi_done with %08x\n", SCpnt->result))
	SCpnt->scsi_done(SCpnt);
}

void fcp_receive_solicited(fc_channel *fc, int proto, int token, int status, fc_hdr *fch)
{
	int magic;
	FCD(("receive_solicited %d %d %d\n", proto, token, status))
	switch (proto) {
	case TYPE_SCSI_FCP:
		fcp_scsi_receive(fc, token, status, fch); break;
	case TYPE_EXTENDED_LS:
	case PROTO_REPORT_AL_MAP:
		magic = 0;
		if (fc->ls)
			magic = ((ls *)(fc->ls))->magic;
		if (magic == LSMAGIC) {
			ls *l = (ls *)fc->ls;
			int i = (token >= l->count) ? token - l->count : token;

			/* Let's be sure */
			if ((unsigned)i < l->count && l->fcmds[i].fc == fc) {
				if (proto == TYPE_EXTENDED_LS)
					fcp_login_done(fc, token, status);
				else
					fcp_report_map_done(fc, token, status);
				break;
			}
		}
		FCD(("fc %p fc->ls %p fc->cmd_slots %p\n", fc, fc->ls, fc->cmd_slots))
		if (proto == TYPE_EXTENDED_LS && !fc->ls && fc->cmd_slots) {
			fcp_cmnd *fcmd;
			
			fcmd = fc->cmd_slots[token];
			if (fcmd && fcmd->ls && ((ls *)(fcmd->ls))->magic == LSEMAGIC) {
				lse *l = (lse *)fcmd->ls;
				
				l->status = status;
				up (&l->sem);
			}
		}
		break;
	case PROTO_OFFLINE:
		if (fc->ls && ((lso *)(fc->ls))->magic == LSOMAGIC) {
			lso *l = (lso *)fc->ls;

			if ((unsigned)token < l->count && l->fcmds[token].fc == fc) {
				/* Wow, OFFLINE response arrived :) */
				FCD(("OFFLINE Response arrived\n"))
				fc->state = FC_STATE_OFFLINE;
				if (atomic_dec_and_test (&l->todo))
					up(&l->sem);
			}
		}
		break;
		
	default:
		break;
	}
}

void fcp_state_change(fc_channel *fc, int state)
{
	FCD(("state_change %d %d\n", state, fc->state))
	if (state == FC_STATE_ONLINE && fc->state == FC_STATE_MAYBEOFFLINE)
		fc->state = FC_STATE_UNINITED;
	else if (state == FC_STATE_ONLINE)
		printk (KERN_WARNING "%s: state change to ONLINE\n", fc->name);
	else
		printk (KERN_ERR "%s: state change to OFFLINE\n", fc->name);
}

int fcp_initialize(fc_channel *fcchain, int count)
{
	fc_channel *fc;
	fcp_cmnd *fcmd;
	int i, retry, ret;
	ls *l;

	FCND(("fcp_inititialize %08lx\n", (long)fcp_init))
	FCND(("fc_channels %08lx\n", (long)fc_channels))
	FCND((" SID %d DID %d\n", fcchain->sid, fcchain->did))
	l = kmalloc(sizeof (ls) + count, GFP_KERNEL);
	if (!l) {
		printk ("FC: Cannot allocate memory for initialization\n");
		return -ENOMEM;
	}
	memset (l, 0, sizeof(ls) + count);
	l->magic = LSMAGIC;
	l->count = count;
	FCND(("FCP Init for %d channels\n", count))
	init_MUTEX_LOCKED(&l->sem);
	init_timer(&l->timer);
	l->timer.function = fcp_login_timeout;
	l->timer.data = (unsigned long)l;
	atomic_set (&l->todo, count);
	l->logi = kmalloc (count * 3 * sizeof(logi), GFP_KERNEL);
	l->fcmds = kmalloc (count * sizeof(fcp_cmnd), GFP_KERNEL);
	if (!l->logi || !l->fcmds) {
		if (l->logi) kfree (l->logi);
		if (l->fcmds) kfree (l->fcmds);
		kfree (l);
		printk ("FC: Cannot allocate DMA memory for initialization\n");
		return -ENOMEM;
	}
	memset (l->logi, 0, count * 3 * sizeof(logi));
	memset (l->fcmds, 0, count * sizeof(fcp_cmnd));
	for (fc = fcchain, i = 0; fc && i < count; fc = fc->next, i++) {
		fc->state = FC_STATE_UNINITED;
		fc->rst_pkt = NULL;	/* kmalloc when first used */
	}
	/* First try if we are in a AL topology */
	FCND(("Initializing REPORT_MAP packets\n"))
	for (fc = fcchain, i = 0; fc && i < count; fc = fc->next, i++) {
		fcmd = l->fcmds + i;
		fc->login = fcmd;
		fc->ls = (void *)l;
		/* Assumes sizeof(fc_al_posmap) < 3 * sizeof(logi), which is true */
		fcmd->cmd = dma_map_single (fc->dev, l->logi + 3 * i, 3 * sizeof(logi),
					    DMA_BIDIRECTIONAL);
		fcmd->proto = PROTO_REPORT_AL_MAP;
		fcmd->token = i;
		fcmd->fc = fc;
	}
	for (retry = 0; retry < 8; retry++) {
		int nqueued = 0;
		FCND(("Sending REPORT_MAP/FLOGI/PLOGI packets\n"))
		for (fc = fcchain, i = 0; fc && i < count; fc = fc->next, i++) {
			if (fc->state == FC_STATE_ONLINE || fc->state == FC_STATE_OFFLINE)
				continue;
			disable_irq(fc->irq);
			if (fc->state == FC_STATE_MAYBEOFFLINE) {
				if (!l->grace[i]) {
					l->grace[i]++;
					FCD(("Grace\n"))
				} else {
					fc->state = FC_STATE_OFFLINE;
					enable_irq(fc->irq);
					dma_unmap_single (fc->dev, l->fcmds[i].cmd, 3 * sizeof(logi), DMA_BIDIRECTIONAL);
					if (atomic_dec_and_test (&l->todo))
						goto all_done;
				}
			}
			ret = fc->hw_enque (fc, fc->login);
			enable_irq(fc->irq);
			if (!ret) {
				nqueued++;
				continue;
			}
			if (ret == -ENOSYS && fc->login->proto == PROTO_REPORT_AL_MAP) {
				/* Oh yes, this card handles Point-to-Point only, so let's try that. */
				fc_hdr *fch;

				FCD(("SID %d DID %d\n", fc->sid, fc->did))
				fcmd = l->fcmds + i;
				dma_unmap_single(fc->dev, fcmd->cmd, 3 * sizeof(logi), DMA_BIDIRECTIONAL);
				fch = &fcmd->fch;
				FILL_FCHDR_RCTL_DID(fch, R_CTL_ELS_REQ, FS_FABRIC_F_PORT);
				FILL_FCHDR_SID(fch, 0);
				FILL_FCHDR_TYPE_FCTL(fch, TYPE_EXTENDED_LS, F_CTL_FIRST_SEQ | F_CTL_SEQ_INITIATIVE);
				FILL_FCHDR_SEQ_DF_SEQ(fch, 0, 0, 0);
				FILL_FCHDR_OXRX(fch, 0xffff, 0xffff);
				fch->param = 0;
				l->logi [3 * i].code = LS_FLOGI;
				fcmd->cmd = dma_map_single (fc->dev, l->logi + 3 * i, 3 * sizeof(logi), DMA_BIDIRECTIONAL);
				fcmd->rsp = fcmd->cmd + sizeof(logi);
				fcmd->cmdlen = sizeof(logi);
				fcmd->rsplen = sizeof(logi);
				fcmd->data = (dma_addr_t)NULL;
				fcmd->class = FC_CLASS_SIMPLE;
				fcmd->proto = TYPE_EXTENDED_LS;
			} else
				printk ("FC: Cannot enque FLOGI/REPORT_MAP packet on %s\n", fc->name);
		}
		
		if (nqueued) {
			l->timer.expires = jiffies + 5 * HZ;
			add_timer(&l->timer);

			down(&l->sem);
			if (!atomic_read(&l->todo)) {
				FCND(("All channels answered in time\n"))
				break; /* All fc channels have answered us */
			}
		}
	}
all_done:
	for (fc = fcchain, i = 0; fc && i < count; fc = fc->next, i++) {
		fc->ls = NULL;
		switch (fc->state) {
		case FC_STATE_ONLINE: break;
		case FC_STATE_OFFLINE: break;
		default: dma_unmap_single (fc->dev, l->fcmds[i].cmd, 3 * sizeof(logi), DMA_BIDIRECTIONAL);
			break;
		}
	}
	del_timer(&l->timer);
	kfree (l->logi);
	kfree (l->fcmds);
	kfree (l);
	return 0;
}

int fcp_forceoffline(fc_channel *fcchain, int count)
{
	fc_channel *fc;
	fcp_cmnd *fcmd;
	int i, ret;
	lso l;

	memset (&l, 0, sizeof(lso));
	l.count = count;
	l.magic = LSOMAGIC;
	FCND(("FCP Force Offline for %d channels\n", count))
	init_MUTEX_LOCKED(&l.sem);
	init_timer(&l.timer);
	l.timer.function = fcp_login_timeout;
	l.timer.data = (unsigned long)&l;
	atomic_set (&l.todo, count);
	l.fcmds = kmalloc (count * sizeof(fcp_cmnd), GFP_KERNEL);
	if (!l.fcmds) {
		kfree (l.fcmds);
		printk ("FC: Cannot allocate memory for forcing offline\n");
		return -ENOMEM;
	}
	memset (l.fcmds, 0, count * sizeof(fcp_cmnd));
	FCND(("Initializing OFFLINE packets\n"))
	for (fc = fcchain, i = 0; fc && i < count; fc = fc->next, i++) {
		fc->state = FC_STATE_UNINITED;
		fcmd = l.fcmds + i;
		fc->login = fcmd;
		fc->ls = (void *)&l;
		fcmd->did = fc->did;
		fcmd->class = FC_CLASS_OFFLINE;
		fcmd->proto = PROTO_OFFLINE;
		fcmd->token = i;
		fcmd->fc = fc;
		disable_irq(fc->irq);
		ret = fc->hw_enque (fc, fc->login);
		enable_irq(fc->irq);
		if (ret) printk ("FC: Cannot enque OFFLINE packet on %s\n", fc->name);
	}
		
	l.timer.expires = jiffies + 5 * HZ;
	add_timer(&l.timer);
	down(&l.sem);
	del_timer(&l.timer);
	
	for (fc = fcchain, i = 0; fc && i < count; fc = fc->next, i++)
		fc->ls = NULL;
	kfree (l.fcmds);
	return 0;
}

int fcp_init(fc_channel *fcchain)
{
	fc_channel *fc;
	int count=0;
	int ret;
	
	for (fc = fcchain; fc; fc = fc->next) {
		fc->fcp_register = fcp_register;
		count++;
	}

	ret = fcp_initialize (fcchain, count);
	if (ret)
		return ret;
		
	if (!fc_channels)
		fc_channels = fcchain;
	else {
		for (fc = fc_channels; fc->next; fc = fc->next);
		fc->next = fcchain;
	}
	return ret;
}

void fcp_release(fc_channel *fcchain, int count)  /* count must > 0 */
{
	fc_channel *fc;
	fc_channel *fcx;

	for (fc = fcchain; --count && fc->next; fc = fc->next);
	if (count) {
		printk("FC: nothing to release\n");
		return;
	}
	
	if (fc_channels == fcchain)
		fc_channels = fc->next;
	else {
		for (fcx = fc_channels; fcx->next != fcchain; fcx = fcx->next);
		fcx->next = fc->next;
	}
	fc->next = NULL;

	/*
	 *  We've just grabbed fcchain out of the fc_channel list
	 *  and zero-terminated it, while destroying the count.
	 *
	 *  Freeing the fc's is the low level driver's responsibility.
	 */
}


static void fcp_scsi_done (Scsi_Cmnd *SCpnt)
{
	unsigned long flags;

	spin_lock_irqsave(SCpnt->device->host->host_lock, flags);
	if (FCP_CMND(SCpnt)->done)
		FCP_CMND(SCpnt)->done(SCpnt);
	spin_unlock_irqrestore(SCpnt->device->host->host_lock, flags);
}

static int fcp_scsi_queue_it(fc_channel *fc, Scsi_Cmnd *SCpnt, fcp_cmnd *fcmd, int prepare)
{
	long i;
	fcp_cmd *cmd;
	u32 fcp_cntl;
	if (prepare) {
		i = find_first_zero_bit (fc->scsi_bitmap, fc->scsi_bitmap_end);
		set_bit (i, fc->scsi_bitmap);
		fcmd->token = i;
		cmd = fc->scsi_cmd_pool + i;

		if (fc->encode_addr (SCpnt, cmd->fcp_addr, fc, fcmd)) {
			/* Invalid channel/id/lun and couldn't map it into fcp_addr */
			clear_bit (i, fc->scsi_bitmap);
			SCpnt->result = (DID_BAD_TARGET << 16);
			SCpnt->scsi_done(SCpnt);
			return 0;
		}
		fc->scsi_free--;
		fc->cmd_slots[fcmd->token] = fcmd;

		if (SCpnt->device->tagged_supported) {
			if (jiffies - fc->ages[SCpnt->device->channel * fc->targets + SCpnt->device->id] > (5 * 60 * HZ)) {
				fc->ages[SCpnt->device->channel * fc->targets + SCpnt->device->id] = jiffies;
				fcp_cntl = FCP_CNTL_QTYPE_ORDERED;
			} else
				fcp_cntl = FCP_CNTL_QTYPE_SIMPLE;
		} else
			fcp_cntl = FCP_CNTL_QTYPE_UNTAGGED;
		if (!SCpnt->request_bufflen && !SCpnt->use_sg) {
			cmd->fcp_cntl = fcp_cntl;
			fcmd->data = (dma_addr_t)NULL;
		} else {
			switch (SCpnt->cmnd[0]) {
			case WRITE_6:
			case WRITE_10:
			case WRITE_12:
				cmd->fcp_cntl = (FCP_CNTL_WRITE | fcp_cntl); break;
			default:
				cmd->fcp_cntl = (FCP_CNTL_READ | fcp_cntl); break;
			}
			if (!SCpnt->use_sg) {
				cmd->fcp_data_len = SCpnt->request_bufflen;
				fcmd->data = dma_map_single (fc->dev, (char *)SCpnt->request_buffer,
							     SCpnt->request_bufflen,
							     SCpnt->sc_data_direction);
			} else {
				struct scatterlist *sg = (struct scatterlist *)SCpnt->buffer;
				int nents;

				FCD(("XXX: Use_sg %d %d\n", SCpnt->use_sg, sg->length))
				nents = dma_map_sg (fc->dev, sg, SCpnt->use_sg,
						    SCpnt->sc_data_direction);
				if (nents > 1) printk ("%s: SG for nents %d (use_sg %d) not handled yet\n", fc->name, nents, SCpnt->use_sg);
				fcmd->data = sg_dma_address(sg);
				cmd->fcp_data_len = sg_dma_len(sg);
			}
		}
		memcpy (cmd->fcp_cdb, SCpnt->cmnd, SCpnt->cmd_len);
		memset (cmd->fcp_cdb+SCpnt->cmd_len, 0, sizeof(cmd->fcp_cdb)-SCpnt->cmd_len);
		FCD(("XXX: %04x.%04x.%04x.%04x - %08x%08x%08x\n", cmd->fcp_addr[0], cmd->fcp_addr[1], cmd->fcp_addr[2], cmd->fcp_addr[3], *(u32 *)SCpnt->cmnd, *(u32 *)(SCpnt->cmnd+4), *(u32 *)(SCpnt->cmnd+8)))
	}
	FCD(("Trying to enque %p\n", fcmd))
	if (!fc->scsi_que) {
		if (!fc->hw_enque (fc, fcmd)) {
			FCD(("hw_enque succeeded for %p\n", fcmd))
			return 0;
		}
	}
	FCD(("Putting into que1 %p\n", fcmd))
	fcp_scsi_insert_queue (fc, fcmd);
	return 0;
}

int fcp_scsi_queuecommand(Scsi_Cmnd *SCpnt, void (* done)(Scsi_Cmnd *))
{
	fcp_cmnd *fcmd = FCP_CMND(SCpnt);
	fc_channel *fc = FC_SCMND(SCpnt);
	
	FCD(("Entering SCSI queuecommand %p\n", fcmd))
	if (SCpnt->done != fcp_scsi_done) {
		fcmd->done = SCpnt->done;
		SCpnt->done = fcp_scsi_done;
		SCpnt->scsi_done = done;
		fcmd->proto = TYPE_SCSI_FCP;
		if (!fc->scsi_free) {
			FCD(("FC: !scsi_free, putting cmd on ML queue\n"))
#if (FCP_SCSI_USE_NEW_EH_CODE == 0)
			printk("fcp_scsi_queue_command: queue full, losing cmd, bad\n");
#endif
			return 1;
		}
		return fcp_scsi_queue_it(fc, SCpnt, fcmd, 1);
	}
	return fcp_scsi_queue_it(fc, SCpnt, fcmd, 0);
}

void fcp_queue_empty(fc_channel *fc)
{
	fcp_cmnd *fcmd;
	
	FCD(("Queue empty\n"))
	while ((fcmd = fc->scsi_que)) {
		/* The hw told us we can try again queue some packet */
		if (fc->hw_enque (fc, fcmd))
			break;
		fcp_scsi_remove_queue (fc, fcmd);
	}
}

int fcp_scsi_abort(Scsi_Cmnd *SCpnt)
{
	/* Internal bookkeeping only. Lose 1 cmd_slots slot. */
	fcp_cmnd *fcmd = FCP_CMND(SCpnt);
	fc_channel *fc = FC_SCMND(SCpnt);
	
	/*
	 * We react to abort requests by simply forgetting
	 * about the command and pretending everything's sweet.
	 * This may or may not be silly. We can't, however,
	 * immediately reuse the command's cmd_slots slot,
	 * as its result may arrive later and we cannot
	 * check whether it is the aborted one, can't we?
	 *
	 * Therefore, after the first few aborts are done,
	 * we tell the scsi error handler to do something clever.
	 * It will eventually call host reset, refreshing
	 * cmd_slots for us.
	 *
	 * There is a theoretical chance that we sometimes allow
	 * more than can_queue packets to the jungle this way,
	 * but the worst outcome possible is a series of
	 * more aborts and eventually the dev_reset catharsis.
	 */

	if (++fc->abort_count < (fc->can_queue >> 1)) {
		unsigned long flags;

		SCpnt->result = DID_ABORT;
		spin_lock_irqsave(SCpnt->device->host->host_lock, flags);
		fcmd->done(SCpnt);
		spin_unlock_irqrestore(SCpnt->device->host->host_lock, flags);
		printk("FC: soft abort\n");
		return SUCCESS;
	} else {
		printk("FC: hard abort refused\n");
		return FAILED;
	}
}

void fcp_scsi_reset_done(Scsi_Cmnd *SCpnt)
{
	fc_channel *fc = FC_SCMND(SCpnt);

	fc->rst_pkt->eh_state = SCSI_STATE_FINISHED;
	up(fc->rst_pkt->device->host->eh_action);
}

#define FCP_RESET_TIMEOUT (2*HZ)

int fcp_scsi_dev_reset(Scsi_Cmnd *SCpnt)
{
	fcp_cmd *cmd;
	fcp_cmnd *fcmd;
	fc_channel *fc = FC_SCMND(SCpnt);
        DECLARE_MUTEX_LOCKED(sem);

	if (!fc->rst_pkt) {
		fc->rst_pkt = (Scsi_Cmnd *) kmalloc(sizeof(SCpnt), GFP_KERNEL);
		if (!fc->rst_pkt) return FAILED;
		
		fcmd = FCP_CMND(fc->rst_pkt);


		fcmd->token = 0;
		cmd = fc->scsi_cmd_pool + 0;
		FCD(("Preparing rst packet\n"))
		fc->encode_addr (SCpnt, cmd->fcp_addr, fc, fcmd);
		fc->rst_pkt->device = SCpnt->device;
		fc->rst_pkt->cmd_len = 0;
		
		fc->cmd_slots[0] = fcmd;

		cmd->fcp_cntl = FCP_CNTL_QTYPE_ORDERED | FCP_CNTL_RESET;
		fcmd->data = (dma_addr_t)NULL;
		fcmd->proto = TYPE_SCSI_FCP;

		memcpy (cmd->fcp_cdb, SCpnt->cmnd, SCpnt->cmd_len);
		memset (cmd->fcp_cdb+SCpnt->cmd_len, 0, sizeof(cmd->fcp_cdb)-SCpnt->cmd_len);
		FCD(("XXX: %04x.%04x.%04x.%04x - %08x%08x%08x\n", cmd->fcp_addr[0], cmd->fcp_addr[1], cmd->fcp_addr[2], cmd->fcp_addr[3], *(u32 *)SCpnt->cmnd, *(u32 *)(SCpnt->cmnd+4), *(u32 *)(SCpnt->cmnd+8)))
	} else {
		fcmd = FCP_CMND(fc->rst_pkt);
		if (fc->rst_pkt->eh_state == SCSI_STATE_QUEUED)
			return FAILED; /* or SUCCESS. Only these */
	}
	fc->rst_pkt->done = NULL;


        fc->rst_pkt->eh_state = SCSI_STATE_QUEUED;
	init_timer(&fc->rst_pkt->eh_timeout);
	fc->rst_pkt->eh_timeout.data = (unsigned long) fc->rst_pkt;
	fc->rst_pkt->eh_timeout.expires = jiffies + FCP_RESET_TIMEOUT;
	fc->rst_pkt->eh_timeout.function = (void (*)(unsigned long))fcp_scsi_reset_done;

        add_timer(&fc->rst_pkt->eh_timeout);

	/*
	 * Set up the semaphore so we wait for the command to complete.
	 */

	fc->rst_pkt->device->host->eh_action = &sem;
	fc->rst_pkt->request->rq_status = RQ_SCSI_BUSY;

	fc->rst_pkt->done = fcp_scsi_reset_done;
	fcp_scsi_queue_it(fc, fc->rst_pkt, fcmd, 0);
	
	down(&sem);

	fc->rst_pkt->device->host->eh_action = NULL;
	del_timer(&fc->rst_pkt->eh_timeout);

	/*
	 * See if timeout.  If so, tell the host to forget about it.
	 * In other words, we don't want a callback any more.
	 */
	if (fc->rst_pkt->eh_state == SCSI_STATE_TIMEOUT ) {
		fc->rst_pkt->eh_state = SCSI_STATE_UNUSED;
		return FAILED;
	}
	fc->rst_pkt->eh_state = SCSI_STATE_UNUSED;
	return SUCCESS;
}

int fcp_scsi_bus_reset(Scsi_Cmnd *SCpnt)
{
	printk ("FC: bus reset!\n");
	return FAILED;
}

int fcp_scsi_host_reset(Scsi_Cmnd *SCpnt)
{
	fc_channel *fc = FC_SCMND(SCpnt);
	fcp_cmnd *fcmd = FCP_CMND(SCpnt);
	int i;

	printk ("FC: host reset\n");

	for (i=0; i < fc->can_queue; i++) {
		if (fc->cmd_slots[i] && SCpnt->result != DID_ABORT) {
			SCpnt->result = DID_RESET;
			fcmd->done(SCpnt);
			fc->cmd_slots[i] = NULL;
		}
	}
	fc->reset(fc);
	fc->abort_count = 0;
	if (fcp_initialize(fc, 1)) return SUCCESS;
	else return FAILED;
}

static int fcp_els_queue_it(fc_channel *fc, fcp_cmnd *fcmd)
{
	long i;

	i = find_first_zero_bit (fc->scsi_bitmap, fc->scsi_bitmap_end);
	set_bit (i, fc->scsi_bitmap);
	fcmd->token = i;
	fc->scsi_free--;
	fc->cmd_slots[fcmd->token] = fcmd;
	return fcp_scsi_queue_it(fc, NULL, fcmd, 0);
}

static int fc_do_els(fc_channel *fc, unsigned int alpa, void *data, int len)
{
	fcp_cmnd _fcmd, *fcmd;
	fc_hdr *fch;
	lse l;
	int i;

	fcmd = &_fcmd;
	memset(fcmd, 0, sizeof(fcmd));
	FCD(("PLOGI SID %d DID %d\n", fc->sid, alpa))
	fch = &fcmd->fch;
	FILL_FCHDR_RCTL_DID(fch, R_CTL_ELS_REQ, alpa);
	FILL_FCHDR_SID(fch, fc->sid);
	FILL_FCHDR_TYPE_FCTL(fch, TYPE_EXTENDED_LS, F_CTL_FIRST_SEQ | F_CTL_SEQ_INITIATIVE);
	FILL_FCHDR_SEQ_DF_SEQ(fch, 0, 0, 0);
	FILL_FCHDR_OXRX(fch, 0xffff, 0xffff);
	fch->param = 0;
	fcmd->cmd = dma_map_single (fc->dev, data, 2 * len, DMA_BIDIRECTIONAL);
	fcmd->rsp = fcmd->cmd + len;
	fcmd->cmdlen = len;
	fcmd->rsplen = len;
	fcmd->data = (dma_addr_t)NULL;
	fcmd->fc = fc;
	fcmd->class = FC_CLASS_SIMPLE;
	fcmd->proto = TYPE_EXTENDED_LS;
	
	memset (&l, 0, sizeof(lse));
	l.magic = LSEMAGIC;
	init_MUTEX_LOCKED(&l.sem);
	l.timer.function = fcp_login_timeout;
	l.timer.data = (unsigned long)&l;
	l.status = FC_STATUS_TIMED_OUT;
	fcmd->ls = (void *)&l;

	disable_irq(fc->irq);	
	fcp_els_queue_it(fc, fcmd);
	enable_irq(fc->irq);

	for (i = 0;;) {
		l.timer.expires = jiffies + 5 * HZ;
		add_timer(&l.timer);
		down(&l.sem);
		del_timer(&l.timer);
		if (l.status != FC_STATUS_TIMED_OUT) break;
		if (++i == 3) break;
		disable_irq(fc->irq);
		fcp_scsi_queue_it(fc, NULL, fcmd, 0);
		enable_irq(fc->irq);
	}
	
	clear_bit(fcmd->token, fc->scsi_bitmap);
	fc->scsi_free++;
	dma_unmap_single (fc->dev, fcmd->cmd, 2 * len, DMA_BIDIRECTIONAL);
	return l.status;
}

int fc_do_plogi(fc_channel *fc, unsigned char alpa, fc_wwn *node, fc_wwn *nport)
{
	logi *l;
	int status;

	l = (logi *)kmalloc(2 * sizeof(logi), GFP_KERNEL);
	if (!l) return -ENOMEM;
	memset(l, 0, 2 * sizeof(logi));
	l->code = LS_PLOGI;
	memcpy (&l->nport_wwn, &fc->wwn_nport, sizeof(fc_wwn));
	memcpy (&l->node_wwn, &fc->wwn_node, sizeof(fc_wwn));
	memcpy (&l->common, fc->common_svc, sizeof(common_svc_parm));
	memcpy (&l->class1, fc->class_svcs, 3*sizeof(svc_parm));
	status = fc_do_els(fc, alpa, l, sizeof(logi));
	if (status == FC_STATUS_OK) {
		if (l[1].code == LS_ACC) {
#ifdef FCDEBUG
			u32 *u = (u32 *)&l[1].nport_wwn;
			FCD(("AL-PA %02x: Port WWN %08x%08x Node WWN %08x%08x\n", alpa, 
				u[0], u[1], u[2], u[3]))
#endif
			memcpy(nport, &l[1].nport_wwn, sizeof(fc_wwn));
			memcpy(node, &l[1].node_wwn, sizeof(fc_wwn));
		} else
			status = FC_STATUS_BAD_RSP;
	}
	kfree(l);
	return status;
}

typedef struct {
	unsigned int code;
	unsigned params[4];
} prli;

int fc_do_prli(fc_channel *fc, unsigned char alpa)
{
	prli *p;
	int status;

	p = (prli *)kmalloc(2 * sizeof(prli), GFP_KERNEL);
	if (!p) return -ENOMEM;
	memset(p, 0, 2 * sizeof(prli));
	p->code = LS_PRLI;
	p->params[0] = 0x08002000;
	p->params[3] = 0x00000022;
	status = fc_do_els(fc, alpa, p, sizeof(prli));
	if (status == FC_STATUS_OK && p[1].code != LS_PRLI_ACC && p[1].code != LS_ACC)
		status = FC_STATUS_BAD_RSP;
	kfree(p);
	return status;
}

MODULE_LICENSE("GPL");

