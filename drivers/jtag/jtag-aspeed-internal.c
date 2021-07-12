// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * JTAG driver for the Aspeed SoC
 *
 * Copyright (C) 2021 ASPEED Technology Inc.
 * Ryan Chen <ryan_chen@aspeedtech.com>
 *
 */
#include <linux/poll.h>
#include <linux/sysfs.h>
#include <linux/clk.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/jtag.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/uaccess.h>
#include <uapi/linux/jtag.h>
/******************************************************************************/
#define ASPEED_JTAG_DATA		0x00
#define ASPEED_JTAG_INST		0x04
#define ASPEED_JTAG_CTRL		0x08
#define ASPEED_JTAG_ISR			0x0C
#define ASPEED_JTAG_SW			0x10
#define ASPEED_JTAG_TCK			0x14
#define ASPEED_JTAG_IDLE		0x18

/* ASPEED_JTAG_CTRL - 0x08 : Engine Control */
#define JTAG_ENG_EN			BIT(31)
#define JTAG_ENG_OUT_EN			BIT(30)
#define JTAG_FORCE_TMS			BIT(29)

#define JTAG_IR_UPDATE			BIT(26)		//AST2500 only

#define JTAG_G6_RESET_FIFO		BIT(21)		//AST2600 only
#define JTAG_G6_CTRL_MODE		BIT(20)		//AST2600 only
#define JTAG_G6_XFER_LEN_MASK		(0x3ff << 8)	//AST2600 only
#define JTAG_G6_SET_XFER_LEN(x)		(x << 8)
#define JTAG_G6_MSB_FIRST		BIT(6)		//AST2600 only
#define JTAG_G6_TERMINATE_XFER		BIT(5)		//AST2600 only
#define JTAG_G6_LAST_XFER		BIT(4)		//AST2600 only
#define JTAG_G6_INST_EN			BIT(1)

#define JTAG_INST_LEN_MASK		(0x3f << 20)
#define JTAG_SET_INST_LEN(x)		(x << 20)
#define JTAG_SET_INST_MSB		BIT(19)
#define JTAG_TERMINATE_INST		BIT(18)
#define JTAG_LAST_INST			BIT(17)
#define JTAG_INST_EN			BIT(16)
#define JTAG_DATA_LEN_MASK		(0x3f << 4)

#define JTAG_DR_UPDATE			BIT(10)		//AST2500 only
#define JTAG_DATA_LEN(x)		(x << 4)
#define JTAG_MSB_FIRST			BIT(3)
#define JTAG_TERMINATE_DATA		BIT(2)
#define JTAG_LAST_DATA			BIT(1)
#define JTAG_DATA_EN			BIT(0)

/* ASPEED_JTAG_ISR	- 0x0C : INterrupt status and enable */
#define JTAG_INST_PAUSE			BIT(19)
#define JTAG_INST_COMPLETE		BIT(18)
#define JTAG_DATA_PAUSE			BIT(17)
#define JTAG_DATA_COMPLETE		BIT(16)

#define JTAG_INST_PAUSE_EN		BIT(3)
#define JTAG_INST_COMPLETE_EN		BIT(2)
#define JTAG_DATA_PAUSE_EN		BIT(1)
#define JTAG_DATA_COMPLETE_EN		BIT(0)

/* ASPEED_JTAG_SW	- 0x10 : Software Mode and Status */
#define JTAG_SW_MODE_EN			BIT(19)
#define JTAG_SW_MODE_TCK		BIT(18)
#define JTAG_SW_MODE_TMS		BIT(17)
#define JTAG_SW_MODE_TDIO		BIT(16)
//
#define JTAG_STS_INST_PAUSE		BIT(2)
#define JTAG_STS_DATA_PAUSE		BIT(1)
#define JTAG_STS_ENG_IDLE		(0x1)

/* ASPEED_JTAG_TCK	- 0x14 : TCK Control */
#define JTAG_TCK_INVERSE		BIT(31)
#define JTAG_TCK_DIVISOR_MASK		(0x7ff)
#define JTAG_GET_TCK_DIVISOR(x)		(x & 0x7ff)

/*  ASPEED_JTAG_IDLE - 0x18 : Ctroller set for go to IDLE */
#define JTAG_CTRL_TRSTn_HIGH		BIT(31)
#define JTAG_GO_IDLE			BIT(0)

#define TCK_FREQ			1000000
/******************************************************************************/
#define ASPEED_JTAG_DEBUG

#ifdef ASPEED_JTAG_DEBUG
#define JTAG_DBUG(fmt, args...)                                                \
	pr_debug("%s() " fmt, __func__, ##args)
#else
#define JTAG_DBUG(fmt, args...)
#endif

struct aspeed_jtag_config {
	u8	jtag_version;
	u32	jtag_buff_len;
};

struct aspeed_jtag_info {
	void __iomem			*reg_base;
	struct device			*dev;
	struct aspeed_jtag_config	*config;
	enum jtag_tapstate		sts;
	int				irq;
	struct reset_control		*reset;
	struct clk			*clk;
	u32				clkin;
	u32				tck_period;
	u32				sw_delay;
	u32				flag;
	wait_queue_head_t		jtag_wq;
	u32				mode;
};

/*
 * This structure represents a TMS cycle, as expressed in a set of bits and a
 * count of bits (note: there are no start->end state transitions that require
 * more than 1 byte of TMS cycles)
 */
struct tms_cycle {
	unsigned char		tmsbits;
	unsigned char		count;
};

/*
 * This is the complete set TMS cycles for going from any TAP state to any
 * other TAP state, following a "shortest path" rule.
 */
static const struct tms_cycle _tms_cycle_lookup[][16] = {
/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* TLR  */{{0x1f, 5}, {0x00, 1}, {0x02, 2}, {0x02, 3}, {0x02, 4}, {0x0a, 4},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x0a, 5}, {0x2a, 6}, {0x1a, 5}, {0x06, 3}, {0x06, 4}, {0x06, 5},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x16, 5}, {0x16, 6}, {0x56, 7}, {0x36, 6} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* RTI  */{{0x1f, 5}, {0x00, 0}, {0x01, 1}, {0x01, 2}, {0x01, 3}, {0x05, 3},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x05, 4}, {0x15, 5}, {0x0d, 4}, {0x03, 2}, {0x03, 3}, {0x03, 4},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x0b, 4}, {0x0b, 5}, {0x2b, 6}, {0x1b, 5} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* SelDR*/{{0x1f, 5}, {0x03, 3}, {0x00, 0}, {0x00, 1}, {0x00, 2}, {0x02, 2},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x02, 3}, {0x0a, 4}, {0x06, 3}, {0x01, 1}, {0x01, 2}, {0x01, 3},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x05, 3}, {0x05, 4}, {0x15, 5}, {0x0d, 4} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* CapDR*/{{0x1f, 5}, {0x03, 3}, {0x07, 3}, {0x00, 0}, {0x00, 1}, {0x01, 1},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x01, 2}, {0x05, 3}, {0x03, 2}, {0x0f, 4}, {0x0f, 5}, {0x0f, 6},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x2f, 6}, {0x2f, 7}, {0xaf, 8}, {0x6f, 7} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* SDR  */{{0x1f, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x00, 0}, {0x01, 1},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x01, 2}, {0x05, 3}, {0x03, 2}, {0x0f, 4}, {0x0f, 5}, {0x0f, 6},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x2f, 6}, {0x2f, 7}, {0xaf, 8}, {0x6f, 7} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* Ex1DR*/{{0x1f, 5}, {0x01, 2}, {0x03, 2}, {0x03, 3}, {0x02, 3}, {0x00, 0},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x00, 1}, {0x02, 2}, {0x01, 1}, {0x07, 3}, {0x07, 4}, {0x07, 5},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x17, 5}, {0x17, 6}, {0x57, 7}, {0x37, 6} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* PDR  */{{0x1f, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x01, 2}, {0x05, 3},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x00, 0}, {0x01, 1}, {0x03, 2}, {0x0f, 4}, {0x0f, 5}, {0x0f, 6},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x2f, 6}, {0x2f, 7}, {0xaf, 8}, {0x6f, 7} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* Ex2DR*/{{0x1f, 5}, {0x01, 2}, {0x03, 2}, {0x03, 3}, {0x00, 1}, {0x02, 2},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x02, 3}, {0x00, 0}, {0x01, 1}, {0x07, 3}, {0x07, 4}, {0x07, 5},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x17, 5}, {0x17, 6}, {0x57, 7}, {0x37, 6} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* UpdDR*/{{0x1f, 5}, {0x00, 1}, {0x01, 1}, {0x01, 2}, {0x01, 3}, {0x05, 3},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x05, 4}, {0x15, 5}, {0x00, 0}, {0x03, 2}, {0x03, 3}, {0x03, 4},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x0b, 4}, {0x0b, 5}, {0x2b, 6}, {0x1b, 5} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* SelIR*/{{0x1f, 5}, {0x01, 2}, {0x05, 3}, {0x05, 4}, {0x05, 5}, {0x15, 5},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x15, 6}, {0x55, 7}, {0x35, 6}, {0x00, 0}, {0x00, 1}, {0x00, 2},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x02, 2}, {0x02, 3}, {0x0a, 4}, {0x06, 3} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* CapIR*/{{0x1f, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x07, 5}, {0x17, 5},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x17, 6}, {0x57, 7}, {0x37, 6}, {0x0f, 4}, {0x00, 0}, {0x00, 1},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x01, 1}, {0x01, 2}, {0x05, 3}, {0x03, 2} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* SIR  */{{0x1f, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x07, 5}, {0x17, 5},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x17, 6}, {0x57, 7}, {0x37, 6}, {0x0f, 4}, {0x0f, 5}, {0x00, 0},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x01, 1}, {0x01, 2}, {0x05, 3}, {0x03, 2} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* Ex1IR*/{{0x1f, 5}, {0x01, 2}, {0x03, 2}, {0x03, 3}, {0x03, 4}, {0x0b, 4},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x0b, 5}, {0x2b, 6}, {0x1b, 5}, {0x07, 3}, {0x07, 4}, {0x02, 3},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x00, 0}, {0x00, 1}, {0x02, 2}, {0x01, 1} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* PIR  */{{0x1f, 5}, {0x03, 3}, {0x07, 3}, {0x07, 4}, {0x07, 5}, {0x17, 5},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x17, 6}, {0x57, 7}, {0x37, 6}, {0x0f, 4}, {0x0f, 5}, {0x01, 2},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x05, 3}, {0x00, 0}, {0x01, 1}, {0x03, 2} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* Ex2IR*/{{0x1f, 5}, {0x01, 2}, {0x03, 2}, {0x03, 3}, {0x03, 4}, {0x0b, 4},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x0b, 5}, {0x2b, 6}, {0x1b, 5}, {0x07, 3}, {0x07, 4}, {0x00, 1},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x02, 2}, {0x02, 3}, {0x00, 0}, {0x01, 1} },

/*	    TLR        RTI        SelDR      CapDR      SDR        Ex1DR*/
/* UpdIR*/{{0x1f, 5}, {0x00, 1}, {0x01, 1}, {0x01, 2}, {0x01, 3}, {0x05, 3},
/*	    PDR        Ex2DR      UpdDR      SelIR      CapIR      SIR*/
	    {0x05, 4}, {0x15, 5}, {0x0d, 4}, {0x03, 2}, {0x03, 3}, {0x03, 4},
/*	    Ex1IR      PIR        Ex2IR      UpdIR*/
	    {0x0b, 4}, {0x0b, 5}, {0x2b, 6}, {0x00, 0} },
};

/******************************************************************************/
static inline u32
aspeed_jtag_read(struct aspeed_jtag_info *aspeed_jtag, u32 reg)
{
	int val;

	val = readl(aspeed_jtag->reg_base + reg);
	JTAG_DBUG("reg = 0x%08x, val = 0x%08x\n", reg, val);
	return val;
}

static inline void
aspeed_jtag_write(struct aspeed_jtag_info *aspeed_jtag, u32 val, u32 reg)
{
	JTAG_DBUG("reg = 0x%08x, val = 0x%08x\n", reg, val);
	writel(val, aspeed_jtag->reg_base + reg);
}

/******************************************************************************/
static int aspeed_jtag_set_freq(struct jtag *jtag, u32 freq)
{
	struct aspeed_jtag_info *aspeed_jtag = jtag_priv(jtag);
	u32 div;

	/* SW mode frequency setting */
	aspeed_jtag->sw_delay = DIV_ROUND_UP(NSEC_PER_SEC, freq);
	JTAG_DBUG("sw mode delay = %d\n", aspeed_jtag->sw_delay);
	/*
	 * HW mode frequency setting
	 * AST2600: TCK period = Period of PCLK * (JTAG14[10:0] + 1)
	 * AST2500: TCK period = Period of PCLK * (JTAG14[10:0] + 1) * 2
	 */
	if (aspeed_jtag->config->jtag_version == 6)
		div = DIV_ROUND_CLOSEST(aspeed_jtag->clkin, freq);
	else
		div = DIV_ROUND_CLOSEST(aspeed_jtag->clkin, freq * 2);
	if (div >= 1)
		div = div - 1;
	if (div > JTAG_TCK_DIVISOR_MASK)
		div = JTAG_TCK_DIVISOR_MASK;
	JTAG_DBUG("%d target freq = %d div = %d", aspeed_jtag->clkin, freq,
		  div);
	/*
	 * HW constraint:
	 * AST2600 minimal TCK divisor = 7
	 * AST2500 minimal TCK divisor = 1
	 */
	if (aspeed_jtag->config->jtag_version == 6) {
		if (div < 7)
			div = 7;
		aspeed_jtag->tck_period = DIV_ROUND_UP_ULL(
			(u64)NSEC_PER_SEC * (div + 1), aspeed_jtag->clkin);
	} else if (aspeed_jtag->config->jtag_version == 0) {
		if (div < 1)
			div = 1;
		aspeed_jtag->tck_period = DIV_ROUND_UP_ULL(
			(u64)NSEC_PER_SEC * (div + 1) << 2, aspeed_jtag->clkin);
	}
	JTAG_DBUG("set div = %x, tck_period = %dns\n", div,
		  aspeed_jtag->tck_period);

	/*
	 * At ast2500: Change clock divider may cause hardware logic confusion.
	 * Enable software mode to assert the jtag hw logical before change
	 * clock divider.
	 */
	if (aspeed_jtag->config->jtag_version == 0)
		aspeed_jtag_write(aspeed_jtag,
				  JTAG_SW_MODE_EN |
					  aspeed_jtag_read(aspeed_jtag,
							   ASPEED_JTAG_SW),
				  ASPEED_JTAG_SW);
	aspeed_jtag_write(aspeed_jtag,
			  ((aspeed_jtag_read(aspeed_jtag, ASPEED_JTAG_TCK) &
			    ~JTAG_TCK_DIVISOR_MASK) |
			   div),
			  ASPEED_JTAG_TCK);
	if (aspeed_jtag->config->jtag_version == 0) {
		aspeed_jtag_write(aspeed_jtag, 0, ASPEED_JTAG_SW);
		aspeed_jtag->sts = JTAG_STATE_IDLE;
	}
	return 0;
}

static int aspeed_jtag_get_freq(struct jtag *jtag, u32 *freq)
{
	struct aspeed_jtag_info *aspeed_jtag = jtag_priv(jtag);

	if (aspeed_jtag->config->jtag_version == 6) {
		/* TCK period = Period of PCLK * (JTAG14[10:0] + 1) */
		*freq = aspeed_jtag->clkin /
		       (JTAG_GET_TCK_DIVISOR(aspeed_jtag_read(
				aspeed_jtag, ASPEED_JTAG_TCK)) + 1);
	} else if (aspeed_jtag->config->jtag_version == 0) {
		/* TCK period = Period of PCLK * (JTAG14[10:0] + 1) * 2 */
		*freq = (aspeed_jtag->clkin /
			(JTAG_GET_TCK_DIVISOR(aspeed_jtag_read(
				 aspeed_jtag, ASPEED_JTAG_TCK)) + 1)) >> 1;
	} else {
		/* unknown jtag version */
		*freq = 0;
	}
	return 0;
}
/******************************************************************************/
static u8 TCK_Cycle(struct aspeed_jtag_info *aspeed_jtag, u8 TMS, u8 TDI)
{
	u8 tdo;

	/* IEEE 1149.1
	 * TMS & TDI shall be sampled by the test logic on the rising edge
	 * test logic shall change TDO on the falling edge
	 */
	// TCK = 0
	aspeed_jtag_write(aspeed_jtag,
			  JTAG_SW_MODE_EN | (TMS * JTAG_SW_MODE_TMS) |
				  (TDI * JTAG_SW_MODE_TDIO),
			  ASPEED_JTAG_SW);

	/* Target device have their operating frequency*/
	ndelay(aspeed_jtag->sw_delay);

	// TCK = 1
	aspeed_jtag_write(aspeed_jtag,
			  JTAG_SW_MODE_EN | JTAG_SW_MODE_TCK |
				  (TMS * JTAG_SW_MODE_TMS) |
				  (TDI * JTAG_SW_MODE_TDIO),
			  ASPEED_JTAG_SW);

	ndelay(aspeed_jtag->sw_delay);
	/* Sampled TDI(slave, master's TDO) on the rising edge */
	if (aspeed_jtag_read(aspeed_jtag, ASPEED_JTAG_SW) & JTAG_SW_MODE_TDIO)
		tdo = 1;
	else
		tdo = 0;

	JTAG_DBUG("tms: %d tdi: %d tdo: %d", TMS, TDI, tdo);

	return tdo;
}

static int aspeed_jtag_sw_set_tap_state(struct aspeed_jtag_info *aspeed_jtag,
				      enum jtag_tapstate endstate)
{
	int i = 0;
	enum jtag_tapstate from, to;

	from = aspeed_jtag->sts;
	to = endstate;
	for (i = 0; i < _tms_cycle_lookup[from][to].count; i++)
		TCK_Cycle(aspeed_jtag,
			((_tms_cycle_lookup[from][to].tmsbits >> i) & 0x1), 0);
	aspeed_jtag->sts = endstate;
	JTAG_DBUG("go to %d", endstate);
	return 0;
}

/******************************************************************************/
static void aspeed_jtag_wait_instruction_pause_complete(
	struct aspeed_jtag_info *aspeed_jtag)
{
	wait_event_interruptible(aspeed_jtag->jtag_wq,
				 (aspeed_jtag->flag & JTAG_INST_PAUSE));
	JTAG_DBUG("\n");
	aspeed_jtag->flag &= ~JTAG_INST_PAUSE;
}
static void
aspeed_jtag_wait_instruction_complete(struct aspeed_jtag_info *aspeed_jtag)
{
	wait_event_interruptible(aspeed_jtag->jtag_wq,
				 (aspeed_jtag->flag & JTAG_INST_COMPLETE));
	JTAG_DBUG("\n");
	aspeed_jtag->flag &= ~JTAG_INST_COMPLETE;
}
static void
aspeed_jtag_wait_data_pause_complete(struct aspeed_jtag_info *aspeed_jtag)
{
	wait_event_interruptible(aspeed_jtag->jtag_wq,
				 (aspeed_jtag->flag & JTAG_DATA_PAUSE));
	JTAG_DBUG("\n");
	aspeed_jtag->flag &= ~JTAG_DATA_PAUSE;
}
static void aspeed_jtag_wait_data_complete(struct aspeed_jtag_info *aspeed_jtag)
{
	wait_event_interruptible(aspeed_jtag->jtag_wq,
				 (aspeed_jtag->flag & JTAG_DATA_COMPLETE));
	JTAG_DBUG("\n");
	aspeed_jtag->flag &= ~JTAG_DATA_COMPLETE;
}
static int aspeed_jtag_run_to_tlr(struct aspeed_jtag_info *aspeed_jtag)
{
	if (aspeed_jtag->sts == JTAG_STATE_PAUSEIR)
		aspeed_jtag_write(aspeed_jtag, JTAG_INST_COMPLETE_EN,
				ASPEED_JTAG_ISR);
	else if (aspeed_jtag->sts == JTAG_STATE_PAUSEDR)
		aspeed_jtag_write(aspeed_jtag, JTAG_DATA_COMPLETE_EN,
				  ASPEED_JTAG_ISR);
	aspeed_jtag_write(aspeed_jtag,
			  JTAG_ENG_EN | JTAG_ENG_OUT_EN | JTAG_FORCE_TMS,
			  ASPEED_JTAG_CTRL); // x TMS high + 1 TMS low
	if (aspeed_jtag->sts == JTAG_STATE_PAUSEIR)
		aspeed_jtag_wait_instruction_complete(aspeed_jtag);
	else if (aspeed_jtag->sts == JTAG_STATE_PAUSEDR)
		aspeed_jtag_wait_data_complete(aspeed_jtag);
	/* After that the fsm will go to idle state: hw constraint */
	aspeed_jtag->sts = JTAG_STATE_IDLE;
	return 0;
}

static int aspeed_jtag_run_to_idle(struct aspeed_jtag_info *aspeed_jtag)
{
	if (aspeed_jtag->sts == JTAG_STATE_IDLE) {
		/* nothing to do */
	} else if (aspeed_jtag->sts == JTAG_STATE_PAUSEDR) {
		aspeed_jtag_write(aspeed_jtag, JTAG_DATA_COMPLETE_EN,
					  ASPEED_JTAG_ISR);
		if (aspeed_jtag->config->jtag_version == 6) {
			aspeed_jtag_write(aspeed_jtag,
					JTAG_ENG_EN | JTAG_ENG_OUT_EN |
						JTAG_G6_TERMINATE_XFER |
						JTAG_DATA_EN,
					ASPEED_JTAG_CTRL);
		} else {
			aspeed_jtag_write(aspeed_jtag,
					  JTAG_ENG_EN | JTAG_ENG_OUT_EN |
						  JTAG_TERMINATE_DATA |
						  JTAG_DATA_EN,
					  ASPEED_JTAG_CTRL);
		}
		aspeed_jtag_wait_data_complete(aspeed_jtag);
	} else if (aspeed_jtag->sts == JTAG_STATE_PAUSEIR) {
		aspeed_jtag_write(aspeed_jtag, JTAG_INST_COMPLETE_EN,
					  ASPEED_JTAG_ISR);
		if (aspeed_jtag->config->jtag_version == 6) {
			aspeed_jtag_write(aspeed_jtag,
					JTAG_ENG_EN | JTAG_ENG_OUT_EN |
						JTAG_G6_TERMINATE_XFER |
						JTAG_G6_INST_EN,
					ASPEED_JTAG_CTRL);
		} else {
			aspeed_jtag_write(aspeed_jtag,
					JTAG_ENG_EN | JTAG_ENG_OUT_EN |
						JTAG_TERMINATE_INST |
						JTAG_INST_EN,
					ASPEED_JTAG_CTRL);
		}
		aspeed_jtag_wait_instruction_complete(aspeed_jtag);
	} else {
		pr_err("Should not get here unless aspeed_jtag->sts error!");
		return -EFAULT;
	}
	aspeed_jtag->sts = JTAG_STATE_IDLE;
	return 0;
}

static int aspeed_jtag_hw_set_tap_state(struct aspeed_jtag_info *aspeed_jtag,
				      enum jtag_tapstate endstate)
{
	int ret;

	aspeed_jtag_write(aspeed_jtag, 0, ASPEED_JTAG_SW); //dis sw mode
	mdelay(2);
	if (endstate == JTAG_STATE_TLRESET) {
		ret = aspeed_jtag_run_to_tlr(aspeed_jtag);
	} else if (endstate == JTAG_STATE_IDLE) {
		ret = aspeed_jtag_run_to_idle(aspeed_jtag);
	} else {
		/* other stable state will auto handle by hardware */
		return 0;
	}
	return ret;
}

/******************************************************************************/
/* JTAG_reset() is to generate at leaspeed 9 TMS high and
 * 1 TMS low to force devices into Run-Test/Idle State
 */
static int aspeed_jtag_status_set(struct jtag *jtag,
				  struct jtag_tap_state *tapstate)
{
	struct aspeed_jtag_info *aspeed_jtag = jtag_priv(jtag);
	int ret;
	uint32_t i;

	if (aspeed_jtag->mode == JTAG_XFER_HW_MODE) {
		if (tapstate->reset == JTAG_FORCE_RESET)
			aspeed_jtag_hw_set_tap_state(aspeed_jtag,
						     JTAG_STATE_TLRESET);
		ret = aspeed_jtag_hw_set_tap_state(aspeed_jtag,
						   tapstate->endstate);
	} else {
		if (tapstate->reset == JTAG_FORCE_RESET)
			aspeed_jtag_sw_set_tap_state(aspeed_jtag,
						     JTAG_STATE_TLRESET);
		ret = aspeed_jtag_sw_set_tap_state(aspeed_jtag,
						   tapstate->endstate);
	}
	if (ret)
		return ret;
	for (i = 0; i < tapstate->tck; i++)
		ndelay(aspeed_jtag->tck_period);
	return 0;
}

static int aspeed_jtag_status_get(struct jtag *jtag, u32 *status)
{
	struct aspeed_jtag_info *aspeed_jtag = jtag_priv(jtag);

	*status = aspeed_jtag->sts;
	return 0;
}
static void aspeed_sw_jtag_xfer(struct aspeed_jtag_info *aspeed_jtag,
				struct jtag_xfer *xfer, u8 *xfer_data)
{
	unsigned int index = 0;
	u32 shift_bits = 0;
	u8 tdi = 0, tdo = 0, tdo_buff = 0;
	u32 remain_xfer = xfer->length;

	if (xfer->type == JTAG_SIR_XFER)
		aspeed_jtag_sw_set_tap_state(aspeed_jtag, JTAG_STATE_SHIFTIR);
	else
		aspeed_jtag_sw_set_tap_state(aspeed_jtag, JTAG_STATE_SHIFTDR);

	while (remain_xfer) {
		tdi = (xfer_data[index]) >> (shift_bits % 8) & (0x1);
		if (remain_xfer == 1)
			tdo = TCK_Cycle(aspeed_jtag, 1, tdi); // go to Exit1-IR
		else
			tdo = TCK_Cycle(aspeed_jtag, 0, tdi); // go to IRShift
		tdo_buff |= (tdo << (shift_bits % 8));
		shift_bits++;
		remain_xfer--;
		if ((shift_bits % 8) == 0) {
			xfer_data[index] = tdo_buff;
			tdo_buff = 0;
			index++;
		}
	}
	TCK_Cycle(aspeed_jtag, 0, 0);
	if (xfer->type == JTAG_SIR_XFER)
		aspeed_jtag->sts = JTAG_STATE_PAUSEIR;
	else
		aspeed_jtag->sts = JTAG_STATE_PAUSEDR;
	aspeed_jtag_sw_set_tap_state(aspeed_jtag, xfer->endstate);
}
static int aspeed_hw_ir_scan(struct aspeed_jtag_info *aspeed_jtag,
			     enum jtag_tapstate endstate, u32 shift_bits)
{
	if (endstate == JTAG_STATE_PAUSEIR) {
		aspeed_jtag_write(aspeed_jtag, JTAG_INST_PAUSE_EN,
					  ASPEED_JTAG_ISR);
		if (aspeed_jtag->config->jtag_version == 6) {
			aspeed_jtag_write(
				aspeed_jtag,
				JTAG_ENG_EN | JTAG_ENG_OUT_EN |
					JTAG_G6_SET_XFER_LEN(shift_bits),
				ASPEED_JTAG_CTRL);
			aspeed_jtag_write(
				aspeed_jtag,
				JTAG_ENG_EN | JTAG_ENG_OUT_EN |
					JTAG_G6_SET_XFER_LEN(shift_bits) |
					JTAG_G6_INST_EN,
				ASPEED_JTAG_CTRL);
		} else {
			if (aspeed_jtag->sts == JTAG_STATE_PAUSEDR)
				aspeed_jtag_write(aspeed_jtag,
						  JTAG_INST_PAUSE_EN |
							  JTAG_DATA_COMPLETE_EN,
						  ASPEED_JTAG_ISR);
			aspeed_jtag_write(aspeed_jtag,
					  JTAG_ENG_EN | JTAG_ENG_OUT_EN |
						  JTAG_SET_INST_LEN(shift_bits),
					  ASPEED_JTAG_CTRL);
			aspeed_jtag_write(
				aspeed_jtag,
				JTAG_ENG_EN | JTAG_ENG_OUT_EN |
					JTAG_SET_INST_LEN(shift_bits) |
					JTAG_INST_EN,
				ASPEED_JTAG_CTRL);
			if (aspeed_jtag->sts == JTAG_STATE_PAUSEDR)
				aspeed_jtag_wait_data_complete(aspeed_jtag);
		}
		aspeed_jtag_wait_instruction_pause_complete(aspeed_jtag);
		aspeed_jtag->sts = JTAG_STATE_PAUSEIR;
	} else if (endstate == JTAG_STATE_IDLE) {
		aspeed_jtag_write(aspeed_jtag, JTAG_INST_COMPLETE_EN,
					  ASPEED_JTAG_ISR);
		if (aspeed_jtag->config->jtag_version == 6) {
			aspeed_jtag_write(
				aspeed_jtag,
				JTAG_ENG_EN | JTAG_ENG_OUT_EN |
					JTAG_G6_LAST_XFER |
					JTAG_G6_SET_XFER_LEN(shift_bits),
				ASPEED_JTAG_CTRL);
			aspeed_jtag_write(
				aspeed_jtag,
				JTAG_ENG_EN | JTAG_ENG_OUT_EN |
					JTAG_G6_LAST_XFER |
					JTAG_G6_SET_XFER_LEN(shift_bits) |
					JTAG_G6_INST_EN,
				ASPEED_JTAG_CTRL);
		} else {
			aspeed_jtag_write(aspeed_jtag,
					  JTAG_ENG_EN | JTAG_ENG_OUT_EN |
						  JTAG_LAST_INST |
						  JTAG_SET_INST_LEN(shift_bits),
					  ASPEED_JTAG_CTRL);
			aspeed_jtag_write(
				aspeed_jtag,
				JTAG_ENG_EN | JTAG_ENG_OUT_EN | JTAG_LAST_INST |
					JTAG_SET_INST_LEN(shift_bits) |
					JTAG_INST_EN,
				ASPEED_JTAG_CTRL);
		}
		aspeed_jtag_wait_instruction_complete(aspeed_jtag);
		aspeed_jtag->sts = JTAG_STATE_IDLE;
	} else {
		pr_err("End state %d not support", endstate);
		return -EFAULT;
	}
	return 0;
}
static int aspeed_hw_dr_scan(struct aspeed_jtag_info *aspeed_jtag,
			     enum jtag_tapstate endstate, u32 shift_bits)
{
	if (endstate == JTAG_STATE_PAUSEDR) {
		aspeed_jtag_write(aspeed_jtag, JTAG_DATA_PAUSE_EN,
					  ASPEED_JTAG_ISR);
		if (aspeed_jtag->config->jtag_version == 6) {
			aspeed_jtag_write(
				aspeed_jtag,
				JTAG_ENG_EN | JTAG_ENG_OUT_EN |
					JTAG_G6_SET_XFER_LEN(shift_bits),
				ASPEED_JTAG_CTRL);
			aspeed_jtag_write(
				aspeed_jtag,
				JTAG_ENG_EN | JTAG_ENG_OUT_EN |
					JTAG_G6_SET_XFER_LEN(shift_bits) |
					JTAG_DATA_EN,
				ASPEED_JTAG_CTRL);
		} else {
			if (aspeed_jtag->sts == JTAG_STATE_PAUSEIR)
				aspeed_jtag_write(aspeed_jtag,
						  JTAG_DATA_PAUSE_EN |
							  JTAG_INST_COMPLETE_EN,
						  ASPEED_JTAG_ISR);
			aspeed_jtag_write(aspeed_jtag,
					  JTAG_ENG_EN | JTAG_ENG_OUT_EN |
						  JTAG_DATA_LEN(shift_bits),
					  ASPEED_JTAG_CTRL);
			aspeed_jtag_write(aspeed_jtag,
					  JTAG_ENG_EN | JTAG_ENG_OUT_EN |
						  JTAG_DATA_LEN(shift_bits) |
						  JTAG_DATA_EN,
					  ASPEED_JTAG_CTRL);
			if (aspeed_jtag->sts == JTAG_STATE_PAUSEIR)
				aspeed_jtag_wait_instruction_complete(
					aspeed_jtag);
		}
		aspeed_jtag_wait_data_pause_complete(aspeed_jtag);
		aspeed_jtag->sts = JTAG_STATE_PAUSEDR;
	} else if (endstate == JTAG_STATE_IDLE) {
		aspeed_jtag_write(aspeed_jtag, JTAG_DATA_COMPLETE_EN,
					  ASPEED_JTAG_ISR);
		if (aspeed_jtag->config->jtag_version == 6) {
			aspeed_jtag_write(
				aspeed_jtag,
				JTAG_ENG_EN | JTAG_ENG_OUT_EN |
					JTAG_G6_LAST_XFER |
					JTAG_G6_SET_XFER_LEN(shift_bits),
				ASPEED_JTAG_CTRL);
			aspeed_jtag_write(
				aspeed_jtag,
				JTAG_ENG_EN | JTAG_ENG_OUT_EN |
					JTAG_G6_LAST_XFER |
					JTAG_G6_SET_XFER_LEN(shift_bits) |
					JTAG_DATA_EN,
				ASPEED_JTAG_CTRL);
		} else {
			aspeed_jtag_write(aspeed_jtag,
					  JTAG_ENG_EN | JTAG_ENG_OUT_EN |
						  JTAG_LAST_DATA |
						  JTAG_DATA_LEN(shift_bits),
					  ASPEED_JTAG_CTRL);
			aspeed_jtag_write(aspeed_jtag,
					  JTAG_ENG_EN | JTAG_ENG_OUT_EN |
						  JTAG_LAST_DATA |
						  JTAG_DATA_LEN(shift_bits) |
						  JTAG_DATA_EN,
					  ASPEED_JTAG_CTRL);
		}
		aspeed_jtag_wait_data_complete(aspeed_jtag);
		aspeed_jtag->sts = JTAG_STATE_IDLE;
	} else {
		pr_err("End state %d not support", endstate);
		return -EFAULT;
	}
	return 0;
}
static void aspeed_hw_jtag_xfer(struct aspeed_jtag_info *aspeed_jtag,
				struct jtag_xfer *xfer, u8 *xfer_data)
{
	unsigned int index = 0;
	u32 shift_bits = 0;
	u32 remain_xfer = xfer->length;
	int i, tmp_idx = 0;
	u32 fifo_reg = xfer->type ? ASPEED_JTAG_DATA : ASPEED_JTAG_INST;
	u32 *xfer_data_32 = (u32 *)xfer_data;

	aspeed_jtag_write(aspeed_jtag, 0, ASPEED_JTAG_SW); //dis sw mode

	while (remain_xfer) {
		if (remain_xfer > aspeed_jtag->config->jtag_buff_len) {
			shift_bits = aspeed_jtag->config->jtag_buff_len;
			tmp_idx = shift_bits / 32;
			for (i = 0; i < tmp_idx; i++)
				aspeed_jtag_write(aspeed_jtag,
						  xfer_data_32[index + i],
						  fifo_reg);
			/*
			 * Add 1 tck period delay to avoid jtag hardware
			 * transfer will get wrong fifo pointer issue.
			 */
			ndelay(aspeed_jtag->tck_period);
			if (xfer->type == JTAG_SIR_XFER)
				aspeed_hw_ir_scan(aspeed_jtag,
						  JTAG_STATE_PAUSEIR,
						  shift_bits);
			else
				aspeed_hw_dr_scan(aspeed_jtag,
						  JTAG_STATE_PAUSEDR,
						  shift_bits);
		} else {
			shift_bits = remain_xfer;
			tmp_idx = shift_bits / 32;
			if (shift_bits % 32)
				tmp_idx += 1;
			for (i = 0; i < tmp_idx; i++)
				aspeed_jtag_write(aspeed_jtag,
						  xfer_data_32[index + i],
						  fifo_reg);
			ndelay(aspeed_jtag->tck_period);
			if (xfer->type == JTAG_SIR_XFER)
				aspeed_hw_ir_scan(aspeed_jtag, xfer->endstate,
						  shift_bits);
			else
				aspeed_hw_dr_scan(aspeed_jtag, xfer->endstate,
						  shift_bits);
		}

		remain_xfer = remain_xfer - shift_bits;

		//handle tdo data
		tmp_idx = shift_bits / 32;
		if (shift_bits % 32)
			tmp_idx += 1;
		for (i = 0; i < tmp_idx; i++) {
			if (shift_bits < 32)
				xfer_data_32[index + i] =
					aspeed_jtag_read(aspeed_jtag,
							 fifo_reg) >>
					(32 - shift_bits);
			else
				xfer_data_32[index + i] =
					aspeed_jtag_read(aspeed_jtag, fifo_reg);
			JTAG_DBUG("TDO[%d]: %x\n", index + i,
				  xfer_data_32[index + i]);
			shift_bits -= 32;
		}
		index += tmp_idx;
	}
}

static int aspeed_jtag_xfer(struct jtag *jtag, struct jtag_xfer *xfer,
			    u8 *xfer_data)
{
	struct aspeed_jtag_info *aspeed_jtag = jtag_priv(jtag);

	JTAG_DBUG("%s mode, END : %d, len : %d\n",
		  aspeed_jtag->mode ? "HW" : "SW", xfer->endstate,
		  xfer->length);

	if (aspeed_jtag->mode == JTAG_XFER_HW_MODE)
		aspeed_hw_jtag_xfer(aspeed_jtag, xfer, xfer_data);
	else
		aspeed_sw_jtag_xfer(aspeed_jtag, xfer, xfer_data);

	return 0;
}

static irqreturn_t aspeed_jtag_isr(int this_irq, void *dev_id)
{
	u32 status;
	struct aspeed_jtag_info *aspeed_jtag = dev_id;

	status = aspeed_jtag_read(aspeed_jtag, ASPEED_JTAG_ISR);
	JTAG_DBUG("sts %x\n", status);

	if (status & JTAG_INST_PAUSE) {
		aspeed_jtag_write(aspeed_jtag, JTAG_INST_PAUSE | (status & 0xf),
				  ASPEED_JTAG_ISR);
		aspeed_jtag->flag |= JTAG_INST_PAUSE;
	}

	if (status & JTAG_INST_COMPLETE) {
		aspeed_jtag_write(aspeed_jtag,
				  JTAG_INST_COMPLETE | (status & 0xf),
				  ASPEED_JTAG_ISR);
		aspeed_jtag->flag |= JTAG_INST_COMPLETE;
	}

	if (status & JTAG_DATA_PAUSE) {
		aspeed_jtag_write(aspeed_jtag, JTAG_DATA_PAUSE | (status & 0xf),
				  ASPEED_JTAG_ISR);
		aspeed_jtag->flag |= JTAG_DATA_PAUSE;
	}

	if (status & JTAG_DATA_COMPLETE) {
		aspeed_jtag_write(aspeed_jtag,
				  JTAG_DATA_COMPLETE | (status & 0xf),
				  ASPEED_JTAG_ISR);
		aspeed_jtag->flag |= JTAG_DATA_COMPLETE;
	}

	if (aspeed_jtag->flag) {
		wake_up_interruptible(&aspeed_jtag->jtag_wq);
		return IRQ_HANDLED;
	}
	pr_err("TODO Check JTAG's interrupt %x\n",
		aspeed_jtag_read(aspeed_jtag, ASPEED_JTAG_ISR));
	return IRQ_NONE;
}


static struct aspeed_jtag_config jtag_config = {
	.jtag_version = 0,
	.jtag_buff_len = 32,
};

static struct aspeed_jtag_config jtag_g6_config = {
	.jtag_version = 6,
	.jtag_buff_len = 512,
};

static const struct of_device_id aspeed_jtag_of_matches[] = {
	{
		.compatible = "aspeed,ast2400-jtag",
		.data = &jtag_config,
	},
	{
		.compatible = "aspeed,ast2500-jtag",
		.data = &jtag_config,
	},
	{
		.compatible = "aspeed,ast2600-jtag",
		.data = &jtag_g6_config,
	},
	{},
};
MODULE_DEVICE_TABLE(of, aspeed_jtag_of_matches);

static int aspeed_jtag_bitbang(struct jtag *jtag,
			       struct bitbang_packet *bitbang,
			       struct tck_bitbang *bitbang_data)
{
	struct aspeed_jtag_info *aspeed_jtag = jtag_priv(jtag);
	int i = 0;

	for (i = 0; i < bitbang->length; i++) {
		bitbang_data[i].tdo =
			TCK_Cycle(aspeed_jtag, bitbang_data[i].tms,
					      bitbang_data[i].tdi);
	}
	return 0;
}

static int aspeed_jtag_mode_set(struct jtag *jtag, struct jtag_mode *jtag_mode)
{
	struct aspeed_jtag_info *aspeed_jtag = jtag_priv(jtag);

	switch (jtag_mode->feature) {
	case JTAG_XFER_MODE:
		aspeed_jtag->mode = jtag_mode->mode;
		break;
	case JTAG_CONTROL_MODE:
		return -ENOTSUPP;
	default:
		return -EINVAL;
	}
	return 0;
}

static int aspeed_jtag_enable(struct jtag *jtag)
{
	return 0;
}

static int aspeed_jtag_disable(struct jtag *jtag)
{
	return 0;
}

static const struct jtag_ops aspeed_jtag_ops = {
	.freq_get = aspeed_jtag_get_freq,
	.freq_set = aspeed_jtag_set_freq,
	.status_get = aspeed_jtag_status_get,
	.status_set = aspeed_jtag_status_set,
	.xfer = aspeed_jtag_xfer,
	.mode_set = aspeed_jtag_mode_set,
	.bitbang = aspeed_jtag_bitbang,
	.enable = aspeed_jtag_enable,
	.disable = aspeed_jtag_disable,
};

static int aspeed_jtag_probe(struct platform_device *pdev)
{
	struct aspeed_jtag_info *aspeed_jtag;
	struct jtag *jtag;
	const struct of_device_id *jtag_dev_id;
	struct resource *res;
	int ret = 0;

	jtag = jtag_alloc(&pdev->dev, sizeof(*aspeed_jtag),
			  &aspeed_jtag_ops);
	if (!jtag)
		return -ENOMEM;

	platform_set_drvdata(pdev, jtag);
	aspeed_jtag = jtag_priv(jtag);
	aspeed_jtag->dev = &pdev->dev;

	jtag_dev_id = of_match_device(aspeed_jtag_of_matches, &pdev->dev);
	if (!jtag_dev_id)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "cannot get IORESOURCE_MEM\n");
		ret = -ENOENT;
		goto out;
	}

	aspeed_jtag->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (!aspeed_jtag->reg_base) {
		ret = -EIO;
		goto out;
	}

	aspeed_jtag->irq = platform_get_irq(pdev, 0);
	if (aspeed_jtag->irq < 0) {
		dev_err(&pdev->dev, "no irq specified\n");
		ret = -ENOENT;
		goto out;
	}
	aspeed_jtag->reset =
		devm_reset_control_get_exclusive(&pdev->dev, "jtag");
	if (IS_ERR(aspeed_jtag->reset)) {
		dev_err(&pdev->dev, "can't get jtag reset\n");
		return PTR_ERR(aspeed_jtag->reset);
	}

	aspeed_jtag->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(aspeed_jtag->clk)) {
		dev_err(&pdev->dev, "no clock defined\n");
		return -ENODEV;
	}

	aspeed_jtag->clkin = clk_get_rate(aspeed_jtag->clk);
	dev_dbg(&pdev->dev, "aspeed_jtag->clkin %d\n", aspeed_jtag->clkin);

	aspeed_jtag->config = (struct aspeed_jtag_config *)jtag_dev_id->data;
	// SCU init
	reset_control_assert(aspeed_jtag->reset);
	udelay(3);
	reset_control_deassert(aspeed_jtag->reset);

	ret = devm_request_irq(&pdev->dev, aspeed_jtag->irq, aspeed_jtag_isr,
			       0, dev_name(&pdev->dev), aspeed_jtag);
	if (ret) {
		dev_dbg(&pdev->dev, "JTAG Unable to get IRQ");
		goto out;
	}

	// clear interrupt
	aspeed_jtag_write(aspeed_jtag,
			  JTAG_INST_PAUSE | JTAG_INST_COMPLETE |
			  JTAG_DATA_PAUSE | JTAG_DATA_COMPLETE,
			  ASPEED_JTAG_ISR);

	aspeed_jtag->mode = JTAG_XFER_HW_MODE;
	aspeed_jtag->flag = 0;
	aspeed_jtag->sts = JTAG_STATE_IDLE;
	init_waitqueue_head(&aspeed_jtag->jtag_wq);

	aspeed_jtag_set_freq(jtag, TCK_FREQ);
	/* Enable jtag clock */
	aspeed_jtag_write(aspeed_jtag, JTAG_ENG_OUT_EN, ASPEED_JTAG_CTRL);

	/* Initialize JTAG core structure*/
	ret = devm_jtag_register(aspeed_jtag->dev, jtag);
	if (ret)
		goto out;

	dev_info(&pdev->dev, "aspeed_jtag: driver successfully loaded.\n");

	return 0;

out:
	reset_control_assert(aspeed_jtag->reset);
	jtag_free(jtag);
	dev_warn(&pdev->dev, "aspeed_jtag: driver init failed (ret=%d)!\n",
		 ret);
	return ret;
}

static int aspeed_jtag_remove(struct platform_device *pdev)
{
	struct jtag *jtag = platform_get_drvdata(pdev);
	struct aspeed_jtag_info *aspeed_jtag;

	aspeed_jtag = jtag_priv(jtag);
	reset_control_assert(aspeed_jtag->reset);
	jtag_free(jtag);
	return 0;
}

static struct platform_driver aspeed_jtag_driver = {
	.probe		= aspeed_jtag_probe,
	.remove		= aspeed_jtag_remove,
	.driver		= {
		.name	= "aspeed-jtag",
		.of_match_table = aspeed_jtag_of_matches,
	},
};

module_platform_driver(aspeed_jtag_driver);

MODULE_AUTHOR("Ryan Chen <ryan_chen@aspeedtech.com>");
MODULE_DESCRIPTION("AST JTAG LIB Driver");
MODULE_LICENSE("GPL");
