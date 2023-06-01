/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 * Author: Xingyu Wu <xingyu.wu@starfivetech.com>
 *         Samin Guo <samin.guo@starfivetech.com>
 */
#ifndef STARFIVE_TIMER_H
#define STARFIVE_TIMER_H

#define NR_TIMERS		TIMERS_MAX
#define PER_TIMER_LEN		0x40
#define TIMER_BASE(x)		((TIMER_##x)*PER_TIMER_LEN)

/*
 * JH7100 timwer TIMER_INT_STATUS:
 * ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * |     Bits     | 08~31 | 7 | 6 | 5 |  4  | 3 | 2 | 1 | 0 |
 * ----------------------------------------------------------
 * | timer(n)_int |  res  | 6 | 5 | 4 | Wdt | 3 | 2 | 1 | 0 |
 * ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *
 * Software can read this register to know which interrupt is occurred.
 */
#define	STF_TIMER_INT_STATUS	0x00
#define STF_TIMER_CTL		0x04
#define STF_TIMER_LOAD		0x08
#define STF_TIMER_ENABLE	0x10
#define STF_TIMER_RELOAD	0x14
#define STF_TIMER_VALUE		0x18
#define STF_TIMER_INT_CLR	0x20
#define STF_TIMER_INT_MASK	0x24
#define INT_STATUS_CLR_AVA	BIT(1)

enum STF_TIMERS {
	TIMER_0 = 0,
	TIMER_1,
	TIMER_2,
	TIMER_3,
	TIMER_4,  /*WDT*/
	TIMER_5,
	TIMER_6,
	TIMER_7,
	TIMERS_MAX
};

enum TIMERI_INTMASK {
	INTMASK_ENABLE_DIS = 0,
	INTMASK_ENABLE = 1
};

enum TIMER_MOD {
	MOD_CONTIN = 0,
	MOD_SINGLE = 1
};

enum TIMER_CTL_EN {
	TIMER_ENA_DIS	= 0,
	TIMER_ENA	= 1
};

enum {
	INT_CLR_AVAILABLE = 0,
	INT_CLR_NOT_AVAILABLE = 1
};

struct starfive_timer {
	u32 ctrl;
	u32 load;
	u32 enable;
	u32 reload;
	u32 value;
	u32 intclr;
	u32 intmask;
	u32 wdt_lock;   /* 0x3c+i*0x40 watchdog use ONLY */
	u32 timer_base[NR_TIMERS];
};

struct starfive_timer_misc_count {
	u8 clk_count;
	bool flg_init_clk;
};

struct starfive_clkevt {
	struct clock_event_device evt;
	struct clocksource cs;
	struct device_node *device_node;
	struct starfive_timer_misc_count *misc;
	struct clk *clk;
	char name[20];
	int index;
	int irq;
	u64 periodic;
	u64 rate;
	u32 reload_val;
	void __iomem *base;
	void __iomem *ctrl;
	void __iomem *load;
	void __iomem *enable;
	void __iomem *reload;
	void __iomem *value;
	void __iomem *intclr;
	void __iomem *intmask;
};
#endif /* STARFIVE_TIMER_H */
