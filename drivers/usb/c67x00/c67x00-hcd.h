/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * c67x00-hcd.h: Cypress C67X00 USB HCD
 *
 * Copyright (C) 2006-2008 Barco N.V.
 *    Derived from the Cypress cy7c67200/300 ezusb linux driver and
 *    based on multiple host controller drivers inside the linux kernel.
 */

#ifndef _USB_C67X00_HCD_H
#define _USB_C67X00_HCD_H

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include "c67x00.h"

/*
 * The following parameters depend on the CPU speed, bus speed, ...
 * These can be tuned for specific use cases, e.g. if isochronous transfers
 * are very important, bandwidth can be sacrificed to guarantee that the
 * 1ms deadline will be met.
 * If bulk transfers are important, the MAX_FRAME_BW can be increased,
 * but some (or many) isochronous deadlines might not be met.
 *
 * The values are specified in bittime.
 */

/*
 * The current implementation switches between _STD (default) and _ISO (when
 * isochronous transfers are scheduled), in order to optimize the throughput
 * in normal circumstances, but also provide good isochronous behaviour.
 *
 * Bandwidth is described in bit time so with a 12MHz USB clock and 1ms
 * frames; there are 12000 bit times per frame.
 */

#define TOTAL_FRAME_BW		12000
#define DEFAULT_EOT		2250

#define MAX_FRAME_BW_STD	(TOTAL_FRAME_BW - DEFAULT_EOT)
#define MAX_FRAME_BW_ISO	2400

/*
 * Periodic transfers may only use 90% of the full frame, but as
 * we currently don't even use 90% of the full frame, we may
 * use the full usable time for periodic transfers.
 */
#define MAX_PERIODIC_BW(full_bw)	full_bw

/* -------------------------------------------------------------------------- */

struct c67x00_hcd {
	spinlock_t lock;
	struct c67x00_sie *sie;
	unsigned int low_speed_ports;	/* bitmask of low speed ports */
	unsigned int urb_count;
	unsigned int urb_iso_count;

	struct list_head list[4];	/* iso, int, ctrl, bulk */
#if PIPE_BULK != 3
#error "Sanity check failed, this code presumes PIPE_... to range from 0 to 3"
#endif

	/* USB bandwidth allocated to td_list */
	int bandwidth_allocated;
	/* USB bandwidth allocated for isoc/int transfer */
	int periodic_bw_allocated;
	struct list_head td_list;
	int max_frame_bw;

	u16 td_base_addr;
	u16 buf_base_addr;
	u16 next_td_addr;
	u16 next_buf_addr;

	struct work_struct work;

	struct completion endpoint_disable;

	u16 current_frame;
	u16 last_frame;
};

static inline struct c67x00_hcd *hcd_to_c67x00_hcd(struct usb_hcd *hcd)
{
	return (struct c67x00_hcd *)(hcd->hcd_priv);
}

static inline struct usb_hcd *c67x00_hcd_to_hcd(struct c67x00_hcd *c67x00)
{
	return container_of((void *)c67x00, struct usb_hcd, hcd_priv);
}

/* ---------------------------------------------------------------------
 * Functions used by c67x00-drv
 */

int c67x00_hcd_probe(struct c67x00_sie *sie);
void c67x00_hcd_remove(struct c67x00_sie *sie);

/* ---------------------------------------------------------------------
 * Transfer Descriptor scheduling functions
 */
int c67x00_urb_enqueue(struct usb_hcd *hcd, struct urb *urb, gfp_t mem_flags);
int c67x00_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status);
void c67x00_endpoint_disable(struct usb_hcd *hcd,
			     struct usb_host_endpoint *ep);

void c67x00_sched_kick(struct c67x00_hcd *c67x00);
int c67x00_sched_start_scheduler(struct c67x00_hcd *c67x00);
void c67x00_sched_stop_scheduler(struct c67x00_hcd *c67x00);

#define c67x00_hcd_dev(x)	(c67x00_hcd_to_hcd(x)->self.controller)

#endif				/* _USB_C67X00_HCD_H */
