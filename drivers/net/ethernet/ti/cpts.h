/*
 * TI Common Platform Time Sync
 *
 * Copyright (C) 2012 Richard Cochran <richardcochran@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef _TI_CPTS_H_
#define _TI_CPTS_H_

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clocksource.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/skbuff.h>
#include <linux/timecounter.h>

struct cpsw_cpts {
	u32 idver;                /* Identification and version */
	u32 control;              /* Time sync control */
	u32 res1;
	u32 ts_push;              /* Time stamp event push */
	u32 ts_load_val;          /* Time stamp load value */
	u32 ts_load_en;           /* Time stamp load enable */
	u32 res2[2];
	u32 intstat_raw;          /* Time sync interrupt status raw */
	u32 intstat_masked;       /* Time sync interrupt status masked */
	u32 int_enable;           /* Time sync interrupt enable */
	u32 res3;
	u32 event_pop;            /* Event interrupt pop */
	u32 event_low;            /* 32 Bit Event Time Stamp */
	u32 event_high;           /* Event Type Fields */
};

/* Bit definitions for the IDVER register */
#define TX_IDENT_SHIFT       (16)    /* TX Identification Value */
#define TX_IDENT_MASK        (0xffff)
#define RTL_VER_SHIFT        (11)    /* RTL Version Value */
#define RTL_VER_MASK         (0x1f)
#define MAJOR_VER_SHIFT      (8)     /* Major Version Value */
#define MAJOR_VER_MASK       (0x7)
#define MINOR_VER_SHIFT      (0)     /* Minor Version Value */
#define MINOR_VER_MASK       (0xff)

/* Bit definitions for the CONTROL register */
#define HW4_TS_PUSH_EN       (1<<11) /* Hardware push 4 enable */
#define HW3_TS_PUSH_EN       (1<<10) /* Hardware push 3 enable */
#define HW2_TS_PUSH_EN       (1<<9)  /* Hardware push 2 enable */
#define HW1_TS_PUSH_EN       (1<<8)  /* Hardware push 1 enable */
#define INT_TEST             (1<<1)  /* Interrupt Test */
#define CPTS_EN              (1<<0)  /* Time Sync Enable */

/*
 * Definitions for the single bit resisters:
 * TS_PUSH TS_LOAD_EN  INTSTAT_RAW INTSTAT_MASKED INT_ENABLE EVENT_POP
 */
#define TS_PUSH             (1<<0)  /* Time stamp event push */
#define TS_LOAD_EN          (1<<0)  /* Time Stamp Load */
#define TS_PEND_RAW         (1<<0)  /* int read (before enable) */
#define TS_PEND             (1<<0)  /* masked interrupt read (after enable) */
#define TS_PEND_EN          (1<<0)  /* masked interrupt enable */
#define EVENT_POP           (1<<0)  /* writing discards one event */

/* Bit definitions for the EVENT_HIGH register */
#define PORT_NUMBER_SHIFT    (24)    /* Indicates Ethernet port or HW pin */
#define PORT_NUMBER_MASK     (0x1f)
#define EVENT_TYPE_SHIFT     (20)    /* Time sync event type */
#define EVENT_TYPE_MASK      (0xf)
#define MESSAGE_TYPE_SHIFT   (16)    /* PTP message type */
#define MESSAGE_TYPE_MASK    (0xf)
#define SEQUENCE_ID_SHIFT    (0)     /* PTP message sequence ID */
#define SEQUENCE_ID_MASK     (0xffff)

enum {
	CPTS_EV_PUSH, /* Time Stamp Push Event */
	CPTS_EV_ROLL, /* Time Stamp Rollover Event */
	CPTS_EV_HALF, /* Time Stamp Half Rollover Event */
	CPTS_EV_HW,   /* Hardware Time Stamp Push Event */
	CPTS_EV_RX,   /* Ethernet Receive Event */
	CPTS_EV_TX,   /* Ethernet Transmit Event */
};

/* This covers any input clock up to about 500 MHz. */
#define CPTS_OVERFLOW_PERIOD (HZ * 8)

#define CPTS_FIFO_DEPTH 16
#define CPTS_MAX_EVENTS 32

struct cpts_event {
	struct list_head list;
	unsigned long tmo;
	u32 high;
	u32 low;
};

struct cpts {
	struct cpsw_cpts __iomem *reg;
	int tx_enable;
	int rx_enable;
#ifdef CONFIG_TI_CPTS
	struct ptp_clock_info info;
	struct ptp_clock *clock;
	spinlock_t lock; /* protects time registers */
	u32 cc_mult; /* for the nominal frequency */
	struct cyclecounter cc;
	struct timecounter tc;
	struct delayed_work overflow_work;
	int phc_index;
	struct clk *refclk;
	struct list_head events;
	struct list_head pool;
	struct cpts_event pool_data[CPTS_MAX_EVENTS];
#endif
};

#ifdef CONFIG_TI_CPTS
void cpts_rx_timestamp(struct cpts *cpts, struct sk_buff *skb);
void cpts_tx_timestamp(struct cpts *cpts, struct sk_buff *skb);
#else
static inline void cpts_rx_timestamp(struct cpts *cpts, struct sk_buff *skb)
{
}
static inline void cpts_tx_timestamp(struct cpts *cpts, struct sk_buff *skb)
{
}
#endif

int cpts_register(struct device *dev, struct cpts *cpts, u32 mult, u32 shift);
void cpts_unregister(struct cpts *cpts);

#endif
