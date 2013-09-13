/*
 * USB Host Controller Driver for IMX21
 *
 * Copyright (C) 2006 Loping Dog Embedded Systems
 * Copyright (C) 2009 Martin Fuzzey
 * Originally written by Jay Monkman <jtm@lopingdog.com>
 * Ported to 2.6.30, debugged and enhanced by Martin Fuzzey
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


 /*
  * The i.MX21 USB hardware contains
  *    * 32 transfer descriptors (called ETDs)
  *    * 4Kb of Data memory
  *
  * The data memory is shared between the host and function controllers
  * (but this driver only supports the host controller)
  *
  * So setting up a transfer involves:
  *    * Allocating a ETD
  *    * Fill in ETD with appropriate information
  *    * Allocating data memory (and putting the offset in the ETD)
  *    * Activate the ETD
  *    * Get interrupt when done.
  *
  * An ETD is assigned to each active endpoint.
  *
  * Low resource (ETD and Data memory) situations are handled differently for
  * isochronous and non insosynchronous transactions :
  *
  * Non ISOC transfers are queued if either ETDs or Data memory are unavailable
  *
  * ISOC transfers use 2 ETDs per endpoint to achieve double buffering.
  * They allocate both ETDs and Data memory during URB submission
  * (and fail if unavailable).
  */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>

#include "imx21-hcd.h"

#ifdef DEBUG
#define DEBUG_LOG_FRAME(imx21, etd, event) \
	(etd)->event##_frame = readl((imx21)->regs + USBH_FRMNUB)
#else
#define DEBUG_LOG_FRAME(imx21, etd, event) do { } while (0)
#endif

static const char hcd_name[] = "imx21-hcd";

static inline struct imx21 *hcd_to_imx21(struct usb_hcd *hcd)
{
	return (struct imx21 *)hcd->hcd_priv;
}


/* =========================================== */
/* Hardware access helpers			*/
/* =========================================== */

static inline void set_register_bits(struct imx21 *imx21, u32 offset, u32 mask)
{
	void __iomem *reg = imx21->regs + offset;
	writel(readl(reg) | mask, reg);
}

static inline void clear_register_bits(struct imx21 *imx21,
	u32 offset, u32 mask)
{
	void __iomem *reg = imx21->regs + offset;
	writel(readl(reg) & ~mask, reg);
}

static inline void clear_toggle_bit(struct imx21 *imx21, u32 offset, u32 mask)
{
	void __iomem *reg = imx21->regs + offset;

	if (readl(reg) & mask)
		writel(mask, reg);
}

static inline void set_toggle_bit(struct imx21 *imx21, u32 offset, u32 mask)
{
	void __iomem *reg = imx21->regs + offset;

	if (!(readl(reg) & mask))
		writel(mask, reg);
}

static void etd_writel(struct imx21 *imx21, int etd_num, int dword, u32 value)
{
	writel(value, imx21->regs + USB_ETD_DWORD(etd_num, dword));
}

static u32 etd_readl(struct imx21 *imx21, int etd_num, int dword)
{
	return readl(imx21->regs + USB_ETD_DWORD(etd_num, dword));
}

static inline int wrap_frame(int counter)
{
	return counter & 0xFFFF;
}

static inline int frame_after(int frame, int after)
{
	/* handle wrapping like jiffies time_afer */
	return (s16)((s16)after - (s16)frame) < 0;
}

static int imx21_hc_get_frame(struct usb_hcd *hcd)
{
	struct imx21 *imx21 = hcd_to_imx21(hcd);

	return wrap_frame(readl(imx21->regs + USBH_FRMNUB));
}

static inline bool unsuitable_for_dma(dma_addr_t addr)
{
	return (addr & 3) != 0;
}

#include "imx21-dbg.c"

static void nonisoc_urb_completed_for_etd(
	struct imx21 *imx21, struct etd_priv *etd, int status);
static void schedule_nonisoc_etd(struct imx21 *imx21, struct urb *urb);
static void free_dmem(struct imx21 *imx21, struct etd_priv *etd);

/* =========================================== */
/* ETD management				*/
/* ===========================================	*/

static int alloc_etd(struct imx21 *imx21)
{
	int i;
	struct etd_priv *etd = imx21->etd;

	for (i = 0; i < USB_NUM_ETD; i++, etd++) {
		if (etd->alloc == 0) {
			memset(etd, 0, sizeof(imx21->etd[0]));
			etd->alloc = 1;
			debug_etd_allocated(imx21);
			return i;
		}
	}
	return -1;
}

static void disactivate_etd(struct imx21 *imx21, int num)
{
	int etd_mask = (1 << num);
	struct etd_priv *etd = &imx21->etd[num];

	writel(etd_mask, imx21->regs + USBH_ETDENCLR);
	clear_register_bits(imx21, USBH_ETDDONEEN, etd_mask);
	writel(etd_mask, imx21->regs + USB_ETDDMACHANLCLR);
	clear_toggle_bit(imx21, USBH_ETDDONESTAT, etd_mask);

	etd->active_count = 0;

	DEBUG_LOG_FRAME(imx21, etd, disactivated);
}

static void reset_etd(struct imx21 *imx21, int num)
{
	struct etd_priv *etd = imx21->etd + num;
	int i;

	disactivate_etd(imx21, num);

	for (i = 0; i < 4; i++)
		etd_writel(imx21, num, i, 0);
	etd->urb = NULL;
	etd->ep = NULL;
	etd->td = NULL;
	etd->bounce_buffer = NULL;
}

static void free_etd(struct imx21 *imx21, int num)
{
	if (num < 0)
		return;

	if (num >= USB_NUM_ETD) {
		dev_err(imx21->dev, "BAD etd=%d!\n", num);
		return;
	}
	if (imx21->etd[num].alloc == 0) {
		dev_err(imx21->dev, "ETD %d already free!\n", num);
		return;
	}

	debug_etd_freed(imx21);
	reset_etd(imx21, num);
	memset(&imx21->etd[num], 0, sizeof(imx21->etd[0]));
}


static void setup_etd_dword0(struct imx21 *imx21,
	int etd_num, struct urb *urb,  u8 dir, u16 maxpacket)
{
	etd_writel(imx21, etd_num, 0,
		((u32) usb_pipedevice(urb->pipe)) <<  DW0_ADDRESS |
		((u32) usb_pipeendpoint(urb->pipe) << DW0_ENDPNT) |
		((u32) dir << DW0_DIRECT) |
		((u32) ((urb->dev->speed == USB_SPEED_LOW) ?
			1 : 0) << DW0_SPEED) |
		((u32) fmt_urb_to_etd[usb_pipetype(urb->pipe)] << DW0_FORMAT) |
		((u32) maxpacket << DW0_MAXPKTSIZ));
}

/**
 * Copy buffer to data controller data memory.
 * We cannot use memcpy_toio() because the hardware requires 32bit writes
 */
static void copy_to_dmem(
	struct imx21 *imx21, int dmem_offset, void *src, int count)
{
	void __iomem *dmem = imx21->regs + USBOTG_DMEM + dmem_offset;
	u32 word = 0;
	u8 *p = src;
	int byte = 0;
	int i;

	for (i = 0; i < count; i++) {
		byte = i % 4;
		word += (*p++ << (byte * 8));
		if (byte == 3) {
			writel(word, dmem);
			dmem += 4;
			word = 0;
		}
	}

	if (count && byte != 3)
		writel(word, dmem);
}

static void activate_etd(struct imx21 *imx21, int etd_num, u8 dir)
{
	u32 etd_mask = 1 << etd_num;
	struct etd_priv *etd = &imx21->etd[etd_num];

	if (etd->dma_handle && unsuitable_for_dma(etd->dma_handle)) {
		/* For non aligned isoc the condition below is always true */
		if (etd->len <= etd->dmem_size) {
			/* Fits into data memory, use PIO */
			if (dir != TD_DIR_IN) {
				copy_to_dmem(imx21,
						etd->dmem_offset,
						etd->cpu_buffer, etd->len);
			}
			etd->dma_handle = 0;

		} else {
			/* Too big for data memory, use bounce buffer */
			enum dma_data_direction dmadir;

			if (dir == TD_DIR_IN) {
				dmadir = DMA_FROM_DEVICE;
				etd->bounce_buffer = kmalloc(etd->len,
								GFP_ATOMIC);
			} else {
				dmadir = DMA_TO_DEVICE;
				etd->bounce_buffer = kmemdup(etd->cpu_buffer,
								etd->len,
								GFP_ATOMIC);
			}
			if (!etd->bounce_buffer) {
				dev_err(imx21->dev, "failed bounce alloc\n");
				goto err_bounce_alloc;
			}

			etd->dma_handle =
				dma_map_single(imx21->dev,
						etd->bounce_buffer,
						etd->len,
						dmadir);
			if (dma_mapping_error(imx21->dev, etd->dma_handle)) {
				dev_err(imx21->dev, "failed bounce map\n");
				goto err_bounce_map;
			}
		}
	}

	clear_toggle_bit(imx21, USBH_ETDDONESTAT, etd_mask);
	set_register_bits(imx21, USBH_ETDDONEEN, etd_mask);
	clear_toggle_bit(imx21, USBH_XFILLSTAT, etd_mask);
	clear_toggle_bit(imx21, USBH_YFILLSTAT, etd_mask);

	if (etd->dma_handle) {
		set_register_bits(imx21, USB_ETDDMACHANLCLR, etd_mask);
		clear_toggle_bit(imx21, USBH_XBUFSTAT, etd_mask);
		clear_toggle_bit(imx21, USBH_YBUFSTAT, etd_mask);
		writel(etd->dma_handle, imx21->regs + USB_ETDSMSA(etd_num));
		set_register_bits(imx21, USB_ETDDMAEN, etd_mask);
	} else {
		if (dir != TD_DIR_IN) {
			/* need to set for ZLP and PIO */
			set_toggle_bit(imx21, USBH_XFILLSTAT, etd_mask);
			set_toggle_bit(imx21, USBH_YFILLSTAT, etd_mask);
		}
	}

	DEBUG_LOG_FRAME(imx21, etd, activated);

#ifdef DEBUG
	if (!etd->active_count) {
		int i;
		etd->activated_frame = readl(imx21->regs + USBH_FRMNUB);
		etd->disactivated_frame = -1;
		etd->last_int_frame = -1;
		etd->last_req_frame = -1;

		for (i = 0; i < 4; i++)
			etd->submitted_dwords[i] = etd_readl(imx21, etd_num, i);
	}
#endif

	etd->active_count = 1;
	writel(etd_mask, imx21->regs + USBH_ETDENSET);
	return;

err_bounce_map:
	kfree(etd->bounce_buffer);

err_bounce_alloc:
	free_dmem(imx21, etd);
	nonisoc_urb_completed_for_etd(imx21, etd, -ENOMEM);
}

/* ===========================================	*/
/* Data memory management			*/
/* ===========================================	*/

static int alloc_dmem(struct imx21 *imx21, unsigned int size,
		      struct usb_host_endpoint *ep)
{
	unsigned int offset = 0;
	struct imx21_dmem_area *area;
	struct imx21_dmem_area *tmp;

	size += (~size + 1) & 0x3; /* Round to 4 byte multiple */

	if (size > DMEM_SIZE) {
		dev_err(imx21->dev, "size=%d > DMEM_SIZE(%d)\n",
			size, DMEM_SIZE);
		return -EINVAL;
	}

	list_for_each_entry(tmp, &imx21->dmem_list, list) {
		if ((size + offset) < offset)
			goto fail;
		if ((size + offset) <= tmp->offset)
			break;
		offset = tmp->size + tmp->offset;
		if ((offset + size) > DMEM_SIZE)
			goto fail;
	}

	area = kmalloc(sizeof(struct imx21_dmem_area), GFP_ATOMIC);
	if (area == NULL)
		return -ENOMEM;

	area->ep = ep;
	area->offset = offset;
	area->size = size;
	list_add_tail(&area->list, &tmp->list);
	debug_dmem_allocated(imx21, size);
	return offset;

fail:
	return -ENOMEM;
}

/* Memory now available for a queued ETD - activate it */
static void activate_queued_etd(struct imx21 *imx21,
	struct etd_priv *etd, u32 dmem_offset)
{
	struct urb_priv *urb_priv = etd->urb->hcpriv;
	int etd_num = etd - &imx21->etd[0];
	u32 maxpacket = etd_readl(imx21, etd_num, 1) >> DW1_YBUFSRTAD;
	u8 dir = (etd_readl(imx21, etd_num, 2) >> DW2_DIRPID) & 0x03;

	dev_dbg(imx21->dev, "activating queued ETD %d now DMEM available\n",
		etd_num);
	etd_writel(imx21, etd_num, 1,
	    ((dmem_offset + maxpacket) << DW1_YBUFSRTAD) | dmem_offset);

	etd->dmem_offset = dmem_offset;
	urb_priv->active = 1;
	activate_etd(imx21, etd_num, dir);
}

static void free_dmem(struct imx21 *imx21, struct etd_priv *etd)
{
	struct imx21_dmem_area *area;
	struct etd_priv *tmp;
	int found = 0;
	int offset;

	if (!etd->dmem_size)
		return;
	etd->dmem_size = 0;

	offset = etd->dmem_offset;
	list_for_each_entry(area, &imx21->dmem_list, list) {
		if (area->offset == offset) {
			debug_dmem_freed(imx21, area->size);
			list_del(&area->list);
			kfree(area);
			found = 1;
			break;
		}
	}

	if (!found)  {
		dev_err(imx21->dev,
			"Trying to free unallocated DMEM %d\n", offset);
		return;
	}

	/* Try again to allocate memory for anything we've queued */
	list_for_each_entry_safe(etd, tmp, &imx21->queue_for_dmem, queue) {
		offset = alloc_dmem(imx21, etd->dmem_size, etd->ep);
		if (offset >= 0) {
			list_del(&etd->queue);
			activate_queued_etd(imx21, etd, (u32)offset);
		}
	}
}

static void free_epdmem(struct imx21 *imx21, struct usb_host_endpoint *ep)
{
	struct imx21_dmem_area *area, *tmp;

	list_for_each_entry_safe(area, tmp, &imx21->dmem_list, list) {
		if (area->ep == ep) {
			dev_err(imx21->dev,
				"Active DMEM %d for disabled ep=%p\n",
				area->offset, ep);
			list_del(&area->list);
			kfree(area);
		}
	}
}


/* ===========================================	*/
/* End handling 				*/
/* ===========================================	*/

/* Endpoint now idle - release its ETD(s) or assign to queued request */
static void ep_idle(struct imx21 *imx21, struct ep_priv *ep_priv)
{
	int i;

	for (i = 0; i < NUM_ISO_ETDS; i++) {
		int etd_num = ep_priv->etd[i];
		struct etd_priv *etd;
		if (etd_num < 0)
			continue;

		etd = &imx21->etd[etd_num];
		ep_priv->etd[i] = -1;

		free_dmem(imx21, etd); /* for isoc */

		if (list_empty(&imx21->queue_for_etd)) {
			free_etd(imx21, etd_num);
			continue;
		}

		dev_dbg(imx21->dev,
			"assigning idle etd %d for queued request\n", etd_num);
		ep_priv = list_first_entry(&imx21->queue_for_etd,
			struct ep_priv, queue);
		list_del(&ep_priv->queue);
		reset_etd(imx21, etd_num);
		ep_priv->waiting_etd = 0;
		ep_priv->etd[i] = etd_num;

		if (list_empty(&ep_priv->ep->urb_list)) {
			dev_err(imx21->dev, "No urb for queued ep!\n");
			continue;
		}
		schedule_nonisoc_etd(imx21, list_first_entry(
			&ep_priv->ep->urb_list, struct urb, urb_list));
	}
}

static void urb_done(struct usb_hcd *hcd, struct urb *urb, int status)
__releases(imx21->lock)
__acquires(imx21->lock)
{
	struct imx21 *imx21 = hcd_to_imx21(hcd);
	struct ep_priv *ep_priv = urb->ep->hcpriv;
	struct urb_priv *urb_priv = urb->hcpriv;

	debug_urb_completed(imx21, urb, status);
	dev_vdbg(imx21->dev, "urb %p done %d\n", urb, status);

	kfree(urb_priv->isoc_td);
	kfree(urb->hcpriv);
	urb->hcpriv = NULL;
	usb_hcd_unlink_urb_from_ep(hcd, urb);
	spin_unlock(&imx21->lock);
	usb_hcd_giveback_urb(hcd, urb, status);
	spin_lock(&imx21->lock);
	if (list_empty(&ep_priv->ep->urb_list))
		ep_idle(imx21, ep_priv);
}

static void nonisoc_urb_completed_for_etd(
	struct imx21 *imx21, struct etd_priv *etd, int status)
{
	struct usb_host_endpoint *ep = etd->ep;

	urb_done(imx21->hcd, etd->urb, status);
	etd->urb = NULL;

	if (!list_empty(&ep->urb_list)) {
		struct urb *urb = list_first_entry(
					&ep->urb_list, struct urb, urb_list);

		dev_vdbg(imx21->dev, "next URB %p\n", urb);
		schedule_nonisoc_etd(imx21, urb);
	}
}


/* ===========================================	*/
/* ISOC Handling ... 				*/
/* ===========================================	*/

static void schedule_isoc_etds(struct usb_hcd *hcd,
	struct usb_host_endpoint *ep)
{
	struct imx21 *imx21 = hcd_to_imx21(hcd);
	struct ep_priv *ep_priv = ep->hcpriv;
	struct etd_priv *etd;
	struct urb_priv *urb_priv;
	struct td *td;
	int etd_num;
	int i;
	int cur_frame;
	u8 dir;

	for (i = 0; i < NUM_ISO_ETDS; i++) {
too_late:
		if (list_empty(&ep_priv->td_list))
			break;

		etd_num = ep_priv->etd[i];
		if (etd_num < 0)
			break;

		etd = &imx21->etd[etd_num];
		if (etd->urb)
			continue;

		td = list_entry(ep_priv->td_list.next, struct td, list);
		list_del(&td->list);
		urb_priv = td->urb->hcpriv;

		cur_frame = imx21_hc_get_frame(hcd);
		if (frame_after(cur_frame, td->frame)) {
			dev_dbg(imx21->dev, "isoc too late frame %d > %d\n",
				cur_frame, td->frame);
			urb_priv->isoc_status = -EXDEV;
			td->urb->iso_frame_desc[
				td->isoc_index].actual_length = 0;
			td->urb->iso_frame_desc[td->isoc_index].status = -EXDEV;
			if (--urb_priv->isoc_remaining == 0)
				urb_done(hcd, td->urb, urb_priv->isoc_status);
			goto too_late;
		}

		urb_priv->active = 1;
		etd->td = td;
		etd->ep = td->ep;
		etd->urb = td->urb;
		etd->len = td->len;
		etd->dma_handle = td->dma_handle;
		etd->cpu_buffer = td->cpu_buffer;

		debug_isoc_submitted(imx21, cur_frame, td);

		dir = usb_pipeout(td->urb->pipe) ? TD_DIR_OUT : TD_DIR_IN;
		setup_etd_dword0(imx21, etd_num, td->urb, dir, etd->dmem_size);
		etd_writel(imx21, etd_num, 1, etd->dmem_offset);
		etd_writel(imx21, etd_num, 2,
			(TD_NOTACCESSED << DW2_COMPCODE) |
			((td->frame & 0xFFFF) << DW2_STARTFRM));
		etd_writel(imx21, etd_num, 3,
			(TD_NOTACCESSED << DW3_COMPCODE0) |
			(td->len << DW3_PKTLEN0));

		activate_etd(imx21, etd_num, dir);
	}
}

static void isoc_etd_done(struct usb_hcd *hcd, int etd_num)
{
	struct imx21 *imx21 = hcd_to_imx21(hcd);
	int etd_mask = 1 << etd_num;
	struct etd_priv *etd = imx21->etd + etd_num;
	struct urb *urb = etd->urb;
	struct urb_priv *urb_priv = urb->hcpriv;
	struct td *td = etd->td;
	struct usb_host_endpoint *ep = etd->ep;
	int isoc_index = td->isoc_index;
	unsigned int pipe = urb->pipe;
	int dir_in = usb_pipein(pipe);
	int cc;
	int bytes_xfrd;

	disactivate_etd(imx21, etd_num);

	cc = (etd_readl(imx21, etd_num, 3) >> DW3_COMPCODE0) & 0xf;
	bytes_xfrd = etd_readl(imx21, etd_num, 3) & 0x3ff;

	/* Input doesn't always fill the buffer, don't generate an error
	 * when this happens.
	 */
	if (dir_in && (cc == TD_DATAUNDERRUN))
		cc = TD_CC_NOERROR;

	if (cc == TD_NOTACCESSED)
		bytes_xfrd = 0;

	debug_isoc_completed(imx21,
		imx21_hc_get_frame(hcd), td, cc, bytes_xfrd);
	if (cc) {
		urb_priv->isoc_status = -EXDEV;
		dev_dbg(imx21->dev,
			"bad iso cc=0x%X frame=%d sched frame=%d "
			"cnt=%d len=%d urb=%p etd=%d index=%d\n",
			cc,  imx21_hc_get_frame(hcd), td->frame,
			bytes_xfrd, td->len, urb, etd_num, isoc_index);
	}

	if (dir_in) {
		clear_toggle_bit(imx21, USBH_XFILLSTAT, etd_mask);
		if (!etd->dma_handle)
			memcpy_fromio(etd->cpu_buffer,
				imx21->regs + USBOTG_DMEM + etd->dmem_offset,
				bytes_xfrd);
	}

	urb->actual_length += bytes_xfrd;
	urb->iso_frame_desc[isoc_index].actual_length = bytes_xfrd;
	urb->iso_frame_desc[isoc_index].status = cc_to_error[cc];

	etd->td = NULL;
	etd->urb = NULL;
	etd->ep = NULL;

	if (--urb_priv->isoc_remaining == 0)
		urb_done(hcd, urb, urb_priv->isoc_status);

	schedule_isoc_etds(hcd, ep);
}

static struct ep_priv *alloc_isoc_ep(
	struct imx21 *imx21, struct usb_host_endpoint *ep)
{
	struct ep_priv *ep_priv;
	int i;

	ep_priv = kzalloc(sizeof(struct ep_priv), GFP_ATOMIC);
	if (!ep_priv)
		return NULL;

	for (i = 0; i < NUM_ISO_ETDS; i++)
		ep_priv->etd[i] = -1;

	INIT_LIST_HEAD(&ep_priv->td_list);
	ep_priv->ep = ep;
	ep->hcpriv = ep_priv;
	return ep_priv;
}

static int alloc_isoc_etds(struct imx21 *imx21, struct ep_priv *ep_priv)
{
	int i, j;
	int etd_num;

	/* Allocate the ETDs if required */
	for (i = 0; i < NUM_ISO_ETDS; i++) {
		if (ep_priv->etd[i] < 0) {
			etd_num = alloc_etd(imx21);
			if (etd_num < 0)
				goto alloc_etd_failed;

			ep_priv->etd[i] = etd_num;
			imx21->etd[etd_num].ep = ep_priv->ep;
		}
	}
	return 0;

alloc_etd_failed:
	dev_err(imx21->dev, "isoc: Couldn't allocate etd\n");
	for (j = 0; j < i; j++) {
		free_etd(imx21, ep_priv->etd[j]);
		ep_priv->etd[j] = -1;
	}
	return -ENOMEM;
}

static int imx21_hc_urb_enqueue_isoc(struct usb_hcd *hcd,
				     struct usb_host_endpoint *ep,
				     struct urb *urb, gfp_t mem_flags)
{
	struct imx21 *imx21 = hcd_to_imx21(hcd);
	struct urb_priv *urb_priv;
	unsigned long flags;
	struct ep_priv *ep_priv;
	struct td *td = NULL;
	int i;
	int ret;
	int cur_frame;
	u16 maxpacket;

	urb_priv = kzalloc(sizeof(struct urb_priv), mem_flags);
	if (urb_priv == NULL)
		return -ENOMEM;

	urb_priv->isoc_td = kzalloc(
		sizeof(struct td) * urb->number_of_packets, mem_flags);
	if (urb_priv->isoc_td == NULL) {
		ret = -ENOMEM;
		goto alloc_td_failed;
	}

	spin_lock_irqsave(&imx21->lock, flags);

	if (ep->hcpriv == NULL) {
		ep_priv = alloc_isoc_ep(imx21, ep);
		if (ep_priv == NULL) {
			ret = -ENOMEM;
			goto alloc_ep_failed;
		}
	} else {
		ep_priv = ep->hcpriv;
	}

	ret = alloc_isoc_etds(imx21, ep_priv);
	if (ret)
		goto alloc_etd_failed;

	ret = usb_hcd_link_urb_to_ep(hcd, urb);
	if (ret)
		goto link_failed;

	urb->status = -EINPROGRESS;
	urb->actual_length = 0;
	urb->error_count = 0;
	urb->hcpriv = urb_priv;
	urb_priv->ep = ep;

	/* allocate data memory for largest packets if not already done */
	maxpacket = usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe));
	for (i = 0; i < NUM_ISO_ETDS; i++) {
		struct etd_priv *etd = &imx21->etd[ep_priv->etd[i]];

		if (etd->dmem_size > 0 && etd->dmem_size < maxpacket) {
			/* not sure if this can really occur.... */
			dev_err(imx21->dev, "increasing isoc buffer %d->%d\n",
				etd->dmem_size, maxpacket);
			ret = -EMSGSIZE;
			goto alloc_dmem_failed;
		}

		if (etd->dmem_size == 0) {
			etd->dmem_offset = alloc_dmem(imx21, maxpacket, ep);
			if (etd->dmem_offset < 0) {
				dev_dbg(imx21->dev, "failed alloc isoc dmem\n");
				ret = -EAGAIN;
				goto alloc_dmem_failed;
			}
			etd->dmem_size = maxpacket;
		}
	}

	/* calculate frame */
	cur_frame = imx21_hc_get_frame(hcd);
	i = 0;
	if (list_empty(&ep_priv->td_list)) {
		urb->start_frame = wrap_frame(cur_frame + 5);
	} else {
		urb->start_frame = wrap_frame(list_entry(ep_priv->td_list.prev,
				struct td, list)->frame + urb->interval);

		if (frame_after(cur_frame, urb->start_frame)) {
			dev_dbg(imx21->dev,
				"enqueue: adjusting iso start %d (cur=%d) asap=%d\n",
				urb->start_frame, cur_frame,
				(urb->transfer_flags & URB_ISO_ASAP) != 0);
			i = DIV_ROUND_UP(wrap_frame(
					cur_frame - urb->start_frame),
					urb->interval);
			if (urb->transfer_flags & URB_ISO_ASAP) {
				urb->start_frame = wrap_frame(urb->start_frame
						+ i * urb->interval);
				i = 0;
			} else if (i >= urb->number_of_packets) {
				ret = -EXDEV;
				goto alloc_dmem_failed;
			}
		}
	}

	/* set up transfers */
	urb_priv->isoc_remaining = urb->number_of_packets - i;
	td = urb_priv->isoc_td;
	for (; i < urb->number_of_packets; i++, td++) {
		unsigned int offset = urb->iso_frame_desc[i].offset;
		td->ep = ep;
		td->urb = urb;
		td->len = urb->iso_frame_desc[i].length;
		td->isoc_index = i;
		td->frame = wrap_frame(urb->start_frame + urb->interval * i);
		td->dma_handle = urb->transfer_dma + offset;
		td->cpu_buffer = urb->transfer_buffer + offset;
		list_add_tail(&td->list, &ep_priv->td_list);
	}

	dev_vdbg(imx21->dev, "setup %d packets for iso frame %d->%d\n",
		urb->number_of_packets, urb->start_frame, td->frame);

	debug_urb_submitted(imx21, urb);
	schedule_isoc_etds(hcd, ep);

	spin_unlock_irqrestore(&imx21->lock, flags);
	return 0;

alloc_dmem_failed:
	usb_hcd_unlink_urb_from_ep(hcd, urb);

link_failed:
alloc_etd_failed:
alloc_ep_failed:
	spin_unlock_irqrestore(&imx21->lock, flags);
	kfree(urb_priv->isoc_td);

alloc_td_failed:
	kfree(urb_priv);
	return ret;
}

static void dequeue_isoc_urb(struct imx21 *imx21,
	struct urb *urb, struct ep_priv *ep_priv)
{
	struct urb_priv *urb_priv = urb->hcpriv;
	struct td *td, *tmp;
	int i;

	if (urb_priv->active) {
		for (i = 0; i < NUM_ISO_ETDS; i++) {
			int etd_num = ep_priv->etd[i];
			if (etd_num != -1 && imx21->etd[etd_num].urb == urb) {
				struct etd_priv *etd = imx21->etd + etd_num;

				reset_etd(imx21, etd_num);
				free_dmem(imx21, etd);
			}
		}
	}

	list_for_each_entry_safe(td, tmp, &ep_priv->td_list, list) {
		if (td->urb == urb) {
			dev_vdbg(imx21->dev, "removing td %p\n", td);
			list_del(&td->list);
		}
	}
}

/* =========================================== */
/* NON ISOC Handling ... 			*/
/* =========================================== */

static void schedule_nonisoc_etd(struct imx21 *imx21, struct urb *urb)
{
	unsigned int pipe = urb->pipe;
	struct urb_priv *urb_priv = urb->hcpriv;
	struct ep_priv *ep_priv = urb_priv->ep->hcpriv;
	int state = urb_priv->state;
	int etd_num = ep_priv->etd[0];
	struct etd_priv *etd;
	u32 count;
	u16 etd_buf_size;
	u16 maxpacket;
	u8 dir;
	u8 bufround;
	u8 datatoggle;
	u8 interval = 0;
	u8 relpolpos = 0;

	if (etd_num < 0) {
		dev_err(imx21->dev, "No valid ETD\n");
		return;
	}
	if (readl(imx21->regs + USBH_ETDENSET) & (1 << etd_num))
		dev_err(imx21->dev, "submitting to active ETD %d\n", etd_num);

	etd = &imx21->etd[etd_num];
	maxpacket = usb_maxpacket(urb->dev, pipe, usb_pipeout(pipe));
	if (!maxpacket)
		maxpacket = 8;

	if (usb_pipecontrol(pipe) && (state != US_CTRL_DATA)) {
		if (state == US_CTRL_SETUP) {
			dir = TD_DIR_SETUP;
			if (unsuitable_for_dma(urb->setup_dma))
				usb_hcd_unmap_urb_setup_for_dma(imx21->hcd,
					urb);
			etd->dma_handle = urb->setup_dma;
			etd->cpu_buffer = urb->setup_packet;
			bufround = 0;
			count = 8;
			datatoggle = TD_TOGGLE_DATA0;
		} else {	/* US_CTRL_ACK */
			dir = usb_pipeout(pipe) ? TD_DIR_IN : TD_DIR_OUT;
			bufround = 0;
			count = 0;
			datatoggle = TD_TOGGLE_DATA1;
		}
	} else {
		dir = usb_pipeout(pipe) ? TD_DIR_OUT : TD_DIR_IN;
		bufround = (dir == TD_DIR_IN) ? 1 : 0;
		if (unsuitable_for_dma(urb->transfer_dma))
			usb_hcd_unmap_urb_for_dma(imx21->hcd, urb);

		etd->dma_handle = urb->transfer_dma;
		etd->cpu_buffer = urb->transfer_buffer;
		if (usb_pipebulk(pipe) && (state == US_BULK0))
			count = 0;
		else
			count = urb->transfer_buffer_length;

		if (usb_pipecontrol(pipe)) {
			datatoggle = TD_TOGGLE_DATA1;
		} else {
			if (usb_gettoggle(
					urb->dev,
					usb_pipeendpoint(urb->pipe),
					usb_pipeout(urb->pipe)))
				datatoggle = TD_TOGGLE_DATA1;
			else
				datatoggle = TD_TOGGLE_DATA0;
		}
	}

	etd->urb = urb;
	etd->ep = urb_priv->ep;
	etd->len = count;

	if (usb_pipeint(pipe)) {
		interval = urb->interval;
		relpolpos = (readl(imx21->regs + USBH_FRMNUB) + 1) & 0xff;
	}

	/* Write ETD to device memory */
	setup_etd_dword0(imx21, etd_num, urb, dir, maxpacket);

	etd_writel(imx21, etd_num, 2,
		(u32) interval << DW2_POLINTERV |
		((u32) relpolpos << DW2_RELPOLPOS) |
		((u32) dir << DW2_DIRPID) |
		((u32) bufround << DW2_BUFROUND) |
		((u32) datatoggle << DW2_DATATOG) |
		((u32) TD_NOTACCESSED << DW2_COMPCODE));

	/* DMA will always transfer buffer size even if TOBYCNT in DWORD3
	   is smaller. Make sure we don't overrun the buffer!
	 */
	if (count && count < maxpacket)
		etd_buf_size = count;
	else
		etd_buf_size = maxpacket;

	etd_writel(imx21, etd_num, 3,
		((u32) (etd_buf_size - 1) << DW3_BUFSIZE) | (u32) count);

	if (!count)
		etd->dma_handle = 0;

	/* allocate x and y buffer space at once */
	etd->dmem_size = (count > maxpacket) ? maxpacket * 2 : maxpacket;
	etd->dmem_offset = alloc_dmem(imx21, etd->dmem_size, urb_priv->ep);
	if (etd->dmem_offset < 0) {
		/* Setup everything we can in HW and update when we get DMEM */
		etd_writel(imx21, etd_num, 1, (u32)maxpacket << 16);

		dev_dbg(imx21->dev, "Queuing etd %d for DMEM\n", etd_num);
		debug_urb_queued_for_dmem(imx21, urb);
		list_add_tail(&etd->queue, &imx21->queue_for_dmem);
		return;
	}

	etd_writel(imx21, etd_num, 1,
		(((u32) etd->dmem_offset + (u32) maxpacket) << DW1_YBUFSRTAD) |
		(u32) etd->dmem_offset);

	urb_priv->active = 1;

	/* enable the ETD to kick off transfer */
	dev_vdbg(imx21->dev, "Activating etd %d for %d bytes %s\n",
		etd_num, count, dir != TD_DIR_IN ? "out" : "in");
	activate_etd(imx21, etd_num, dir);

}

static void nonisoc_etd_done(struct usb_hcd *hcd, int etd_num)
{
	struct imx21 *imx21 = hcd_to_imx21(hcd);
	struct etd_priv *etd = &imx21->etd[etd_num];
	struct urb *urb = etd->urb;
	u32 etd_mask = 1 << etd_num;
	struct urb_priv *urb_priv = urb->hcpriv;
	int dir;
	int cc;
	u32 bytes_xfrd;
	int etd_done;

	disactivate_etd(imx21, etd_num);

	dir = (etd_readl(imx21, etd_num, 0) >> DW0_DIRECT) & 0x3;
	cc = (etd_readl(imx21, etd_num, 2) >> DW2_COMPCODE) & 0xf;
	bytes_xfrd = etd->len - (etd_readl(imx21, etd_num, 3) & 0x1fffff);

	/* save toggle carry */
	usb_settoggle(urb->dev, usb_pipeendpoint(urb->pipe),
		      usb_pipeout(urb->pipe),
		      (etd_readl(imx21, etd_num, 0) >> DW0_TOGCRY) & 0x1);

	if (dir == TD_DIR_IN) {
		clear_toggle_bit(imx21, USBH_XFILLSTAT, etd_mask);
		clear_toggle_bit(imx21, USBH_YFILLSTAT, etd_mask);

		if (etd->bounce_buffer) {
			memcpy(etd->cpu_buffer, etd->bounce_buffer, bytes_xfrd);
			dma_unmap_single(imx21->dev,
				etd->dma_handle, etd->len, DMA_FROM_DEVICE);
		} else if (!etd->dma_handle && bytes_xfrd) {/* PIO */
			memcpy_fromio(etd->cpu_buffer,
				imx21->regs + USBOTG_DMEM + etd->dmem_offset,
				bytes_xfrd);
		}
	}

	kfree(etd->bounce_buffer);
	etd->bounce_buffer = NULL;
	free_dmem(imx21, etd);

	urb->error_count = 0;
	if (!(urb->transfer_flags & URB_SHORT_NOT_OK)
			&& (cc == TD_DATAUNDERRUN))
		cc = TD_CC_NOERROR;

	if (cc != 0)
		dev_vdbg(imx21->dev, "cc is 0x%x\n", cc);

	etd_done = (cc_to_error[cc] != 0);	/* stop if error */

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		switch (urb_priv->state) {
		case US_CTRL_SETUP:
			if (urb->transfer_buffer_length > 0)
				urb_priv->state = US_CTRL_DATA;
			else
				urb_priv->state = US_CTRL_ACK;
			break;
		case US_CTRL_DATA:
			urb->actual_length += bytes_xfrd;
			urb_priv->state = US_CTRL_ACK;
			break;
		case US_CTRL_ACK:
			etd_done = 1;
			break;
		default:
			dev_err(imx21->dev,
				"Invalid pipe state %d\n", urb_priv->state);
			etd_done = 1;
			break;
		}
		break;

	case PIPE_BULK:
		urb->actual_length += bytes_xfrd;
		if ((urb_priv->state == US_BULK)
		    && (urb->transfer_flags & URB_ZERO_PACKET)
		    && urb->transfer_buffer_length > 0
		    && ((urb->transfer_buffer_length %
			 usb_maxpacket(urb->dev, urb->pipe,
				       usb_pipeout(urb->pipe))) == 0)) {
			/* need a 0-packet */
			urb_priv->state = US_BULK0;
		} else {
			etd_done = 1;
		}
		break;

	case PIPE_INTERRUPT:
		urb->actual_length += bytes_xfrd;
		etd_done = 1;
		break;
	}

	if (etd_done)
		nonisoc_urb_completed_for_etd(imx21, etd, cc_to_error[cc]);
	else {
		dev_vdbg(imx21->dev, "next state=%d\n", urb_priv->state);
		schedule_nonisoc_etd(imx21, urb);
	}
}


static struct ep_priv *alloc_ep(void)
{
	int i;
	struct ep_priv *ep_priv;

	ep_priv = kzalloc(sizeof(struct ep_priv), GFP_ATOMIC);
	if (!ep_priv)
		return NULL;

	for (i = 0; i < NUM_ISO_ETDS; ++i)
		ep_priv->etd[i] = -1;

	return ep_priv;
}

static int imx21_hc_urb_enqueue(struct usb_hcd *hcd,
				struct urb *urb, gfp_t mem_flags)
{
	struct imx21 *imx21 = hcd_to_imx21(hcd);
	struct usb_host_endpoint *ep = urb->ep;
	struct urb_priv *urb_priv;
	struct ep_priv *ep_priv;
	struct etd_priv *etd;
	int ret;
	unsigned long flags;

	dev_vdbg(imx21->dev,
		"enqueue urb=%p ep=%p len=%d "
		"buffer=%p dma=%08X setupBuf=%p setupDma=%08X\n",
		urb, ep,
		urb->transfer_buffer_length,
		urb->transfer_buffer, urb->transfer_dma,
		urb->setup_packet, urb->setup_dma);

	if (usb_pipeisoc(urb->pipe))
		return imx21_hc_urb_enqueue_isoc(hcd, ep, urb, mem_flags);

	urb_priv = kzalloc(sizeof(struct urb_priv), mem_flags);
	if (!urb_priv)
		return -ENOMEM;

	spin_lock_irqsave(&imx21->lock, flags);

	ep_priv = ep->hcpriv;
	if (ep_priv == NULL) {
		ep_priv = alloc_ep();
		if (!ep_priv) {
			ret = -ENOMEM;
			goto failed_alloc_ep;
		}
		ep->hcpriv = ep_priv;
		ep_priv->ep = ep;
	}

	ret = usb_hcd_link_urb_to_ep(hcd, urb);
	if (ret)
		goto failed_link;

	urb->status = -EINPROGRESS;
	urb->actual_length = 0;
	urb->error_count = 0;
	urb->hcpriv = urb_priv;
	urb_priv->ep = ep;

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		urb_priv->state = US_CTRL_SETUP;
		break;
	case PIPE_BULK:
		urb_priv->state = US_BULK;
		break;
	}

	debug_urb_submitted(imx21, urb);
	if (ep_priv->etd[0] < 0) {
		if (ep_priv->waiting_etd) {
			dev_dbg(imx21->dev,
				"no ETD available already queued %p\n",
				ep_priv);
			debug_urb_queued_for_etd(imx21, urb);
			goto out;
		}
		ep_priv->etd[0] = alloc_etd(imx21);
		if (ep_priv->etd[0] < 0) {
			dev_dbg(imx21->dev,
				"no ETD available queueing %p\n", ep_priv);
			debug_urb_queued_for_etd(imx21, urb);
			list_add_tail(&ep_priv->queue, &imx21->queue_for_etd);
			ep_priv->waiting_etd = 1;
			goto out;
		}
	}

	/* Schedule if no URB already active for this endpoint */
	etd = &imx21->etd[ep_priv->etd[0]];
	if (etd->urb == NULL) {
		DEBUG_LOG_FRAME(imx21, etd, last_req);
		schedule_nonisoc_etd(imx21, urb);
	}

out:
	spin_unlock_irqrestore(&imx21->lock, flags);
	return 0;

failed_link:
failed_alloc_ep:
	spin_unlock_irqrestore(&imx21->lock, flags);
	kfree(urb_priv);
	return ret;
}

static int imx21_hc_urb_dequeue(struct usb_hcd *hcd, struct urb *urb,
				int status)
{
	struct imx21 *imx21 = hcd_to_imx21(hcd);
	unsigned long flags;
	struct usb_host_endpoint *ep;
	struct ep_priv *ep_priv;
	struct urb_priv *urb_priv = urb->hcpriv;
	int ret = -EINVAL;

	dev_vdbg(imx21->dev, "dequeue urb=%p iso=%d status=%d\n",
		urb, usb_pipeisoc(urb->pipe), status);

	spin_lock_irqsave(&imx21->lock, flags);

	ret = usb_hcd_check_unlink_urb(hcd, urb, status);
	if (ret)
		goto fail;
	ep = urb_priv->ep;
	ep_priv = ep->hcpriv;

	debug_urb_unlinked(imx21, urb);

	if (usb_pipeisoc(urb->pipe)) {
		dequeue_isoc_urb(imx21, urb, ep_priv);
		schedule_isoc_etds(hcd, ep);
	} else if (urb_priv->active) {
		int etd_num = ep_priv->etd[0];
		if (etd_num != -1) {
			struct etd_priv *etd = &imx21->etd[etd_num];

			disactivate_etd(imx21, etd_num);
			free_dmem(imx21, etd);
			etd->urb = NULL;
			kfree(etd->bounce_buffer);
			etd->bounce_buffer = NULL;
		}
	}

	urb_done(hcd, urb, status);

	spin_unlock_irqrestore(&imx21->lock, flags);
	return 0;

fail:
	spin_unlock_irqrestore(&imx21->lock, flags);
	return ret;
}

/* =========================================== */
/* Interrupt dispatch	 			*/
/* =========================================== */

static void process_etds(struct usb_hcd *hcd, struct imx21 *imx21, int sof)
{
	int etd_num;
	int enable_sof_int = 0;
	unsigned long flags;

	spin_lock_irqsave(&imx21->lock, flags);

	for (etd_num = 0; etd_num < USB_NUM_ETD; etd_num++) {
		u32 etd_mask = 1 << etd_num;
		u32 enabled = readl(imx21->regs + USBH_ETDENSET) & etd_mask;
		u32 done = readl(imx21->regs + USBH_ETDDONESTAT) & etd_mask;
		struct etd_priv *etd = &imx21->etd[etd_num];


		if (done) {
			DEBUG_LOG_FRAME(imx21, etd, last_int);
		} else {
/*
 * Kludge warning!
 *
 * When multiple transfers are using the bus we sometimes get into a state
 * where the transfer has completed (the CC field of the ETD is != 0x0F),
 * the ETD has self disabled but the ETDDONESTAT flag is not set
 * (and hence no interrupt occurs).
 * This causes the transfer in question to hang.
 * The kludge below checks for this condition at each SOF and processes any
 * blocked ETDs (after an arbitrary 10 frame wait)
 *
 * With a single active transfer the usbtest test suite will run for days
 * without the kludge.
 * With other bus activity (eg mass storage) even just test1 will hang without
 * the kludge.
 */
			u32 dword0;
			int cc;

			if (etd->active_count && !enabled) /* suspicious... */
				enable_sof_int = 1;

			if (!sof || enabled || !etd->active_count)
				continue;

			cc = etd_readl(imx21, etd_num, 2) >> DW2_COMPCODE;
			if (cc == TD_NOTACCESSED)
				continue;

			if (++etd->active_count < 10)
				continue;

			dword0 = etd_readl(imx21, etd_num, 0);
			dev_dbg(imx21->dev,
				"unblock ETD %d dev=0x%X ep=0x%X cc=0x%02X!\n",
				etd_num, dword0 & 0x7F,
				(dword0 >> DW0_ENDPNT) & 0x0F,
				cc);

#ifdef DEBUG
			dev_dbg(imx21->dev,
				"frame: act=%d disact=%d"
				" int=%d req=%d cur=%d\n",
				etd->activated_frame,
				etd->disactivated_frame,
				etd->last_int_frame,
				etd->last_req_frame,
				readl(imx21->regs + USBH_FRMNUB));
			imx21->debug_unblocks++;
#endif
			etd->active_count = 0;
/* End of kludge */
		}

		if (etd->ep == NULL || etd->urb == NULL) {
			dev_dbg(imx21->dev,
				"Interrupt for unexpected etd %d"
				" ep=%p urb=%p\n",
				etd_num, etd->ep, etd->urb);
			disactivate_etd(imx21, etd_num);
			continue;
		}

		if (usb_pipeisoc(etd->urb->pipe))
			isoc_etd_done(hcd, etd_num);
		else
			nonisoc_etd_done(hcd, etd_num);
	}

	/* only enable SOF interrupt if it may be needed for the kludge */
	if (enable_sof_int)
		set_register_bits(imx21, USBH_SYSIEN, USBH_SYSIEN_SOFINT);
	else
		clear_register_bits(imx21, USBH_SYSIEN, USBH_SYSIEN_SOFINT);


	spin_unlock_irqrestore(&imx21->lock, flags);
}

static irqreturn_t imx21_irq(struct usb_hcd *hcd)
{
	struct imx21 *imx21 = hcd_to_imx21(hcd);
	u32 ints = readl(imx21->regs + USBH_SYSISR);

	if (ints & USBH_SYSIEN_HERRINT)
		dev_dbg(imx21->dev, "Scheduling error\n");

	if (ints & USBH_SYSIEN_SORINT)
		dev_dbg(imx21->dev, "Scheduling overrun\n");

	if (ints & (USBH_SYSISR_DONEINT | USBH_SYSISR_SOFINT))
		process_etds(hcd, imx21, ints & USBH_SYSISR_SOFINT);

	writel(ints, imx21->regs + USBH_SYSISR);
	return IRQ_HANDLED;
}

static void imx21_hc_endpoint_disable(struct usb_hcd *hcd,
				      struct usb_host_endpoint *ep)
{
	struct imx21 *imx21 = hcd_to_imx21(hcd);
	unsigned long flags;
	struct ep_priv *ep_priv;
	int i;

	if (ep == NULL)
		return;

	spin_lock_irqsave(&imx21->lock, flags);
	ep_priv = ep->hcpriv;
	dev_vdbg(imx21->dev, "disable ep=%p, ep->hcpriv=%p\n", ep, ep_priv);

	if (!list_empty(&ep->urb_list))
		dev_dbg(imx21->dev, "ep's URB list is not empty\n");

	if (ep_priv != NULL) {
		for (i = 0; i < NUM_ISO_ETDS; i++) {
			if (ep_priv->etd[i] > -1)
				dev_dbg(imx21->dev, "free etd %d for disable\n",
					ep_priv->etd[i]);

			free_etd(imx21, ep_priv->etd[i]);
		}
		kfree(ep_priv);
		ep->hcpriv = NULL;
	}

	for (i = 0; i < USB_NUM_ETD; i++) {
		if (imx21->etd[i].alloc && imx21->etd[i].ep == ep) {
			dev_err(imx21->dev,
				"Active etd %d for disabled ep=%p!\n", i, ep);
			free_etd(imx21, i);
		}
	}
	free_epdmem(imx21, ep);
	spin_unlock_irqrestore(&imx21->lock, flags);
}

/* =========================================== */
/* Hub handling		 			*/
/* =========================================== */

static int get_hub_descriptor(struct usb_hcd *hcd,
			      struct usb_hub_descriptor *desc)
{
	struct imx21 *imx21 = hcd_to_imx21(hcd);
	desc->bDescriptorType = 0x29;	/* HUB descriptor */
	desc->bHubContrCurrent = 0;

	desc->bNbrPorts = readl(imx21->regs + USBH_ROOTHUBA)
		& USBH_ROOTHUBA_NDNSTMPRT_MASK;
	desc->bDescLength = 9;
	desc->bPwrOn2PwrGood = 0;
	desc->wHubCharacteristics = (__force __u16) cpu_to_le16(
		0x0002 |	/* No power switching */
		0x0010 |	/* No over current protection */
		0);

	desc->u.hs.DeviceRemovable[0] = 1 << 1;
	desc->u.hs.DeviceRemovable[1] = ~0;
	return 0;
}

static int imx21_hc_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct imx21 *imx21 = hcd_to_imx21(hcd);
	int ports;
	int changed = 0;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&imx21->lock, flags);
	ports = readl(imx21->regs + USBH_ROOTHUBA)
		& USBH_ROOTHUBA_NDNSTMPRT_MASK;
	if (ports > 7) {
		ports = 7;
		dev_err(imx21->dev, "ports %d > 7\n", ports);
	}
	for (i = 0; i < ports; i++) {
		if (readl(imx21->regs + USBH_PORTSTAT(i)) &
			(USBH_PORTSTAT_CONNECTSC |
			USBH_PORTSTAT_PRTENBLSC |
			USBH_PORTSTAT_PRTSTATSC |
			USBH_PORTSTAT_OVRCURIC |
			USBH_PORTSTAT_PRTRSTSC)) {

			changed = 1;
			buf[0] |= 1 << (i + 1);
		}
	}
	spin_unlock_irqrestore(&imx21->lock, flags);

	if (changed)
		dev_info(imx21->dev, "Hub status changed\n");
	return changed;
}

static int imx21_hc_hub_control(struct usb_hcd *hcd,
				u16 typeReq,
				u16 wValue, u16 wIndex, char *buf, u16 wLength)
{
	struct imx21 *imx21 = hcd_to_imx21(hcd);
	int rc = 0;
	u32 status_write = 0;

	switch (typeReq) {
	case ClearHubFeature:
		dev_dbg(imx21->dev, "ClearHubFeature\n");
		switch (wValue) {
		case C_HUB_OVER_CURRENT:
			dev_dbg(imx21->dev, "    OVER_CURRENT\n");
			break;
		case C_HUB_LOCAL_POWER:
			dev_dbg(imx21->dev, "    LOCAL_POWER\n");
			break;
		default:
			dev_dbg(imx21->dev, "    unknown\n");
			rc = -EINVAL;
			break;
		}
		break;

	case ClearPortFeature:
		dev_dbg(imx21->dev, "ClearPortFeature\n");
		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			dev_dbg(imx21->dev, "    ENABLE\n");
			status_write = USBH_PORTSTAT_CURCONST;
			break;
		case USB_PORT_FEAT_SUSPEND:
			dev_dbg(imx21->dev, "    SUSPEND\n");
			status_write = USBH_PORTSTAT_PRTOVRCURI;
			break;
		case USB_PORT_FEAT_POWER:
			dev_dbg(imx21->dev, "    POWER\n");
			status_write = USBH_PORTSTAT_LSDEVCON;
			break;
		case USB_PORT_FEAT_C_ENABLE:
			dev_dbg(imx21->dev, "    C_ENABLE\n");
			status_write = USBH_PORTSTAT_PRTENBLSC;
			break;
		case USB_PORT_FEAT_C_SUSPEND:
			dev_dbg(imx21->dev, "    C_SUSPEND\n");
			status_write = USBH_PORTSTAT_PRTSTATSC;
			break;
		case USB_PORT_FEAT_C_CONNECTION:
			dev_dbg(imx21->dev, "    C_CONNECTION\n");
			status_write = USBH_PORTSTAT_CONNECTSC;
			break;
		case USB_PORT_FEAT_C_OVER_CURRENT:
			dev_dbg(imx21->dev, "    C_OVER_CURRENT\n");
			status_write = USBH_PORTSTAT_OVRCURIC;
			break;
		case USB_PORT_FEAT_C_RESET:
			dev_dbg(imx21->dev, "    C_RESET\n");
			status_write = USBH_PORTSTAT_PRTRSTSC;
			break;
		default:
			dev_dbg(imx21->dev, "    unknown\n");
			rc = -EINVAL;
			break;
		}

		break;

	case GetHubDescriptor:
		dev_dbg(imx21->dev, "GetHubDescriptor\n");
		rc = get_hub_descriptor(hcd, (void *)buf);
		break;

	case GetHubStatus:
		dev_dbg(imx21->dev, "  GetHubStatus\n");
		*(__le32 *) buf = 0;
		break;

	case GetPortStatus:
		dev_dbg(imx21->dev, "GetPortStatus: port: %d, 0x%x\n",
		    wIndex, USBH_PORTSTAT(wIndex - 1));
		*(__le32 *) buf = readl(imx21->regs +
			USBH_PORTSTAT(wIndex - 1));
		break;

	case SetHubFeature:
		dev_dbg(imx21->dev, "SetHubFeature\n");
		switch (wValue) {
		case C_HUB_OVER_CURRENT:
			dev_dbg(imx21->dev, "    OVER_CURRENT\n");
			break;

		case C_HUB_LOCAL_POWER:
			dev_dbg(imx21->dev, "    LOCAL_POWER\n");
			break;
		default:
			dev_dbg(imx21->dev, "    unknown\n");
			rc = -EINVAL;
			break;
		}

		break;

	case SetPortFeature:
		dev_dbg(imx21->dev, "SetPortFeature\n");
		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			dev_dbg(imx21->dev, "    SUSPEND\n");
			status_write = USBH_PORTSTAT_PRTSUSPST;
			break;
		case USB_PORT_FEAT_POWER:
			dev_dbg(imx21->dev, "    POWER\n");
			status_write = USBH_PORTSTAT_PRTPWRST;
			break;
		case USB_PORT_FEAT_RESET:
			dev_dbg(imx21->dev, "    RESET\n");
			status_write = USBH_PORTSTAT_PRTRSTST;
			break;
		default:
			dev_dbg(imx21->dev, "    unknown\n");
			rc = -EINVAL;
			break;
		}
		break;

	default:
		dev_dbg(imx21->dev, "  unknown\n");
		rc = -EINVAL;
		break;
	}

	if (status_write)
		writel(status_write, imx21->regs + USBH_PORTSTAT(wIndex - 1));
	return rc;
}

/* =========================================== */
/* Host controller management 			*/
/* =========================================== */

static int imx21_hc_reset(struct usb_hcd *hcd)
{
	struct imx21 *imx21 = hcd_to_imx21(hcd);
	unsigned long timeout;
	unsigned long flags;

	spin_lock_irqsave(&imx21->lock, flags);

	/* Reset the Host controller modules */
	writel(USBOTG_RST_RSTCTRL | USBOTG_RST_RSTRH |
		USBOTG_RST_RSTHSIE | USBOTG_RST_RSTHC,
		imx21->regs + USBOTG_RST_CTRL);

	/* Wait for reset to finish */
	timeout = jiffies + HZ;
	while (readl(imx21->regs + USBOTG_RST_CTRL) != 0) {
		if (time_after(jiffies, timeout)) {
			spin_unlock_irqrestore(&imx21->lock, flags);
			dev_err(imx21->dev, "timeout waiting for reset\n");
			return -ETIMEDOUT;
		}
		spin_unlock_irq(&imx21->lock);
		schedule_timeout_uninterruptible(1);
		spin_lock_irq(&imx21->lock);
	}
	spin_unlock_irqrestore(&imx21->lock, flags);
	return 0;
}

static int imx21_hc_start(struct usb_hcd *hcd)
{
	struct imx21 *imx21 = hcd_to_imx21(hcd);
	unsigned long flags;
	int i, j;
	u32 hw_mode = USBOTG_HWMODE_CRECFG_HOST;
	u32 usb_control = 0;

	hw_mode |= ((imx21->pdata->host_xcvr << USBOTG_HWMODE_HOSTXCVR_SHIFT) &
			USBOTG_HWMODE_HOSTXCVR_MASK);
	hw_mode |= ((imx21->pdata->otg_xcvr << USBOTG_HWMODE_OTGXCVR_SHIFT) &
			USBOTG_HWMODE_OTGXCVR_MASK);

	if (imx21->pdata->host1_txenoe)
		usb_control |= USBCTRL_HOST1_TXEN_OE;

	if (!imx21->pdata->host1_xcverless)
		usb_control |= USBCTRL_HOST1_BYP_TLL;

	if (imx21->pdata->otg_ext_xcvr)
		usb_control |= USBCTRL_OTC_RCV_RXDP;


	spin_lock_irqsave(&imx21->lock, flags);

	writel((USBOTG_CLK_CTRL_HST | USBOTG_CLK_CTRL_MAIN),
		imx21->regs + USBOTG_CLK_CTRL);
	writel(hw_mode, imx21->regs + USBOTG_HWMODE);
	writel(usb_control, imx21->regs + USBCTRL);
	writel(USB_MISCCONTROL_SKPRTRY  | USB_MISCCONTROL_ARBMODE,
		imx21->regs + USB_MISCCONTROL);

	/* Clear the ETDs */
	for (i = 0; i < USB_NUM_ETD; i++)
		for (j = 0; j < 4; j++)
			etd_writel(imx21, i, j, 0);

	/* Take the HC out of reset */
	writel(USBH_HOST_CTRL_HCUSBSTE_OPERATIONAL | USBH_HOST_CTRL_CTLBLKSR_1,
		imx21->regs + USBH_HOST_CTRL);

	/* Enable ports */
	if (imx21->pdata->enable_otg_host)
		writel(USBH_PORTSTAT_PRTPWRST | USBH_PORTSTAT_PRTENABST,
			imx21->regs + USBH_PORTSTAT(0));

	if (imx21->pdata->enable_host1)
		writel(USBH_PORTSTAT_PRTPWRST | USBH_PORTSTAT_PRTENABST,
			imx21->regs + USBH_PORTSTAT(1));

	if (imx21->pdata->enable_host2)
		writel(USBH_PORTSTAT_PRTPWRST | USBH_PORTSTAT_PRTENABST,
			imx21->regs + USBH_PORTSTAT(2));


	hcd->state = HC_STATE_RUNNING;

	/* Enable host controller interrupts */
	set_register_bits(imx21, USBH_SYSIEN,
		USBH_SYSIEN_HERRINT |
		USBH_SYSIEN_DONEINT | USBH_SYSIEN_SORINT);
	set_register_bits(imx21, USBOTG_CINT_STEN, USBOTG_HCINT);

	spin_unlock_irqrestore(&imx21->lock, flags);

	return 0;
}

static void imx21_hc_stop(struct usb_hcd *hcd)
{
	struct imx21 *imx21 = hcd_to_imx21(hcd);
	unsigned long flags;

	spin_lock_irqsave(&imx21->lock, flags);

	writel(0, imx21->regs + USBH_SYSIEN);
	clear_register_bits(imx21, USBOTG_CINT_STEN, USBOTG_HCINT);
	clear_register_bits(imx21, USBOTG_CLK_CTRL_HST | USBOTG_CLK_CTRL_MAIN,
					USBOTG_CLK_CTRL);
	spin_unlock_irqrestore(&imx21->lock, flags);
}

/* =========================================== */
/* Driver glue		 			*/
/* =========================================== */

static struct hc_driver imx21_hc_driver = {
	.description = hcd_name,
	.product_desc = "IMX21 USB Host Controller",
	.hcd_priv_size = sizeof(struct imx21),

	.flags = HCD_USB11,
	.irq = imx21_irq,

	.reset = imx21_hc_reset,
	.start = imx21_hc_start,
	.stop = imx21_hc_stop,

	/* I/O requests */
	.urb_enqueue = imx21_hc_urb_enqueue,
	.urb_dequeue = imx21_hc_urb_dequeue,
	.endpoint_disable = imx21_hc_endpoint_disable,

	/* scheduling support */
	.get_frame_number = imx21_hc_get_frame,

	/* Root hub support */
	.hub_status_data = imx21_hc_hub_status_data,
	.hub_control = imx21_hc_hub_control,

};

static struct mx21_usbh_platform_data default_pdata = {
	.host_xcvr = MX21_USBXCVR_TXDIF_RXDIF,
	.otg_xcvr = MX21_USBXCVR_TXDIF_RXDIF,
	.enable_host1 = 1,
	.enable_host2 = 1,
	.enable_otg_host = 1,

};

static int imx21_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct imx21 *imx21 = hcd_to_imx21(hcd);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	remove_debug_files(imx21);
	usb_remove_hcd(hcd);

	if (res != NULL) {
		clk_disable_unprepare(imx21->clk);
		clk_put(imx21->clk);
		iounmap(imx21->regs);
		release_mem_region(res->start, resource_size(res));
	}

	kfree(hcd);
	return 0;
}


static int imx21_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct imx21 *imx21;
	struct resource *res;
	int ret;
	int irq;

	printk(KERN_INFO "%s\n", imx21_hc_driver.product_desc);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENXIO;

	hcd = usb_create_hcd(&imx21_hc_driver,
		&pdev->dev, dev_name(&pdev->dev));
	if (hcd == NULL) {
		dev_err(&pdev->dev, "Cannot create hcd (%s)\n",
		    dev_name(&pdev->dev));
		return -ENOMEM;
	}

	imx21 = hcd_to_imx21(hcd);
	imx21->hcd = hcd;
	imx21->dev = &pdev->dev;
	imx21->pdata = dev_get_platdata(&pdev->dev);
	if (!imx21->pdata)
		imx21->pdata = &default_pdata;

	spin_lock_init(&imx21->lock);
	INIT_LIST_HEAD(&imx21->dmem_list);
	INIT_LIST_HEAD(&imx21->queue_for_etd);
	INIT_LIST_HEAD(&imx21->queue_for_dmem);
	create_debug_files(imx21);

	res = request_mem_region(res->start, resource_size(res), hcd_name);
	if (!res) {
		ret = -EBUSY;
		goto failed_request_mem;
	}

	imx21->regs = ioremap(res->start, resource_size(res));
	if (imx21->regs == NULL) {
		dev_err(imx21->dev, "Cannot map registers\n");
		ret = -ENOMEM;
		goto failed_ioremap;
	}

	/* Enable clocks source */
	imx21->clk = clk_get(imx21->dev, NULL);
	if (IS_ERR(imx21->clk)) {
		dev_err(imx21->dev, "no clock found\n");
		ret = PTR_ERR(imx21->clk);
		goto failed_clock_get;
	}

	ret = clk_set_rate(imx21->clk, clk_round_rate(imx21->clk, 48000000));
	if (ret)
		goto failed_clock_set;
	ret = clk_prepare_enable(imx21->clk);
	if (ret)
		goto failed_clock_enable;

	dev_info(imx21->dev, "Hardware HC revision: 0x%02X\n",
		(readl(imx21->regs + USBOTG_HWMODE) >> 16) & 0xFF);

	ret = usb_add_hcd(hcd, irq, 0);
	if (ret != 0) {
		dev_err(imx21->dev, "usb_add_hcd() returned %d\n", ret);
		goto failed_add_hcd;
	}

	return 0;

failed_add_hcd:
	clk_disable_unprepare(imx21->clk);
failed_clock_enable:
failed_clock_set:
	clk_put(imx21->clk);
failed_clock_get:
	iounmap(imx21->regs);
failed_ioremap:
	release_mem_region(res->start, resource_size(res));
failed_request_mem:
	remove_debug_files(imx21);
	usb_put_hcd(hcd);
	return ret;
}

static struct platform_driver imx21_hcd_driver = {
	.driver = {
		   .name = (char *)hcd_name,
		   },
	.probe = imx21_probe,
	.remove = imx21_remove,
	.suspend = NULL,
	.resume = NULL,
};

module_platform_driver(imx21_hcd_driver);

MODULE_DESCRIPTION("i.MX21 USB Host controller");
MODULE_AUTHOR("Martin Fuzzey");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:imx21-hcd");
