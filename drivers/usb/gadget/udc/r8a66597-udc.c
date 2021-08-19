// SPDX-License-Identifier: GPL-2.0
/*
 * R8A66597 UDC (USB gadget)
 *
 * Copyright (C) 2006-2009 Renesas Solutions Corp.
 *
 * Author : Yoshihiro Shimoda <yoshihiro.shimoda.uh@renesas.com>
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#include "r8a66597-udc.h"

#define DRIVER_VERSION	"2011-09-26"

static const char udc_name[] = "r8a66597_udc";
static const char *r8a66597_ep_name[] = {
	"ep0", "ep1", "ep2", "ep3", "ep4", "ep5", "ep6", "ep7",
	"ep8", "ep9",
};

static void init_controller(struct r8a66597 *r8a66597);
static void disable_controller(struct r8a66597 *r8a66597);
static void irq_ep0_write(struct r8a66597_ep *ep, struct r8a66597_request *req);
static void irq_packet_write(struct r8a66597_ep *ep,
				struct r8a66597_request *req);
static int r8a66597_queue(struct usb_ep *_ep, struct usb_request *_req,
			gfp_t gfp_flags);

static void transfer_complete(struct r8a66597_ep *ep,
		struct r8a66597_request *req, int status);

/*-------------------------------------------------------------------------*/
static inline u16 get_usb_speed(struct r8a66597 *r8a66597)
{
	return r8a66597_read(r8a66597, DVSTCTR0) & RHST;
}

static void enable_pipe_irq(struct r8a66597 *r8a66597, u16 pipenum,
		unsigned long reg)
{
	u16 tmp;

	tmp = r8a66597_read(r8a66597, INTENB0);
	r8a66597_bclr(r8a66597, BEMPE | NRDYE | BRDYE,
			INTENB0);
	r8a66597_bset(r8a66597, (1 << pipenum), reg);
	r8a66597_write(r8a66597, tmp, INTENB0);
}

static void disable_pipe_irq(struct r8a66597 *r8a66597, u16 pipenum,
		unsigned long reg)
{
	u16 tmp;

	tmp = r8a66597_read(r8a66597, INTENB0);
	r8a66597_bclr(r8a66597, BEMPE | NRDYE | BRDYE,
			INTENB0);
	r8a66597_bclr(r8a66597, (1 << pipenum), reg);
	r8a66597_write(r8a66597, tmp, INTENB0);
}

static void r8a66597_usb_connect(struct r8a66597 *r8a66597)
{
	r8a66597_bset(r8a66597, CTRE, INTENB0);
	r8a66597_bset(r8a66597, BEMPE | BRDYE, INTENB0);

	r8a66597_bset(r8a66597, DPRPU, SYSCFG0);
}

static void r8a66597_usb_disconnect(struct r8a66597 *r8a66597)
__releases(r8a66597->lock)
__acquires(r8a66597->lock)
{
	r8a66597_bclr(r8a66597, CTRE, INTENB0);
	r8a66597_bclr(r8a66597, BEMPE | BRDYE, INTENB0);
	r8a66597_bclr(r8a66597, DPRPU, SYSCFG0);

	r8a66597->gadget.speed = USB_SPEED_UNKNOWN;
	spin_unlock(&r8a66597->lock);
	r8a66597->driver->disconnect(&r8a66597->gadget);
	spin_lock(&r8a66597->lock);

	disable_controller(r8a66597);
	init_controller(r8a66597);
	r8a66597_bset(r8a66597, VBSE, INTENB0);
	INIT_LIST_HEAD(&r8a66597->ep[0].queue);
}

static inline u16 control_reg_get_pid(struct r8a66597 *r8a66597, u16 pipenum)
{
	u16 pid = 0;
	unsigned long offset;

	if (pipenum == 0) {
		pid = r8a66597_read(r8a66597, DCPCTR) & PID;
	} else if (pipenum < R8A66597_MAX_NUM_PIPE) {
		offset = get_pipectr_addr(pipenum);
		pid = r8a66597_read(r8a66597, offset) & PID;
	} else {
		dev_err(r8a66597_to_dev(r8a66597), "unexpect pipe num (%d)\n",
			pipenum);
	}

	return pid;
}

static inline void control_reg_set_pid(struct r8a66597 *r8a66597, u16 pipenum,
		u16 pid)
{
	unsigned long offset;

	if (pipenum == 0) {
		r8a66597_mdfy(r8a66597, pid, PID, DCPCTR);
	} else if (pipenum < R8A66597_MAX_NUM_PIPE) {
		offset = get_pipectr_addr(pipenum);
		r8a66597_mdfy(r8a66597, pid, PID, offset);
	} else {
		dev_err(r8a66597_to_dev(r8a66597), "unexpect pipe num (%d)\n",
			pipenum);
	}
}

static inline void pipe_start(struct r8a66597 *r8a66597, u16 pipenum)
{
	control_reg_set_pid(r8a66597, pipenum, PID_BUF);
}

static inline void pipe_stop(struct r8a66597 *r8a66597, u16 pipenum)
{
	control_reg_set_pid(r8a66597, pipenum, PID_NAK);
}

static inline void pipe_stall(struct r8a66597 *r8a66597, u16 pipenum)
{
	control_reg_set_pid(r8a66597, pipenum, PID_STALL);
}

static inline u16 control_reg_get(struct r8a66597 *r8a66597, u16 pipenum)
{
	u16 ret = 0;
	unsigned long offset;

	if (pipenum == 0) {
		ret = r8a66597_read(r8a66597, DCPCTR);
	} else if (pipenum < R8A66597_MAX_NUM_PIPE) {
		offset = get_pipectr_addr(pipenum);
		ret = r8a66597_read(r8a66597, offset);
	} else {
		dev_err(r8a66597_to_dev(r8a66597), "unexpect pipe num (%d)\n",
			pipenum);
	}

	return ret;
}

static inline void control_reg_sqclr(struct r8a66597 *r8a66597, u16 pipenum)
{
	unsigned long offset;

	pipe_stop(r8a66597, pipenum);

	if (pipenum == 0) {
		r8a66597_bset(r8a66597, SQCLR, DCPCTR);
	} else if (pipenum < R8A66597_MAX_NUM_PIPE) {
		offset = get_pipectr_addr(pipenum);
		r8a66597_bset(r8a66597, SQCLR, offset);
	} else {
		dev_err(r8a66597_to_dev(r8a66597), "unexpect pipe num (%d)\n",
			pipenum);
	}
}

static void control_reg_sqset(struct r8a66597 *r8a66597, u16 pipenum)
{
	unsigned long offset;

	pipe_stop(r8a66597, pipenum);

	if (pipenum == 0) {
		r8a66597_bset(r8a66597, SQSET, DCPCTR);
	} else if (pipenum < R8A66597_MAX_NUM_PIPE) {
		offset = get_pipectr_addr(pipenum);
		r8a66597_bset(r8a66597, SQSET, offset);
	} else {
		dev_err(r8a66597_to_dev(r8a66597),
			"unexpect pipe num(%d)\n", pipenum);
	}
}

static u16 control_reg_sqmon(struct r8a66597 *r8a66597, u16 pipenum)
{
	unsigned long offset;

	if (pipenum == 0) {
		return r8a66597_read(r8a66597, DCPCTR) & SQMON;
	} else if (pipenum < R8A66597_MAX_NUM_PIPE) {
		offset = get_pipectr_addr(pipenum);
		return r8a66597_read(r8a66597, offset) & SQMON;
	} else {
		dev_err(r8a66597_to_dev(r8a66597),
			"unexpect pipe num(%d)\n", pipenum);
	}

	return 0;
}

static u16 save_usb_toggle(struct r8a66597 *r8a66597, u16 pipenum)
{
	return control_reg_sqmon(r8a66597, pipenum);
}

static void restore_usb_toggle(struct r8a66597 *r8a66597, u16 pipenum,
			       u16 toggle)
{
	if (toggle)
		control_reg_sqset(r8a66597, pipenum);
	else
		control_reg_sqclr(r8a66597, pipenum);
}

static inline int get_buffer_size(struct r8a66597 *r8a66597, u16 pipenum)
{
	u16 tmp;
	int size;

	if (pipenum == 0) {
		tmp = r8a66597_read(r8a66597, DCPCFG);
		if ((tmp & R8A66597_CNTMD) != 0)
			size = 256;
		else {
			tmp = r8a66597_read(r8a66597, DCPMAXP);
			size = tmp & MAXP;
		}
	} else {
		r8a66597_write(r8a66597, pipenum, PIPESEL);
		tmp = r8a66597_read(r8a66597, PIPECFG);
		if ((tmp & R8A66597_CNTMD) != 0) {
			tmp = r8a66597_read(r8a66597, PIPEBUF);
			size = ((tmp >> 10) + 1) * 64;
		} else {
			tmp = r8a66597_read(r8a66597, PIPEMAXP);
			size = tmp & MXPS;
		}
	}

	return size;
}

static inline unsigned short mbw_value(struct r8a66597 *r8a66597)
{
	if (r8a66597->pdata->on_chip)
		return MBW_32;
	else
		return MBW_16;
}

static void r8a66597_change_curpipe(struct r8a66597 *r8a66597, u16 pipenum,
				    u16 isel, u16 fifosel)
{
	u16 tmp, mask, loop;
	int i = 0;

	if (!pipenum) {
		mask = ISEL | CURPIPE;
		loop = isel;
	} else {
		mask = CURPIPE;
		loop = pipenum;
	}
	r8a66597_mdfy(r8a66597, loop, mask, fifosel);

	do {
		tmp = r8a66597_read(r8a66597, fifosel);
		if (i++ > 1000000) {
			dev_err(r8a66597_to_dev(r8a66597),
				"r8a66597: register%x, loop %x "
				"is timeout\n", fifosel, loop);
			break;
		}
		ndelay(1);
	} while ((tmp & mask) != loop);
}

static void pipe_change(struct r8a66597 *r8a66597, u16 pipenum)
{
	struct r8a66597_ep *ep = r8a66597->pipenum2ep[pipenum];

	if (ep->use_dma)
		r8a66597_bclr(r8a66597, DREQE, ep->fifosel);

	r8a66597_mdfy(r8a66597, pipenum, CURPIPE, ep->fifosel);

	ndelay(450);

	if (r8a66597_is_sudmac(r8a66597) && ep->use_dma)
		r8a66597_bclr(r8a66597, mbw_value(r8a66597), ep->fifosel);
	else
		r8a66597_bset(r8a66597, mbw_value(r8a66597), ep->fifosel);

	if (ep->use_dma)
		r8a66597_bset(r8a66597, DREQE, ep->fifosel);
}

static int pipe_buffer_setting(struct r8a66597 *r8a66597,
		struct r8a66597_pipe_info *info)
{
	u16 bufnum = 0, buf_bsize = 0;
	u16 pipecfg = 0;

	if (info->pipe == 0)
		return -EINVAL;

	r8a66597_write(r8a66597, info->pipe, PIPESEL);

	if (info->dir_in)
		pipecfg |= R8A66597_DIR;
	pipecfg |= info->type;
	pipecfg |= info->epnum;
	switch (info->type) {
	case R8A66597_INT:
		bufnum = 4 + (info->pipe - R8A66597_BASE_PIPENUM_INT);
		buf_bsize = 0;
		break;
	case R8A66597_BULK:
		/* isochronous pipes may be used as bulk pipes */
		if (info->pipe >= R8A66597_BASE_PIPENUM_BULK)
			bufnum = info->pipe - R8A66597_BASE_PIPENUM_BULK;
		else
			bufnum = info->pipe - R8A66597_BASE_PIPENUM_ISOC;

		bufnum = R8A66597_BASE_BUFNUM + (bufnum * 16);
		buf_bsize = 7;
		pipecfg |= R8A66597_DBLB;
		if (!info->dir_in)
			pipecfg |= R8A66597_SHTNAK;
		break;
	case R8A66597_ISO:
		bufnum = R8A66597_BASE_BUFNUM +
			 (info->pipe - R8A66597_BASE_PIPENUM_ISOC) * 16;
		buf_bsize = 7;
		break;
	}

	if (buf_bsize && ((bufnum + 16) >= R8A66597_MAX_BUFNUM)) {
		pr_err("r8a66597 pipe memory is insufficient\n");
		return -ENOMEM;
	}

	r8a66597_write(r8a66597, pipecfg, PIPECFG);
	r8a66597_write(r8a66597, (buf_bsize << 10) | (bufnum), PIPEBUF);
	r8a66597_write(r8a66597, info->maxpacket, PIPEMAXP);
	if (info->interval)
		info->interval--;
	r8a66597_write(r8a66597, info->interval, PIPEPERI);

	return 0;
}

static void pipe_buffer_release(struct r8a66597 *r8a66597,
				struct r8a66597_pipe_info *info)
{
	if (info->pipe == 0)
		return;

	if (is_bulk_pipe(info->pipe)) {
		r8a66597->bulk--;
	} else if (is_interrupt_pipe(info->pipe)) {
		r8a66597->interrupt--;
	} else if (is_isoc_pipe(info->pipe)) {
		r8a66597->isochronous--;
		if (info->type == R8A66597_BULK)
			r8a66597->bulk--;
	} else {
		dev_err(r8a66597_to_dev(r8a66597),
			"ep_release: unexpect pipenum (%d)\n", info->pipe);
	}
}

static void pipe_initialize(struct r8a66597_ep *ep)
{
	struct r8a66597 *r8a66597 = ep->r8a66597;

	r8a66597_mdfy(r8a66597, 0, CURPIPE, ep->fifosel);

	r8a66597_write(r8a66597, ACLRM, ep->pipectr);
	r8a66597_write(r8a66597, 0, ep->pipectr);
	r8a66597_write(r8a66597, SQCLR, ep->pipectr);
	if (ep->use_dma) {
		r8a66597_mdfy(r8a66597, ep->pipenum, CURPIPE, ep->fifosel);

		ndelay(450);

		r8a66597_bset(r8a66597, mbw_value(r8a66597), ep->fifosel);
	}
}

static void r8a66597_ep_setting(struct r8a66597 *r8a66597,
				struct r8a66597_ep *ep,
				const struct usb_endpoint_descriptor *desc,
				u16 pipenum, int dma)
{
	ep->use_dma = 0;
	ep->fifoaddr = CFIFO;
	ep->fifosel = CFIFOSEL;
	ep->fifoctr = CFIFOCTR;

	ep->pipectr = get_pipectr_addr(pipenum);
	if (is_bulk_pipe(pipenum) || is_isoc_pipe(pipenum)) {
		ep->pipetre = get_pipetre_addr(pipenum);
		ep->pipetrn = get_pipetrn_addr(pipenum);
	} else {
		ep->pipetre = 0;
		ep->pipetrn = 0;
	}
	ep->pipenum = pipenum;
	ep->ep.maxpacket = usb_endpoint_maxp(desc);
	r8a66597->pipenum2ep[pipenum] = ep;
	r8a66597->epaddr2ep[usb_endpoint_num(desc)]
		= ep;
	INIT_LIST_HEAD(&ep->queue);
}

static void r8a66597_ep_release(struct r8a66597_ep *ep)
{
	struct r8a66597 *r8a66597 = ep->r8a66597;
	u16 pipenum = ep->pipenum;

	if (pipenum == 0)
		return;

	if (ep->use_dma)
		r8a66597->num_dma--;
	ep->pipenum = 0;
	ep->busy = 0;
	ep->use_dma = 0;
}

static int alloc_pipe_config(struct r8a66597_ep *ep,
		const struct usb_endpoint_descriptor *desc)
{
	struct r8a66597 *r8a66597 = ep->r8a66597;
	struct r8a66597_pipe_info info;
	int dma = 0;
	unsigned char *counter;
	int ret;

	ep->ep.desc = desc;

	if (ep->pipenum)	/* already allocated pipe  */
		return 0;

	switch (usb_endpoint_type(desc)) {
	case USB_ENDPOINT_XFER_BULK:
		if (r8a66597->bulk >= R8A66597_MAX_NUM_BULK) {
			if (r8a66597->isochronous >= R8A66597_MAX_NUM_ISOC) {
				dev_err(r8a66597_to_dev(r8a66597),
					"bulk pipe is insufficient\n");
				return -ENODEV;
			} else {
				info.pipe = R8A66597_BASE_PIPENUM_ISOC
						+ r8a66597->isochronous;
				counter = &r8a66597->isochronous;
			}
		} else {
			info.pipe = R8A66597_BASE_PIPENUM_BULK + r8a66597->bulk;
			counter = &r8a66597->bulk;
		}
		info.type = R8A66597_BULK;
		dma = 1;
		break;
	case USB_ENDPOINT_XFER_INT:
		if (r8a66597->interrupt >= R8A66597_MAX_NUM_INT) {
			dev_err(r8a66597_to_dev(r8a66597),
				"interrupt pipe is insufficient\n");
			return -ENODEV;
		}
		info.pipe = R8A66597_BASE_PIPENUM_INT + r8a66597->interrupt;
		info.type = R8A66597_INT;
		counter = &r8a66597->interrupt;
		break;
	case USB_ENDPOINT_XFER_ISOC:
		if (r8a66597->isochronous >= R8A66597_MAX_NUM_ISOC) {
			dev_err(r8a66597_to_dev(r8a66597),
				"isochronous pipe is insufficient\n");
			return -ENODEV;
		}
		info.pipe = R8A66597_BASE_PIPENUM_ISOC + r8a66597->isochronous;
		info.type = R8A66597_ISO;
		counter = &r8a66597->isochronous;
		break;
	default:
		dev_err(r8a66597_to_dev(r8a66597), "unexpect xfer type\n");
		return -EINVAL;
	}
	ep->type = info.type;

	info.epnum = usb_endpoint_num(desc);
	info.maxpacket = usb_endpoint_maxp(desc);
	info.interval = desc->bInterval;
	if (desc->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
		info.dir_in = 1;
	else
		info.dir_in = 0;

	ret = pipe_buffer_setting(r8a66597, &info);
	if (ret < 0) {
		dev_err(r8a66597_to_dev(r8a66597),
			"pipe_buffer_setting fail\n");
		return ret;
	}

	(*counter)++;
	if ((counter == &r8a66597->isochronous) && info.type == R8A66597_BULK)
		r8a66597->bulk++;

	r8a66597_ep_setting(r8a66597, ep, desc, info.pipe, dma);
	pipe_initialize(ep);

	return 0;
}

static int free_pipe_config(struct r8a66597_ep *ep)
{
	struct r8a66597 *r8a66597 = ep->r8a66597;
	struct r8a66597_pipe_info info;

	info.pipe = ep->pipenum;
	info.type = ep->type;
	pipe_buffer_release(r8a66597, &info);
	r8a66597_ep_release(ep);

	return 0;
}

/*-------------------------------------------------------------------------*/
static void pipe_irq_enable(struct r8a66597 *r8a66597, u16 pipenum)
{
	enable_irq_ready(r8a66597, pipenum);
	enable_irq_nrdy(r8a66597, pipenum);
}

static void pipe_irq_disable(struct r8a66597 *r8a66597, u16 pipenum)
{
	disable_irq_ready(r8a66597, pipenum);
	disable_irq_nrdy(r8a66597, pipenum);
}

/* if complete is true, gadget driver complete function is not call */
static void control_end(struct r8a66597 *r8a66597, unsigned ccpl)
{
	r8a66597->ep[0].internal_ccpl = ccpl;
	pipe_start(r8a66597, 0);
	r8a66597_bset(r8a66597, CCPL, DCPCTR);
}

static void start_ep0_write(struct r8a66597_ep *ep,
				struct r8a66597_request *req)
{
	struct r8a66597 *r8a66597 = ep->r8a66597;

	pipe_change(r8a66597, ep->pipenum);
	r8a66597_mdfy(r8a66597, ISEL, (ISEL | CURPIPE), CFIFOSEL);
	r8a66597_write(r8a66597, BCLR, ep->fifoctr);
	if (req->req.length == 0) {
		r8a66597_bset(r8a66597, BVAL, ep->fifoctr);
		pipe_start(r8a66597, 0);
		transfer_complete(ep, req, 0);
	} else {
		r8a66597_write(r8a66597, ~BEMP0, BEMPSTS);
		irq_ep0_write(ep, req);
	}
}

static void disable_fifosel(struct r8a66597 *r8a66597, u16 pipenum,
			    u16 fifosel)
{
	u16 tmp;

	tmp = r8a66597_read(r8a66597, fifosel) & CURPIPE;
	if (tmp == pipenum)
		r8a66597_change_curpipe(r8a66597, 0, 0, fifosel);
}

static void change_bfre_mode(struct r8a66597 *r8a66597, u16 pipenum,
			     int enable)
{
	struct r8a66597_ep *ep = r8a66597->pipenum2ep[pipenum];
	u16 tmp, toggle;

	/* check current BFRE bit */
	r8a66597_write(r8a66597, pipenum, PIPESEL);
	tmp = r8a66597_read(r8a66597, PIPECFG) & R8A66597_BFRE;
	if ((enable && tmp) || (!enable && !tmp))
		return;

	/* change BFRE bit */
	pipe_stop(r8a66597, pipenum);
	disable_fifosel(r8a66597, pipenum, CFIFOSEL);
	disable_fifosel(r8a66597, pipenum, D0FIFOSEL);
	disable_fifosel(r8a66597, pipenum, D1FIFOSEL);

	toggle = save_usb_toggle(r8a66597, pipenum);

	r8a66597_write(r8a66597, pipenum, PIPESEL);
	if (enable)
		r8a66597_bset(r8a66597, R8A66597_BFRE, PIPECFG);
	else
		r8a66597_bclr(r8a66597, R8A66597_BFRE, PIPECFG);

	/* initialize for internal BFRE flag */
	r8a66597_bset(r8a66597, ACLRM, ep->pipectr);
	r8a66597_bclr(r8a66597, ACLRM, ep->pipectr);

	restore_usb_toggle(r8a66597, pipenum, toggle);
}

static int sudmac_alloc_channel(struct r8a66597 *r8a66597,
				struct r8a66597_ep *ep,
				struct r8a66597_request *req)
{
	struct r8a66597_dma *dma;

	if (!r8a66597_is_sudmac(r8a66597))
		return -ENODEV;

	/* Check transfer type */
	if (!is_bulk_pipe(ep->pipenum))
		return -EIO;

	if (r8a66597->dma.used)
		return -EBUSY;

	/* set SUDMAC parameters */
	dma = &r8a66597->dma;
	dma->used = 1;
	if (ep->ep.desc->bEndpointAddress & USB_DIR_IN) {
		dma->dir = 1;
	} else {
		dma->dir = 0;
		change_bfre_mode(r8a66597, ep->pipenum, 1);
	}

	/* set r8a66597_ep paramters */
	ep->use_dma = 1;
	ep->dma = dma;
	ep->fifoaddr = D0FIFO;
	ep->fifosel = D0FIFOSEL;
	ep->fifoctr = D0FIFOCTR;

	/* dma mapping */
	return usb_gadget_map_request(&r8a66597->gadget, &req->req, dma->dir);
}

static void sudmac_free_channel(struct r8a66597 *r8a66597,
				struct r8a66597_ep *ep,
				struct r8a66597_request *req)
{
	if (!r8a66597_is_sudmac(r8a66597))
		return;

	usb_gadget_unmap_request(&r8a66597->gadget, &req->req, ep->dma->dir);

	r8a66597_bclr(r8a66597, DREQE, ep->fifosel);
	r8a66597_change_curpipe(r8a66597, 0, 0, ep->fifosel);

	ep->dma->used = 0;
	ep->use_dma = 0;
	ep->fifoaddr = CFIFO;
	ep->fifosel = CFIFOSEL;
	ep->fifoctr = CFIFOCTR;
}

static void sudmac_start(struct r8a66597 *r8a66597, struct r8a66597_ep *ep,
			 struct r8a66597_request *req)
{
	BUG_ON(req->req.length == 0);

	r8a66597_sudmac_write(r8a66597, LBA_WAIT, CH0CFG);
	r8a66597_sudmac_write(r8a66597, req->req.dma, CH0BA);
	r8a66597_sudmac_write(r8a66597, req->req.length, CH0BBC);
	r8a66597_sudmac_write(r8a66597, CH0ENDE, DINTCTRL);

	r8a66597_sudmac_write(r8a66597, DEN, CH0DEN);
}

static void start_packet_write(struct r8a66597_ep *ep,
				struct r8a66597_request *req)
{
	struct r8a66597 *r8a66597 = ep->r8a66597;
	u16 tmp;

	pipe_change(r8a66597, ep->pipenum);
	disable_irq_empty(r8a66597, ep->pipenum);
	pipe_start(r8a66597, ep->pipenum);

	if (req->req.length == 0) {
		transfer_complete(ep, req, 0);
	} else {
		r8a66597_write(r8a66597, ~(1 << ep->pipenum), BRDYSTS);
		if (sudmac_alloc_channel(r8a66597, ep, req) < 0) {
			/* PIO mode */
			pipe_change(r8a66597, ep->pipenum);
			disable_irq_empty(r8a66597, ep->pipenum);
			pipe_start(r8a66597, ep->pipenum);
			tmp = r8a66597_read(r8a66597, ep->fifoctr);
			if (unlikely((tmp & FRDY) == 0))
				pipe_irq_enable(r8a66597, ep->pipenum);
			else
				irq_packet_write(ep, req);
		} else {
			/* DMA mode */
			pipe_change(r8a66597, ep->pipenum);
			disable_irq_nrdy(r8a66597, ep->pipenum);
			pipe_start(r8a66597, ep->pipenum);
			enable_irq_nrdy(r8a66597, ep->pipenum);
			sudmac_start(r8a66597, ep, req);
		}
	}
}

static void start_packet_read(struct r8a66597_ep *ep,
				struct r8a66597_request *req)
{
	struct r8a66597 *r8a66597 = ep->r8a66597;
	u16 pipenum = ep->pipenum;

	if (ep->pipenum == 0) {
		r8a66597_mdfy(r8a66597, 0, (ISEL | CURPIPE), CFIFOSEL);
		r8a66597_write(r8a66597, BCLR, ep->fifoctr);
		pipe_start(r8a66597, pipenum);
		pipe_irq_enable(r8a66597, pipenum);
	} else {
		pipe_stop(r8a66597, pipenum);
		if (ep->pipetre) {
			enable_irq_nrdy(r8a66597, pipenum);
			r8a66597_write(r8a66597, TRCLR, ep->pipetre);
			r8a66597_write(r8a66597,
				DIV_ROUND_UP(req->req.length, ep->ep.maxpacket),
				ep->pipetrn);
			r8a66597_bset(r8a66597, TRENB, ep->pipetre);
		}

		if (sudmac_alloc_channel(r8a66597, ep, req) < 0) {
			/* PIO mode */
			change_bfre_mode(r8a66597, ep->pipenum, 0);
			pipe_start(r8a66597, pipenum);	/* trigger once */
			pipe_irq_enable(r8a66597, pipenum);
		} else {
			pipe_change(r8a66597, pipenum);
			sudmac_start(r8a66597, ep, req);
			pipe_start(r8a66597, pipenum);	/* trigger once */
		}
	}
}

static void start_packet(struct r8a66597_ep *ep, struct r8a66597_request *req)
{
	if (ep->ep.desc->bEndpointAddress & USB_DIR_IN)
		start_packet_write(ep, req);
	else
		start_packet_read(ep, req);
}

static void start_ep0(struct r8a66597_ep *ep, struct r8a66597_request *req)
{
	u16 ctsq;

	ctsq = r8a66597_read(ep->r8a66597, INTSTS0) & CTSQ;

	switch (ctsq) {
	case CS_RDDS:
		start_ep0_write(ep, req);
		break;
	case CS_WRDS:
		start_packet_read(ep, req);
		break;

	case CS_WRND:
		control_end(ep->r8a66597, 0);
		break;
	default:
		dev_err(r8a66597_to_dev(ep->r8a66597),
			"start_ep0: unexpect ctsq(%x)\n", ctsq);
		break;
	}
}

static void init_controller(struct r8a66597 *r8a66597)
{
	u16 vif = r8a66597->pdata->vif ? LDRV : 0;
	u16 irq_sense = r8a66597->irq_sense_low ? INTL : 0;
	u16 endian = r8a66597->pdata->endian ? BIGEND : 0;

	if (r8a66597->pdata->on_chip) {
		if (r8a66597->pdata->buswait)
			r8a66597_write(r8a66597, r8a66597->pdata->buswait,
					SYSCFG1);
		else
			r8a66597_write(r8a66597, 0x0f, SYSCFG1);
		r8a66597_bset(r8a66597, HSE, SYSCFG0);

		r8a66597_bclr(r8a66597, USBE, SYSCFG0);
		r8a66597_bclr(r8a66597, DPRPU, SYSCFG0);
		r8a66597_bset(r8a66597, USBE, SYSCFG0);

		r8a66597_bset(r8a66597, SCKE, SYSCFG0);

		r8a66597_bset(r8a66597, irq_sense, INTENB1);
		r8a66597_write(r8a66597, BURST | CPU_ADR_RD_WR,
				DMA0CFG);
	} else {
		r8a66597_bset(r8a66597, vif | endian, PINCFG);
		r8a66597_bset(r8a66597, HSE, SYSCFG0);		/* High spd */
		r8a66597_mdfy(r8a66597, get_xtal_from_pdata(r8a66597->pdata),
				XTAL, SYSCFG0);

		r8a66597_bclr(r8a66597, USBE, SYSCFG0);
		r8a66597_bclr(r8a66597, DPRPU, SYSCFG0);
		r8a66597_bset(r8a66597, USBE, SYSCFG0);

		r8a66597_bset(r8a66597, XCKE, SYSCFG0);

		mdelay(3);

		r8a66597_bset(r8a66597, PLLC, SYSCFG0);

		mdelay(1);

		r8a66597_bset(r8a66597, SCKE, SYSCFG0);

		r8a66597_bset(r8a66597, irq_sense, INTENB1);
		r8a66597_write(r8a66597, BURST | CPU_ADR_RD_WR,
			       DMA0CFG);
	}
}

static void disable_controller(struct r8a66597 *r8a66597)
{
	if (r8a66597->pdata->on_chip) {
		r8a66597_bset(r8a66597, SCKE, SYSCFG0);
		r8a66597_bclr(r8a66597, UTST, TESTMODE);

		/* disable interrupts */
		r8a66597_write(r8a66597, 0, INTENB0);
		r8a66597_write(r8a66597, 0, INTENB1);
		r8a66597_write(r8a66597, 0, BRDYENB);
		r8a66597_write(r8a66597, 0, BEMPENB);
		r8a66597_write(r8a66597, 0, NRDYENB);

		/* clear status */
		r8a66597_write(r8a66597, 0, BRDYSTS);
		r8a66597_write(r8a66597, 0, NRDYSTS);
		r8a66597_write(r8a66597, 0, BEMPSTS);

		r8a66597_bclr(r8a66597, USBE, SYSCFG0);
		r8a66597_bclr(r8a66597, SCKE, SYSCFG0);

	} else {
		r8a66597_bclr(r8a66597, UTST, TESTMODE);
		r8a66597_bclr(r8a66597, SCKE, SYSCFG0);
		udelay(1);
		r8a66597_bclr(r8a66597, PLLC, SYSCFG0);
		udelay(1);
		udelay(1);
		r8a66597_bclr(r8a66597, XCKE, SYSCFG0);
	}
}

static void r8a66597_start_xclock(struct r8a66597 *r8a66597)
{
	u16 tmp;

	if (!r8a66597->pdata->on_chip) {
		tmp = r8a66597_read(r8a66597, SYSCFG0);
		if (!(tmp & XCKE))
			r8a66597_bset(r8a66597, XCKE, SYSCFG0);
	}
}

static struct r8a66597_request *get_request_from_ep(struct r8a66597_ep *ep)
{
	return list_entry(ep->queue.next, struct r8a66597_request, queue);
}

/*-------------------------------------------------------------------------*/
static void transfer_complete(struct r8a66597_ep *ep,
		struct r8a66597_request *req, int status)
__releases(r8a66597->lock)
__acquires(r8a66597->lock)
{
	int restart = 0;

	if (unlikely(ep->pipenum == 0)) {
		if (ep->internal_ccpl) {
			ep->internal_ccpl = 0;
			return;
		}
	}

	list_del_init(&req->queue);
	if (ep->r8a66597->gadget.speed == USB_SPEED_UNKNOWN)
		req->req.status = -ESHUTDOWN;
	else
		req->req.status = status;

	if (!list_empty(&ep->queue))
		restart = 1;

	if (ep->use_dma)
		sudmac_free_channel(ep->r8a66597, ep, req);

	spin_unlock(&ep->r8a66597->lock);
	usb_gadget_giveback_request(&ep->ep, &req->req);
	spin_lock(&ep->r8a66597->lock);

	if (restart) {
		req = get_request_from_ep(ep);
		if (ep->ep.desc)
			start_packet(ep, req);
	}
}

static void irq_ep0_write(struct r8a66597_ep *ep, struct r8a66597_request *req)
{
	int i;
	u16 tmp;
	unsigned bufsize;
	size_t size;
	void *buf;
	u16 pipenum = ep->pipenum;
	struct r8a66597 *r8a66597 = ep->r8a66597;

	pipe_change(r8a66597, pipenum);
	r8a66597_bset(r8a66597, ISEL, ep->fifosel);

	i = 0;
	do {
		tmp = r8a66597_read(r8a66597, ep->fifoctr);
		if (i++ > 100000) {
			dev_err(r8a66597_to_dev(r8a66597),
				"pipe0 is busy. maybe cpu i/o bus "
				"conflict. please power off this controller.");
			return;
		}
		ndelay(1);
	} while ((tmp & FRDY) == 0);

	/* prepare parameters */
	bufsize = get_buffer_size(r8a66597, pipenum);
	buf = req->req.buf + req->req.actual;
	size = min(bufsize, req->req.length - req->req.actual);

	/* write fifo */
	if (req->req.buf) {
		if (size > 0)
			r8a66597_write_fifo(r8a66597, ep, buf, size);
		if ((size == 0) || ((size % ep->ep.maxpacket) != 0))
			r8a66597_bset(r8a66597, BVAL, ep->fifoctr);
	}

	/* update parameters */
	req->req.actual += size;

	/* check transfer finish */
	if ((!req->req.zero && (req->req.actual == req->req.length))
			|| (size % ep->ep.maxpacket)
			|| (size == 0)) {
		disable_irq_ready(r8a66597, pipenum);
		disable_irq_empty(r8a66597, pipenum);
	} else {
		disable_irq_ready(r8a66597, pipenum);
		enable_irq_empty(r8a66597, pipenum);
	}
	pipe_start(r8a66597, pipenum);
}

static void irq_packet_write(struct r8a66597_ep *ep,
				struct r8a66597_request *req)
{
	u16 tmp;
	unsigned bufsize;
	size_t size;
	void *buf;
	u16 pipenum = ep->pipenum;
	struct r8a66597 *r8a66597 = ep->r8a66597;

	pipe_change(r8a66597, pipenum);
	tmp = r8a66597_read(r8a66597, ep->fifoctr);
	if (unlikely((tmp & FRDY) == 0)) {
		pipe_stop(r8a66597, pipenum);
		pipe_irq_disable(r8a66597, pipenum);
		dev_err(r8a66597_to_dev(r8a66597),
			"write fifo not ready. pipnum=%d\n", pipenum);
		return;
	}

	/* prepare parameters */
	bufsize = get_buffer_size(r8a66597, pipenum);
	buf = req->req.buf + req->req.actual;
	size = min(bufsize, req->req.length - req->req.actual);

	/* write fifo */
	if (req->req.buf) {
		r8a66597_write_fifo(r8a66597, ep, buf, size);
		if ((size == 0)
				|| ((size % ep->ep.maxpacket) != 0)
				|| ((bufsize != ep->ep.maxpacket)
					&& (bufsize > size)))
			r8a66597_bset(r8a66597, BVAL, ep->fifoctr);
	}

	/* update parameters */
	req->req.actual += size;
	/* check transfer finish */
	if ((!req->req.zero && (req->req.actual == req->req.length))
			|| (size % ep->ep.maxpacket)
			|| (size == 0)) {
		disable_irq_ready(r8a66597, pipenum);
		enable_irq_empty(r8a66597, pipenum);
	} else {
		disable_irq_empty(r8a66597, pipenum);
		pipe_irq_enable(r8a66597, pipenum);
	}
}

static void irq_packet_read(struct r8a66597_ep *ep,
				struct r8a66597_request *req)
{
	u16 tmp;
	int rcv_len, bufsize, req_len;
	int size;
	void *buf;
	u16 pipenum = ep->pipenum;
	struct r8a66597 *r8a66597 = ep->r8a66597;
	int finish = 0;

	pipe_change(r8a66597, pipenum);
	tmp = r8a66597_read(r8a66597, ep->fifoctr);
	if (unlikely((tmp & FRDY) == 0)) {
		req->req.status = -EPIPE;
		pipe_stop(r8a66597, pipenum);
		pipe_irq_disable(r8a66597, pipenum);
		dev_err(r8a66597_to_dev(r8a66597), "read fifo not ready");
		return;
	}

	/* prepare parameters */
	rcv_len = tmp & DTLN;
	bufsize = get_buffer_size(r8a66597, pipenum);

	buf = req->req.buf + req->req.actual;
	req_len = req->req.length - req->req.actual;
	if (rcv_len < bufsize)
		size = min(rcv_len, req_len);
	else
		size = min(bufsize, req_len);

	/* update parameters */
	req->req.actual += size;

	/* check transfer finish */
	if ((!req->req.zero && (req->req.actual == req->req.length))
			|| (size % ep->ep.maxpacket)
			|| (size == 0)) {
		pipe_stop(r8a66597, pipenum);
		pipe_irq_disable(r8a66597, pipenum);
		finish = 1;
	}

	/* read fifo */
	if (req->req.buf) {
		if (size == 0)
			r8a66597_write(r8a66597, BCLR, ep->fifoctr);
		else
			r8a66597_read_fifo(r8a66597, ep->fifoaddr, buf, size);

	}

	if ((ep->pipenum != 0) && finish)
		transfer_complete(ep, req, 0);
}

static void irq_pipe_ready(struct r8a66597 *r8a66597, u16 status, u16 enb)
{
	u16 check;
	u16 pipenum;
	struct r8a66597_ep *ep;
	struct r8a66597_request *req;

	if ((status & BRDY0) && (enb & BRDY0)) {
		r8a66597_write(r8a66597, ~BRDY0, BRDYSTS);
		r8a66597_mdfy(r8a66597, 0, CURPIPE, CFIFOSEL);

		ep = &r8a66597->ep[0];
		req = get_request_from_ep(ep);
		irq_packet_read(ep, req);
	} else {
		for (pipenum = 1; pipenum < R8A66597_MAX_NUM_PIPE; pipenum++) {
			check = 1 << pipenum;
			if ((status & check) && (enb & check)) {
				r8a66597_write(r8a66597, ~check, BRDYSTS);
				ep = r8a66597->pipenum2ep[pipenum];
				req = get_request_from_ep(ep);
				if (ep->ep.desc->bEndpointAddress & USB_DIR_IN)
					irq_packet_write(ep, req);
				else
					irq_packet_read(ep, req);
			}
		}
	}
}

static void irq_pipe_empty(struct r8a66597 *r8a66597, u16 status, u16 enb)
{
	u16 tmp;
	u16 check;
	u16 pipenum;
	struct r8a66597_ep *ep;
	struct r8a66597_request *req;

	if ((status & BEMP0) && (enb & BEMP0)) {
		r8a66597_write(r8a66597, ~BEMP0, BEMPSTS);

		ep = &r8a66597->ep[0];
		req = get_request_from_ep(ep);
		irq_ep0_write(ep, req);
	} else {
		for (pipenum = 1; pipenum < R8A66597_MAX_NUM_PIPE; pipenum++) {
			check = 1 << pipenum;
			if ((status & check) && (enb & check)) {
				r8a66597_write(r8a66597, ~check, BEMPSTS);
				tmp = control_reg_get(r8a66597, pipenum);
				if ((tmp & INBUFM) == 0) {
					disable_irq_empty(r8a66597, pipenum);
					pipe_irq_disable(r8a66597, pipenum);
					pipe_stop(r8a66597, pipenum);
					ep = r8a66597->pipenum2ep[pipenum];
					req = get_request_from_ep(ep);
					if (!list_empty(&ep->queue))
						transfer_complete(ep, req, 0);
				}
			}
		}
	}
}

static void get_status(struct r8a66597 *r8a66597, struct usb_ctrlrequest *ctrl)
__releases(r8a66597->lock)
__acquires(r8a66597->lock)
{
	struct r8a66597_ep *ep;
	u16 pid;
	u16 status = 0;
	u16 w_index = le16_to_cpu(ctrl->wIndex);

	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		status = r8a66597->device_status;
		break;
	case USB_RECIP_INTERFACE:
		status = 0;
		break;
	case USB_RECIP_ENDPOINT:
		ep = r8a66597->epaddr2ep[w_index & USB_ENDPOINT_NUMBER_MASK];
		pid = control_reg_get_pid(r8a66597, ep->pipenum);
		if (pid == PID_STALL)
			status = 1 << USB_ENDPOINT_HALT;
		else
			status = 0;
		break;
	default:
		pipe_stall(r8a66597, 0);
		return;		/* exit */
	}

	r8a66597->ep0_data = cpu_to_le16(status);
	r8a66597->ep0_req->buf = &r8a66597->ep0_data;
	r8a66597->ep0_req->length = 2;
	/* AV: what happens if we get called again before that gets through? */
	spin_unlock(&r8a66597->lock);
	r8a66597_queue(r8a66597->gadget.ep0, r8a66597->ep0_req, GFP_ATOMIC);
	spin_lock(&r8a66597->lock);
}

static void clear_feature(struct r8a66597 *r8a66597,
				struct usb_ctrlrequest *ctrl)
{
	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		control_end(r8a66597, 1);
		break;
	case USB_RECIP_INTERFACE:
		control_end(r8a66597, 1);
		break;
	case USB_RECIP_ENDPOINT: {
		struct r8a66597_ep *ep;
		struct r8a66597_request *req;
		u16 w_index = le16_to_cpu(ctrl->wIndex);

		ep = r8a66597->epaddr2ep[w_index & USB_ENDPOINT_NUMBER_MASK];
		if (!ep->wedge) {
			pipe_stop(r8a66597, ep->pipenum);
			control_reg_sqclr(r8a66597, ep->pipenum);
			spin_unlock(&r8a66597->lock);
			usb_ep_clear_halt(&ep->ep);
			spin_lock(&r8a66597->lock);
		}

		control_end(r8a66597, 1);

		req = get_request_from_ep(ep);
		if (ep->busy) {
			ep->busy = 0;
			if (list_empty(&ep->queue))
				break;
			start_packet(ep, req);
		} else if (!list_empty(&ep->queue))
			pipe_start(r8a66597, ep->pipenum);
		}
		break;
	default:
		pipe_stall(r8a66597, 0);
		break;
	}
}

static void set_feature(struct r8a66597 *r8a66597, struct usb_ctrlrequest *ctrl)
{
	u16 tmp;
	int timeout = 3000;

	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		switch (le16_to_cpu(ctrl->wValue)) {
		case USB_DEVICE_TEST_MODE:
			control_end(r8a66597, 1);
			/* Wait for the completion of status stage */
			do {
				tmp = r8a66597_read(r8a66597, INTSTS0) & CTSQ;
				udelay(1);
			} while (tmp != CS_IDST || timeout-- > 0);

			if (tmp == CS_IDST)
				r8a66597_bset(r8a66597,
					      le16_to_cpu(ctrl->wIndex >> 8),
					      TESTMODE);
			break;
		default:
			pipe_stall(r8a66597, 0);
			break;
		}
		break;
	case USB_RECIP_INTERFACE:
		control_end(r8a66597, 1);
		break;
	case USB_RECIP_ENDPOINT: {
		struct r8a66597_ep *ep;
		u16 w_index = le16_to_cpu(ctrl->wIndex);

		ep = r8a66597->epaddr2ep[w_index & USB_ENDPOINT_NUMBER_MASK];
		pipe_stall(r8a66597, ep->pipenum);

		control_end(r8a66597, 1);
		}
		break;
	default:
		pipe_stall(r8a66597, 0);
		break;
	}
}

/* if return value is true, call class driver's setup() */
static int setup_packet(struct r8a66597 *r8a66597, struct usb_ctrlrequest *ctrl)
{
	u16 *p = (u16 *)ctrl;
	unsigned long offset = USBREQ;
	int i, ret = 0;

	/* read fifo */
	r8a66597_write(r8a66597, ~VALID, INTSTS0);

	for (i = 0; i < 4; i++)
		p[i] = r8a66597_read(r8a66597, offset + i*2);

	/* check request */
	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		switch (ctrl->bRequest) {
		case USB_REQ_GET_STATUS:
			get_status(r8a66597, ctrl);
			break;
		case USB_REQ_CLEAR_FEATURE:
			clear_feature(r8a66597, ctrl);
			break;
		case USB_REQ_SET_FEATURE:
			set_feature(r8a66597, ctrl);
			break;
		default:
			ret = 1;
			break;
		}
	} else
		ret = 1;
	return ret;
}

static void r8a66597_update_usb_speed(struct r8a66597 *r8a66597)
{
	u16 speed = get_usb_speed(r8a66597);

	switch (speed) {
	case HSMODE:
		r8a66597->gadget.speed = USB_SPEED_HIGH;
		break;
	case FSMODE:
		r8a66597->gadget.speed = USB_SPEED_FULL;
		break;
	default:
		r8a66597->gadget.speed = USB_SPEED_UNKNOWN;
		dev_err(r8a66597_to_dev(r8a66597), "USB speed unknown\n");
	}
}

static void irq_device_state(struct r8a66597 *r8a66597)
{
	u16 dvsq;

	dvsq = r8a66597_read(r8a66597, INTSTS0) & DVSQ;
	r8a66597_write(r8a66597, ~DVST, INTSTS0);

	if (dvsq == DS_DFLT) {
		/* bus reset */
		spin_unlock(&r8a66597->lock);
		usb_gadget_udc_reset(&r8a66597->gadget, r8a66597->driver);
		spin_lock(&r8a66597->lock);
		r8a66597_update_usb_speed(r8a66597);
	}
	if (r8a66597->old_dvsq == DS_CNFG && dvsq != DS_CNFG)
		r8a66597_update_usb_speed(r8a66597);
	if ((dvsq == DS_CNFG || dvsq == DS_ADDS)
			&& r8a66597->gadget.speed == USB_SPEED_UNKNOWN)
		r8a66597_update_usb_speed(r8a66597);

	r8a66597->old_dvsq = dvsq;
}

static void irq_control_stage(struct r8a66597 *r8a66597)
__releases(r8a66597->lock)
__acquires(r8a66597->lock)
{
	struct usb_ctrlrequest ctrl;
	u16 ctsq;

	ctsq = r8a66597_read(r8a66597, INTSTS0) & CTSQ;
	r8a66597_write(r8a66597, ~CTRT, INTSTS0);

	switch (ctsq) {
	case CS_IDST: {
		struct r8a66597_ep *ep;
		struct r8a66597_request *req;
		ep = &r8a66597->ep[0];
		req = get_request_from_ep(ep);
		transfer_complete(ep, req, 0);
		}
		break;

	case CS_RDDS:
	case CS_WRDS:
	case CS_WRND:
		if (setup_packet(r8a66597, &ctrl)) {
			spin_unlock(&r8a66597->lock);
			if (r8a66597->driver->setup(&r8a66597->gadget, &ctrl)
				< 0)
				pipe_stall(r8a66597, 0);
			spin_lock(&r8a66597->lock);
		}
		break;
	case CS_RDSS:
	case CS_WRSS:
		control_end(r8a66597, 0);
		break;
	default:
		dev_err(r8a66597_to_dev(r8a66597),
			"ctrl_stage: unexpect ctsq(%x)\n", ctsq);
		break;
	}
}

static void sudmac_finish(struct r8a66597 *r8a66597, struct r8a66597_ep *ep)
{
	u16 pipenum;
	struct r8a66597_request *req;
	u32 len;
	int i = 0;

	pipenum = ep->pipenum;
	pipe_change(r8a66597, pipenum);

	while (!(r8a66597_read(r8a66597, ep->fifoctr) & FRDY)) {
		udelay(1);
		if (unlikely(i++ >= 10000)) {	/* timeout = 10 msec */
			dev_err(r8a66597_to_dev(r8a66597),
				"%s: FRDY was not set (%d)\n",
				__func__, pipenum);
			return;
		}
	}

	r8a66597_bset(r8a66597, BCLR, ep->fifoctr);
	req = get_request_from_ep(ep);

	/* prepare parameters */
	len = r8a66597_sudmac_read(r8a66597, CH0CBC);
	req->req.actual += len;

	/* clear */
	r8a66597_sudmac_write(r8a66597, CH0STCLR, DSTSCLR);

	/* check transfer finish */
	if ((!req->req.zero && (req->req.actual == req->req.length))
			|| (len % ep->ep.maxpacket)) {
		if (ep->dma->dir) {
			disable_irq_ready(r8a66597, pipenum);
			enable_irq_empty(r8a66597, pipenum);
		} else {
			/* Clear the interrupt flag for next transfer */
			r8a66597_write(r8a66597, ~(1 << pipenum), BRDYSTS);
			transfer_complete(ep, req, 0);
		}
	}
}

static void r8a66597_sudmac_irq(struct r8a66597 *r8a66597)
{
	u32 irqsts;
	struct r8a66597_ep *ep;
	u16 pipenum;

	irqsts = r8a66597_sudmac_read(r8a66597, DINTSTS);
	if (irqsts & CH0ENDS) {
		r8a66597_sudmac_write(r8a66597, CH0ENDC, DINTSTSCLR);
		pipenum = (r8a66597_read(r8a66597, D0FIFOSEL) & CURPIPE);
		ep = r8a66597->pipenum2ep[pipenum];
		sudmac_finish(r8a66597, ep);
	}
}

static irqreturn_t r8a66597_irq(int irq, void *_r8a66597)
{
	struct r8a66597 *r8a66597 = _r8a66597;
	u16 intsts0;
	u16 intenb0;
	u16 savepipe;
	u16 mask0;

	spin_lock(&r8a66597->lock);

	if (r8a66597_is_sudmac(r8a66597))
		r8a66597_sudmac_irq(r8a66597);

	intsts0 = r8a66597_read(r8a66597, INTSTS0);
	intenb0 = r8a66597_read(r8a66597, INTENB0);

	savepipe = r8a66597_read(r8a66597, CFIFOSEL);

	mask0 = intsts0 & intenb0;
	if (mask0) {
		u16 brdysts = r8a66597_read(r8a66597, BRDYSTS);
		u16 bempsts = r8a66597_read(r8a66597, BEMPSTS);
		u16 brdyenb = r8a66597_read(r8a66597, BRDYENB);
		u16 bempenb = r8a66597_read(r8a66597, BEMPENB);

		if (mask0 & VBINT) {
			r8a66597_write(r8a66597,  0xffff & ~VBINT,
					INTSTS0);
			r8a66597_start_xclock(r8a66597);

			/* start vbus sampling */
			r8a66597->old_vbus = r8a66597_read(r8a66597, INTSTS0)
					& VBSTS;
			r8a66597->scount = R8A66597_MAX_SAMPLING;

			mod_timer(&r8a66597->timer,
					jiffies + msecs_to_jiffies(50));
		}
		if (intsts0 & DVSQ)
			irq_device_state(r8a66597);

		if ((intsts0 & BRDY) && (intenb0 & BRDYE)
				&& (brdysts & brdyenb))
			irq_pipe_ready(r8a66597, brdysts, brdyenb);
		if ((intsts0 & BEMP) && (intenb0 & BEMPE)
				&& (bempsts & bempenb))
			irq_pipe_empty(r8a66597, bempsts, bempenb);

		if (intsts0 & CTRT)
			irq_control_stage(r8a66597);
	}

	r8a66597_write(r8a66597, savepipe, CFIFOSEL);

	spin_unlock(&r8a66597->lock);
	return IRQ_HANDLED;
}

static void r8a66597_timer(struct timer_list *t)
{
	struct r8a66597 *r8a66597 = from_timer(r8a66597, t, timer);
	unsigned long flags;
	u16 tmp;

	spin_lock_irqsave(&r8a66597->lock, flags);
	tmp = r8a66597_read(r8a66597, SYSCFG0);
	if (r8a66597->scount > 0) {
		tmp = r8a66597_read(r8a66597, INTSTS0) & VBSTS;
		if (tmp == r8a66597->old_vbus) {
			r8a66597->scount--;
			if (r8a66597->scount == 0) {
				if (tmp == VBSTS)
					r8a66597_usb_connect(r8a66597);
				else
					r8a66597_usb_disconnect(r8a66597);
			} else {
				mod_timer(&r8a66597->timer,
					jiffies + msecs_to_jiffies(50));
			}
		} else {
			r8a66597->scount = R8A66597_MAX_SAMPLING;
			r8a66597->old_vbus = tmp;
			mod_timer(&r8a66597->timer,
					jiffies + msecs_to_jiffies(50));
		}
	}
	spin_unlock_irqrestore(&r8a66597->lock, flags);
}

/*-------------------------------------------------------------------------*/
static int r8a66597_enable(struct usb_ep *_ep,
			 const struct usb_endpoint_descriptor *desc)
{
	struct r8a66597_ep *ep;

	ep = container_of(_ep, struct r8a66597_ep, ep);
	return alloc_pipe_config(ep, desc);
}

static int r8a66597_disable(struct usb_ep *_ep)
{
	struct r8a66597_ep *ep;
	struct r8a66597_request *req;
	unsigned long flags;

	ep = container_of(_ep, struct r8a66597_ep, ep);
	BUG_ON(!ep);

	while (!list_empty(&ep->queue)) {
		req = get_request_from_ep(ep);
		spin_lock_irqsave(&ep->r8a66597->lock, flags);
		transfer_complete(ep, req, -ECONNRESET);
		spin_unlock_irqrestore(&ep->r8a66597->lock, flags);
	}

	pipe_irq_disable(ep->r8a66597, ep->pipenum);
	return free_pipe_config(ep);
}

static struct usb_request *r8a66597_alloc_request(struct usb_ep *_ep,
						gfp_t gfp_flags)
{
	struct r8a66597_request *req;

	req = kzalloc(sizeof(struct r8a66597_request), gfp_flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

static void r8a66597_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct r8a66597_request *req;

	req = container_of(_req, struct r8a66597_request, req);
	kfree(req);
}

static int r8a66597_queue(struct usb_ep *_ep, struct usb_request *_req,
			gfp_t gfp_flags)
{
	struct r8a66597_ep *ep;
	struct r8a66597_request *req;
	unsigned long flags;
	int request = 0;

	ep = container_of(_ep, struct r8a66597_ep, ep);
	req = container_of(_req, struct r8a66597_request, req);

	if (ep->r8a66597->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	spin_lock_irqsave(&ep->r8a66597->lock, flags);

	if (list_empty(&ep->queue))
		request = 1;

	list_add_tail(&req->queue, &ep->queue);
	req->req.actual = 0;
	req->req.status = -EINPROGRESS;

	if (ep->ep.desc == NULL)	/* control */
		start_ep0(ep, req);
	else {
		if (request && !ep->busy)
			start_packet(ep, req);
	}

	spin_unlock_irqrestore(&ep->r8a66597->lock, flags);

	return 0;
}

static int r8a66597_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct r8a66597_ep *ep;
	struct r8a66597_request *req;
	unsigned long flags;

	ep = container_of(_ep, struct r8a66597_ep, ep);
	req = container_of(_req, struct r8a66597_request, req);

	spin_lock_irqsave(&ep->r8a66597->lock, flags);
	if (!list_empty(&ep->queue))
		transfer_complete(ep, req, -ECONNRESET);
	spin_unlock_irqrestore(&ep->r8a66597->lock, flags);

	return 0;
}

static int r8a66597_set_halt(struct usb_ep *_ep, int value)
{
	struct r8a66597_ep *ep = container_of(_ep, struct r8a66597_ep, ep);
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&ep->r8a66597->lock, flags);
	if (!list_empty(&ep->queue)) {
		ret = -EAGAIN;
	} else if (value) {
		ep->busy = 1;
		pipe_stall(ep->r8a66597, ep->pipenum);
	} else {
		ep->busy = 0;
		ep->wedge = 0;
		pipe_stop(ep->r8a66597, ep->pipenum);
	}
	spin_unlock_irqrestore(&ep->r8a66597->lock, flags);
	return ret;
}

static int r8a66597_set_wedge(struct usb_ep *_ep)
{
	struct r8a66597_ep *ep;
	unsigned long flags;

	ep = container_of(_ep, struct r8a66597_ep, ep);

	if (!ep || !ep->ep.desc)
		return -EINVAL;

	spin_lock_irqsave(&ep->r8a66597->lock, flags);
	ep->wedge = 1;
	spin_unlock_irqrestore(&ep->r8a66597->lock, flags);

	return usb_ep_set_halt(_ep);
}

static void r8a66597_fifo_flush(struct usb_ep *_ep)
{
	struct r8a66597_ep *ep;
	unsigned long flags;

	ep = container_of(_ep, struct r8a66597_ep, ep);
	spin_lock_irqsave(&ep->r8a66597->lock, flags);
	if (list_empty(&ep->queue) && !ep->busy) {
		pipe_stop(ep->r8a66597, ep->pipenum);
		r8a66597_bclr(ep->r8a66597, BCLR, ep->fifoctr);
		r8a66597_write(ep->r8a66597, ACLRM, ep->pipectr);
		r8a66597_write(ep->r8a66597, 0, ep->pipectr);
	}
	spin_unlock_irqrestore(&ep->r8a66597->lock, flags);
}

static const struct usb_ep_ops r8a66597_ep_ops = {
	.enable		= r8a66597_enable,
	.disable	= r8a66597_disable,

	.alloc_request	= r8a66597_alloc_request,
	.free_request	= r8a66597_free_request,

	.queue		= r8a66597_queue,
	.dequeue	= r8a66597_dequeue,

	.set_halt	= r8a66597_set_halt,
	.set_wedge	= r8a66597_set_wedge,
	.fifo_flush	= r8a66597_fifo_flush,
};

/*-------------------------------------------------------------------------*/
static int r8a66597_start(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver)
{
	struct r8a66597 *r8a66597 = gadget_to_r8a66597(gadget);

	if (!driver
			|| driver->max_speed < USB_SPEED_HIGH
			|| !driver->setup)
		return -EINVAL;
	if (!r8a66597)
		return -ENODEV;

	/* hook up the driver */
	r8a66597->driver = driver;

	init_controller(r8a66597);
	r8a66597_bset(r8a66597, VBSE, INTENB0);
	if (r8a66597_read(r8a66597, INTSTS0) & VBSTS) {
		r8a66597_start_xclock(r8a66597);
		/* start vbus sampling */
		r8a66597->old_vbus = r8a66597_read(r8a66597,
					 INTSTS0) & VBSTS;
		r8a66597->scount = R8A66597_MAX_SAMPLING;
		mod_timer(&r8a66597->timer, jiffies + msecs_to_jiffies(50));
	}

	return 0;
}

static int r8a66597_stop(struct usb_gadget *gadget)
{
	struct r8a66597 *r8a66597 = gadget_to_r8a66597(gadget);
	unsigned long flags;

	spin_lock_irqsave(&r8a66597->lock, flags);
	r8a66597_bclr(r8a66597, VBSE, INTENB0);
	disable_controller(r8a66597);
	spin_unlock_irqrestore(&r8a66597->lock, flags);

	r8a66597->driver = NULL;
	return 0;
}

/*-------------------------------------------------------------------------*/
static int r8a66597_get_frame(struct usb_gadget *_gadget)
{
	struct r8a66597 *r8a66597 = gadget_to_r8a66597(_gadget);
	return r8a66597_read(r8a66597, FRMNUM) & 0x03FF;
}

static int r8a66597_pullup(struct usb_gadget *gadget, int is_on)
{
	struct r8a66597 *r8a66597 = gadget_to_r8a66597(gadget);
	unsigned long flags;

	spin_lock_irqsave(&r8a66597->lock, flags);
	if (is_on)
		r8a66597_bset(r8a66597, DPRPU, SYSCFG0);
	else
		r8a66597_bclr(r8a66597, DPRPU, SYSCFG0);
	spin_unlock_irqrestore(&r8a66597->lock, flags);

	return 0;
}

static int r8a66597_set_selfpowered(struct usb_gadget *gadget, int is_self)
{
	struct r8a66597 *r8a66597 = gadget_to_r8a66597(gadget);

	gadget->is_selfpowered = (is_self != 0);
	if (is_self)
		r8a66597->device_status |= 1 << USB_DEVICE_SELF_POWERED;
	else
		r8a66597->device_status &= ~(1 << USB_DEVICE_SELF_POWERED);

	return 0;
}

static const struct usb_gadget_ops r8a66597_gadget_ops = {
	.get_frame		= r8a66597_get_frame,
	.udc_start		= r8a66597_start,
	.udc_stop		= r8a66597_stop,
	.pullup			= r8a66597_pullup,
	.set_selfpowered	= r8a66597_set_selfpowered,
};

static int r8a66597_remove(struct platform_device *pdev)
{
	struct r8a66597		*r8a66597 = platform_get_drvdata(pdev);

	usb_del_gadget_udc(&r8a66597->gadget);
	del_timer_sync(&r8a66597->timer);
	r8a66597_free_request(&r8a66597->ep[0].ep, r8a66597->ep0_req);

	if (r8a66597->pdata->on_chip) {
		clk_disable_unprepare(r8a66597->clk);
	}

	return 0;
}

static void nop_completion(struct usb_ep *ep, struct usb_request *r)
{
}

static int r8a66597_sudmac_ioremap(struct r8a66597 *r8a66597,
					  struct platform_device *pdev)
{
	r8a66597->sudmac_reg =
		devm_platform_ioremap_resource_byname(pdev, "sudmac");
	return PTR_ERR_OR_ZERO(r8a66597->sudmac_reg);
}

static int r8a66597_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	char clk_name[8];
	struct resource *ires;
	int irq;
	void __iomem *reg = NULL;
	struct r8a66597 *r8a66597 = NULL;
	int ret = 0;
	int i;
	unsigned long irq_trigger;

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	ires = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!ires)
		return -EINVAL;
	irq = ires->start;
	irq_trigger = ires->flags & IRQF_TRIGGER_MASK;

	if (irq < 0) {
		dev_err(dev, "platform_get_irq error.\n");
		return -ENODEV;
	}

	/* initialize ucd */
	r8a66597 = devm_kzalloc(dev, sizeof(struct r8a66597), GFP_KERNEL);
	if (r8a66597 == NULL)
		return -ENOMEM;

	spin_lock_init(&r8a66597->lock);
	platform_set_drvdata(pdev, r8a66597);
	r8a66597->pdata = dev_get_platdata(dev);
	r8a66597->irq_sense_low = irq_trigger == IRQF_TRIGGER_LOW;

	r8a66597->gadget.ops = &r8a66597_gadget_ops;
	r8a66597->gadget.max_speed = USB_SPEED_HIGH;
	r8a66597->gadget.name = udc_name;

	timer_setup(&r8a66597->timer, r8a66597_timer, 0);
	r8a66597->reg = reg;

	if (r8a66597->pdata->on_chip) {
		snprintf(clk_name, sizeof(clk_name), "usb%d", pdev->id);
		r8a66597->clk = devm_clk_get(dev, clk_name);
		if (IS_ERR(r8a66597->clk)) {
			dev_err(dev, "cannot get clock \"%s\"\n", clk_name);
			return PTR_ERR(r8a66597->clk);
		}
		clk_prepare_enable(r8a66597->clk);
	}

	if (r8a66597->pdata->sudmac) {
		ret = r8a66597_sudmac_ioremap(r8a66597, pdev);
		if (ret < 0)
			goto clean_up2;
	}

	disable_controller(r8a66597); /* make sure controller is disabled */

	ret = devm_request_irq(dev, irq, r8a66597_irq, IRQF_SHARED,
			       udc_name, r8a66597);
	if (ret < 0) {
		dev_err(dev, "request_irq error (%d)\n", ret);
		goto clean_up2;
	}

	INIT_LIST_HEAD(&r8a66597->gadget.ep_list);
	r8a66597->gadget.ep0 = &r8a66597->ep[0].ep;
	INIT_LIST_HEAD(&r8a66597->gadget.ep0->ep_list);
	for (i = 0; i < R8A66597_MAX_NUM_PIPE; i++) {
		struct r8a66597_ep *ep = &r8a66597->ep[i];

		if (i != 0) {
			INIT_LIST_HEAD(&r8a66597->ep[i].ep.ep_list);
			list_add_tail(&r8a66597->ep[i].ep.ep_list,
					&r8a66597->gadget.ep_list);
		}
		ep->r8a66597 = r8a66597;
		INIT_LIST_HEAD(&ep->queue);
		ep->ep.name = r8a66597_ep_name[i];
		ep->ep.ops = &r8a66597_ep_ops;
		usb_ep_set_maxpacket_limit(&ep->ep, 512);

		if (i == 0) {
			ep->ep.caps.type_control = true;
		} else {
			ep->ep.caps.type_iso = true;
			ep->ep.caps.type_bulk = true;
			ep->ep.caps.type_int = true;
		}
		ep->ep.caps.dir_in = true;
		ep->ep.caps.dir_out = true;
	}
	usb_ep_set_maxpacket_limit(&r8a66597->ep[0].ep, 64);
	r8a66597->ep[0].pipenum = 0;
	r8a66597->ep[0].fifoaddr = CFIFO;
	r8a66597->ep[0].fifosel = CFIFOSEL;
	r8a66597->ep[0].fifoctr = CFIFOCTR;
	r8a66597->ep[0].pipectr = get_pipectr_addr(0);
	r8a66597->pipenum2ep[0] = &r8a66597->ep[0];
	r8a66597->epaddr2ep[0] = &r8a66597->ep[0];

	r8a66597->ep0_req = r8a66597_alloc_request(&r8a66597->ep[0].ep,
							GFP_KERNEL);
	if (r8a66597->ep0_req == NULL) {
		ret = -ENOMEM;
		goto clean_up2;
	}
	r8a66597->ep0_req->complete = nop_completion;

	ret = usb_add_gadget_udc(dev, &r8a66597->gadget);
	if (ret)
		goto err_add_udc;

	dev_info(dev, "version %s\n", DRIVER_VERSION);
	return 0;

err_add_udc:
	r8a66597_free_request(&r8a66597->ep[0].ep, r8a66597->ep0_req);
clean_up2:
	if (r8a66597->pdata->on_chip)
		clk_disable_unprepare(r8a66597->clk);

	if (r8a66597->ep0_req)
		r8a66597_free_request(&r8a66597->ep[0].ep, r8a66597->ep0_req);

	return ret;
}

/*-------------------------------------------------------------------------*/
static struct platform_driver r8a66597_driver = {
	.remove =	r8a66597_remove,
	.driver		= {
		.name =	udc_name,
	},
};

module_platform_driver_probe(r8a66597_driver, r8a66597_probe);

MODULE_DESCRIPTION("R8A66597 USB gadget driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yoshihiro Shimoda");
MODULE_ALIAS("platform:r8a66597_udc");
