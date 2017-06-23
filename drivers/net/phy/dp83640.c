/*
 * Driver for the National Semiconductor DP83640 PHYTER
 *
 * Copyright (C) 2010 OMICRON electronics GmbH
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/crc32.h>
#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/net_tstamp.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/phy.h>
#include <linux/ptp_classify.h>
#include <linux/ptp_clock_kernel.h>

#include "dp83640_reg.h"

#define DP83640_PHY_ID	0x20005ce1
#define PAGESEL		0x13
#define MAX_RXTS	64
#define N_EXT_TS	6
#define N_PER_OUT	7
#define PSF_PTPVER	2
#define PSF_EVNT	0x4000
#define PSF_RX		0x2000
#define PSF_TX		0x1000
#define EXT_EVENT	1
#define CAL_EVENT	7
#define CAL_TRIGGER	1
#define DP83640_N_PINS	12

#define MII_DP83640_MICR 0x11
#define MII_DP83640_MISR 0x12

#define MII_DP83640_MICR_OE 0x1
#define MII_DP83640_MICR_IE 0x2

#define MII_DP83640_MISR_RHF_INT_EN 0x01
#define MII_DP83640_MISR_FHF_INT_EN 0x02
#define MII_DP83640_MISR_ANC_INT_EN 0x04
#define MII_DP83640_MISR_DUP_INT_EN 0x08
#define MII_DP83640_MISR_SPD_INT_EN 0x10
#define MII_DP83640_MISR_LINK_INT_EN 0x20
#define MII_DP83640_MISR_ED_INT_EN 0x40
#define MII_DP83640_MISR_LQ_INT_EN 0x80

/* phyter seems to miss the mark by 16 ns */
#define ADJTIME_FIX	16

#define SKB_TIMESTAMP_TIMEOUT	2 /* jiffies */

#if defined(__BIG_ENDIAN)
#define ENDIAN_FLAG	0
#elif defined(__LITTLE_ENDIAN)
#define ENDIAN_FLAG	PSF_ENDIAN
#endif

struct dp83640_skb_info {
	int ptp_type;
	unsigned long tmo;
};

struct phy_rxts {
	u16 ns_lo;   /* ns[15:0] */
	u16 ns_hi;   /* overflow[1:0], ns[29:16] */
	u16 sec_lo;  /* sec[15:0] */
	u16 sec_hi;  /* sec[31:16] */
	u16 seqid;   /* sequenceId[15:0] */
	u16 msgtype; /* messageType[3:0], hash[11:0] */
};

struct phy_txts {
	u16 ns_lo;   /* ns[15:0] */
	u16 ns_hi;   /* overflow[1:0], ns[29:16] */
	u16 sec_lo;  /* sec[15:0] */
	u16 sec_hi;  /* sec[31:16] */
};

struct rxts {
	struct list_head list;
	unsigned long tmo;
	u64 ns;
	u16 seqid;
	u8  msgtype;
	u16 hash;
};

struct dp83640_clock;

struct dp83640_private {
	struct list_head list;
	struct dp83640_clock *clock;
	struct phy_device *phydev;
	struct delayed_work ts_work;
	int hwts_tx_en;
	int hwts_rx_en;
	int layer;
	int version;
	/* remember state of cfg0 during calibration */
	int cfg0;
	/* remember the last event time stamp */
	struct phy_txts edata;
	/* list of rx timestamps */
	struct list_head rxts;
	struct list_head rxpool;
	struct rxts rx_pool_data[MAX_RXTS];
	/* protects above three fields from concurrent access */
	spinlock_t rx_lock;
	/* queues of incoming and outgoing packets */
	struct sk_buff_head rx_queue;
	struct sk_buff_head tx_queue;
};

struct dp83640_clock {
	/* keeps the instance in the 'phyter_clocks' list */
	struct list_head list;
	/* we create one clock instance per MII bus */
	struct mii_bus *bus;
	/* protects extended registers from concurrent access */
	struct mutex extreg_lock;
	/* remembers which page was last selected */
	int page;
	/* our advertised capabilities */
	struct ptp_clock_info caps;
	/* protects the three fields below from concurrent access */
	struct mutex clock_lock;
	/* the one phyter from which we shall read */
	struct dp83640_private *chosen;
	/* list of the other attached phyters, not chosen */
	struct list_head phylist;
	/* reference to our PTP hardware clock */
	struct ptp_clock *ptp_clock;
};

/* globals */

enum {
	CALIBRATE_GPIO,
	PEROUT_GPIO,
	EXTTS0_GPIO,
	EXTTS1_GPIO,
	EXTTS2_GPIO,
	EXTTS3_GPIO,
	EXTTS4_GPIO,
	EXTTS5_GPIO,
	GPIO_TABLE_SIZE
};

static int chosen_phy = -1;
static ushort gpio_tab[GPIO_TABLE_SIZE] = {
	1, 2, 3, 4, 8, 9, 10, 11
};

module_param(chosen_phy, int, 0444);
module_param_array(gpio_tab, ushort, NULL, 0444);

MODULE_PARM_DESC(chosen_phy, \
	"The address of the PHY to use for the ancillary clock features");
MODULE_PARM_DESC(gpio_tab, \
	"Which GPIO line to use for which purpose: cal,perout,extts1,...,extts6");

static void dp83640_gpio_defaults(struct ptp_pin_desc *pd)
{
	int i, index;

	for (i = 0; i < DP83640_N_PINS; i++) {
		snprintf(pd[i].name, sizeof(pd[i].name), "GPIO%d", 1 + i);
		pd[i].index = i;
	}

	for (i = 0; i < GPIO_TABLE_SIZE; i++) {
		if (gpio_tab[i] < 1 || gpio_tab[i] > DP83640_N_PINS) {
			pr_err("gpio_tab[%d]=%hu out of range", i, gpio_tab[i]);
			return;
		}
	}

	index = gpio_tab[CALIBRATE_GPIO] - 1;
	pd[index].func = PTP_PF_PHYSYNC;
	pd[index].chan = 0;

	index = gpio_tab[PEROUT_GPIO] - 1;
	pd[index].func = PTP_PF_PEROUT;
	pd[index].chan = 0;

	for (i = EXTTS0_GPIO; i < GPIO_TABLE_SIZE; i++) {
		index = gpio_tab[i] - 1;
		pd[index].func = PTP_PF_EXTTS;
		pd[index].chan = i - EXTTS0_GPIO;
	}
}

/* a list of clocks and a mutex to protect it */
static LIST_HEAD(phyter_clocks);
static DEFINE_MUTEX(phyter_clocks_lock);

static void rx_timestamp_work(struct work_struct *work);

/* extended register access functions */

#define BROADCAST_ADDR 31

static inline int broadcast_write(struct mii_bus *bus, u32 regnum, u16 val)
{
	return mdiobus_write(bus, BROADCAST_ADDR, regnum, val);
}

/* Caller must hold extreg_lock. */
static int ext_read(struct phy_device *phydev, int page, u32 regnum)
{
	struct dp83640_private *dp83640 = phydev->priv;
	int val;

	if (dp83640->clock->page != page) {
		broadcast_write(phydev->bus, PAGESEL, page);
		dp83640->clock->page = page;
	}
	val = phy_read(phydev, regnum);

	return val;
}

/* Caller must hold extreg_lock. */
static void ext_write(int broadcast, struct phy_device *phydev,
		      int page, u32 regnum, u16 val)
{
	struct dp83640_private *dp83640 = phydev->priv;

	if (dp83640->clock->page != page) {
		broadcast_write(phydev->bus, PAGESEL, page);
		dp83640->clock->page = page;
	}
	if (broadcast)
		broadcast_write(phydev->bus, regnum, val);
	else
		phy_write(phydev, regnum, val);
}

/* Caller must hold extreg_lock. */
static int tdr_write(int bc, struct phy_device *dev,
		     const struct timespec64 *ts, u16 cmd)
{
	ext_write(bc, dev, PAGE4, PTP_TDR, ts->tv_nsec & 0xffff);/* ns[15:0]  */
	ext_write(bc, dev, PAGE4, PTP_TDR, ts->tv_nsec >> 16);   /* ns[31:16] */
	ext_write(bc, dev, PAGE4, PTP_TDR, ts->tv_sec & 0xffff); /* sec[15:0] */
	ext_write(bc, dev, PAGE4, PTP_TDR, ts->tv_sec >> 16);    /* sec[31:16]*/

	ext_write(bc, dev, PAGE4, PTP_CTL, cmd);

	return 0;
}

/* convert phy timestamps into driver timestamps */

static void phy2rxts(struct phy_rxts *p, struct rxts *rxts)
{
	u32 sec;

	sec = p->sec_lo;
	sec |= p->sec_hi << 16;

	rxts->ns = p->ns_lo;
	rxts->ns |= (p->ns_hi & 0x3fff) << 16;
	rxts->ns += ((u64)sec) * 1000000000ULL;
	rxts->seqid = p->seqid;
	rxts->msgtype = (p->msgtype >> 12) & 0xf;
	rxts->hash = p->msgtype & 0x0fff;
	rxts->tmo = jiffies + SKB_TIMESTAMP_TIMEOUT;
}

static u64 phy2txts(struct phy_txts *p)
{
	u64 ns;
	u32 sec;

	sec = p->sec_lo;
	sec |= p->sec_hi << 16;

	ns = p->ns_lo;
	ns |= (p->ns_hi & 0x3fff) << 16;
	ns += ((u64)sec) * 1000000000ULL;

	return ns;
}

static int periodic_output(struct dp83640_clock *clock,
			   struct ptp_clock_request *clkreq, bool on,
			   int trigger)
{
	struct dp83640_private *dp83640 = clock->chosen;
	struct phy_device *phydev = dp83640->phydev;
	u32 sec, nsec, pwidth;
	u16 gpio, ptp_trig, val;

	if (on) {
		gpio = 1 + ptp_find_pin(clock->ptp_clock, PTP_PF_PEROUT,
					trigger);
		if (gpio < 1)
			return -EINVAL;
	} else {
		gpio = 0;
	}

	ptp_trig = TRIG_WR |
		(trigger & TRIG_CSEL_MASK) << TRIG_CSEL_SHIFT |
		(gpio & TRIG_GPIO_MASK) << TRIG_GPIO_SHIFT |
		TRIG_PER |
		TRIG_PULSE;

	val = (trigger & TRIG_SEL_MASK) << TRIG_SEL_SHIFT;

	if (!on) {
		val |= TRIG_DIS;
		mutex_lock(&clock->extreg_lock);
		ext_write(0, phydev, PAGE5, PTP_TRIG, ptp_trig);
		ext_write(0, phydev, PAGE4, PTP_CTL, val);
		mutex_unlock(&clock->extreg_lock);
		return 0;
	}

	sec = clkreq->perout.start.sec;
	nsec = clkreq->perout.start.nsec;
	pwidth = clkreq->perout.period.sec * 1000000000UL;
	pwidth += clkreq->perout.period.nsec;
	pwidth /= 2;

	mutex_lock(&clock->extreg_lock);

	ext_write(0, phydev, PAGE5, PTP_TRIG, ptp_trig);

	/*load trigger*/
	val |= TRIG_LOAD;
	ext_write(0, phydev, PAGE4, PTP_CTL, val);
	ext_write(0, phydev, PAGE4, PTP_TDR, nsec & 0xffff);   /* ns[15:0] */
	ext_write(0, phydev, PAGE4, PTP_TDR, nsec >> 16);      /* ns[31:16] */
	ext_write(0, phydev, PAGE4, PTP_TDR, sec & 0xffff);    /* sec[15:0] */
	ext_write(0, phydev, PAGE4, PTP_TDR, sec >> 16);       /* sec[31:16] */
	ext_write(0, phydev, PAGE4, PTP_TDR, pwidth & 0xffff); /* ns[15:0] */
	ext_write(0, phydev, PAGE4, PTP_TDR, pwidth >> 16);    /* ns[31:16] */
	/* Triggers 0 and 1 has programmable pulsewidth2 */
	if (trigger < 2) {
		ext_write(0, phydev, PAGE4, PTP_TDR, pwidth & 0xffff);
		ext_write(0, phydev, PAGE4, PTP_TDR, pwidth >> 16);
	}

	/*enable trigger*/
	val &= ~TRIG_LOAD;
	val |= TRIG_EN;
	ext_write(0, phydev, PAGE4, PTP_CTL, val);

	mutex_unlock(&clock->extreg_lock);
	return 0;
}

/* ptp clock methods */

static int ptp_dp83640_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	struct dp83640_clock *clock =
		container_of(ptp, struct dp83640_clock, caps);
	struct phy_device *phydev = clock->chosen->phydev;
	u64 rate;
	int neg_adj = 0;
	u16 hi, lo;

	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}
	rate = ppb;
	rate <<= 26;
	rate = div_u64(rate, 1953125);

	hi = (rate >> 16) & PTP_RATE_HI_MASK;
	if (neg_adj)
		hi |= PTP_RATE_DIR;

	lo = rate & 0xffff;

	mutex_lock(&clock->extreg_lock);

	ext_write(1, phydev, PAGE4, PTP_RATEH, hi);
	ext_write(1, phydev, PAGE4, PTP_RATEL, lo);

	mutex_unlock(&clock->extreg_lock);

	return 0;
}

static int ptp_dp83640_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct dp83640_clock *clock =
		container_of(ptp, struct dp83640_clock, caps);
	struct phy_device *phydev = clock->chosen->phydev;
	struct timespec64 ts;
	int err;

	delta += ADJTIME_FIX;

	ts = ns_to_timespec64(delta);

	mutex_lock(&clock->extreg_lock);

	err = tdr_write(1, phydev, &ts, PTP_STEP_CLK);

	mutex_unlock(&clock->extreg_lock);

	return err;
}

static int ptp_dp83640_gettime(struct ptp_clock_info *ptp,
			       struct timespec64 *ts)
{
	struct dp83640_clock *clock =
		container_of(ptp, struct dp83640_clock, caps);
	struct phy_device *phydev = clock->chosen->phydev;
	unsigned int val[4];

	mutex_lock(&clock->extreg_lock);

	ext_write(0, phydev, PAGE4, PTP_CTL, PTP_RD_CLK);

	val[0] = ext_read(phydev, PAGE4, PTP_TDR); /* ns[15:0] */
	val[1] = ext_read(phydev, PAGE4, PTP_TDR); /* ns[31:16] */
	val[2] = ext_read(phydev, PAGE4, PTP_TDR); /* sec[15:0] */
	val[3] = ext_read(phydev, PAGE4, PTP_TDR); /* sec[31:16] */

	mutex_unlock(&clock->extreg_lock);

	ts->tv_nsec = val[0] | (val[1] << 16);
	ts->tv_sec  = val[2] | (val[3] << 16);

	return 0;
}

static int ptp_dp83640_settime(struct ptp_clock_info *ptp,
			       const struct timespec64 *ts)
{
	struct dp83640_clock *clock =
		container_of(ptp, struct dp83640_clock, caps);
	struct phy_device *phydev = clock->chosen->phydev;
	int err;

	mutex_lock(&clock->extreg_lock);

	err = tdr_write(1, phydev, ts, PTP_LOAD_CLK);

	mutex_unlock(&clock->extreg_lock);

	return err;
}

static int ptp_dp83640_enable(struct ptp_clock_info *ptp,
			      struct ptp_clock_request *rq, int on)
{
	struct dp83640_clock *clock =
		container_of(ptp, struct dp83640_clock, caps);
	struct phy_device *phydev = clock->chosen->phydev;
	unsigned int index;
	u16 evnt, event_num, gpio_num;

	switch (rq->type) {
	case PTP_CLK_REQ_EXTTS:
		index = rq->extts.index;
		if (index >= N_EXT_TS)
			return -EINVAL;
		event_num = EXT_EVENT + index;
		evnt = EVNT_WR | (event_num & EVNT_SEL_MASK) << EVNT_SEL_SHIFT;
		if (on) {
			gpio_num = 1 + ptp_find_pin(clock->ptp_clock,
						    PTP_PF_EXTTS, index);
			if (gpio_num < 1)
				return -EINVAL;
			evnt |= (gpio_num & EVNT_GPIO_MASK) << EVNT_GPIO_SHIFT;
			if (rq->extts.flags & PTP_FALLING_EDGE)
				evnt |= EVNT_FALL;
			else
				evnt |= EVNT_RISE;
		}
		mutex_lock(&clock->extreg_lock);
		ext_write(0, phydev, PAGE5, PTP_EVNT, evnt);
		mutex_unlock(&clock->extreg_lock);
		return 0;

	case PTP_CLK_REQ_PEROUT:
		if (rq->perout.index >= N_PER_OUT)
			return -EINVAL;
		return periodic_output(clock, rq, on, rq->perout.index);

	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int ptp_dp83640_verify(struct ptp_clock_info *ptp, unsigned int pin,
			      enum ptp_pin_function func, unsigned int chan)
{
	struct dp83640_clock *clock =
		container_of(ptp, struct dp83640_clock, caps);

	if (clock->caps.pin_config[pin].func == PTP_PF_PHYSYNC &&
	    !list_empty(&clock->phylist))
		return 1;

	if (func == PTP_PF_PHYSYNC)
		return 1;

	return 0;
}

static u8 status_frame_dst[6] = { 0x01, 0x1B, 0x19, 0x00, 0x00, 0x00 };
static u8 status_frame_src[6] = { 0x08, 0x00, 0x17, 0x0B, 0x6B, 0x0F };

static void enable_status_frames(struct phy_device *phydev, bool on)
{
	struct dp83640_private *dp83640 = phydev->priv;
	struct dp83640_clock *clock = dp83640->clock;
	u16 cfg0 = 0, ver;

	if (on)
		cfg0 = PSF_EVNT_EN | PSF_RXTS_EN | PSF_TXTS_EN | ENDIAN_FLAG;

	ver = (PSF_PTPVER & VERSIONPTP_MASK) << VERSIONPTP_SHIFT;

	mutex_lock(&clock->extreg_lock);

	ext_write(0, phydev, PAGE5, PSF_CFG0, cfg0);
	ext_write(0, phydev, PAGE6, PSF_CFG1, ver);

	mutex_unlock(&clock->extreg_lock);

	if (!phydev->attached_dev) {
		pr_warn("expected to find an attached netdevice\n");
		return;
	}

	if (on) {
		if (dev_mc_add(phydev->attached_dev, status_frame_dst))
			pr_warn("failed to add mc address\n");
	} else {
		if (dev_mc_del(phydev->attached_dev, status_frame_dst))
			pr_warn("failed to delete mc address\n");
	}
}

static bool is_status_frame(struct sk_buff *skb, int type)
{
	struct ethhdr *h = eth_hdr(skb);

	if (PTP_CLASS_V2_L2 == type &&
	    !memcmp(h->h_source, status_frame_src, sizeof(status_frame_src)))
		return true;
	else
		return false;
}

static int expired(struct rxts *rxts)
{
	return time_after(jiffies, rxts->tmo);
}

/* Caller must hold rx_lock. */
static void prune_rx_ts(struct dp83640_private *dp83640)
{
	struct list_head *this, *next;
	struct rxts *rxts;

	list_for_each_safe(this, next, &dp83640->rxts) {
		rxts = list_entry(this, struct rxts, list);
		if (expired(rxts)) {
			list_del_init(&rxts->list);
			list_add(&rxts->list, &dp83640->rxpool);
		}
	}
}

/* synchronize the phyters so they act as one clock */

static void enable_broadcast(struct phy_device *phydev, int init_page, int on)
{
	int val;
	phy_write(phydev, PAGESEL, 0);
	val = phy_read(phydev, PHYCR2);
	if (on)
		val |= BC_WRITE;
	else
		val &= ~BC_WRITE;
	phy_write(phydev, PHYCR2, val);
	phy_write(phydev, PAGESEL, init_page);
}

static void recalibrate(struct dp83640_clock *clock)
{
	s64 now, diff;
	struct phy_txts event_ts;
	struct timespec64 ts;
	struct list_head *this;
	struct dp83640_private *tmp;
	struct phy_device *master = clock->chosen->phydev;
	u16 cal_gpio, cfg0, evnt, ptp_trig, trigger, val;

	trigger = CAL_TRIGGER;
	cal_gpio = 1 + ptp_find_pin(clock->ptp_clock, PTP_PF_PHYSYNC, 0);
	if (cal_gpio < 1) {
		pr_err("PHY calibration pin not available - PHY is not calibrated.");
		return;
	}

	mutex_lock(&clock->extreg_lock);

	/*
	 * enable broadcast, disable status frames, enable ptp clock
	 */
	list_for_each(this, &clock->phylist) {
		tmp = list_entry(this, struct dp83640_private, list);
		enable_broadcast(tmp->phydev, clock->page, 1);
		tmp->cfg0 = ext_read(tmp->phydev, PAGE5, PSF_CFG0);
		ext_write(0, tmp->phydev, PAGE5, PSF_CFG0, 0);
		ext_write(0, tmp->phydev, PAGE4, PTP_CTL, PTP_ENABLE);
	}
	enable_broadcast(master, clock->page, 1);
	cfg0 = ext_read(master, PAGE5, PSF_CFG0);
	ext_write(0, master, PAGE5, PSF_CFG0, 0);
	ext_write(0, master, PAGE4, PTP_CTL, PTP_ENABLE);

	/*
	 * enable an event timestamp
	 */
	evnt = EVNT_WR | EVNT_RISE | EVNT_SINGLE;
	evnt |= (CAL_EVENT & EVNT_SEL_MASK) << EVNT_SEL_SHIFT;
	evnt |= (cal_gpio & EVNT_GPIO_MASK) << EVNT_GPIO_SHIFT;

	list_for_each(this, &clock->phylist) {
		tmp = list_entry(this, struct dp83640_private, list);
		ext_write(0, tmp->phydev, PAGE5, PTP_EVNT, evnt);
	}
	ext_write(0, master, PAGE5, PTP_EVNT, evnt);

	/*
	 * configure a trigger
	 */
	ptp_trig = TRIG_WR | TRIG_IF_LATE | TRIG_PULSE;
	ptp_trig |= (trigger  & TRIG_CSEL_MASK) << TRIG_CSEL_SHIFT;
	ptp_trig |= (cal_gpio & TRIG_GPIO_MASK) << TRIG_GPIO_SHIFT;
	ext_write(0, master, PAGE5, PTP_TRIG, ptp_trig);

	/* load trigger */
	val = (trigger & TRIG_SEL_MASK) << TRIG_SEL_SHIFT;
	val |= TRIG_LOAD;
	ext_write(0, master, PAGE4, PTP_CTL, val);

	/* enable trigger */
	val &= ~TRIG_LOAD;
	val |= TRIG_EN;
	ext_write(0, master, PAGE4, PTP_CTL, val);

	/* disable trigger */
	val = (trigger & TRIG_SEL_MASK) << TRIG_SEL_SHIFT;
	val |= TRIG_DIS;
	ext_write(0, master, PAGE4, PTP_CTL, val);

	/*
	 * read out and correct offsets
	 */
	val = ext_read(master, PAGE4, PTP_STS);
	pr_info("master PTP_STS  0x%04hx\n", val);
	val = ext_read(master, PAGE4, PTP_ESTS);
	pr_info("master PTP_ESTS 0x%04hx\n", val);
	event_ts.ns_lo  = ext_read(master, PAGE4, PTP_EDATA);
	event_ts.ns_hi  = ext_read(master, PAGE4, PTP_EDATA);
	event_ts.sec_lo = ext_read(master, PAGE4, PTP_EDATA);
	event_ts.sec_hi = ext_read(master, PAGE4, PTP_EDATA);
	now = phy2txts(&event_ts);

	list_for_each(this, &clock->phylist) {
		tmp = list_entry(this, struct dp83640_private, list);
		val = ext_read(tmp->phydev, PAGE4, PTP_STS);
		pr_info("slave  PTP_STS  0x%04hx\n", val);
		val = ext_read(tmp->phydev, PAGE4, PTP_ESTS);
		pr_info("slave  PTP_ESTS 0x%04hx\n", val);
		event_ts.ns_lo  = ext_read(tmp->phydev, PAGE4, PTP_EDATA);
		event_ts.ns_hi  = ext_read(tmp->phydev, PAGE4, PTP_EDATA);
		event_ts.sec_lo = ext_read(tmp->phydev, PAGE4, PTP_EDATA);
		event_ts.sec_hi = ext_read(tmp->phydev, PAGE4, PTP_EDATA);
		diff = now - (s64) phy2txts(&event_ts);
		pr_info("slave offset %lld nanoseconds\n", diff);
		diff += ADJTIME_FIX;
		ts = ns_to_timespec64(diff);
		tdr_write(0, tmp->phydev, &ts, PTP_STEP_CLK);
	}

	/*
	 * restore status frames
	 */
	list_for_each(this, &clock->phylist) {
		tmp = list_entry(this, struct dp83640_private, list);
		ext_write(0, tmp->phydev, PAGE5, PSF_CFG0, tmp->cfg0);
	}
	ext_write(0, master, PAGE5, PSF_CFG0, cfg0);

	mutex_unlock(&clock->extreg_lock);
}

/* time stamping methods */

static inline u16 exts_chan_to_edata(int ch)
{
	return 1 << ((ch + EXT_EVENT) * 2);
}

static int decode_evnt(struct dp83640_private *dp83640,
		       void *data, int len, u16 ests)
{
	struct phy_txts *phy_txts;
	struct ptp_clock_event event;
	int i, parsed;
	int words = (ests >> EVNT_TS_LEN_SHIFT) & EVNT_TS_LEN_MASK;
	u16 ext_status = 0;

	/* calculate length of the event timestamp status message */
	if (ests & MULT_EVNT)
		parsed = (words + 2) * sizeof(u16);
	else
		parsed = (words + 1) * sizeof(u16);

	/* check if enough data is available */
	if (len < parsed)
		return len;

	if (ests & MULT_EVNT) {
		ext_status = *(u16 *) data;
		data += sizeof(ext_status);
	}

	phy_txts = data;

	switch (words) { /* fall through in every case */
	case 3:
		dp83640->edata.sec_hi = phy_txts->sec_hi;
	case 2:
		dp83640->edata.sec_lo = phy_txts->sec_lo;
	case 1:
		dp83640->edata.ns_hi = phy_txts->ns_hi;
	case 0:
		dp83640->edata.ns_lo = phy_txts->ns_lo;
	}

	if (!ext_status) {
		i = ((ests >> EVNT_NUM_SHIFT) & EVNT_NUM_MASK) - EXT_EVENT;
		ext_status = exts_chan_to_edata(i);
	}

	event.type = PTP_CLOCK_EXTTS;
	event.timestamp = phy2txts(&dp83640->edata);

	/* Compensate for input path and synchronization delays */
	event.timestamp -= 35;

	for (i = 0; i < N_EXT_TS; i++) {
		if (ext_status & exts_chan_to_edata(i)) {
			event.index = i;
			ptp_clock_event(dp83640->clock->ptp_clock, &event);
		}
	}

	return parsed;
}

#define DP83640_PACKET_HASH_OFFSET	20
#define DP83640_PACKET_HASH_LEN		10

static int match(struct sk_buff *skb, unsigned int type, struct rxts *rxts)
{
	u16 *seqid, hash;
	unsigned int offset = 0;
	u8 *msgtype, *data = skb_mac_header(skb);

	/* check sequenceID, messageType, 12 bit hash of offset 20-29 */

	if (type & PTP_CLASS_VLAN)
		offset += VLAN_HLEN;

	switch (type & PTP_CLASS_PMASK) {
	case PTP_CLASS_IPV4:
		offset += ETH_HLEN + IPV4_HLEN(data + offset) + UDP_HLEN;
		break;
	case PTP_CLASS_IPV6:
		offset += ETH_HLEN + IP6_HLEN + UDP_HLEN;
		break;
	case PTP_CLASS_L2:
		offset += ETH_HLEN;
		break;
	default:
		return 0;
	}

	if (skb->len + ETH_HLEN < offset + OFF_PTP_SEQUENCE_ID + sizeof(*seqid))
		return 0;

	if (unlikely(type & PTP_CLASS_V1))
		msgtype = data + offset + OFF_PTP_CONTROL;
	else
		msgtype = data + offset;
	if (rxts->msgtype != (*msgtype & 0xf))
		return 0;

	seqid = (u16 *)(data + offset + OFF_PTP_SEQUENCE_ID);
	if (rxts->seqid != ntohs(*seqid))
		return 0;

	hash = ether_crc(DP83640_PACKET_HASH_LEN,
			 data + offset + DP83640_PACKET_HASH_OFFSET) >> 20;
	if (rxts->hash != hash)
		return 0;

	return 1;
}

static void decode_rxts(struct dp83640_private *dp83640,
			struct phy_rxts *phy_rxts)
{
	struct rxts *rxts;
	struct skb_shared_hwtstamps *shhwtstamps = NULL;
	struct sk_buff *skb;
	unsigned long flags;
	u8 overflow;

	overflow = (phy_rxts->ns_hi >> 14) & 0x3;
	if (overflow)
		pr_debug("rx timestamp queue overflow, count %d\n", overflow);

	spin_lock_irqsave(&dp83640->rx_lock, flags);

	prune_rx_ts(dp83640);

	if (list_empty(&dp83640->rxpool)) {
		pr_debug("rx timestamp pool is empty\n");
		goto out;
	}
	rxts = list_first_entry(&dp83640->rxpool, struct rxts, list);
	list_del_init(&rxts->list);
	phy2rxts(phy_rxts, rxts);

	spin_lock(&dp83640->rx_queue.lock);
	skb_queue_walk(&dp83640->rx_queue, skb) {
		struct dp83640_skb_info *skb_info;

		skb_info = (struct dp83640_skb_info *)skb->cb;
		if (match(skb, skb_info->ptp_type, rxts)) {
			__skb_unlink(skb, &dp83640->rx_queue);
			shhwtstamps = skb_hwtstamps(skb);
			memset(shhwtstamps, 0, sizeof(*shhwtstamps));
			shhwtstamps->hwtstamp = ns_to_ktime(rxts->ns);
			netif_rx_ni(skb);
			list_add(&rxts->list, &dp83640->rxpool);
			break;
		}
	}
	spin_unlock(&dp83640->rx_queue.lock);

	if (!shhwtstamps)
		list_add_tail(&rxts->list, &dp83640->rxts);
out:
	spin_unlock_irqrestore(&dp83640->rx_lock, flags);
}

static void decode_txts(struct dp83640_private *dp83640,
			struct phy_txts *phy_txts)
{
	struct skb_shared_hwtstamps shhwtstamps;
	struct sk_buff *skb;
	u64 ns;
	u8 overflow;

	/* We must already have the skb that triggered this. */

	skb = skb_dequeue(&dp83640->tx_queue);

	if (!skb) {
		pr_debug("have timestamp but tx_queue empty\n");
		return;
	}

	overflow = (phy_txts->ns_hi >> 14) & 0x3;
	if (overflow) {
		pr_debug("tx timestamp queue overflow, count %d\n", overflow);
		while (skb) {
			kfree_skb(skb);
			skb = skb_dequeue(&dp83640->tx_queue);
		}
		return;
	}

	ns = phy2txts(phy_txts);
	memset(&shhwtstamps, 0, sizeof(shhwtstamps));
	shhwtstamps.hwtstamp = ns_to_ktime(ns);
	skb_complete_tx_timestamp(skb, &shhwtstamps);
}

static void decode_status_frame(struct dp83640_private *dp83640,
				struct sk_buff *skb)
{
	struct phy_rxts *phy_rxts;
	struct phy_txts *phy_txts;
	u8 *ptr;
	int len, size;
	u16 ests, type;

	ptr = skb->data + 2;

	for (len = skb_headlen(skb) - 2; len > sizeof(type); len -= size) {

		type = *(u16 *)ptr;
		ests = type & 0x0fff;
		type = type & 0xf000;
		len -= sizeof(type);
		ptr += sizeof(type);

		if (PSF_RX == type && len >= sizeof(*phy_rxts)) {

			phy_rxts = (struct phy_rxts *) ptr;
			decode_rxts(dp83640, phy_rxts);
			size = sizeof(*phy_rxts);

		} else if (PSF_TX == type && len >= sizeof(*phy_txts)) {

			phy_txts = (struct phy_txts *) ptr;
			decode_txts(dp83640, phy_txts);
			size = sizeof(*phy_txts);

		} else if (PSF_EVNT == type) {

			size = decode_evnt(dp83640, ptr, len, ests);

		} else {
			size = 0;
			break;
		}
		ptr += size;
	}
}

static int is_sync(struct sk_buff *skb, int type)
{
	u8 *data = skb->data, *msgtype;
	unsigned int offset = 0;

	if (type & PTP_CLASS_VLAN)
		offset += VLAN_HLEN;

	switch (type & PTP_CLASS_PMASK) {
	case PTP_CLASS_IPV4:
		offset += ETH_HLEN + IPV4_HLEN(data + offset) + UDP_HLEN;
		break;
	case PTP_CLASS_IPV6:
		offset += ETH_HLEN + IP6_HLEN + UDP_HLEN;
		break;
	case PTP_CLASS_L2:
		offset += ETH_HLEN;
		break;
	default:
		return 0;
	}

	if (type & PTP_CLASS_V1)
		offset += OFF_PTP_CONTROL;

	if (skb->len < offset + 1)
		return 0;

	msgtype = data + offset;

	return (*msgtype & 0xf) == 0;
}

static void dp83640_free_clocks(void)
{
	struct dp83640_clock *clock;
	struct list_head *this, *next;

	mutex_lock(&phyter_clocks_lock);

	list_for_each_safe(this, next, &phyter_clocks) {
		clock = list_entry(this, struct dp83640_clock, list);
		if (!list_empty(&clock->phylist)) {
			pr_warn("phy list non-empty while unloading\n");
			BUG();
		}
		list_del(&clock->list);
		mutex_destroy(&clock->extreg_lock);
		mutex_destroy(&clock->clock_lock);
		put_device(&clock->bus->dev);
		kfree(clock->caps.pin_config);
		kfree(clock);
	}

	mutex_unlock(&phyter_clocks_lock);
}

static void dp83640_clock_init(struct dp83640_clock *clock, struct mii_bus *bus)
{
	INIT_LIST_HEAD(&clock->list);
	clock->bus = bus;
	mutex_init(&clock->extreg_lock);
	mutex_init(&clock->clock_lock);
	INIT_LIST_HEAD(&clock->phylist);
	clock->caps.owner = THIS_MODULE;
	sprintf(clock->caps.name, "dp83640 timer");
	clock->caps.max_adj	= 1953124;
	clock->caps.n_alarm	= 0;
	clock->caps.n_ext_ts	= N_EXT_TS;
	clock->caps.n_per_out	= N_PER_OUT;
	clock->caps.n_pins	= DP83640_N_PINS;
	clock->caps.pps		= 0;
	clock->caps.adjfreq	= ptp_dp83640_adjfreq;
	clock->caps.adjtime	= ptp_dp83640_adjtime;
	clock->caps.gettime64	= ptp_dp83640_gettime;
	clock->caps.settime64	= ptp_dp83640_settime;
	clock->caps.enable	= ptp_dp83640_enable;
	clock->caps.verify	= ptp_dp83640_verify;
	/*
	 * Convert the module param defaults into a dynamic pin configuration.
	 */
	dp83640_gpio_defaults(clock->caps.pin_config);
	/*
	 * Get a reference to this bus instance.
	 */
	get_device(&bus->dev);
}

static int choose_this_phy(struct dp83640_clock *clock,
			   struct phy_device *phydev)
{
	if (chosen_phy == -1 && !clock->chosen)
		return 1;

	if (chosen_phy == phydev->addr)
		return 1;

	return 0;
}

static struct dp83640_clock *dp83640_clock_get(struct dp83640_clock *clock)
{
	if (clock)
		mutex_lock(&clock->clock_lock);
	return clock;
}

/*
 * Look up and lock a clock by bus instance.
 * If there is no clock for this bus, then create it first.
 */
static struct dp83640_clock *dp83640_clock_get_bus(struct mii_bus *bus)
{
	struct dp83640_clock *clock = NULL, *tmp;
	struct list_head *this;

	mutex_lock(&phyter_clocks_lock);

	list_for_each(this, &phyter_clocks) {
		tmp = list_entry(this, struct dp83640_clock, list);
		if (tmp->bus == bus) {
			clock = tmp;
			break;
		}
	}
	if (clock)
		goto out;

	clock = kzalloc(sizeof(struct dp83640_clock), GFP_KERNEL);
	if (!clock)
		goto out;

	clock->caps.pin_config = kzalloc(sizeof(struct ptp_pin_desc) *
					 DP83640_N_PINS, GFP_KERNEL);
	if (!clock->caps.pin_config) {
		kfree(clock);
		clock = NULL;
		goto out;
	}
	dp83640_clock_init(clock, bus);
	list_add_tail(&phyter_clocks, &clock->list);
out:
	mutex_unlock(&phyter_clocks_lock);

	return dp83640_clock_get(clock);
}

static void dp83640_clock_put(struct dp83640_clock *clock)
{
	mutex_unlock(&clock->clock_lock);
}

static int dp83640_probe(struct phy_device *phydev)
{
	struct dp83640_clock *clock;
	struct dp83640_private *dp83640;
	int err = -ENOMEM, i;

	if (phydev->addr == BROADCAST_ADDR)
		return 0;

	clock = dp83640_clock_get_bus(phydev->bus);
	if (!clock)
		goto no_clock;

	dp83640 = kzalloc(sizeof(struct dp83640_private), GFP_KERNEL);
	if (!dp83640)
		goto no_memory;

	dp83640->phydev = phydev;
	INIT_DELAYED_WORK(&dp83640->ts_work, rx_timestamp_work);

	INIT_LIST_HEAD(&dp83640->rxts);
	INIT_LIST_HEAD(&dp83640->rxpool);
	for (i = 0; i < MAX_RXTS; i++)
		list_add(&dp83640->rx_pool_data[i].list, &dp83640->rxpool);

	phydev->priv = dp83640;

	spin_lock_init(&dp83640->rx_lock);
	skb_queue_head_init(&dp83640->rx_queue);
	skb_queue_head_init(&dp83640->tx_queue);

	dp83640->clock = clock;

	if (choose_this_phy(clock, phydev)) {
		clock->chosen = dp83640;
		clock->ptp_clock = ptp_clock_register(&clock->caps, &phydev->dev);
		if (IS_ERR(clock->ptp_clock)) {
			err = PTR_ERR(clock->ptp_clock);
			goto no_register;
		}
	} else
		list_add_tail(&dp83640->list, &clock->phylist);

	dp83640_clock_put(clock);
	return 0;

no_register:
	clock->chosen = NULL;
	kfree(dp83640);
no_memory:
	dp83640_clock_put(clock);
no_clock:
	return err;
}

static void dp83640_remove(struct phy_device *phydev)
{
	struct dp83640_clock *clock;
	struct list_head *this, *next;
	struct dp83640_private *tmp, *dp83640 = phydev->priv;

	if (phydev->addr == BROADCAST_ADDR)
		return;

	enable_status_frames(phydev, false);
	cancel_delayed_work_sync(&dp83640->ts_work);

	skb_queue_purge(&dp83640->rx_queue);
	skb_queue_purge(&dp83640->tx_queue);

	clock = dp83640_clock_get(dp83640->clock);

	if (dp83640 == clock->chosen) {
		ptp_clock_unregister(clock->ptp_clock);
		clock->chosen = NULL;
	} else {
		list_for_each_safe(this, next, &clock->phylist) {
			tmp = list_entry(this, struct dp83640_private, list);
			if (tmp == dp83640) {
				list_del_init(&tmp->list);
				break;
			}
		}
	}

	dp83640_clock_put(clock);
	kfree(dp83640);
}

static int dp83640_config_init(struct phy_device *phydev)
{
	struct dp83640_private *dp83640 = phydev->priv;
	struct dp83640_clock *clock = dp83640->clock;

	if (clock->chosen && !list_empty(&clock->phylist))
		recalibrate(clock);
	else {
		mutex_lock(&clock->extreg_lock);
		enable_broadcast(phydev, clock->page, 1);
		mutex_unlock(&clock->extreg_lock);
	}

	enable_status_frames(phydev, true);

	mutex_lock(&clock->extreg_lock);
	ext_write(0, phydev, PAGE4, PTP_CTL, PTP_ENABLE);
	mutex_unlock(&clock->extreg_lock);

	return 0;
}

static int dp83640_ack_interrupt(struct phy_device *phydev)
{
	int err = phy_read(phydev, MII_DP83640_MISR);

	if (err < 0)
		return err;

	return 0;
}

static int dp83640_config_intr(struct phy_device *phydev)
{
	int micr;
	int misr;
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		misr = phy_read(phydev, MII_DP83640_MISR);
		if (misr < 0)
			return misr;
		misr |=
			(MII_DP83640_MISR_ANC_INT_EN |
			MII_DP83640_MISR_DUP_INT_EN |
			MII_DP83640_MISR_SPD_INT_EN |
			MII_DP83640_MISR_LINK_INT_EN);
		err = phy_write(phydev, MII_DP83640_MISR, misr);
		if (err < 0)
			return err;

		micr = phy_read(phydev, MII_DP83640_MICR);
		if (micr < 0)
			return micr;
		micr |=
			(MII_DP83640_MICR_OE |
			MII_DP83640_MICR_IE);
		return phy_write(phydev, MII_DP83640_MICR, micr);
	} else {
		micr = phy_read(phydev, MII_DP83640_MICR);
		if (micr < 0)
			return micr;
		micr &=
			~(MII_DP83640_MICR_OE |
			MII_DP83640_MICR_IE);
		err = phy_write(phydev, MII_DP83640_MICR, micr);
		if (err < 0)
			return err;

		misr = phy_read(phydev, MII_DP83640_MISR);
		if (misr < 0)
			return misr;
		misr &=
			~(MII_DP83640_MISR_ANC_INT_EN |
			MII_DP83640_MISR_DUP_INT_EN |
			MII_DP83640_MISR_SPD_INT_EN |
			MII_DP83640_MISR_LINK_INT_EN);
		return phy_write(phydev, MII_DP83640_MISR, misr);
	}
}

static int dp83640_hwtstamp(struct phy_device *phydev, struct ifreq *ifr)
{
	struct dp83640_private *dp83640 = phydev->priv;
	struct hwtstamp_config cfg;
	u16 txcfg0, rxcfg0;

	if (copy_from_user(&cfg, ifr->ifr_data, sizeof(cfg)))
		return -EFAULT;

	if (cfg.flags) /* reserved for future extensions */
		return -EINVAL;

	if (cfg.tx_type < 0 || cfg.tx_type > HWTSTAMP_TX_ONESTEP_SYNC)
		return -ERANGE;

	dp83640->hwts_tx_en = cfg.tx_type;

	switch (cfg.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		dp83640->hwts_rx_en = 0;
		dp83640->layer = 0;
		dp83640->version = 0;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		dp83640->hwts_rx_en = 1;
		dp83640->layer = PTP_CLASS_L4;
		dp83640->version = PTP_CLASS_V1;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		dp83640->hwts_rx_en = 1;
		dp83640->layer = PTP_CLASS_L4;
		dp83640->version = PTP_CLASS_V2;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		dp83640->hwts_rx_en = 1;
		dp83640->layer = PTP_CLASS_L2;
		dp83640->version = PTP_CLASS_V2;
		break;
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		dp83640->hwts_rx_en = 1;
		dp83640->layer = PTP_CLASS_L4 | PTP_CLASS_L2;
		dp83640->version = PTP_CLASS_V2;
		break;
	default:
		return -ERANGE;
	}

	txcfg0 = (dp83640->version & TX_PTP_VER_MASK) << TX_PTP_VER_SHIFT;
	rxcfg0 = (dp83640->version & TX_PTP_VER_MASK) << TX_PTP_VER_SHIFT;

	if (dp83640->layer & PTP_CLASS_L2) {
		txcfg0 |= TX_L2_EN;
		rxcfg0 |= RX_L2_EN;
	}
	if (dp83640->layer & PTP_CLASS_L4) {
		txcfg0 |= TX_IPV6_EN | TX_IPV4_EN;
		rxcfg0 |= RX_IPV6_EN | RX_IPV4_EN;
	}

	if (dp83640->hwts_tx_en)
		txcfg0 |= TX_TS_EN;

	if (dp83640->hwts_tx_en == HWTSTAMP_TX_ONESTEP_SYNC)
		txcfg0 |= SYNC_1STEP | CHK_1STEP;

	if (dp83640->hwts_rx_en)
		rxcfg0 |= RX_TS_EN;

	mutex_lock(&dp83640->clock->extreg_lock);

	ext_write(0, phydev, PAGE5, PTP_TXCFG0, txcfg0);
	ext_write(0, phydev, PAGE5, PTP_RXCFG0, rxcfg0);

	mutex_unlock(&dp83640->clock->extreg_lock);

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}

static void rx_timestamp_work(struct work_struct *work)
{
	struct dp83640_private *dp83640 =
		container_of(work, struct dp83640_private, ts_work.work);
	struct sk_buff *skb;

	/* Deliver expired packets. */
	while ((skb = skb_dequeue(&dp83640->rx_queue))) {
		struct dp83640_skb_info *skb_info;

		skb_info = (struct dp83640_skb_info *)skb->cb;
		if (!time_after(jiffies, skb_info->tmo)) {
			skb_queue_head(&dp83640->rx_queue, skb);
			break;
		}

		netif_rx_ni(skb);
	}

	if (!skb_queue_empty(&dp83640->rx_queue))
		schedule_delayed_work(&dp83640->ts_work, SKB_TIMESTAMP_TIMEOUT);
}

static bool dp83640_rxtstamp(struct phy_device *phydev,
			     struct sk_buff *skb, int type)
{
	struct dp83640_private *dp83640 = phydev->priv;
	struct dp83640_skb_info *skb_info = (struct dp83640_skb_info *)skb->cb;
	struct list_head *this, *next;
	struct rxts *rxts;
	struct skb_shared_hwtstamps *shhwtstamps = NULL;
	unsigned long flags;

	if (is_status_frame(skb, type)) {
		decode_status_frame(dp83640, skb);
		kfree_skb(skb);
		return true;
	}

	if (!dp83640->hwts_rx_en)
		return false;

	if ((type & dp83640->version) == 0 || (type & dp83640->layer) == 0)
		return false;

	spin_lock_irqsave(&dp83640->rx_lock, flags);
	prune_rx_ts(dp83640);
	list_for_each_safe(this, next, &dp83640->rxts) {
		rxts = list_entry(this, struct rxts, list);
		if (match(skb, type, rxts)) {
			shhwtstamps = skb_hwtstamps(skb);
			memset(shhwtstamps, 0, sizeof(*shhwtstamps));
			shhwtstamps->hwtstamp = ns_to_ktime(rxts->ns);
			netif_rx_ni(skb);
			list_del_init(&rxts->list);
			list_add(&rxts->list, &dp83640->rxpool);
			break;
		}
	}
	spin_unlock_irqrestore(&dp83640->rx_lock, flags);

	if (!shhwtstamps) {
		skb_info->ptp_type = type;
		skb_info->tmo = jiffies + SKB_TIMESTAMP_TIMEOUT;
		skb_queue_tail(&dp83640->rx_queue, skb);
		schedule_delayed_work(&dp83640->ts_work, SKB_TIMESTAMP_TIMEOUT);
	}

	return true;
}

static void dp83640_txtstamp(struct phy_device *phydev,
			     struct sk_buff *skb, int type)
{
	struct dp83640_private *dp83640 = phydev->priv;

	switch (dp83640->hwts_tx_en) {

	case HWTSTAMP_TX_ONESTEP_SYNC:
		if (is_sync(skb, type)) {
			kfree_skb(skb);
			return;
		}
		/* fall through */
	case HWTSTAMP_TX_ON:
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		skb_queue_tail(&dp83640->tx_queue, skb);
		break;

	case HWTSTAMP_TX_OFF:
	default:
		kfree_skb(skb);
		break;
	}
}

static int dp83640_ts_info(struct phy_device *dev, struct ethtool_ts_info *info)
{
	struct dp83640_private *dp83640 = dev->priv;

	info->so_timestamping =
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;
	info->phc_index = ptp_clock_index(dp83640->clock->ptp_clock);
	info->tx_types =
		(1 << HWTSTAMP_TX_OFF) |
		(1 << HWTSTAMP_TX_ON) |
		(1 << HWTSTAMP_TX_ONESTEP_SYNC);
	info->rx_filters =
		(1 << HWTSTAMP_FILTER_NONE) |
		(1 << HWTSTAMP_FILTER_PTP_V1_L4_EVENT) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
		(1 << HWTSTAMP_FILTER_PTP_V2_EVENT);
	return 0;
}

static struct phy_driver dp83640_driver = {
	.phy_id		= DP83640_PHY_ID,
	.phy_id_mask	= 0xfffffff0,
	.name		= "NatSemi DP83640",
	.features	= PHY_BASIC_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,
	.probe		= dp83640_probe,
	.remove		= dp83640_remove,
	.config_init	= dp83640_config_init,
	.config_aneg	= genphy_config_aneg,
	.read_status	= genphy_read_status,
	.ack_interrupt  = dp83640_ack_interrupt,
	.config_intr    = dp83640_config_intr,
	.ts_info	= dp83640_ts_info,
	.hwtstamp	= dp83640_hwtstamp,
	.rxtstamp	= dp83640_rxtstamp,
	.txtstamp	= dp83640_txtstamp,
	.driver		= {.owner = THIS_MODULE,}
};

static int __init dp83640_init(void)
{
	return phy_driver_register(&dp83640_driver);
}

static void __exit dp83640_exit(void)
{
	dp83640_free_clocks();
	phy_driver_unregister(&dp83640_driver);
}

MODULE_DESCRIPTION("National Semiconductor DP83640 PHY driver");
MODULE_AUTHOR("Richard Cochran <richardcochran@gmail.com>");
MODULE_LICENSE("GPL");

module_init(dp83640_init);
module_exit(dp83640_exit);

static struct mdio_device_id __maybe_unused dp83640_tbl[] = {
	{ DP83640_PHY_ID, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, dp83640_tbl);
