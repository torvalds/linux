/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _BRCM_MAC80211_IF_H_
#define _BRCM_MAC80211_IF_H_

#include <linux/timer.h>
#include <linux/interrupt.h>

/* softmac ioctl definitions */
#define BRCMS_SET_SHORTSLOT_OVERRIDE		146


/* BMAC Note: High-only driver is no longer working in softirq context as it needs to block and
 * sleep so perimeter lock has to be a semaphore instead of spinlock. This requires timers to be
 * submitted to workqueue instead of being on kernel timer
 */
struct brcms_timer {
	struct timer_list timer;
	struct brcms_info *wl;
	void (*fn) (void *);
	void *arg;		/* argument to fn */
	uint ms;
	bool periodic;
	bool set;
	struct brcms_timer *next;
#ifdef BCMDBG
	char *name;		/* Description of the timer */
#endif
};

struct brcms_if {
	uint subunit;		/* WDS/BSS unit */
	struct pci_dev *pci_dev;
};

#define MAX_FW_IMAGES		4
struct brcms_firmware {
	u32 fw_cnt;
	const struct firmware *fw_bin[MAX_FW_IMAGES];
	const struct firmware *fw_hdr[MAX_FW_IMAGES];
	u32 hdr_num_entries[MAX_FW_IMAGES];
};

struct brcms_info {
	struct brcms_pub *pub;		/* pointer to public wlc state */
	void *wlc;		/* pointer to private common os-independent data */
	u32 magic;

	int irq;

	spinlock_t lock;	/* per-device perimeter lock */
	spinlock_t isr_lock;	/* per-device ISR synchronization lock */

	/* bus type and regsva for unmap in brcms_free() */
	uint bcm_bustype;	/* bus type */
	void *regsva;		/* opaque chip registers virtual address */

	/* timer related fields */
	atomic_t callbacks;	/* # outstanding callback functions */
	struct brcms_timer *timers;	/* timer cleanup queue */

	struct tasklet_struct tasklet;	/* dpc tasklet */
	bool resched;		/* dpc needs to be and is rescheduled */
#ifdef LINUXSTA_PS
	u32 pci_psstate[16];	/* pci ps-state save/restore */
#endif
	struct brcms_firmware fw;
	struct wiphy *wiphy;
};

/* misc callbacks */
extern void brcms_init(struct brcms_info *wl);
extern uint brcms_reset(struct brcms_info *wl);
extern void brcms_intrson(struct brcms_info *wl);
extern u32 brcms_intrsoff(struct brcms_info *wl);
extern void brcms_intrsrestore(struct brcms_info *wl, u32 macintmask);
extern int brcms_up(struct brcms_info *wl);
extern void brcms_down(struct brcms_info *wl);
extern void brcms_txflowcontrol(struct brcms_info *wl, struct brcms_if *wlif,
				bool state, int prio);
extern bool wl_alloc_dma_resources(struct brcms_info *wl, uint dmaddrwidth);
extern bool brcms_rfkill_set_hw_state(struct brcms_info *wl);

/* timer functions */
extern struct brcms_timer *brcms_init_timer(struct brcms_info *wl,
				      void (*fn) (void *arg), void *arg,
				      const char *name);
extern void brcms_free_timer(struct brcms_info *wl, struct brcms_timer *timer);
extern void brcms_add_timer(struct brcms_info *wl, struct brcms_timer *timer,
			    uint ms, int periodic);
extern bool brcms_del_timer(struct brcms_info *wl, struct brcms_timer *timer);
extern void brcms_msleep(struct brcms_info *wl, uint ms);

#endif				/* _BRCM_MAC80211_IF_H_ */
