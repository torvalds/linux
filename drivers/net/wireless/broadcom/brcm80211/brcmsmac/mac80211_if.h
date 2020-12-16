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
#include <linux/workqueue.h>
#include <linux/leds.h>

#include "ucode_loader.h"
#include "led.h"
/*
 * Starting index for 5G rates in the
 * legacy rate table.
 */
#define BRCMS_LEGACY_5G_RATE_OFFSET	4

/* softmac ioctl definitions */
#define BRCMS_SET_SHORTSLOT_OVERRIDE		146

struct brcms_timer {
	struct delayed_work dly_wrk;
	struct brcms_info *wl;
	void (*fn) (void *);	/* function called upon expiration */
	void *arg;		/* fixed argument provided to called function */
	uint ms;
	bool periodic;
	bool set;		/* indicates if timer is active */
	struct brcms_timer *next;	/* for freeing on unload */
#ifdef DEBUG
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
	struct brcms_c_info *wlc;	/* pointer to private common data */
	u32 magic;

	int irq;

	spinlock_t lock;	/* per-device perimeter lock */
	spinlock_t isr_lock;	/* per-device ISR synchronization lock */

	/* tx flush */
	wait_queue_head_t tx_flush_wq;

	/* timer related fields */
	atomic_t callbacks;	/* # outstanding callback functions */
	struct brcms_timer *timers;	/* timer cleanup queue */

	struct tasklet_struct tasklet;	/* dpc tasklet */
	bool resched;		/* dpc needs to be and is rescheduled */
	struct brcms_firmware fw;
	struct wiphy *wiphy;
	struct brcms_ucode ucode;
	bool mute_tx;
	struct brcms_led radio_led;
	struct led_classdev led_dev;
};

/* misc callbacks */
void brcms_init(struct brcms_info *wl);
uint brcms_reset(struct brcms_info *wl);
void brcms_intrson(struct brcms_info *wl);
u32 brcms_intrsoff(struct brcms_info *wl);
void brcms_intrsrestore(struct brcms_info *wl, u32 macintmask);
int brcms_up(struct brcms_info *wl);
void brcms_down(struct brcms_info *wl);
void brcms_txflowcontrol(struct brcms_info *wl, struct brcms_if *wlif,
			 bool state, int prio);
bool brcms_rfkill_set_hw_state(struct brcms_info *wl);

/* timer functions */
struct brcms_timer *brcms_init_timer(struct brcms_info *wl,
				     void (*fn) (void *arg), void *arg,
				     const char *name);
void brcms_free_timer(struct brcms_timer *timer);
void brcms_add_timer(struct brcms_timer *timer, uint ms, int periodic);
bool brcms_del_timer(struct brcms_timer *timer);
void brcms_dpc(struct tasklet_struct *t);
void brcms_timer(struct brcms_timer *t);
void brcms_fatal_error(struct brcms_info *wl);

#endif				/* _BRCM_MAC80211_IF_H_ */
