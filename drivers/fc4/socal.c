/* socal.c: Sparc SUNW,socal (SOC+) Fibre Channel Sbus adapter support.
 *
 * Copyright (C) 1998,1999 Jakub Jelinek (jj@ultra.linux.cz)
 *
 * Sources:
 *	Fibre Channel Physical & Signaling Interface (FC-PH), dpANS, 1994
 *	dpANS Fibre Channel Protocol for SCSI (X3.269-199X), Rev. 012, 1995
 *	SOC+ Programming Guide 0.1
 *	Fibre Channel Arbitrated Loop (FC-AL), dpANS rev. 4.5, 1995
 *
 * Supported hardware:
 *      On-board SOC+ adapters of Ultra Enterprise servers and sun4d.
 */

static char *version =
        "socal.c: SOC+ driver v1.1 9/Feb/99 Jakub Jelinek (jj@ultra.linux.cz)\n";

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/errno.h>
#include <asm/byteorder.h>

#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/pgtable.h>
#include <asm/irq.h>

/* #define SOCALDEBUG */
/* #define HAVE_SOCAL_UCODE */
/* #define USE_64BIT_MODE */

#include "fcp_impl.h"
#include "socal.h"
#ifdef HAVE_SOCAL_UCODE
#include "socal_asm.h"
#endif

#define socal_printk printk ("socal%d: ", s->socal_no); printk 

#ifdef SOCALDEBUG
#define SOD(x)  socal_printk x;
#else
#define SOD(x)
#endif

#define for_each_socal(s) for (s = socals; s; s = s->next)
struct socal *socals = NULL;

static void socal_copy_from_xram(void *d, void __iomem *xram, long size)
{
	u32 *dp = (u32 *) d;
	while (size) {
		*dp++ = sbus_readl(xram);
		xram += sizeof(u32);
		size -= sizeof(u32);
	}
}

static void socal_copy_to_xram(void __iomem *xram, void *s, long size)
{
	u32 *sp = (u32 *) s;
	while (size) {
		u32 val = *sp++;
		sbus_writel(val, xram);
		xram += sizeof(u32);
		size -= sizeof(u32);
	}
}

#ifdef HAVE_SOCAL_UCODE
static void socal_bzero(unsigned long xram, int size)
{
	while (size) {
		sbus_writel(0, xram);
		xram += sizeof(u32);
		size -= sizeof(u32);
	}
}
#endif

static inline void socal_disable(struct socal *s)
{
	sbus_writel(0, s->regs + IMASK);
	sbus_writel(SOCAL_CMD_SOFT_RESET, s->regs + CMD);
}

static inline void socal_enable(struct socal *s)
{
	SOD(("enable %08x\n", s->cfg))
	sbus_writel(0, s->regs + SAE);
	sbus_writel(s->cfg, s->regs + CFG);
	sbus_writel(SOCAL_CMD_RSP_QALL, s->regs + CMD);
	SOCAL_SETIMASK(s, SOCAL_IMASK_RSP_QALL | SOCAL_IMASK_SAE);
	SOD(("imask %08x %08x\n", s->imask, sbus_readl(s->regs + IMASK)));
}

static void socal_reset(fc_channel *fc)
{
	socal_port *port = (socal_port *)fc;
	struct socal *s = port->s;
	
	/* FIXME */
	socal_disable(s);
	s->req[0].seqno = 1;
	s->req[1].seqno = 1;
	s->rsp[0].seqno = 1;
	s->rsp[1].seqno = 1;
	s->req[0].in = 0;
	s->req[1].in = 0;
	s->rsp[0].in = 0;
	s->rsp[1].in = 0;
	s->req[0].out = 0;
	s->req[1].out = 0;
	s->rsp[0].out = 0;
	s->rsp[1].out = 0;

	/* FIXME */
	socal_enable(s);
}

static inline void socal_solicited(struct socal *s, unsigned long qno)
{
	socal_rsp *hwrsp;
	socal_cq *sw_cq;
	int token;
	int status;
	fc_channel *fc;

	sw_cq = &s->rsp[qno];

	/* Finally an improvement against old SOC :) */
	sw_cq->in = sbus_readb(s->regs + RESP + qno);
	SOD (("socal_solicited, %d packets arrived\n",
	      (sw_cq->in - sw_cq->out) & sw_cq->last))
	for (;;) {
		hwrsp = (socal_rsp *)sw_cq->pool + sw_cq->out;
		SOD(("hwrsp %p out %d\n", hwrsp, sw_cq->out))
		
#if defined(SOCALDEBUG) && 0
		{
		u32 *u = (u32 *)hwrsp;
		SOD(("%08x.%08x.%08x.%08x.%08x.%08x.%08x.%08x\n",
		     u[0],u[1],u[2],u[3],u[4],u[5],u[6],u[7]))
		u += 8;
		SOD(("%08x.%08x.%08x.%08x.%08x.%08x.%08x.%08x\n",
		     u[0],u[1],u[2],u[3],u[4],u[5],u[6],u[7]))
		u = (u32 *)s->xram;
		while (u < ((u32 *)s->regs)) {
			if (sbus_readl(&u[0]) == 0x00003000 ||
			    sbus_readl(&u[0]) == 0x00003801) {
			SOD(("Found at %04lx\n",
			     (unsigned long)u - (unsigned long)s->xram))
			SOD(("  %08x.%08x.%08x.%08x.%08x.%08x.%08x.%08x\n",
			     sbus_readl(&u[0]), sbus_readl(&u[1]),
			     sbus_readl(&u[2]), sbus_readl(&u[3]),
			     sbus_readl(&u[4]), sbus_readl(&u[5]),
			     sbus_readl(&u[6]), sbus_readl(&u[7])))
			u += 8;
			SOD(("  %08x.%08x.%08x.%08x.%08x.%08x.%08x.%08x\n",
			     sbus_readl(&u[0]), sbus_readl(&u[1]),
			     sbus_readl(&u[2]), sbus_readl(&u[3]),
			     sbus_readl(&u[4]), sbus_readl(&u[5]),
			     sbus_readl(&u[6]), sbus_readl(&u[7])))
			u -= 8;
			}
			u++;
		}
		}
#endif

		token = hwrsp->shdr.token;
		status = hwrsp->status;
		fc = (fc_channel *)(&s->port[(token >> 11) & 1]);
		
		SOD(("Solicited token %08x status %08x\n", token, status))
		if (status == SOCAL_OK) {
			fcp_receive_solicited(fc, token >> 12,
					      token & ((1 << 11) - 1),
					      FC_STATUS_OK, NULL);
		} else {
			/* We have intentionally defined FC_STATUS_* constants
			 * to match SOCAL_* constants, otherwise we'd have to
			 * translate status.
			 */
			fcp_receive_solicited(fc, token >> 12,
					      token & ((1 << 11) - 1), status, &hwrsp->fchdr);
		}
			
		if (++sw_cq->out > sw_cq->last) {
			sw_cq->seqno++;
			sw_cq->out = 0;
		}
		
		if (sw_cq->out == sw_cq->in) {
			sw_cq->in = sbus_readb(s->regs + RESP + qno);
			if (sw_cq->out == sw_cq->in) {
				/* Tell the hardware about it */
				sbus_writel((sw_cq->out << 24) |
					    (SOCAL_CMD_RSP_QALL &
					     ~(SOCAL_CMD_RSP_Q0 << qno)),
					    s->regs + CMD);

				/* Read it, so that we're sure it has been updated */
				sbus_readl(s->regs + CMD);
				sw_cq->in = sbus_readb(s->regs + RESP + qno);
				if (sw_cq->out == sw_cq->in)
					break;
			}
		}
	}
}

static inline void socal_request (struct socal *s, u32 cmd)
{
	SOCAL_SETIMASK(s, s->imask & ~(cmd & SOCAL_CMD_REQ_QALL));
	SOD(("imask %08x %08x\n", s->imask, sbus_readl(s->regs + IMASK)));

	SOD(("Queues available %08x OUT %X\n", cmd, s->regs->reqpr[0]))
	if (s->port[s->curr_port].fc.state != FC_STATE_OFFLINE) {
		fcp_queue_empty ((fc_channel *)&(s->port[s->curr_port]));
		if (((s->req[1].in + 1) & s->req[1].last) != (s->req[1].out))
			fcp_queue_empty ((fc_channel *)&(s->port[1 - s->curr_port]));
	} else {
		fcp_queue_empty ((fc_channel *)&(s->port[1 - s->curr_port]));
	}
	if (s->port[1 - s->curr_port].fc.state != FC_STATE_OFFLINE)
		s->curr_port ^= 1;
}

static inline void socal_unsolicited (struct socal *s, unsigned long qno)
{
	socal_rsp *hwrsp, *hwrspc;
	socal_cq *sw_cq;
	int count;
	int status;
	int flags;
	fc_channel *fc;

	sw_cq = &s->rsp[qno];

	sw_cq->in = sbus_readb(s->regs + RESP + qno);
	SOD (("socal_unsolicited, %d packets arrived, in %d\n",
	      (sw_cq->in - sw_cq->out) & sw_cq->last, sw_cq->in))
	while (sw_cq->in != sw_cq->out) {
		/* ...real work per entry here... */
		hwrsp = (socal_rsp *)sw_cq->pool + sw_cq->out;
		SOD(("hwrsp %p out %d\n", hwrsp, sw_cq->out))

#if defined(SOCALDEBUG) && 0
		{
		u32 *u = (u32 *)hwrsp;
		SOD(("%08x.%08x.%08x.%08x.%08x.%08x.%08x.%08x\n",
		     u[0],u[1],u[2],u[3],u[4],u[5],u[6],u[7]))
		u += 8;
		SOD(("%08x.%08x.%08x.%08x.%08x.%08x.%08x.%08x\n",
		     u[0],u[1],u[2],u[3],u[4],u[5],u[6],u[7]))
		}
#endif

		hwrspc = NULL;
		flags = hwrsp->shdr.flags;
		count = hwrsp->count;
		fc = (fc_channel *)&s->port[flags & SOCAL_PORT_B];
		SOD(("FC %08lx\n", (long)fc))
		
		if (count != 1) {
			/* Ugh, continuation entries */
			u8 in;

			if (count != 2) {
				printk("%s: Too many continuations entries %d\n",
				       fc->name, count);
				goto update_out;
			}
			
			in = sw_cq->in;
			if (in < sw_cq->out)
				in += sw_cq->last + 1;
			if (in < sw_cq->out + 2) {
				/* Ask the hardware if they haven't arrived yet. */
				sbus_writel((sw_cq->out << 24) |
					    (SOCAL_CMD_RSP_QALL &
					     ~(SOCAL_CMD_RSP_Q0 << qno)),
					    s->regs + CMD);

				/* Read it, so that we're sure it has been updated */
				sbus_readl(s->regs + CMD);
				sw_cq->in = sbus_readb(s->regs + RESP + qno);
				in = sw_cq->in;
				if (in < sw_cq->out)
					in += sw_cq->last + 1;
				if (in < sw_cq->out + 2) /* Nothing came, let us wait */
					return;
			}
			if (sw_cq->out == sw_cq->last)
				hwrspc = (socal_rsp *)sw_cq->pool;
			else
				hwrspc = hwrsp + 1;
		}
		
		switch (flags & ~SOCAL_PORT_B) {
		case SOCAL_STATUS:
			status = hwrsp->status;
			switch (status) {
			case SOCAL_ONLINE:
				SOD(("State change to ONLINE\n"));
				fcp_state_change(fc, FC_STATE_ONLINE);
				break;
			case SOCAL_ONLINE_LOOP:
				SOD(("State change to ONLINE_LOOP\n"));
				fcp_state_change(fc, FC_STATE_ONLINE);
				break;
			case SOCAL_OFFLINE:
				SOD(("State change to OFFLINE\n"));
				fcp_state_change(fc, FC_STATE_OFFLINE);
				break;
			default:
				printk ("%s: Unknown STATUS no %d\n",
					fc->name, status);
				break;
			};

			break;
		case (SOCAL_UNSOLICITED|SOCAL_FC_HDR):
			{
				int r_ctl = *((u8 *)&hwrsp->fchdr);
				unsigned len;
				
				if ((r_ctl & 0xf0) == R_CTL_EXTENDED_SVC) {
					len = hwrsp->shdr.bytecnt;
					if (len < 4 || !hwrspc) {
						printk ("%s: Invalid R_CTL %02x "
							"continuation entries\n",
							fc->name, r_ctl);
					} else {
						if (len > 60)
							len = 60;
						if (*(u32 *)hwrspc == LS_DISPLAY) {
							int i;
							
							for (i = 4; i < len; i++)
								if (((u8 *)hwrspc)[i] == '\n')
									((u8 *)hwrspc)[i] = ' ';
							((u8 *)hwrspc)[len] = 0;
							printk ("%s message: %s\n",
								fc->name, ((u8 *)hwrspc) + 4);
						} else {
							printk ("%s: Unknown LS_CMD "
								"%08x\n", fc->name,
								*(u32 *)hwrspc);
						}
					}
				} else {
					printk ("%s: Unsolicited R_CTL %02x "
						"not handled\n", fc->name, r_ctl);
				}
			}
			break;
		default:
			printk ("%s: Unexpected flags %08x\n", fc->name, flags);
			break;
		};
update_out:
		if (++sw_cq->out > sw_cq->last) {
			sw_cq->seqno++;
			sw_cq->out = 0;
		}
		
		if (hwrspc) {
			if (++sw_cq->out > sw_cq->last) {
				sw_cq->seqno++;
				sw_cq->out = 0;
			}
		}
		
		if (sw_cq->out == sw_cq->in) {
			sw_cq->in = sbus_readb(s->regs + RESP + qno);
			if (sw_cq->out == sw_cq->in) {
				/* Tell the hardware about it */
				sbus_writel((sw_cq->out << 24) |
					    (SOCAL_CMD_RSP_QALL &
					     ~(SOCAL_CMD_RSP_Q0 << qno)),
					    s->regs + CMD);

				/* Read it, so that we're sure it has been updated */
				sbus_readl(s->regs + CMD);
				sw_cq->in = sbus_readb(s->regs + RESP + qno);
			}
		}
	}
}

static irqreturn_t socal_intr(int irq, void *dev_id)
{
	u32 cmd;
	unsigned long flags;
	register struct socal *s = (struct socal *)dev_id;

	spin_lock_irqsave(&s->lock, flags);
	cmd = sbus_readl(s->regs + CMD);
	for (; (cmd = SOCAL_INTR (s, cmd)); cmd = sbus_readl(s->regs + CMD)) {
#ifdef SOCALDEBUG
		static int cnt = 0;
		if (cnt++ < 50)
			printk("soc_intr %08x\n", cmd);
#endif	
		if (cmd & SOCAL_CMD_RSP_Q2)
			socal_unsolicited (s, SOCAL_UNSOLICITED_RSP_Q);
		if (cmd & SOCAL_CMD_RSP_Q1)
			socal_unsolicited (s, SOCAL_SOLICITED_BAD_RSP_Q);
		if (cmd & SOCAL_CMD_RSP_Q0)
			socal_solicited (s, SOCAL_SOLICITED_RSP_Q);
		if (cmd & SOCAL_CMD_REQ_QALL)
			socal_request (s, cmd);
	}
	spin_unlock_irqrestore(&s->lock, flags);

	return IRQ_HANDLED;
}

#define TOKEN(proto, port, token) (((proto)<<12)|(token)|(port))

static int socal_hw_enque (fc_channel *fc, fcp_cmnd *fcmd)
{
	socal_port *port = (socal_port *)fc;
	struct socal *s = port->s;
	unsigned long qno;
	socal_cq *sw_cq;
	int cq_next_in;
	socal_req *request;
	fc_hdr *fch;
	int i;

	if (fcmd->proto == TYPE_SCSI_FCP)
		qno = 1;
	else
		qno = 0;
	SOD(("Putting a FCP packet type %d into hw queue %d\n", fcmd->proto, qno))
	if (s->imask & (SOCAL_IMASK_REQ_Q0 << qno)) {
		SOD(("EIO %08x\n", s->imask))
		return -EIO;
	}
	sw_cq = s->req + qno;
	cq_next_in = (sw_cq->in + 1) & sw_cq->last;
	
	if (cq_next_in == sw_cq->out &&
	    cq_next_in == (sw_cq->out = sbus_readb(s->regs + REQP + qno))) {
		SOD(("%d IN %d OUT %d LAST %d\n",
		     qno, sw_cq->in,
		     sw_cq->out, sw_cq->last))
		SOCAL_SETIMASK(s, s->imask | (SOCAL_IMASK_REQ_Q0 << qno));
		SOD(("imask %08x %08x\n", s->imask, sbus_readl(s->regs + IMASK)));

		/* If queue is full, just say NO. */
		return -EBUSY;
	}
	
	request = sw_cq->pool + sw_cq->in;
	fch = &request->fchdr;
	
	switch (fcmd->proto) {
	case TYPE_SCSI_FCP:
		request->shdr.token = TOKEN(TYPE_SCSI_FCP, port->mask, fcmd->token); 
		request->data[0].base = fc->dma_scsi_cmd + fcmd->token * sizeof(fcp_cmd);
		request->data[0].count = sizeof(fcp_cmd);
		request->data[1].base = fc->dma_scsi_rsp + fcmd->token * fc->rsp_size;
		request->data[1].count = fc->rsp_size;
		if (fcmd->data) {
			request->shdr.segcnt = 3;
			i = fc->scsi_cmd_pool[fcmd->token].fcp_data_len;
			request->shdr.bytecnt = i;
			request->data[2].base = fcmd->data;
			request->data[2].count = i;
			request->type = (fc->scsi_cmd_pool[fcmd->token].fcp_cntl & FCP_CNTL_WRITE) ?
				SOCAL_CQTYPE_IO_WRITE : SOCAL_CQTYPE_IO_READ;
		} else {
			request->shdr.segcnt = 2;
			request->shdr.bytecnt = 0;
			request->data[2].base = 0;
			request->data[2].count = 0;
			request->type = SOCAL_CQTYPE_SIMPLE;
		}
		FILL_FCHDR_RCTL_DID(fch, R_CTL_COMMAND, fcmd->did);
		FILL_FCHDR_SID(fch, fc->sid);
		FILL_FCHDR_TYPE_FCTL(fch, TYPE_SCSI_FCP, F_CTL_FIRST_SEQ | F_CTL_SEQ_INITIATIVE);
		FILL_FCHDR_SEQ_DF_SEQ(fch, 0, 0, 0);
		FILL_FCHDR_OXRX(fch, 0xffff, 0xffff);
		fch->param = 0;
		request->shdr.flags = port->flags;
		request->shdr.class = fc->posmap ? 3 : 2;
		break;
		
	case PROTO_OFFLINE:
		memset (request, 0, sizeof(*request));
		request->shdr.token = TOKEN(PROTO_OFFLINE, port->mask, fcmd->token); 
		request->type = SOCAL_CQTYPE_OFFLINE;
		FILL_FCHDR_RCTL_DID(fch, R_CTL_COMMAND, fcmd->did);
		FILL_FCHDR_SID(fch, fc->sid);
		FILL_FCHDR_TYPE_FCTL(fch, TYPE_SCSI_FCP, F_CTL_FIRST_SEQ | F_CTL_SEQ_INITIATIVE);
		FILL_FCHDR_SEQ_DF_SEQ(fch, 0, 0, 0);
		FILL_FCHDR_OXRX(fch, 0xffff, 0xffff);
		request->shdr.flags = port->flags;
		break;
		
	case PROTO_REPORT_AL_MAP:
		memset (request, 0, sizeof(*request));
		request->shdr.token = TOKEN(PROTO_REPORT_AL_MAP, port->mask, fcmd->token); 
		request->type = SOCAL_CQTYPE_REPORT_MAP;
		request->shdr.flags = port->flags;
		request->shdr.segcnt = 1;
		request->shdr.bytecnt = sizeof(fc_al_posmap);
		request->data[0].base = fcmd->cmd;
		request->data[0].count = sizeof(fc_al_posmap);
		break;

	default: 
		request->shdr.token = TOKEN(fcmd->proto, port->mask, fcmd->token);
		request->shdr.class = fc->posmap ? 3 : 2;
		request->shdr.flags = port->flags;
		memcpy (fch, &fcmd->fch, sizeof(fc_hdr));
		request->data[0].count = fcmd->cmdlen;
		request->data[1].count = fcmd->rsplen;
		request->type = fcmd->class;
		switch (fcmd->class) {
		case FC_CLASS_OUTBOUND:
			request->data[0].base = fcmd->cmd;
			request->data[0].count = fcmd->cmdlen;
			request->type = SOCAL_CQTYPE_OUTBOUND;
			request->shdr.bytecnt = fcmd->cmdlen;
			request->shdr.segcnt = 1;
			break;
		case FC_CLASS_INBOUND:
			request->data[0].base = fcmd->rsp;
			request->data[0].count = fcmd->rsplen;
			request->type = SOCAL_CQTYPE_INBOUND;
			request->shdr.bytecnt = 0;
			request->shdr.segcnt = 1;
			break;
		case FC_CLASS_SIMPLE:
			request->data[0].base = fcmd->cmd;
			request->data[1].base = fcmd->rsp;
			request->data[0].count = fcmd->cmdlen;
			request->data[1].count = fcmd->rsplen;
			request->type = SOCAL_CQTYPE_SIMPLE;
			request->shdr.bytecnt = fcmd->cmdlen;
			request->shdr.segcnt = 2;
			break;
		case FC_CLASS_IO_READ:
		case FC_CLASS_IO_WRITE:
			request->data[0].base = fcmd->cmd;
			request->data[1].base = fcmd->rsp;
			request->data[0].count = fcmd->cmdlen;
			request->data[1].count = fcmd->rsplen;
			request->type = (fcmd->class == FC_CLASS_IO_READ) ? SOCAL_CQTYPE_IO_READ : SOCAL_CQTYPE_IO_WRITE;
			if (fcmd->data) {
				request->data[2].base = fcmd->data;
				request->data[2].count = fcmd->datalen;
				request->shdr.bytecnt = fcmd->datalen;
				request->shdr.segcnt = 3;
			} else {
				request->shdr.bytecnt = 0;
				request->shdr.segcnt = 2;
			}
			break;
		}
		break;
	}

	request->count = 1;
	request->flags = 0;
	request->seqno = sw_cq->seqno;
	
	SOD(("queueing token %08x\n", request->shdr.token))
	
	/* And now tell the SOCAL about it */

	if (++sw_cq->in > sw_cq->last) {
		sw_cq->in = 0;
		sw_cq->seqno++;
	}
	
	SOD(("Putting %08x into cmd\n", SOCAL_CMD_RSP_QALL | (sw_cq->in << 24) | (SOCAL_CMD_REQ_Q0 << qno)))
	
	sbus_writel(SOCAL_CMD_RSP_QALL | (sw_cq->in << 24) | (SOCAL_CMD_REQ_Q0 << qno),
		    s->regs + CMD);

	/* Read so that command is completed */	
	sbus_readl(s->regs + CMD);
	
	return 0;
}

static inline void socal_download_fw(struct socal *s)
{
#ifdef HAVE_SOCAL_UCODE
	SOD(("Loading %ld bytes from %p to %p\n", sizeof(socal_ucode), socal_ucode, s->xram))
	socal_copy_to_xram(s->xram, socal_ucode, sizeof(socal_ucode));
	SOD(("Clearing the rest of memory\n"))
	socal_bzero (s->xram + sizeof(socal_ucode), 65536 - sizeof(socal_ucode));
	SOD(("Done\n"))
#endif
}

/* Check for what the best SBUS burst we can use happens
 * to be on this machine.
 */
static inline void socal_init_bursts(struct socal *s, struct sbus_dev *sdev)
{
	int bsizes, bsizes_more;
	u32 cfg;

	bsizes = (prom_getintdefault(sdev->prom_node,"burst-sizes",0xff) & 0xff);
	bsizes_more = (prom_getintdefault(sdev->bus->prom_node, "burst-sizes", 0xff) & 0xff);
	bsizes &= bsizes_more;
#ifdef USE_64BIT_MODE
#ifdef __sparc_v9__
	mmu_set_sbus64(sdev, bsizes >> 16);
#endif
#endif
	if ((bsizes & 0x7f) == 0x7f)
		cfg = SOCAL_CFG_BURST_64;
	else if ((bsizes & 0x3f) == 0x3f) 
		cfg = SOCAL_CFG_BURST_32;
	else if ((bsizes & 0x1f) == 0x1f)
		cfg = SOCAL_CFG_BURST_16;
	else
		cfg = SOCAL_CFG_BURST_4;
#ifdef USE_64BIT_MODE
#ifdef __sparc_v9__
	/* What is BURST_128? -jj */
	if ((bsizes & 0x780000) == 0x780000)
		cfg |= (SOCAL_CFG_BURST_64 << 8) | SOCAL_CFG_SBUS_ENHANCED;
	else if ((bsizes & 0x380000) == 0x380000) 
		cfg |= (SOCAL_CFG_BURST_32 << 8) | SOCAL_CFG_SBUS_ENHANCED;
	else if ((bsizes & 0x180000) == 0x180000)
		cfg |= (SOCAL_CFG_BURST_16 << 8) | SOCAL_CFG_SBUS_ENHANCED;
	else
		cfg |= (SOCAL_CFG_BURST_8 << 8) | SOCAL_CFG_SBUS_ENHANCED;
#endif
#endif		
	s->cfg = cfg;
}

static inline void socal_init(struct sbus_dev *sdev, int no)
{
	unsigned char tmp[60];
	int propl;
	struct socal *s;
	static unsigned version_printed = 0;
	socal_hw_cq cq[8];
	int size, i;
	int irq, node;
	
	s = kzalloc (sizeof (struct socal), GFP_KERNEL);
	if (!s) return;
	spin_lock_init(&s->lock);
	s->socal_no = no;

	SOD(("socals %08lx socal_intr %08lx socal_hw_enque %08lx\n",
	     (long)socals, (long)socal_intr, (long)socal_hw_enque))
	if (version_printed++ == 0)
		printk (version);

	s->port[0].fc.module = THIS_MODULE;
	s->port[1].fc.module = THIS_MODULE;
                                	
	s->next = socals;
	socals = s;
	s->port[0].fc.dev = sdev;
	s->port[1].fc.dev = sdev;
	s->port[0].s = s;
	s->port[1].s = s;

	s->port[0].fc.next = &s->port[1].fc;

	/* World Wide Name of SOCAL */
	propl = prom_getproperty (sdev->prom_node, "wwn", tmp, sizeof(tmp));
	if (propl != sizeof (fc_wwn)) {
		s->wwn.naaid = NAAID_IEEE_REG;
		s->wwn.nportid = 0x123;
		s->wwn.hi = 0x1234;
		s->wwn.lo = 0x12345678;
	} else
		memcpy (&s->wwn, tmp, sizeof (fc_wwn));
	
	memcpy (&s->port[0].fc.wwn_nport, &s->wwn, sizeof (fc_wwn));
	s->port[0].fc.wwn_nport.lo++;
	memcpy (&s->port[1].fc.wwn_nport, &s->wwn, sizeof (fc_wwn));
	s->port[1].fc.wwn_nport.lo+=2;
	
	node = prom_getchild (sdev->prom_node);
	while (node && (node = prom_searchsiblings (node, "sf"))) {
		int port;
		
		port = prom_getintdefault(node, "port#", -1);
		switch (port) {
		case 0:
		case 1:
			if (prom_getproplen(node, "port-wwn") == sizeof (fc_wwn))
				prom_getproperty (node, "port-wwn", 
						  (char *)&s->port[port].fc.wwn_nport,
						  sizeof (fc_wwn));
			break;
		default:
			break;
		};

		node = prom_getsibling(node);
	}

	memcpy (&s->port[0].fc.wwn_node, &s->wwn, sizeof (fc_wwn));
	memcpy (&s->port[1].fc.wwn_node, &s->wwn, sizeof (fc_wwn));
	SOD(("Got wwns %08x%08x ports %08x%08x and %08x%08x\n", 
	     *(u32 *)&s->port[0].fc.wwn_node, s->port[0].fc.wwn_node.lo,
	     *(u32 *)&s->port[0].fc.wwn_nport, s->port[0].fc.wwn_nport.lo,
	     *(u32 *)&s->port[1].fc.wwn_nport, s->port[1].fc.wwn_nport.lo))
		
	s->port[0].fc.sid = 1;
	s->port[1].fc.sid = 17;
	s->port[0].fc.did = 2;
	s->port[1].fc.did = 18;
	
	s->port[0].fc.reset = socal_reset;
	s->port[1].fc.reset = socal_reset;
	
	if (sdev->num_registers == 1) {
		s->eeprom = sbus_ioremap(&sdev->resource[0], 0,
					 sdev->reg_addrs[0].reg_size, "socal xram");
		if (sdev->reg_addrs[0].reg_size > 0x20000)
			s->xram = s->eeprom + 0x10000UL;
		else
			s->xram = s->eeprom;
		s->regs = (s->xram + 0x10000UL);
	} else {
		/* E.g. starfire presents 3 registers for SOCAL */
		s->xram = sbus_ioremap(&sdev->resource[1], 0,
				       sdev->reg_addrs[1].reg_size, "socal xram");
		s->regs = sbus_ioremap(&sdev->resource[2], 0,
				       sdev->reg_addrs[2].reg_size, "socal regs");
	}
	
	socal_init_bursts(s, sdev);
	
	SOD(("Disabling SOCAL\n"))
	
	socal_disable (s);
	
	irq = sdev->irqs[0];

	if (request_irq (irq, socal_intr, IRQF_SHARED, "SOCAL", (void *)s)) {
		socal_printk ("Cannot order irq %d to go\n", irq);
		socals = s->next;
		return;
	}

	SOD(("SOCAL uses IRQ %d\n", irq))
	
	s->port[0].fc.irq = irq;
	s->port[1].fc.irq = irq;
	
	sprintf (s->port[0].fc.name, "socal%d port A", no);
	sprintf (s->port[1].fc.name, "socal%d port B", no);
	s->port[0].flags = SOCAL_FC_HDR | SOCAL_PORT_A;
	s->port[1].flags = SOCAL_FC_HDR | SOCAL_PORT_B;
	s->port[1].mask = (1 << 11);
	
	s->port[0].fc.hw_enque = socal_hw_enque;
	s->port[1].fc.hw_enque = socal_hw_enque;
	
	socal_download_fw (s);
	
	SOD(("Downloaded firmware\n"))

	/* Now setup xram circular queues */
	memset (cq, 0, sizeof(cq));

	size = (SOCAL_CQ_REQ0_SIZE + SOCAL_CQ_REQ1_SIZE +
		SOCAL_CQ_RSP0_SIZE + SOCAL_CQ_RSP1_SIZE +
		SOCAL_CQ_RSP2_SIZE) * sizeof(socal_req);
	s->req_cpu = sbus_alloc_consistent(sdev, size, &s->req_dvma);
	s->req[0].pool = s->req_cpu;
	cq[0].address = s->req_dvma;
	s->req[1].pool = s->req[0].pool + SOCAL_CQ_REQ0_SIZE;
	s->rsp[0].pool = s->req[1].pool + SOCAL_CQ_REQ1_SIZE;
	s->rsp[1].pool = s->rsp[0].pool + SOCAL_CQ_RSP0_SIZE;
	s->rsp[2].pool = s->rsp[1].pool + SOCAL_CQ_RSP1_SIZE;
	
	s->req[0].hw_cq = (socal_hw_cq __iomem *)(s->xram + SOCAL_CQ_REQ_OFFSET);
	s->req[1].hw_cq = (socal_hw_cq __iomem *)(s->xram + SOCAL_CQ_REQ_OFFSET + sizeof(socal_hw_cq));
	s->rsp[0].hw_cq = (socal_hw_cq __iomem *)(s->xram + SOCAL_CQ_RSP_OFFSET);
	s->rsp[1].hw_cq = (socal_hw_cq __iomem *)(s->xram + SOCAL_CQ_RSP_OFFSET + sizeof(socal_hw_cq));
	s->rsp[2].hw_cq = (socal_hw_cq __iomem *)(s->xram + SOCAL_CQ_RSP_OFFSET + 2 * sizeof(socal_hw_cq));
	
	cq[1].address = cq[0].address + (SOCAL_CQ_REQ0_SIZE * sizeof(socal_req));
	cq[4].address = cq[1].address + (SOCAL_CQ_REQ1_SIZE * sizeof(socal_req));
	cq[5].address = cq[4].address + (SOCAL_CQ_RSP0_SIZE * sizeof(socal_req));
	cq[6].address = cq[5].address + (SOCAL_CQ_RSP1_SIZE * sizeof(socal_req));

	cq[0].last = SOCAL_CQ_REQ0_SIZE - 1;
	cq[1].last = SOCAL_CQ_REQ1_SIZE - 1;
	cq[4].last = SOCAL_CQ_RSP0_SIZE - 1;
	cq[5].last = SOCAL_CQ_RSP1_SIZE - 1;
	cq[6].last = SOCAL_CQ_RSP2_SIZE - 1;
	for (i = 0; i < 8; i++)
		cq[i].seqno = 1;
	
	s->req[0].last = SOCAL_CQ_REQ0_SIZE - 1;
	s->req[1].last = SOCAL_CQ_REQ1_SIZE - 1;
	s->rsp[0].last = SOCAL_CQ_RSP0_SIZE - 1;
	s->rsp[1].last = SOCAL_CQ_RSP1_SIZE - 1;
	s->rsp[2].last = SOCAL_CQ_RSP2_SIZE - 1;
	
	s->req[0].seqno = 1;
	s->req[1].seqno = 1;
	s->rsp[0].seqno = 1;
	s->rsp[1].seqno = 1;
	s->rsp[2].seqno = 1;
	
	socal_copy_to_xram(s->xram + SOCAL_CQ_REQ_OFFSET, cq, sizeof(cq));
	
	SOD(("Setting up params\n"))
	
	/* Make our sw copy of SOCAL service parameters */
	socal_copy_from_xram(s->serv_params, s->xram + 0x280, sizeof (s->serv_params));
	
	s->port[0].fc.common_svc = (common_svc_parm *)s->serv_params;
	s->port[0].fc.class_svcs = (svc_parm *)(s->serv_params + 0x20);
	s->port[1].fc.common_svc = (common_svc_parm *)&s->serv_params;
	s->port[1].fc.class_svcs = (svc_parm *)(s->serv_params + 0x20);
	
	socal_enable (s);
	
	SOD(("Enabled SOCAL\n"))
}

static int __init socal_probe(void)
{
	struct sbus_bus *sbus;
	struct sbus_dev *sdev = NULL;
	struct socal *s;
	int cards = 0;

	for_each_sbus(sbus) {
		for_each_sbusdev(sdev, sbus) {
			if(!strcmp(sdev->prom_name, "SUNW,socal")) {
				socal_init(sdev, cards);
				cards++;
			}
		}
	}
	if (!cards)
		return -EIO;

	for_each_socal(s)
		if (s->next)
			s->port[1].fc.next = &s->next->port[0].fc;
			
	fcp_init (&socals->port[0].fc);
	return 0;
}

static void __exit socal_cleanup(void)
{
	struct socal *s;
	int irq;
	struct sbus_dev *sdev;
	
	for_each_socal(s) {
		irq = s->port[0].fc.irq;
		free_irq (irq, s);

		fcp_release(&(s->port[0].fc), 2);

		sdev = s->port[0].fc.dev;
		if (sdev->num_registers == 1) {
			sbus_iounmap(s->eeprom, sdev->reg_addrs[0].reg_size);
		} else {
			sbus_iounmap(s->xram, sdev->reg_addrs[1].reg_size);
			sbus_iounmap(s->regs, sdev->reg_addrs[2].reg_size);
		}
		sbus_free_consistent(sdev,
				     (SOCAL_CQ_REQ0_SIZE + SOCAL_CQ_REQ1_SIZE +
				      SOCAL_CQ_RSP0_SIZE + SOCAL_CQ_RSP1_SIZE +
				      SOCAL_CQ_RSP2_SIZE) * sizeof(socal_req),
				     s->req_cpu, s->req_dvma);
	}
}

module_init(socal_probe);
module_exit(socal_cleanup);
MODULE_LICENSE("GPL");
