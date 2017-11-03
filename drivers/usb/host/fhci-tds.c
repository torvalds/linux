// SPDX-License-Identifier: GPL-2.0+
/*
 * Freescale QUICC Engine USB Host Controller Driver
 *
 * Copyright (c) Freescale Semicondutor, Inc. 2006.
 *               Shlomi Gridish <gridish@freescale.com>
 *               Jerry Huang <Chang-Ming.Huang@freescale.com>
 * Copyright (c) Logic Product Development, Inc. 2007
 *               Peter Barada <peterb@logicpd.com>
 * Copyright (c) MontaVista Software, Inc. 2008.
 *               Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include "fhci.h"

#define DUMMY_BD_BUFFER  0xdeadbeef
#define DUMMY2_BD_BUFFER 0xbaadf00d

/* Transaction Descriptors bits */
#define TD_R		0x8000 /* ready bit */
#define TD_W		0x2000 /* wrap bit */
#define TD_I		0x1000 /* interrupt on completion */
#define TD_L		0x0800 /* last */
#define TD_TC		0x0400 /* transmit CRC */
#define TD_CNF		0x0200 /* CNF - Must be always 1 */
#define TD_LSP		0x0100 /* Low-speed transaction */
#define TD_PID		0x00c0 /* packet id */
#define TD_RXER		0x0020 /* Rx error or not */

#define TD_NAK		0x0010 /* No ack. */
#define TD_STAL		0x0008 /* Stall received */
#define TD_TO		0x0004 /* time out */
#define TD_UN		0x0002 /* underrun */
#define TD_NO		0x0010 /* Rx Non Octet Aligned Packet */
#define TD_AB		0x0008 /* Frame Aborted */
#define TD_CR		0x0004 /* CRC Error */
#define TD_OV		0x0002 /* Overrun */
#define TD_BOV		0x0001 /* Buffer Overrun */

#define TD_ERRORS	(TD_NAK | TD_STAL | TD_TO | TD_UN | \
			 TD_NO | TD_AB | TD_CR | TD_OV | TD_BOV)

#define TD_PID_DATA0	0x0080 /* Data 0 toggle */
#define TD_PID_DATA1	0x00c0 /* Data 1 toggle */
#define TD_PID_TOGGLE	0x00c0 /* Data 0/1 toggle mask */

#define TD_TOK_SETUP	0x0000
#define TD_TOK_OUT	0x4000
#define TD_TOK_IN	0x8000
#define TD_ISO		0x1000
#define TD_ENDP		0x0780
#define TD_ADDR		0x007f

#define TD_ENDP_SHIFT 7

struct usb_td {
	__be16 status;
	__be16 length;
	__be32 buf_ptr;
	__be16 extra;
	__be16 reserved;
};

static struct usb_td __iomem *next_bd(struct usb_td __iomem *base,
				      struct usb_td __iomem *td,
				      u16 status)
{
	if (status & TD_W)
		return base;
	else
		return ++td;
}

void fhci_push_dummy_bd(struct endpoint *ep)
{
	if (!ep->already_pushed_dummy_bd) {
		u16 td_status = in_be16(&ep->empty_td->status);

		out_be32(&ep->empty_td->buf_ptr, DUMMY_BD_BUFFER);
		/* get the next TD in the ring */
		ep->empty_td = next_bd(ep->td_base, ep->empty_td, td_status);
		ep->already_pushed_dummy_bd = true;
	}
}

/* destroy an USB endpoint */
void fhci_ep0_free(struct fhci_usb *usb)
{
	struct endpoint *ep;
	int size;

	ep = usb->ep0;
	if (ep) {
		if (ep->td_base)
			cpm_muram_free(cpm_muram_offset(ep->td_base));

		if (kfifo_initialized(&ep->conf_frame_Q)) {
			size = cq_howmany(&ep->conf_frame_Q);
			for (; size; size--) {
				struct packet *pkt = cq_get(&ep->conf_frame_Q);

				kfree(pkt);
			}
			cq_delete(&ep->conf_frame_Q);
		}

		if (kfifo_initialized(&ep->empty_frame_Q)) {
			size = cq_howmany(&ep->empty_frame_Q);
			for (; size; size--) {
				struct packet *pkt = cq_get(&ep->empty_frame_Q);

				kfree(pkt);
			}
			cq_delete(&ep->empty_frame_Q);
		}

		if (kfifo_initialized(&ep->dummy_packets_Q)) {
			size = cq_howmany(&ep->dummy_packets_Q);
			for (; size; size--) {
				u8 *buff = cq_get(&ep->dummy_packets_Q);

				kfree(buff);
			}
			cq_delete(&ep->dummy_packets_Q);
		}

		kfree(ep);
		usb->ep0 = NULL;
	}
}

/*
 * create the endpoint structure
 *
 * arguments:
 * usb		A pointer to the data structure of the USB
 * data_mem	The data memory partition(BUS)
 * ring_len	TD ring length
 */
u32 fhci_create_ep(struct fhci_usb *usb, enum fhci_mem_alloc data_mem,
			   u32 ring_len)
{
	struct endpoint *ep;
	struct usb_td __iomem *td;
	unsigned long ep_offset;
	char *err_for = "endpoint PRAM";
	int ep_mem_size;
	u32 i;

	/* we need at least 3 TDs in the ring */
	if (!(ring_len > 2)) {
		fhci_err(usb->fhci, "illegal TD ring length parameters\n");
		return -EINVAL;
	}

	ep = kzalloc(sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;

	ep_mem_size = ring_len * sizeof(*td) + sizeof(struct fhci_ep_pram);
	ep_offset = cpm_muram_alloc(ep_mem_size, 32);
	if (IS_ERR_VALUE(ep_offset))
		goto err;
	ep->td_base = cpm_muram_addr(ep_offset);

	/* zero all queue pointers */
	if (cq_new(&ep->conf_frame_Q, ring_len + 2) ||
	    cq_new(&ep->empty_frame_Q, ring_len + 2) ||
	    cq_new(&ep->dummy_packets_Q, ring_len + 2)) {
		err_for = "frame_queues";
		goto err;
	}

	for (i = 0; i < (ring_len + 1); i++) {
		struct packet *pkt;
		u8 *buff;

		pkt = kmalloc(sizeof(*pkt), GFP_KERNEL);
		if (!pkt) {
			err_for = "frame";
			goto err;
		}

		buff = kmalloc(1028 * sizeof(*buff), GFP_KERNEL);
		if (!buff) {
			kfree(pkt);
			err_for = "buffer";
			goto err;
		}
		cq_put(&ep->empty_frame_Q, pkt);
		cq_put(&ep->dummy_packets_Q, buff);
	}

	/* we put the endpoint parameter RAM right behind the TD ring */
	ep->ep_pram_ptr = (void __iomem *)ep->td_base + sizeof(*td) * ring_len;

	ep->conf_td = ep->td_base;
	ep->empty_td = ep->td_base;

	ep->already_pushed_dummy_bd = false;

	/* initialize tds */
	td = ep->td_base;
	for (i = 0; i < ring_len; i++) {
		out_be32(&td->buf_ptr, 0);
		out_be16(&td->status, 0);
		out_be16(&td->length, 0);
		out_be16(&td->extra, 0);
		td++;
	}
	td--;
	out_be16(&td->status, TD_W); /* for last TD set Wrap bit */
	out_be16(&td->length, 0);

	/* endpoint structure has been created */
	usb->ep0 = ep;

	return 0;
err:
	fhci_ep0_free(usb);
	kfree(ep);
	fhci_err(usb->fhci, "no memory for the %s\n", err_for);
	return -ENOMEM;
}

/*
 * initialize the endpoint register according to the given parameters
 *
 * artuments:
 * usb		A pointer to the data strucutre of the USB
 * ep		A pointer to the endpoint structre
 * data_mem	The data memory partition(BUS)
 */
void fhci_init_ep_registers(struct fhci_usb *usb, struct endpoint *ep,
			    enum fhci_mem_alloc data_mem)
{
	u8 rt;

	/* set the endpoint registers according to the endpoint */
	out_be16(&usb->fhci->regs->usb_usep[0],
		 USB_TRANS_CTR | USB_EP_MF | USB_EP_RTE);
	out_be16(&usb->fhci->pram->ep_ptr[0],
		 cpm_muram_offset(ep->ep_pram_ptr));

	rt = (BUS_MODE_BO_BE | BUS_MODE_GBL);
#ifdef MULTI_DATA_BUS
	if (data_mem == MEM_SECONDARY)
		rt |= BUS_MODE_DTB;
#endif
	out_8(&ep->ep_pram_ptr->rx_func_code, rt);
	out_8(&ep->ep_pram_ptr->tx_func_code, rt);
	out_be16(&ep->ep_pram_ptr->rx_buff_len, 1028);
	out_be16(&ep->ep_pram_ptr->rx_base, 0);
	out_be16(&ep->ep_pram_ptr->tx_base, cpm_muram_offset(ep->td_base));
	out_be16(&ep->ep_pram_ptr->rx_bd_ptr, 0);
	out_be16(&ep->ep_pram_ptr->tx_bd_ptr, cpm_muram_offset(ep->td_base));
	out_be32(&ep->ep_pram_ptr->tx_state, 0);
}

/*
 * Collect the submitted frames and inform the application about them
 * It is also preparing the TDs for new frames. If the Tx interrupts
 * are disabled, the application should call that routine to get
 * confirmation about the submitted frames. Otherwise, the routine is
 * called from the interrupt service routine during the Tx interrupt.
 * In that case the application is informed by calling the application
 * specific 'fhci_transaction_confirm' routine
 */
static void fhci_td_transaction_confirm(struct fhci_usb *usb)
{
	struct endpoint *ep = usb->ep0;
	struct packet *pkt;
	struct usb_td __iomem *td;
	u16 extra_data;
	u16 td_status;
	u16 td_length;
	u32 buf;

	/*
	 * collect transmitted BDs from the chip. The routine clears all BDs
	 * with R bit = 0 and the pointer to data buffer is not NULL, that is
	 * BDs which point to the transmitted data buffer
	 */
	while (1) {
		td = ep->conf_td;
		td_status = in_be16(&td->status);
		td_length = in_be16(&td->length);
		buf = in_be32(&td->buf_ptr);
		extra_data = in_be16(&td->extra);

		/* check if the TD is empty */
		if (!(!(td_status & TD_R) && ((td_status & ~TD_W) || buf)))
			break;
		/* check if it is a dummy buffer */
		else if ((buf == DUMMY_BD_BUFFER) && !(td_status & ~TD_W))
			break;

		/* mark TD as empty */
		clrbits16(&td->status, ~TD_W);
		out_be16(&td->length, 0);
		out_be32(&td->buf_ptr, 0);
		out_be16(&td->extra, 0);
		/* advance the TD pointer */
		ep->conf_td = next_bd(ep->td_base, ep->conf_td, td_status);

		/* check if it is a dummy buffer(type2) */
		if ((buf == DUMMY2_BD_BUFFER) && !(td_status & ~TD_W))
			continue;

		pkt = cq_get(&ep->conf_frame_Q);
		if (!pkt)
			fhci_err(usb->fhci, "no frame to confirm\n");

		if (td_status & TD_ERRORS) {
			if (td_status & TD_RXER) {
				if (td_status & TD_CR)
					pkt->status = USB_TD_RX_ER_CRC;
				else if (td_status & TD_AB)
					pkt->status = USB_TD_RX_ER_BITSTUFF;
				else if (td_status & TD_OV)
					pkt->status = USB_TD_RX_ER_OVERUN;
				else if (td_status & TD_BOV)
					pkt->status = USB_TD_RX_DATA_OVERUN;
				else if (td_status & TD_NO)
					pkt->status = USB_TD_RX_ER_NONOCT;
				else
					fhci_err(usb->fhci, "illegal error "
						 "occurred\n");
			} else if (td_status & TD_NAK)
				pkt->status = USB_TD_TX_ER_NAK;
			else if (td_status & TD_TO)
				pkt->status = USB_TD_TX_ER_TIMEOUT;
			else if (td_status & TD_UN)
				pkt->status = USB_TD_TX_ER_UNDERUN;
			else if (td_status & TD_STAL)
				pkt->status = USB_TD_TX_ER_STALL;
			else
				fhci_err(usb->fhci, "illegal error occurred\n");
		} else if ((extra_data & TD_TOK_IN) &&
				pkt->len > td_length - CRC_SIZE) {
			pkt->status = USB_TD_RX_DATA_UNDERUN;
		}

		if (extra_data & TD_TOK_IN)
			pkt->len = td_length - CRC_SIZE;
		else if (pkt->info & PKT_ZLP)
			pkt->len = 0;
		else
			pkt->len = td_length;

		fhci_transaction_confirm(usb, pkt);
	}
}

/*
 * Submitting a data frame to a specified endpoint of a USB device
 * The frame is put in the driver's transmit queue for this endpoint
 *
 * Arguments:
 * usb          A pointer to the USB structure
 * pkt          A pointer to the user frame structure
 * trans_type   Transaction tyep - IN,OUT or SETUP
 * dest_addr    Device address - 0~127
 * dest_ep      Endpoint number of the device - 0~16
 * trans_mode   Pipe type - ISO,Interrupt,bulk or control
 * dest_speed   USB speed - Low speed or FULL speed
 * data_toggle  Data sequence toggle - 0 or 1
 */
u32 fhci_host_transaction(struct fhci_usb *usb,
			  struct packet *pkt,
			  enum fhci_ta_type trans_type,
			  u8 dest_addr,
			  u8 dest_ep,
			  enum fhci_tf_mode trans_mode,
			  enum fhci_speed dest_speed, u8 data_toggle)
{
	struct endpoint *ep = usb->ep0;
	struct usb_td __iomem *td;
	u16 extra_data;
	u16 td_status;

	fhci_usb_disable_interrupt(usb);
	/* start from the next BD that should be filled */
	td = ep->empty_td;
	td_status = in_be16(&td->status);

	if (td_status & TD_R && in_be16(&td->length)) {
		/* if the TD is not free */
		fhci_usb_enable_interrupt(usb);
		return -1;
	}

	/* get the next TD in the ring */
	ep->empty_td = next_bd(ep->td_base, ep->empty_td, td_status);
	fhci_usb_enable_interrupt(usb);
	pkt->priv_data = td;
	out_be32(&td->buf_ptr, virt_to_phys(pkt->data));
	/* sets up transaction parameters - addr,endp,dir,and type */
	extra_data = (dest_ep << TD_ENDP_SHIFT) | dest_addr;
	switch (trans_type) {
	case FHCI_TA_IN:
		extra_data |= TD_TOK_IN;
		break;
	case FHCI_TA_OUT:
		extra_data |= TD_TOK_OUT;
		break;
	case FHCI_TA_SETUP:
		extra_data |= TD_TOK_SETUP;
		break;
	}
	if (trans_mode == FHCI_TF_ISO)
		extra_data |= TD_ISO;
	out_be16(&td->extra, extra_data);

	/* sets up the buffer descriptor */
	td_status = ((td_status & TD_W) | TD_R | TD_L | TD_I | TD_CNF);
	if (!(pkt->info & PKT_NO_CRC))
		td_status |= TD_TC;

	switch (trans_type) {
	case FHCI_TA_IN:
		if (data_toggle)
			pkt->info |= PKT_PID_DATA1;
		else
			pkt->info |= PKT_PID_DATA0;
		break;
	default:
		if (data_toggle) {
			td_status |= TD_PID_DATA1;
			pkt->info |= PKT_PID_DATA1;
		} else {
			td_status |= TD_PID_DATA0;
			pkt->info |= PKT_PID_DATA0;
		}
		break;
	}

	if ((dest_speed == FHCI_LOW_SPEED) &&
	    (usb->port_status == FHCI_PORT_FULL))
		td_status |= TD_LSP;

	out_be16(&td->status, td_status);

	/* set up buffer length */
	if (trans_type == FHCI_TA_IN)
		out_be16(&td->length, pkt->len + CRC_SIZE);
	else
		out_be16(&td->length, pkt->len);

	/* put the frame to the confirmation queue */
	cq_put(&ep->conf_frame_Q, pkt);

	if (cq_howmany(&ep->conf_frame_Q) == 1)
		out_8(&usb->fhci->regs->usb_uscom, USB_CMD_STR_FIFO);

	return 0;
}

/* Reset the Tx BD ring */
void fhci_flush_bds(struct fhci_usb *usb)
{
	u16 extra_data;
	u16 td_status;
	u32 buf;
	struct usb_td __iomem *td;
	struct endpoint *ep = usb->ep0;

	td = ep->td_base;
	while (1) {
		td_status = in_be16(&td->status);
		buf = in_be32(&td->buf_ptr);
		extra_data = in_be16(&td->extra);

		/* if the TD is not empty - we'll confirm it as Timeout */
		if (td_status & TD_R)
			out_be16(&td->status, (td_status & ~TD_R) | TD_TO);
		/* if this TD is dummy - let's skip this TD */
		else if (in_be32(&td->buf_ptr) == DUMMY_BD_BUFFER)
			out_be32(&td->buf_ptr, DUMMY2_BD_BUFFER);
		/* if this is the last TD - break */
		if (td_status & TD_W)
			break;

		td++;
	}

	fhci_td_transaction_confirm(usb);

	td = ep->td_base;
	do {
		out_be16(&td->status, 0);
		out_be16(&td->length, 0);
		out_be32(&td->buf_ptr, 0);
		out_be16(&td->extra, 0);
		td++;
	} while (!(in_be16(&td->status) & TD_W));
	out_be16(&td->status, TD_W); /* for last TD set Wrap bit */
	out_be16(&td->length, 0);
	out_be32(&td->buf_ptr, 0);
	out_be16(&td->extra, 0);

	out_be16(&ep->ep_pram_ptr->tx_bd_ptr,
		 in_be16(&ep->ep_pram_ptr->tx_base));
	out_be32(&ep->ep_pram_ptr->tx_state, 0);
	out_be16(&ep->ep_pram_ptr->tx_cnt, 0);
	ep->empty_td = ep->td_base;
	ep->conf_td = ep->td_base;
}

/*
 * Flush all transmitted packets from TDs in the actual frame.
 * This routine is called when something wrong with the controller and
 * we want to get rid of the actual frame and start again next frame
 */
void fhci_flush_actual_frame(struct fhci_usb *usb)
{
	u8 mode;
	u16 tb_ptr;
	u16 extra_data;
	u16 td_status;
	u32 buf_ptr;
	struct usb_td __iomem *td;
	struct endpoint *ep = usb->ep0;

	/* disable the USB controller */
	mode = in_8(&usb->fhci->regs->usb_usmod);
	out_8(&usb->fhci->regs->usb_usmod, mode & ~USB_MODE_EN);

	tb_ptr = in_be16(&ep->ep_pram_ptr->tx_bd_ptr);
	td = cpm_muram_addr(tb_ptr);
	td_status = in_be16(&td->status);
	buf_ptr = in_be32(&td->buf_ptr);
	extra_data = in_be16(&td->extra);
	do {
		if (td_status & TD_R) {
			out_be16(&td->status, (td_status & ~TD_R) | TD_TO);
		} else {
			out_be32(&td->buf_ptr, 0);
			ep->already_pushed_dummy_bd = false;
			break;
		}

		/* advance the TD pointer */
		td = next_bd(ep->td_base, td, td_status);
		td_status = in_be16(&td->status);
		buf_ptr = in_be32(&td->buf_ptr);
		extra_data = in_be16(&td->extra);
	} while ((td_status & TD_R) || buf_ptr);

	fhci_td_transaction_confirm(usb);

	out_be16(&ep->ep_pram_ptr->tx_bd_ptr,
		 in_be16(&ep->ep_pram_ptr->tx_base));
	out_be32(&ep->ep_pram_ptr->tx_state, 0);
	out_be16(&ep->ep_pram_ptr->tx_cnt, 0);
	ep->empty_td = ep->td_base;
	ep->conf_td = ep->td_base;

	usb->actual_frame->frame_status = FRAME_TIMER_END_TRANSMISSION;

	/* reset the event register */
	out_be16(&usb->fhci->regs->usb_usber, 0xffff);
	/* enable the USB controller */
	out_8(&usb->fhci->regs->usb_usmod, mode | USB_MODE_EN);
}

/* handles Tx confirm and Tx error interrupt */
void fhci_tx_conf_interrupt(struct fhci_usb *usb)
{
	fhci_td_transaction_confirm(usb);

	/*
	 * Schedule another transaction to this frame only if we have
	 * already confirmed all transaction in the frame.
	 */
	if (((fhci_get_sof_timer_count(usb) < usb->max_frame_usage) ||
	     (usb->actual_frame->frame_status & FRAME_END_TRANSMISSION)) &&
	    (list_empty(&usb->actual_frame->tds_list)))
		fhci_schedule_transactions(usb);
}

void fhci_host_transmit_actual_frame(struct fhci_usb *usb)
{
	u16 tb_ptr;
	u16 td_status;
	struct usb_td __iomem *td;
	struct endpoint *ep = usb->ep0;

	tb_ptr = in_be16(&ep->ep_pram_ptr->tx_bd_ptr);
	td = cpm_muram_addr(tb_ptr);

	if (in_be32(&td->buf_ptr) == DUMMY_BD_BUFFER) {
		struct usb_td __iomem *old_td = td;

		ep->already_pushed_dummy_bd = false;
		td_status = in_be16(&td->status);
		/* gets the next TD in the ring */
		td = next_bd(ep->td_base, td, td_status);
		tb_ptr = cpm_muram_offset(td);
		out_be16(&ep->ep_pram_ptr->tx_bd_ptr, tb_ptr);

		/* start transmit only if we have something in the TDs */
		if (in_be16(&td->status) & TD_R)
			out_8(&usb->fhci->regs->usb_uscom, USB_CMD_STR_FIFO);

		if (in_be32(&ep->conf_td->buf_ptr) == DUMMY_BD_BUFFER) {
			out_be32(&old_td->buf_ptr, 0);
			ep->conf_td = next_bd(ep->td_base, ep->conf_td,
					      td_status);
		} else {
			out_be32(&old_td->buf_ptr, DUMMY2_BD_BUFFER);
		}
	}
}
