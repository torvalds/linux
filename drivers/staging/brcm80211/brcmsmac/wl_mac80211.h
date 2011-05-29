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

#ifndef _wl_mac80211_h_
#define _wl_mac80211_h_

/* BMAC Note: High-only driver is no longer working in softirq context as it needs to block and
 * sleep so perimeter lock has to be a semaphore instead of spinlock. This requires timers to be
 * submitted to workqueue instead of being on kernel timer
 */
struct wl_timer {
	struct timer_list timer;
	struct wl_info *wl;
	void (*fn) (void *);
	void *arg;		/* argument to fn */
	uint ms;
	bool periodic;
	bool set;
	struct wl_timer *next;
#ifdef BCMDBG
	char *name;		/* Description of the timer */
#endif
};

struct wl_if {
	uint subunit;		/* WDS/BSS unit */
	struct pci_dev *pci_dev;
};

#define WL_MAX_FW		4
struct wl_firmware {
	u32 fw_cnt;
	const struct firmware *fw_bin[WL_MAX_FW];
	const struct firmware *fw_hdr[WL_MAX_FW];
	u32 hdr_num_entries[WL_MAX_FW];
};

struct wl_info {
	struct wlc_pub *pub;		/* pointer to public wlc state */
	void *wlc;		/* pointer to private common os-independent data */
	u32 magic;

	int irq;

	spinlock_t lock;	/* per-device perimeter lock */
	spinlock_t isr_lock;	/* per-device ISR synchronization lock */
	uint bcm_bustype;	/* bus type */
	bool piomode;		/* set from insmod argument */
	void *regsva;		/* opaque chip registers virtual address */
	atomic_t callbacks;	/* # outstanding callback functions */
	struct wl_timer *timers;	/* timer cleanup queue */
	struct tasklet_struct tasklet;	/* dpc tasklet */
	bool resched;		/* dpc needs to be and is rescheduled */
#ifdef LINUXSTA_PS
	u32 pci_psstate[16];	/* pci ps-state save/restore */
#endif
	/* RPC, handle, lock, txq, workitem */
	uint stats_id;		/* the current set of stats */
	/* ping-pong stats counters updated by Linux watchdog */
	struct net_device_stats stats_watchdog[2];
	struct wl_firmware fw;
};

#define WL_LOCK(wl)	spin_lock_bh(&(wl)->lock)
#define WL_UNLOCK(wl)	spin_unlock_bh(&(wl)->lock)

/* locking from inside wl_isr */
#define WL_ISRLOCK(wl, flags) do {spin_lock(&(wl)->isr_lock); (void)(flags); } while (0)
#define WL_ISRUNLOCK(wl, flags) do {spin_unlock(&(wl)->isr_lock); (void)(flags); } while (0)

/* locking under WL_LOCK() to synchronize with wl_isr */
#define INT_LOCK(wl, flags)	spin_lock_irqsave(&(wl)->isr_lock, flags)
#define INT_UNLOCK(wl, flags)	spin_unlock_irqrestore(&(wl)->isr_lock, flags)

#endif				/* _wl_mac80211_h_ */
