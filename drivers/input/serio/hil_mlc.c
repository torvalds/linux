/*
 * HIL MLC state machine and serio interface driver
 *
 * Copyright (c) 2001 Brian S. Julin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *
 * References:
 * HP-HIL Technical Reference Manual.  Hewlett Packard Product No. 45918A
 *
 *
 *	Driver theory of operation:
 *
 *	Some access methods and an ISR is defined by the sub-driver 
 *	(e.g. hp_sdc_mlc.c).  These methods are expected to provide a 
 *	few bits of logic in addition to raw access to the HIL MLC, 
 *	specifically, the ISR, which is entirely registered by the 
 *	sub-driver and invoked directly, must check for record 
 *	termination or packet match, at which point a semaphore must
 *	be cleared and then the hil_mlcs_tasklet must be scheduled.
 *
 *	The hil_mlcs_tasklet processes the state machine for all MLCs
 *	each time it runs, checking each MLC's progress at the current
 *	node in the state machine, and moving the MLC to subsequent nodes
 *	in the state machine when appropriate.  It will reschedule
 *	itself if output is pending.  (This rescheduling should be replaced
 *	at some point with a sub-driver-specific mechanism.)
 *
 *	A timer task prods the tasklet once per second to prevent 
 *	hangups when attached devices do not return expected data
 *	and to initiate probes of the loop for new devices.
 */

#include <linux/hil_mlc.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/list.h>

MODULE_AUTHOR("Brian S. Julin <bri@calyx.com>");
MODULE_DESCRIPTION("HIL MLC serio");
MODULE_LICENSE("Dual BSD/GPL");

EXPORT_SYMBOL(hil_mlc_register);
EXPORT_SYMBOL(hil_mlc_unregister);

#define PREFIX "HIL MLC: "

static LIST_HEAD(hil_mlcs);
static DEFINE_RWLOCK(hil_mlcs_lock);
static struct timer_list	hil_mlcs_kicker;
static int			hil_mlcs_probe;

static void hil_mlcs_process(unsigned long unused);
DECLARE_TASKLET_DISABLED(hil_mlcs_tasklet, hil_mlcs_process, 0);


/* #define HIL_MLC_DEBUG */

/********************** Device info/instance management **********************/

static void hil_mlc_clear_di_map (hil_mlc *mlc, int val) {
	int j;
	for (j = val; j < 7 ; j++) {
		mlc->di_map[j] = -1;
	}
}

static void hil_mlc_clear_di_scratch (hil_mlc *mlc) {
	memset(&(mlc->di_scratch), 0, sizeof(mlc->di_scratch));
}

static void hil_mlc_copy_di_scratch (hil_mlc *mlc, int idx) {
	memcpy(&(mlc->di[idx]), &(mlc->di_scratch), sizeof(mlc->di_scratch));
}

static int hil_mlc_match_di_scratch (hil_mlc *mlc) {
	int idx;

	for (idx = 0; idx < HIL_MLC_DEVMEM; idx++) {
		int j, found;

		/* In-use slots are not eligible. */
		found = 0;
		for (j = 0; j < 7 ; j++) {
			if (mlc->di_map[j] == idx) found++;
		}
		if (found) continue;
		if (!memcmp(mlc->di + idx, 
			    &(mlc->di_scratch), 
			    sizeof(mlc->di_scratch))) break;
	}
	return((idx >= HIL_MLC_DEVMEM) ? -1 : idx);
}

static int hil_mlc_find_free_di(hil_mlc *mlc) {
	int idx;
	/* TODO: Pick all-zero slots first, failing that, 
	 * randomize the slot picked among those eligible. 
	 */
	for (idx = 0; idx < HIL_MLC_DEVMEM; idx++) {
		int j, found;
		found = 0;
		for (j = 0; j < 7 ; j++) {
			if (mlc->di_map[j] == idx) found++;
		}
		if (!found) break;
	}
	return(idx); /* Note: It is guaranteed at least one above will match */
}

static inline void hil_mlc_clean_serio_map(hil_mlc *mlc) {
	int idx;
	for (idx = 0; idx < HIL_MLC_DEVMEM; idx++) {
		int j, found;
		found = 0;
		for (j = 0; j < 7 ; j++) {
			if (mlc->di_map[j] == idx) found++;
		}
		if (!found) mlc->serio_map[idx].di_revmap = -1;
	}
}

static void hil_mlc_send_polls(hil_mlc *mlc) {
	int did, i, cnt;
	struct serio *serio;
	struct serio_driver *drv;

	i = cnt = 0;
	did = (mlc->ipacket[0] & HIL_PKT_ADDR_MASK) >> 8;
	serio = did ? mlc->serio[mlc->di_map[did - 1]] : NULL;
	drv = (serio != NULL) ? serio->drv : NULL;

	while (mlc->icount < 15 - i) {
		hil_packet p;
		p = mlc->ipacket[i];
		if (did != (p & HIL_PKT_ADDR_MASK) >> 8) {
			if (drv == NULL || drv->interrupt == NULL) goto skip;

			drv->interrupt(serio, 0, 0, NULL);
			drv->interrupt(serio, HIL_ERR_INT >> 16, 0, NULL);
			drv->interrupt(serio, HIL_PKT_CMD >> 8,  0, NULL);
			drv->interrupt(serio, HIL_CMD_POL + cnt, 0, NULL);
		skip:
			did = (p & HIL_PKT_ADDR_MASK) >> 8;
			serio = did ? mlc->serio[mlc->di_map[did-1]] : NULL;
			drv = (serio != NULL) ? serio->drv : NULL;
			cnt = 0;
		}
		cnt++; i++;
		if (drv == NULL || drv->interrupt == NULL) continue;
		drv->interrupt(serio, (p >> 24), 0, NULL);
		drv->interrupt(serio, (p >> 16) & 0xff, 0, NULL);
		drv->interrupt(serio, (p >> 8) & ~HIL_PKT_ADDR_MASK, 0, NULL);
		drv->interrupt(serio, p & 0xff, 0, NULL);
	}
}

/*************************** State engine *********************************/

#define HILSEN_SCHED	0x000100	/* Schedule the tasklet		*/
#define HILSEN_BREAK	0x000200	/* Wait until next pass		*/
#define HILSEN_UP	0x000400	/* relative node#, decrement	*/
#define HILSEN_DOWN	0x000800	/* relative node#, increment	*/
#define HILSEN_FOLLOW	0x001000	/* use retval as next node#	*/

#define HILSEN_MASK	0x0000ff
#define HILSEN_START	0
#define HILSEN_RESTART	1
#define HILSEN_DHR	9
#define HILSEN_DHR2	10
#define HILSEN_IFC	14
#define HILSEN_HEAL0	16
#define HILSEN_HEAL	18
#define HILSEN_ACF      21
#define HILSEN_ACF2	22
#define HILSEN_DISC0	25
#define HILSEN_DISC	27
#define HILSEN_MATCH	40
#define HILSEN_OPERATE	41
#define HILSEN_PROBE	44
#define HILSEN_DSR	52
#define HILSEN_REPOLL	55
#define HILSEN_IFCACF	58
#define HILSEN_END	60

#define HILSEN_NEXT	(HILSEN_DOWN | 1)
#define HILSEN_SAME	(HILSEN_DOWN | 0)
#define HILSEN_LAST	(HILSEN_UP | 1)

#define HILSEN_DOZE	(HILSEN_SAME | HILSEN_SCHED | HILSEN_BREAK)
#define HILSEN_SLEEP	(HILSEN_SAME | HILSEN_BREAK)

static int hilse_match(hil_mlc *mlc, int unused) {
	int rc;
	rc = hil_mlc_match_di_scratch(mlc);
	if (rc == -1) {
		rc = hil_mlc_find_free_di(mlc);
		if (rc == -1) goto err;
#ifdef HIL_MLC_DEBUG
		printk(KERN_DEBUG PREFIX "new in slot %i\n", rc);
#endif
		hil_mlc_copy_di_scratch(mlc, rc);
		mlc->di_map[mlc->ddi] = rc;
		mlc->serio_map[rc].di_revmap = mlc->ddi;
		hil_mlc_clean_serio_map(mlc);
		serio_rescan(mlc->serio[rc]);
		return -1;
	}
	mlc->di_map[mlc->ddi] = rc;
#ifdef HIL_MLC_DEBUG
	printk(KERN_DEBUG PREFIX "same in slot %i\n", rc);
#endif
	mlc->serio_map[rc].di_revmap = mlc->ddi;
	hil_mlc_clean_serio_map(mlc);
	return 0;
 err:
	printk(KERN_ERR PREFIX "Residual device slots exhausted, close some serios!\n");
	return 1;
}

/* An LCV used to prevent runaway loops, forces 5 second sleep when reset. */
static int hilse_init_lcv(hil_mlc *mlc, int unused) {
	struct timeval tv;

	do_gettimeofday(&tv);

	if(mlc->lcv == 0) goto restart;  /* First init, no need to dally */
	if(tv.tv_sec - mlc->lcv_tv.tv_sec < 5) return -1;
 restart:
	mlc->lcv_tv = tv;
	mlc->lcv = 0;
	return 0;
}

static int hilse_inc_lcv(hil_mlc *mlc, int lim) {
	if (mlc->lcv++ >= lim) return -1;
	return 0;
}

#if 0
static int hilse_set_lcv(hil_mlc *mlc, int val) {
	mlc->lcv = val;
	return 0;
}
#endif

/* Management of the discovered device index (zero based, -1 means no devs) */
static int hilse_set_ddi(hil_mlc *mlc, int val) {
	mlc->ddi = val;
	hil_mlc_clear_di_map(mlc, val + 1);
	return 0;
}

static int hilse_dec_ddi(hil_mlc *mlc, int unused) {
	mlc->ddi--;
	if (mlc->ddi <= -1) { 
		mlc->ddi = -1;
		hil_mlc_clear_di_map(mlc, 0);
		return -1;
	}
	hil_mlc_clear_di_map(mlc, mlc->ddi + 1);
	return 0;
}

static int hilse_inc_ddi(hil_mlc *mlc, int unused) {
	if (mlc->ddi >= 6) {
		BUG();
		return -1;
	}
	mlc->ddi++;
	return 0;
}

static int hilse_take_idd(hil_mlc *mlc, int unused) {
	int i;

	/* Help the state engine: 
	 * Is this a real IDD response or just an echo? 
	 *
	 * Real IDD response does not start with a command. 
	 */
	if (mlc->ipacket[0] & HIL_PKT_CMD) goto bail;
	/* Should have the command echoed further down. */
	for (i = 1; i < 16; i++) {
		if (((mlc->ipacket[i] & HIL_PKT_ADDR_MASK) == 
		     (mlc->ipacket[0] & HIL_PKT_ADDR_MASK)) &&
		    (mlc->ipacket[i] & HIL_PKT_CMD) && 
		    ((mlc->ipacket[i] & HIL_PKT_DATA_MASK) == HIL_CMD_IDD))
			break;
	}
	if (i > 15) goto bail;
	/* And the rest of the packets should still be clear. */
	while (++i < 16) {
		if (mlc->ipacket[i]) break;
	}
	if (i < 16) goto bail;
	for (i = 0; i < 16; i++) {
		mlc->di_scratch.idd[i] = 
			mlc->ipacket[i] & HIL_PKT_DATA_MASK;
	}
	/* Next step is to see if RSC supported */
	if (mlc->di_scratch.idd[1] & HIL_IDD_HEADER_RSC) 
		return HILSEN_NEXT;
	if (mlc->di_scratch.idd[1] & HIL_IDD_HEADER_EXD) 
		return HILSEN_DOWN | 4;
	return 0;
 bail:
	mlc->ddi--;
	return -1; /* This should send us off to ACF */
}

static int hilse_take_rsc(hil_mlc *mlc, int unused) {
	int i;

	for (i = 0; i < 16; i++) {
		mlc->di_scratch.rsc[i] = 
			mlc->ipacket[i] & HIL_PKT_DATA_MASK;
	}
	/* Next step is to see if EXD supported (IDD has already been read) */
	if (mlc->di_scratch.idd[1] & HIL_IDD_HEADER_EXD) 
		return HILSEN_NEXT;
	return 0;
}

static int hilse_take_exd(hil_mlc *mlc, int unused) {
	int i;

	for (i = 0; i < 16; i++) {
		mlc->di_scratch.exd[i] = 
			mlc->ipacket[i] & HIL_PKT_DATA_MASK;
	}
	/* Next step is to see if RNM supported. */
	if (mlc->di_scratch.exd[0] & HIL_EXD_HEADER_RNM) 
		return HILSEN_NEXT;
	return 0;
}

static int hilse_take_rnm(hil_mlc *mlc, int unused) {
	int i;

	for (i = 0; i < 16; i++) {
		mlc->di_scratch.rnm[i] = 
			mlc->ipacket[i] & HIL_PKT_DATA_MASK;
	}
	do {
	  char nam[17];
	  snprintf(nam, 16, "%s", mlc->di_scratch.rnm);
	  nam[16] = '\0';
	  printk(KERN_INFO PREFIX "Device name gotten: %s\n", nam);
	} while (0);
	return 0;
}

static int hilse_operate(hil_mlc *mlc, int repoll) { 

	if (mlc->opercnt == 0) hil_mlcs_probe = 0;
	mlc->opercnt = 1;

	hil_mlc_send_polls(mlc);

	if (!hil_mlcs_probe) return 0;
	hil_mlcs_probe = 0;
	mlc->opercnt = 0;
	return 1;
}

#define FUNC(funct, funct_arg, zero_rc, neg_rc, pos_rc) \
{ HILSE_FUNC,		{ func: &funct }, funct_arg, zero_rc, neg_rc, pos_rc },
#define OUT(pack) \
{ HILSE_OUT,		{ packet: pack }, 0, HILSEN_NEXT, HILSEN_DOZE, 0 },
#define CTS \
{ HILSE_CTS,		{ packet: 0    }, 0, HILSEN_NEXT | HILSEN_SCHED | HILSEN_BREAK, HILSEN_DOZE, 0 },
#define EXPECT(comp, to, got, got_wrong, timed_out) \
{ HILSE_EXPECT,		{ packet: comp }, to, got, got_wrong, timed_out },
#define EXPECT_LAST(comp, to, got, got_wrong, timed_out) \
{ HILSE_EXPECT_LAST,	{ packet: comp }, to, got, got_wrong, timed_out },
#define EXPECT_DISC(comp, to, got, got_wrong, timed_out) \
{ HILSE_EXPECT_DISC,	{ packet: comp }, to, got, got_wrong, timed_out },
#define IN(to, got, got_error, timed_out) \
{ HILSE_IN,		{ packet: 0    }, to, got, got_error, timed_out },
#define OUT_DISC(pack) \
{ HILSE_OUT_DISC,	{ packet: pack }, 0, 0, 0, 0 },
#define OUT_LAST(pack) \
{ HILSE_OUT_LAST,	{ packet: pack }, 0, 0, 0, 0 },

struct hilse_node hil_mlc_se[HILSEN_END] = {

	/* 0  HILSEN_START */
	FUNC(hilse_init_lcv, 0,	HILSEN_NEXT,	HILSEN_SLEEP,	0)

	/* 1  HILSEN_RESTART */
	FUNC(hilse_inc_lcv, 10,	HILSEN_NEXT,	HILSEN_START,  0)
	OUT(HIL_CTRL_ONLY)			/* Disable APE */
	CTS

#define TEST_PACKET(x) \
(HIL_PKT_CMD | (x << HIL_PKT_ADDR_SHIFT) | x << 4 | x)

	OUT(HIL_DO_ALTER_CTRL | HIL_CTRL_TEST | TEST_PACKET(0x5))
	EXPECT(HIL_ERR_INT | TEST_PACKET(0x5),
	       2000,		HILSEN_NEXT,	HILSEN_RESTART,	HILSEN_RESTART)
	OUT(HIL_DO_ALTER_CTRL | HIL_CTRL_TEST | TEST_PACKET(0xa))
	EXPECT(HIL_ERR_INT | TEST_PACKET(0xa),
	       2000,		HILSEN_NEXT,	HILSEN_RESTART,	HILSEN_RESTART)
	OUT(HIL_CTRL_ONLY | 0)			/* Disable test mode */
	
	/* 9  HILSEN_DHR */
	FUNC(hilse_init_lcv, 0,	HILSEN_NEXT,	HILSEN_SLEEP,	0)

	/* 10 HILSEN_DHR2 */
	FUNC(hilse_inc_lcv, 10,	HILSEN_NEXT,	HILSEN_START,	0)
	FUNC(hilse_set_ddi, -1,	HILSEN_NEXT,	0,		0)
	OUT(HIL_PKT_CMD | HIL_CMD_DHR)
	IN(300000,		HILSEN_DHR2,	HILSEN_DHR2,	HILSEN_NEXT)

	/* 14 HILSEN_IFC */
  	OUT(HIL_PKT_CMD | HIL_CMD_IFC)
	EXPECT(HIL_PKT_CMD | HIL_CMD_IFC | HIL_ERR_INT,
	       20000,		HILSEN_DISC,	HILSEN_DHR2,	HILSEN_NEXT )

	/* If devices are there, they weren't in PUP or other loopback mode.
	 * We're more concerned at this point with restoring operation
	 * to devices than discovering new ones, so we try to salvage
	 * the loop configuration by closing off the loop.
	 */

	/* 16 HILSEN_HEAL0 */
	FUNC(hilse_dec_ddi, 0,	HILSEN_NEXT,	HILSEN_ACF,	0)
	FUNC(hilse_inc_ddi, 0,	HILSEN_NEXT,	0,		0)

	/* 18 HILSEN_HEAL */
	OUT_LAST(HIL_CMD_ELB)
	EXPECT_LAST(HIL_CMD_ELB | HIL_ERR_INT, 
		    20000,	HILSEN_REPOLL,	HILSEN_DSR,	HILSEN_NEXT)
	FUNC(hilse_dec_ddi, 0,	HILSEN_HEAL,	HILSEN_NEXT,	0)

	/* 21 HILSEN_ACF */
	FUNC(hilse_init_lcv, 0,	HILSEN_NEXT,	HILSEN_DOZE,	0)

	/* 22 HILSEN_ACF2 */
	FUNC(hilse_inc_lcv, 10,	HILSEN_NEXT,	HILSEN_START,	0)
	OUT(HIL_PKT_CMD | HIL_CMD_ACF | 1)
	IN(20000,		HILSEN_NEXT,	HILSEN_DSR,	HILSEN_NEXT)

	/* 25 HILSEN_DISC0 */
	OUT_DISC(HIL_PKT_CMD | HIL_CMD_ELB)
	EXPECT_DISC(HIL_PKT_CMD | HIL_CMD_ELB | HIL_ERR_INT,
	       20000,		HILSEN_NEXT,	HILSEN_DSR,	HILSEN_DSR)

	/* Only enter here if response just received */
	/* 27 HILSEN_DISC */
	OUT_DISC(HIL_PKT_CMD | HIL_CMD_IDD)
	EXPECT_DISC(HIL_PKT_CMD | HIL_CMD_IDD | HIL_ERR_INT,
	       20000,		HILSEN_NEXT,	HILSEN_DSR,	HILSEN_START)
	FUNC(hilse_inc_ddi,  0,	HILSEN_NEXT,	HILSEN_START,	0)
	FUNC(hilse_take_idd, 0,	HILSEN_MATCH,	HILSEN_IFCACF,	HILSEN_FOLLOW)
	OUT_LAST(HIL_PKT_CMD | HIL_CMD_RSC)
	EXPECT_LAST(HIL_PKT_CMD | HIL_CMD_RSC | HIL_ERR_INT,
	       30000,		HILSEN_NEXT,	HILSEN_DSR,	HILSEN_DSR)
	FUNC(hilse_take_rsc, 0,	HILSEN_MATCH,	0,		HILSEN_FOLLOW)
	OUT_LAST(HIL_PKT_CMD | HIL_CMD_EXD)
	EXPECT_LAST(HIL_PKT_CMD | HIL_CMD_EXD | HIL_ERR_INT,
	       30000,		HILSEN_NEXT,	HILSEN_DSR,	HILSEN_DSR)
	FUNC(hilse_take_exd, 0,	HILSEN_MATCH,	0,		HILSEN_FOLLOW)
	OUT_LAST(HIL_PKT_CMD | HIL_CMD_RNM)
	EXPECT_LAST(HIL_PKT_CMD | HIL_CMD_RNM | HIL_ERR_INT,
	       30000,		HILSEN_NEXT,	HILSEN_DSR,	HILSEN_DSR)
	FUNC(hilse_take_rnm, 0, HILSEN_MATCH,	0,		0)

	/* 40 HILSEN_MATCH */
	FUNC(hilse_match, 0,	HILSEN_NEXT,	HILSEN_NEXT,	/* TODO */ 0)

	/* 41 HILSEN_OPERATE */
	OUT(HIL_PKT_CMD | HIL_CMD_POL)
	EXPECT(HIL_PKT_CMD | HIL_CMD_POL | HIL_ERR_INT,
	       20000,		HILSEN_NEXT,	HILSEN_DSR,	HILSEN_NEXT)
	FUNC(hilse_operate, 0,	HILSEN_OPERATE,	HILSEN_IFC,	HILSEN_NEXT)

	/* 44 HILSEN_PROBE */
	OUT_LAST(HIL_PKT_CMD | HIL_CMD_EPT)
	IN(10000, 		HILSEN_DISC,	HILSEN_DSR,	HILSEN_NEXT)
	OUT_DISC(HIL_PKT_CMD | HIL_CMD_ELB)
	IN(10000,		HILSEN_DISC,	HILSEN_DSR,	HILSEN_NEXT)
	OUT(HIL_PKT_CMD | HIL_CMD_ACF | 1)
	IN(10000,		HILSEN_DISC0,	HILSEN_DSR,	HILSEN_NEXT)
	OUT_LAST(HIL_PKT_CMD | HIL_CMD_ELB)
	IN(10000,		HILSEN_OPERATE,	HILSEN_DSR,	HILSEN_DSR)

	/* 52 HILSEN_DSR */
	FUNC(hilse_set_ddi, -1,	HILSEN_NEXT,	0,		0)
	OUT(HIL_PKT_CMD | HIL_CMD_DSR)
	IN(20000, 		HILSEN_DHR,	HILSEN_DHR,	HILSEN_IFC)

	/* 55 HILSEN_REPOLL */
	OUT(HIL_PKT_CMD | HIL_CMD_RPL)
	EXPECT(HIL_PKT_CMD | HIL_CMD_RPL | HIL_ERR_INT,
	       20000,		HILSEN_NEXT,	HILSEN_DSR,	HILSEN_NEXT)
	FUNC(hilse_operate, 1,	HILSEN_OPERATE,	HILSEN_IFC,	HILSEN_PROBE)

	/* 58 HILSEN_IFCACF */
  	OUT(HIL_PKT_CMD | HIL_CMD_IFC)
	EXPECT(HIL_PKT_CMD | HIL_CMD_IFC | HIL_ERR_INT,
	       20000,		HILSEN_ACF2,	HILSEN_DHR2,	HILSEN_HEAL)

	/* 60 HILSEN_END */
};

static inline void hilse_setup_input(hil_mlc *mlc, struct hilse_node *node) {

	switch (node->act) {
	case HILSE_EXPECT_DISC:
		mlc->imatch = node->object.packet;
		mlc->imatch |= ((mlc->ddi + 2) << HIL_PKT_ADDR_SHIFT);
		break;
	case HILSE_EXPECT_LAST:
		mlc->imatch = node->object.packet;
		mlc->imatch |= ((mlc->ddi + 1) << HIL_PKT_ADDR_SHIFT);
		break;
	case HILSE_EXPECT:
		mlc->imatch = node->object.packet;
		break;
	case HILSE_IN:
		mlc->imatch = 0;
		break;
	default:
		BUG();
	}
	mlc->istarted = 1;
	mlc->intimeout = node->arg;
	do_gettimeofday(&(mlc->instart));
	mlc->icount = 15;
	memset(mlc->ipacket, 0, 16 * sizeof(hil_packet));
	BUG_ON(down_trylock(&(mlc->isem)));

	return;
}

#ifdef HIL_MLC_DEBUG
static int doze = 0;
static int seidx; /* For debug */
static int kick = 1;
#endif

static int hilse_donode (hil_mlc *mlc) {
	struct hilse_node *node;
	int nextidx = 0;
	int sched_long = 0;
	unsigned long flags;

#ifdef HIL_MLC_DEBUG
	if (mlc->seidx && (mlc->seidx != seidx)  && mlc->seidx != 41 && mlc->seidx != 42 && mlc->seidx != 43) {
	  printk(KERN_DEBUG PREFIX "z%i \n%s {%i}", doze, kick ? "K" : "", mlc->seidx);
		doze = 0;
	}
	kick = 0;

	seidx = mlc->seidx;
#endif
	node = hil_mlc_se + mlc->seidx;

	switch (node->act) {
		int rc;
		hil_packet pack;

	case HILSE_FUNC:
		if (node->object.func == NULL) break;
		rc = node->object.func(mlc, node->arg);
		nextidx = (rc > 0) ? node->ugly : 
			((rc < 0) ? node->bad : node->good);
		if (nextidx == HILSEN_FOLLOW) nextidx = rc;
		break;
	case HILSE_EXPECT_LAST:
	case HILSE_EXPECT_DISC:
	case HILSE_EXPECT:
	case HILSE_IN:
		/* Already set up from previous HILSE_OUT_* */
		write_lock_irqsave(&(mlc->lock), flags);
		rc = mlc->in(mlc, node->arg);
		if (rc == 2)  {
			nextidx = HILSEN_DOZE;
			sched_long = 1;
			write_unlock_irqrestore(&(mlc->lock), flags);
			break;
		}
		if (rc == 1)		nextidx = node->ugly;
		else if (rc == 0)	nextidx = node->good;
		else			nextidx = node->bad;
		mlc->istarted = 0;
		write_unlock_irqrestore(&(mlc->lock), flags);
		break;
	case HILSE_OUT_LAST:
		write_lock_irqsave(&(mlc->lock), flags);
		pack = node->object.packet;
		pack |= ((mlc->ddi + 1) << HIL_PKT_ADDR_SHIFT);
		goto out;
	case HILSE_OUT_DISC:
		write_lock_irqsave(&(mlc->lock), flags);
		pack = node->object.packet;
		pack |= ((mlc->ddi + 2) << HIL_PKT_ADDR_SHIFT);
		goto out;
	case HILSE_OUT:
		write_lock_irqsave(&(mlc->lock), flags);
		pack = node->object.packet;
	out:
		if (mlc->istarted) goto out2;
		/* Prepare to receive input */
		if ((node + 1)->act & HILSE_IN)
			hilse_setup_input(mlc, node + 1);

	out2:
		write_unlock_irqrestore(&(mlc->lock), flags);

		if (down_trylock(&mlc->osem)) {
			nextidx = HILSEN_DOZE;
			break;
		}
		up(&mlc->osem);

		write_lock_irqsave(&(mlc->lock), flags);
		if (!(mlc->ostarted)) {
			mlc->ostarted = 1;
			mlc->opacket = pack;
			mlc->out(mlc);
			nextidx = HILSEN_DOZE;
			write_unlock_irqrestore(&(mlc->lock), flags);
			break;
		}
		mlc->ostarted = 0;
		do_gettimeofday(&(mlc->instart));
		write_unlock_irqrestore(&(mlc->lock), flags);
		nextidx = HILSEN_NEXT;
		break;
	case HILSE_CTS:
		nextidx = mlc->cts(mlc) ? node->bad : node->good;
		break;
	default:
		BUG();
		nextidx = 0;
		break;
	}

#ifdef HIL_MLC_DEBUG
	if (nextidx == HILSEN_DOZE) doze++;
#endif

	while (nextidx & HILSEN_SCHED) {
		struct timeval tv;

		if (!sched_long) goto sched;

		do_gettimeofday(&tv);
		tv.tv_usec += 1000000 * (tv.tv_sec - mlc->instart.tv_sec);
		tv.tv_usec -= mlc->instart.tv_usec;
		if (tv.tv_usec >= mlc->intimeout) goto sched;
		tv.tv_usec = (mlc->intimeout - tv.tv_usec) * HZ / 1000000;
		if (!tv.tv_usec) goto sched;
		mod_timer(&hil_mlcs_kicker, jiffies + tv.tv_usec);
		break;
	sched:
		tasklet_schedule(&hil_mlcs_tasklet);
		break;
	} 
	if (nextidx & HILSEN_DOWN) mlc->seidx += nextidx & HILSEN_MASK;
	else if (nextidx & HILSEN_UP) mlc->seidx -= nextidx & HILSEN_MASK;
	else mlc->seidx = nextidx & HILSEN_MASK;

	if (nextidx & HILSEN_BREAK)	return 1;
	return 0;
}

/******************** tasklet context functions **************************/
static void hil_mlcs_process(unsigned long unused) {
	struct list_head *tmp;

	read_lock(&hil_mlcs_lock);
	list_for_each(tmp, &hil_mlcs) {
		struct hil_mlc *mlc = list_entry(tmp, hil_mlc, list);
		while (hilse_donode(mlc) == 0) {
#ifdef HIL_MLC_DEBUG
		  if (mlc->seidx != 41 && 
		      mlc->seidx != 42 && 
		      mlc->seidx != 43) 
		    printk(KERN_DEBUG PREFIX " + ");
#endif
		};
	}
	read_unlock(&hil_mlcs_lock);
}

/************************* Keepalive timer task *********************/

void hil_mlcs_timer (unsigned long data) {
	hil_mlcs_probe = 1;
	tasklet_schedule(&hil_mlcs_tasklet);
	/* Re-insert the periodic task. */
	if (!timer_pending(&hil_mlcs_kicker))
		mod_timer(&hil_mlcs_kicker, jiffies + HZ);
}

/******************** user/kernel context functions **********************/

static int hil_mlc_serio_write(struct serio *serio, unsigned char c) {
	struct hil_mlc_serio_map *map;
	struct hil_mlc *mlc;
	struct serio_driver *drv;
	uint8_t *idx, *last;

	map = serio->port_data;
	if (map == NULL) {
		BUG();
		return -EIO;
	}
	mlc = map->mlc;
	if (mlc == NULL) {
		BUG();
		return -EIO;
	}
	mlc->serio_opacket[map->didx] |= 
		((hil_packet)c) << (8 * (3 - mlc->serio_oidx[map->didx]));

	if (mlc->serio_oidx[map->didx] >= 3) {
		/* for now only commands */
		if (!(mlc->serio_opacket[map->didx] & HIL_PKT_CMD)) 
			return -EIO;
		switch (mlc->serio_opacket[map->didx] & HIL_PKT_DATA_MASK) {
		case HIL_CMD_IDD:
			idx = mlc->di[map->didx].idd;
			goto emu;
		case HIL_CMD_RSC:
			idx = mlc->di[map->didx].rsc;
			goto emu;
		case HIL_CMD_EXD:
			idx = mlc->di[map->didx].exd;
			goto emu;
		case HIL_CMD_RNM:
			idx = mlc->di[map->didx].rnm;
			goto emu;
		default:
			break;
		}
		mlc->serio_oidx[map->didx] = 0;
		mlc->serio_opacket[map->didx] = 0;
	}

	mlc->serio_oidx[map->didx]++;
	return -EIO;
 emu:
	drv = serio->drv;
	if (drv == NULL) {
		BUG();
		return -EIO;
	}
	last = idx + 15;
	while ((last != idx) && (*last == 0)) last--;

	while (idx != last) {
		drv->interrupt(serio, 0, 0, NULL);
		drv->interrupt(serio, HIL_ERR_INT >> 16, 0, NULL);
		drv->interrupt(serio, 0, 0, NULL);
		drv->interrupt(serio, *idx, 0, NULL);
		idx++;
	}
	drv->interrupt(serio, 0, 0, NULL);
	drv->interrupt(serio, HIL_ERR_INT >> 16, 0, NULL);
	drv->interrupt(serio, HIL_PKT_CMD >> 8, 0, NULL);
	drv->interrupt(serio, *idx, 0, NULL);
	
	mlc->serio_oidx[map->didx] = 0;
	mlc->serio_opacket[map->didx] = 0;

	return 0;
}

static int hil_mlc_serio_open(struct serio *serio) {
	struct hil_mlc_serio_map *map;
	struct hil_mlc *mlc;

	if (serio_get_drvdata(serio) != NULL)
		return -EBUSY;

	map = serio->port_data;
	if (map == NULL) {
		BUG();
		return -ENODEV;
	}
	mlc = map->mlc;
	if (mlc == NULL) {
		BUG();
		return -ENODEV;
	}

	return 0;
}

static void hil_mlc_serio_close(struct serio *serio) {
	struct hil_mlc_serio_map *map;
	struct hil_mlc *mlc;

	map = serio->port_data;
	if (map == NULL) {
		BUG();
		return;
	}
	mlc = map->mlc;
	if (mlc == NULL) {
		BUG();
		return;
	}

	serio_set_drvdata(serio, NULL);
	serio->drv = NULL;
	/* TODO wake up interruptable */
}

static struct serio_device_id hil_mlc_serio_id = {
	.type = SERIO_HIL_MLC,
	.proto = SERIO_HIL,
	.extra = SERIO_ANY,
	.id = SERIO_ANY,
};

int hil_mlc_register(hil_mlc *mlc) {
	int i;
        unsigned long flags;

	if (mlc == NULL) {
		return -EINVAL;
	}

	mlc->istarted = 0;
        mlc->ostarted = 0;

        rwlock_init(&mlc->lock);
        init_MUTEX(&(mlc->osem));

        init_MUTEX(&(mlc->isem));
        mlc->icount = -1;
        mlc->imatch = 0;

	mlc->opercnt = 0;

        init_MUTEX_LOCKED(&(mlc->csem));

	hil_mlc_clear_di_scratch(mlc);
	hil_mlc_clear_di_map(mlc, 0);
	for (i = 0; i < HIL_MLC_DEVMEM; i++) {
		struct serio *mlc_serio;
		hil_mlc_copy_di_scratch(mlc, i);
		mlc_serio = kmalloc(sizeof(*mlc_serio), GFP_KERNEL);
		mlc->serio[i] = mlc_serio;
		memset(mlc_serio, 0, sizeof(*mlc_serio));
		mlc_serio->id			= hil_mlc_serio_id;
		mlc_serio->write		= hil_mlc_serio_write;
		mlc_serio->open			= hil_mlc_serio_open;
		mlc_serio->close		= hil_mlc_serio_close;
		mlc_serio->port_data		= &(mlc->serio_map[i]);
		mlc->serio_map[i].mlc		= mlc;
		mlc->serio_map[i].didx		= i;
		mlc->serio_map[i].di_revmap	= -1;
		mlc->serio_opacket[i]		= 0;
		mlc->serio_oidx[i]		= 0;
		serio_register_port(mlc_serio);
	}

	mlc->tasklet = &hil_mlcs_tasklet;

	write_lock_irqsave(&hil_mlcs_lock, flags);
	list_add_tail(&mlc->list, &hil_mlcs);
	mlc->seidx = HILSEN_START;
	write_unlock_irqrestore(&hil_mlcs_lock, flags);

	tasklet_schedule(&hil_mlcs_tasklet);
	return 0;
}

int hil_mlc_unregister(hil_mlc *mlc) {
	struct list_head *tmp;
        unsigned long flags;
	int i;

	if (mlc == NULL)
		return -EINVAL;

	write_lock_irqsave(&hil_mlcs_lock, flags);
	list_for_each(tmp, &hil_mlcs) {
		if (list_entry(tmp, hil_mlc, list) == mlc)
			goto found;
	}

	/* not found in list */
	write_unlock_irqrestore(&hil_mlcs_lock, flags);
	tasklet_schedule(&hil_mlcs_tasklet);
	return -ENODEV;

 found:
	list_del(tmp);
        write_unlock_irqrestore(&hil_mlcs_lock, flags);

	for (i = 0; i < HIL_MLC_DEVMEM; i++) {
		serio_unregister_port(mlc->serio[i]);
		mlc->serio[i] = NULL;
	}

	tasklet_schedule(&hil_mlcs_tasklet);
	return 0;
}

/**************************** Module interface *************************/

static int __init hil_mlc_init(void)
{
	init_timer(&hil_mlcs_kicker);
	hil_mlcs_kicker.expires = jiffies + HZ;
	hil_mlcs_kicker.function = &hil_mlcs_timer;
	add_timer(&hil_mlcs_kicker);

	tasklet_enable(&hil_mlcs_tasklet);

	return 0;
}
                
static void __exit hil_mlc_exit(void)
{
	del_timer(&hil_mlcs_kicker);

	tasklet_disable(&hil_mlcs_tasklet);
	tasklet_kill(&hil_mlcs_tasklet);
}
                        
module_init(hil_mlc_init);
module_exit(hil_mlc_exit);
