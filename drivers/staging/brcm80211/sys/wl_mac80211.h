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

#include <wlc_types.h>

/* BMAC Note: High-only driver is no longer working in softirq context as it needs to block and
 * sleep so perimeter lock has to be a semaphore instead of spinlock. This requires timers to be
 * submitted to workqueue instead of being on kernel timer
 */
typedef struct wl_timer {
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
} wl_timer_t;

/* contortion to call functions at safe time */
/* In 2.6.20 kernels work functions get passed a pointer to the struct work, so things
 * will continue to work as long as the work structure is the first component of the task structure.
 */
typedef struct wl_task {
	struct work_struct work;
	void *context;
} wl_task_t;

struct wl_if {
	uint subunit;		/* WDS/BSS unit */
	struct pci_dev *pci_dev;
};

#define WL_MAX_FW		4
struct wl_firmware {
	uint32 fw_cnt;
	const struct firmware *fw_bin[WL_MAX_FW];
	const struct firmware *fw_hdr[WL_MAX_FW];
	uint32 hdr_num_entries[WL_MAX_FW];
};

struct wl_info {
	wlc_pub_t *pub;		/* pointer to public wlc state */
	void *wlc;		/* pointer to private common os-independent data */
	osl_t *osh;		/* pointer to os handler */
	uint32 magic;

	int irq;

#ifdef WLC_HIGH_ONLY
	struct semaphore sem;	/* use semaphore to allow sleep */
#else
	spinlock_t lock;	/* per-device perimeter lock */
	spinlock_t isr_lock;	/* per-device ISR synchronization lock */
#endif
	uint bcm_bustype;	/* bus type */
	bool piomode;		/* set from insmod argument */
	void *regsva;		/* opaque chip registers virtual address */
	atomic_t callbacks;	/* # outstanding callback functions */
	struct wl_timer *timers;	/* timer cleanup queue */
	struct tasklet_struct tasklet;	/* dpc tasklet */
#ifdef BCMSDIO
	bcmsdh_info_t *sdh;	/* pointer to sdio bus handler */
	ulong flags;		/* current irq flags */
#endif				/* BCMSDIO */
	bool resched;		/* dpc needs to be and is rescheduled */
#ifdef LINUXSTA_PS
	uint32 pci_psstate[16];	/* pci ps-state save/restore */
#endif
	/* RPC, handle, lock, txq, workitem */
#ifdef WLC_HIGH_ONLY
	rpc_info_t *rpc;	/* RPC handle */
	rpc_tp_info_t *rpc_th;	/* RPC transport handle */
	wlc_rpc_ctx_t rpc_dispatch_ctx;

	bool rpcq_dispatched;	/* Avoid scheduling multiple tasks */
	spinlock_t rpcq_lock;	/* Lock for the queue */
	rpc_buf_t *rpcq_head;	/* RPC Q */
	rpc_buf_t *rpcq_tail;	/* Points to the last buf */

	bool txq_dispatched;	/* Avoid scheduling multiple tasks */
	spinlock_t txq_lock;	/* Lock for the queue */
	struct sk_buff *txq_head;	/* TX Q */
	struct sk_buff *txq_tail;	/* Points to the last buf */

	wl_task_t txq_task;	/* work queue for wl_start() */
#endif				/* WLC_HIGH_ONLY */
	uint stats_id;		/* the current set of stats */
	/* ping-pong stats counters updated by Linux watchdog */
	struct net_device_stats stats_watchdog[2];
	struct wl_firmware fw;
};

#ifndef WLC_HIGH_ONLY
#define WL_LOCK(wl)	spin_lock_bh(&(wl)->lock)
#define WL_UNLOCK(wl)	spin_unlock_bh(&(wl)->lock)

/* locking from inside wl_isr */
#define WL_ISRLOCK(wl, flags) do {spin_lock(&(wl)->isr_lock); (void)(flags); } while (0)
#define WL_ISRUNLOCK(wl, flags) do {spin_unlock(&(wl)->isr_lock); (void)(flags); } while (0)

/* locking under WL_LOCK() to synchronize with wl_isr */
#define INT_LOCK(wl, flags)	spin_lock_irqsave(&(wl)->isr_lock, flags)
#define INT_UNLOCK(wl, flags)	spin_unlock_irqrestore(&(wl)->isr_lock, flags)
#else				/* BCMSDIO */

#define WL_LOCK(wl)	down(&(wl)->sem)
#define WL_UNLOCK(wl)	up(&(wl)->sem)

#define WL_ISRLOCK(wl)
#define WL_ISRUNLOCK(wl)
#endif				/* WLC_HIGH_ONLY */

/* handle forward declaration */
typedef struct wl_info wl_info_t;

#ifndef PCI_D0
#define PCI_D0		0
#endif

#ifndef PCI_D3hot
#define PCI_D3hot	3
#endif

/* exported functions */

extern irqreturn_t wl_isr(int irq, void *dev_id);

extern int __devinit wl_pci_probe(struct pci_dev *pdev,
				  const struct pci_device_id *ent);
extern void wl_free(wl_info_t *wl);
extern int wl_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
extern int wl_ucode_data_init(wl_info_t *wl);
extern void wl_ucode_data_free(void);
#ifdef WLC_LOW
extern void wl_ucode_free_buf(void *);
extern int wl_ucode_init_buf(wl_info_t *wl, void **pbuf, uint32 idx);
extern int wl_ucode_init_uint(wl_info_t *wl, uint32 *data, uint32 idx);
#endif				/* WLC_LOW */

#endif				/* _wl_mac80211_h_ */
