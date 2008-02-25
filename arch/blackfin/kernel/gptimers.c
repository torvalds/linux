/*
 * gptimers.c - Blackfin General Purpose Timer core API
 *
 * Copyright (c) 2005-2008 Analog Devices Inc.
 * Copyright (C) 2005 John DeHority
 * Copyright (C) 2006 Hella Aglaia GmbH (awe@aglaia-gmbh.de)
 *
 * Licensed under the GPLv2.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/io.h>
#include <asm/blackfin.h>
#include <asm/gptimers.h>

#ifdef DEBUG
# define tassert(expr)
#else
# define tassert(expr) \
	if (!(expr)) \
		printk(KERN_DEBUG "%s:%s:%i: Assertion failed: " #expr "\n", __FILE__, __func__, __LINE__);
#endif

#define BFIN_TIMER_NUM_GROUP  (BFIN_TIMER_OCTET(MAX_BLACKFIN_GPTIMERS - 1) + 1)

typedef struct {
	uint16_t config;
	uint16_t __pad;
	uint32_t counter;
	uint32_t period;
	uint32_t width;
} GPTIMER_timer_regs;

typedef struct {
	uint16_t enable;
	uint16_t __pad0;
	uint16_t disable;
	uint16_t __pad1;
	uint32_t status;
} GPTIMER_group_regs;

static volatile GPTIMER_timer_regs *const timer_regs[MAX_BLACKFIN_GPTIMERS] =
{
	(GPTIMER_timer_regs *)TIMER0_CONFIG,
	(GPTIMER_timer_regs *)TIMER1_CONFIG,
	(GPTIMER_timer_regs *)TIMER2_CONFIG,
#if (MAX_BLACKFIN_GPTIMERS > 3)
	(GPTIMER_timer_regs *)TIMER3_CONFIG,
	(GPTIMER_timer_regs *)TIMER4_CONFIG,
	(GPTIMER_timer_regs *)TIMER5_CONFIG,
	(GPTIMER_timer_regs *)TIMER6_CONFIG,
	(GPTIMER_timer_regs *)TIMER7_CONFIG,
#endif
#if (MAX_BLACKFIN_GPTIMERS > 8)
	(GPTIMER_timer_regs *)TIMER8_CONFIG,
	(GPTIMER_timer_regs *)TIMER9_CONFIG,
	(GPTIMER_timer_regs *)TIMER10_CONFIG,
	(GPTIMER_timer_regs *)TIMER11_CONFIG,
#endif
};

static volatile GPTIMER_group_regs *const group_regs[BFIN_TIMER_NUM_GROUP] =
{
	(GPTIMER_group_regs *)TIMER0_GROUP_REG,
#if (MAX_BLACKFIN_GPTIMERS > 8)
	(GPTIMER_group_regs *)TIMER8_GROUP_REG,
#endif
};

static uint32_t const trun_mask[MAX_BLACKFIN_GPTIMERS] =
{
	TIMER_STATUS_TRUN0,
	TIMER_STATUS_TRUN1,
	TIMER_STATUS_TRUN2,
#if (MAX_BLACKFIN_GPTIMERS > 3)
	TIMER_STATUS_TRUN3,
	TIMER_STATUS_TRUN4,
	TIMER_STATUS_TRUN5,
	TIMER_STATUS_TRUN6,
	TIMER_STATUS_TRUN7,
#endif
#if (MAX_BLACKFIN_GPTIMERS > 8)
	TIMER_STATUS_TRUN8,
	TIMER_STATUS_TRUN9,
	TIMER_STATUS_TRUN10,
	TIMER_STATUS_TRUN11,
#endif
};

static uint32_t const tovf_mask[MAX_BLACKFIN_GPTIMERS] =
{
	TIMER_STATUS_TOVF0,
	TIMER_STATUS_TOVF1,
	TIMER_STATUS_TOVF2,
#if (MAX_BLACKFIN_GPTIMERS > 3)
	TIMER_STATUS_TOVF3,
	TIMER_STATUS_TOVF4,
	TIMER_STATUS_TOVF5,
	TIMER_STATUS_TOVF6,
	TIMER_STATUS_TOVF7,
#endif
#if (MAX_BLACKFIN_GPTIMERS > 8)
	TIMER_STATUS_TOVF8,
	TIMER_STATUS_TOVF9,
	TIMER_STATUS_TOVF10,
	TIMER_STATUS_TOVF11,
#endif
};

static uint32_t const timil_mask[MAX_BLACKFIN_GPTIMERS] =
{
	TIMER_STATUS_TIMIL0,
	TIMER_STATUS_TIMIL1,
	TIMER_STATUS_TIMIL2,
#if (MAX_BLACKFIN_GPTIMERS > 3)
	TIMER_STATUS_TIMIL3,
	TIMER_STATUS_TIMIL4,
	TIMER_STATUS_TIMIL5,
	TIMER_STATUS_TIMIL6,
	TIMER_STATUS_TIMIL7,
#endif
#if (MAX_BLACKFIN_GPTIMERS > 8)
	TIMER_STATUS_TIMIL8,
	TIMER_STATUS_TIMIL9,
	TIMER_STATUS_TIMIL10,
	TIMER_STATUS_TIMIL11,
#endif
};

void set_gptimer_pwidth(int timer_id, uint32_t value)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	timer_regs[timer_id]->width = value;
	SSYNC();
}
EXPORT_SYMBOL(set_gptimer_pwidth);

uint32_t get_gptimer_pwidth(int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	return timer_regs[timer_id]->width;
}
EXPORT_SYMBOL(get_gptimer_pwidth);

void set_gptimer_period(int timer_id, uint32_t period)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	timer_regs[timer_id]->period = period;
	SSYNC();
}
EXPORT_SYMBOL(set_gptimer_period);

uint32_t get_gptimer_period(int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	return timer_regs[timer_id]->period;
}
EXPORT_SYMBOL(get_gptimer_period);

uint32_t get_gptimer_count(int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	return timer_regs[timer_id]->counter;
}
EXPORT_SYMBOL(get_gptimer_count);

uint32_t get_gptimer_status(int group)
{
	tassert(group < BFIN_TIMER_NUM_GROUP);
	return group_regs[group]->status;
}
EXPORT_SYMBOL(get_gptimer_status);

void set_gptimer_status(int group, uint32_t value)
{
	tassert(group < BFIN_TIMER_NUM_GROUP);
	group_regs[group]->status = value;
	SSYNC();
}
EXPORT_SYMBOL(set_gptimer_status);

uint16_t get_gptimer_intr(int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	return (group_regs[BFIN_TIMER_OCTET(timer_id)]->status & timil_mask[timer_id]) ? 1 : 0;
}
EXPORT_SYMBOL(get_gptimer_intr);

void clear_gptimer_intr(int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	group_regs[BFIN_TIMER_OCTET(timer_id)]->status = timil_mask[timer_id];
}
EXPORT_SYMBOL(clear_gptimer_intr);

uint16_t get_gptimer_over(int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	return (group_regs[BFIN_TIMER_OCTET(timer_id)]->status & tovf_mask[timer_id]) ? 1 : 0;
}
EXPORT_SYMBOL(get_gptimer_over);

void clear_gptimer_over(int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	group_regs[BFIN_TIMER_OCTET(timer_id)]->status = tovf_mask[timer_id];
}
EXPORT_SYMBOL(clear_gptimer_over);

void set_gptimer_config(int timer_id, uint16_t config)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	timer_regs[timer_id]->config = config;
	SSYNC();
}
EXPORT_SYMBOL(set_gptimer_config);

uint16_t get_gptimer_config(int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	return timer_regs[timer_id]->config;
}
EXPORT_SYMBOL(get_gptimer_config);

void enable_gptimers(uint16_t mask)
{
	int i;
	tassert((mask & ~BLACKFIN_GPTIMER_IDMASK) == 0);
	for (i = 0; i < BFIN_TIMER_NUM_GROUP; ++i) {
		group_regs[i]->enable = mask & 0xFF;
		mask >>= 8;
	}
	SSYNC();
}
EXPORT_SYMBOL(enable_gptimers);

void disable_gptimers(uint16_t mask)
{
	int i;
	uint16_t m = mask;
	tassert((mask & ~BLACKFIN_GPTIMER_IDMASK) == 0);
	for (i = 0; i < BFIN_TIMER_NUM_GROUP; ++i) {
		group_regs[i]->disable = m & 0xFF;
		m >>= 8;
	}
	for (i = 0; i < MAX_BLACKFIN_GPTIMERS; ++i)
		if (mask & (1 << i))
			group_regs[BFIN_TIMER_OCTET(i)]->status |= trun_mask[i];
	SSYNC();
}
EXPORT_SYMBOL(disable_gptimers);

void set_gptimer_pulse_hi(int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	timer_regs[timer_id]->config |= TIMER_PULSE_HI;
	SSYNC();
}
EXPORT_SYMBOL(set_gptimer_pulse_hi);

void clear_gptimer_pulse_hi(int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	timer_regs[timer_id]->config &= ~TIMER_PULSE_HI;
	SSYNC();
}
EXPORT_SYMBOL(clear_gptimer_pulse_hi);

uint16_t get_enabled_gptimers(void)
{
	int i;
	uint16_t result = 0;
	for (i = 0; i < BFIN_TIMER_NUM_GROUP; ++i)
		result |= (group_regs[i]->enable << (i << 3));
	return result;
}
EXPORT_SYMBOL(get_enabled_gptimers);

MODULE_AUTHOR("Axel Weiss (awe@aglaia-gmbh.de)");
MODULE_DESCRIPTION("Blackfin General Purpose Timers API");
MODULE_LICENSE("GPL");
