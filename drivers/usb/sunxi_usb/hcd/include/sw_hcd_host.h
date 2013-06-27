/*
 * drivers/usb/sunxi_usb/hcd/include/sw_hcd_host.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * javen <javen@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef  __SW_HCD_HOST_H__
#define  __SW_HCD_HOST_H__

/* 从 struct usb_hcd 结构里获得 struct sw_hcd 结构 */
static inline struct usb_hcd *sw_hcd_to_hcd(struct sw_hcd *sw_hcd)
{
	return container_of((void *) sw_hcd, struct usb_hcd, hcd_priv);
}

/* 从 struct sw_hcd 结构里获得 struct usb_hcd 结构 */
static inline struct sw_hcd *hcd_to_sw_hcd(struct usb_hcd *hcd)
{
	return (struct sw_hcd *) (hcd->hcd_priv);
}

/* stored in "usb_host_endpoint.hcpriv" for scheduled endpoints */
typedef struct sw_hcd_qh{
	struct usb_host_endpoint *hep;  /* usbcore info                 */
	struct usb_device  *dev;        /* usb device                   */
	struct sw_hcd_hw_ep  *hw_ep;		/* current binding              */

	struct list_head  ring;		    /* of sw_hcd_qh                   */
	struct sw_hcd_qh *next;	        /* for periodic tree            */
	u8  mux;		                /* qh multiplexed to hw_ep      */
	unsigned  offset;		        /* in urb->transfer_buffer      */
	unsigned  segsize;	            /* current xfer fragment        */

	u8  type_reg;	                /* {rx,tx} type register        */
	u8  intv_reg;	                /* {rx,tx} interval register    */
	u8  addr_reg;	                /* device address register      */
	u8  h_addr_reg;	                /* hub address register         */
	u8  h_port_reg;	                /* hub port register            */

	u8  is_ready;	                /* safe to modify hw_ep         */
	u8  type;		                /* ep type XFERTYPE_*           */
	u8  epnum;                      /* target ep index. 对应的外设的ep */
	u16  maxpacket;                 /* max packet size              */
	u16  frame;		                /* for periodic schedule        */
	unsigned  iso_idx;	            /* in urb->iso_frame_desc[]     */

	u32 dma_working;				/* flag. dma working flag 		*/
	u32 dma_transfer_len;			/* flag. dma transfer length 	*/
}sw_hcd_qh_t;

/* map from control or bulk queue head to the first qh on that ring */
static inline struct sw_hcd_qh *first_qh(struct list_head *q)
{
	if(q == NULL){
	    DMSG_WRN("ERR: invalid argment\n");
	    return NULL;
	}

	if (list_empty(q)){
	    DMSG_WRN("ERR: invalid argment\n");
	    return NULL;
	}

	return list_entry(q->next, struct sw_hcd_qh, ring);
}

/* get next urb */
static inline struct urb *next_urb(struct sw_hcd_qh *qh)
{
	struct list_head *queue;

	if (!qh){
	    DMSG_WRN("ERR: invalid argment\n");
	    return NULL;
	}

	queue = &qh->hep->urb_list;
	if (list_empty(queue) || queue->next == NULL){
	    DMSG_WRN("ERR: list is empty, queue->next = 0x%p\n", queue->next);
		return NULL;
	}

	return list_entry(queue->next, struct urb, urb_list);
}

//---------------------------------------------------------------
//  函数 定义
//---------------------------------------------------------------
irqreturn_t sw_hcd_h_ep0_irq(struct sw_hcd *sw_hcd);

int sw_hcd_urb_enqueue(struct usb_hcd *hcd, struct urb *urb, gfp_t mem_flags);
int sw_hcd_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status);
void sw_hcd_h_disable(struct usb_hcd *hcd, struct usb_host_endpoint *hep);

int sw_hcd_h_start(struct usb_hcd *hcd);
void sw_hcd_h_stop(struct usb_hcd *hcd);

int sw_hcd_h_get_frame_number(struct usb_hcd *hcd);
int sw_hcd_bus_suspend(struct usb_hcd *hcd);
int sw_hcd_bus_resume(struct usb_hcd *hcd);

void sw_hcd_host_rx(struct sw_hcd *sw_hcd, u8 epnum);
void sw_hcd_host_tx(struct sw_hcd *sw_hcd, u8 epnum);


#endif   //__SW_HCD_HOST_H__

