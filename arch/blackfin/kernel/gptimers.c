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
#include <linux/io.h>

#include <asm/blackfin.h>
#include <asm/gptimers.h>

#ifdef DEBUG
# define tassert(expr)
#else
# define tassert(expr) \
	if (!(expr)) \
		printk(KERN_DEBUG "%s:%s:%i: Assertion failed: " #expr "\n", __FILE__, __func__, __LINE__);
#endif

#ifndef CONFIG_BF60x
# define BFIN_TIMER_NUM_GROUP  (BFIN_TIMER_OCTET(MAX_BLACKFIN_GPTIMERS - 1) + 1)
#else
# define BFIN_TIMER_NUM_GROUP  1
#endif

static struct bfin_gptimer_regs * const timer_regs[MAX_BLACKFIN_GPTIMERS] =
{
	(void *)TIMER0_CONFIG,
	(void *)TIMER1_CONFIG,
	(void *)TIMER2_CONFIG,
#if (MAX_BLACKFIN_GPTIMERS > 3)
	(void *)TIMER3_CONFIG,
	(void *)TIMER4_CONFIG,
	(void *)TIMER5_CONFIG,
	(void *)TIMER6_CONFIG,
	(void *)TIMER7_CONFIG,
# if (MAX_BLACKFIN_GPTIMERS > 8)
	(void *)TIMER8_CONFIG,
	(void *)TIMER9_CONFIG,
	(void *)TIMER10_CONFIG,
#  if (MAX_BLACKFIN_GPTIMERS > 11)
	(void *)TIMER11_CONFIG,
#  endif
# endif
#endif
};

static struct bfin_gptimer_group_regs * const group_regs[BFIN_TIMER_NUM_GROUP] =
{
	(void *)TIMER0_GROUP_REG,
#if (MAX_BLACKFIN_GPTIMERS > 8)
	(void *)TIMER8_GROUP_REG,
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
# if (MAX_BLACKFIN_GPTIMERS > 8)
	TIMER_STATUS_TRUN8,
	TIMER_STATUS_TRUN9,
	TIMER_STATUS_TRUN10,
#  if (MAX_BLACKFIN_GPTIMERS > 11)
	TIMER_STATUS_TRUN11,
#  endif
# endif
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
# if (MAX_BLACKFIN_GPTIMERS > 8)
	TIMER_STATUS_TOVF8,
	TIMER_STATUS_TOVF9,
	TIMER_STATUS_TOVF10,
#  if (MAX_BLACKFIN_GPTIMERS > 11)
	TIMER_STATUS_TOVF11,
#  endif
# endif
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
# if (MAX_BLACKFIN_GPTIMERS > 8)
	TIMER_STATUS_TIMIL8,
	TIMER_STATUS_TIMIL9,
	TIMER_STATUS_TIMIL10,
#  if (MAX_BLACKFIN_GPTIMERS > 11)
	TIMER_STATUS_TIMIL11,
#  endif
# endif
#endif
};

void set_gptimer_pwidth(unsigned int timer_id, uint32_t value)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	bfin_write(&timer_regs[timer_id]->width, value);
	SSYNC();
}
EXPORT_SYMBOL(set_gptimer_pwidth);

uint32_t get_gptimer_pwidth(unsigned int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	return bfin_read(&timer_regs[timer_id]->width);
}
EXPORT_SYMBOL(get_gptimer_pwidth);

void set_gptimer_period(unsigned int timer_id, uint32_t period)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	bfin_write(&timer_regs[timer_id]->period, period);
	SSYNC();
}
EXPORT_SYMBOL(set_gptimer_period);

uint32_t get_gptimer_period(unsigned int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	return bfin_read(&timer_regs[timer_id]->period);
}
EXPORT_SYMBOL(get_gptimer_period);

uint32_t get_gptimer_count(unsigned int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	return bfin_read(&timer_regs[timer_id]->counter);
}
EXPORT_SYMBOL(get_gptimer_count);

#ifdef CONFIG_BF60x
void set_gptimer_delay(unsigned int timer_id, uint32_t delay)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	bfin_write(&timer_regs[timer_id]->delay, delay);
	SSYNC();
}
EXPORT_SYMBOL(set_gptimer_delay);

uint32_t get_gptimer_delay(unsigned int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	return bfin_read(&timer_regs[timer_id]->delay);
}
EXPORT_SYMBOL(get_gptimer_delay);
#endif

#ifdef CONFIG_BF60x
int get_gptimer_intr(unsigned int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	return !!(bfin_read(&group_regs[BFIN_TIMER_OCTET(timer_id)]->data_ilat) & timil_mask[timer_id]);
}
EXPORT_SYMBOL(get_gptimer_intr);

void clear_gptimer_intr(unsigned int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	bfin_write(&group_regs[BFIN_TIMER_OCTET(timer_id)]->data_ilat, timil_mask[timer_id]);
}
EXPORT_SYMBOL(clear_gptimer_intr);

int get_gptimer_over(unsigned int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	return !!(bfin_read(&group_regs[BFIN_TIMER_OCTET(timer_id)]->stat_ilat) & tovf_mask[timer_id]);
}
EXPORT_SYMBOL(get_gptimer_over);

void clear_gptimer_over(unsigned int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	bfin_write(&group_regs[BFIN_TIMER_OCTET(timer_id)]->stat_ilat, tovf_mask[timer_id]);
}
EXPORT_SYMBOL(clear_gptimer_over);

int get_gptimer_run(unsigned int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	return !!(bfin_read(&group_regs[BFIN_TIMER_OCTET(timer_id)]->run) & trun_mask[timer_id]);
}
EXPORT_SYMBOL(get_gptimer_run);

uint32_t get_gptimer_status(unsigned int group)
{
	tassert(group < BFIN_TIMER_NUM_GROUP);
	return bfin_read(&group_regs[group]->data_ilat);
}
EXPORT_SYMBOL(get_gptimer_status);

void set_gptimer_status(unsigned int group, uint32_t value)
{
	tassert(group < BFIN_TIMER_NUM_GROUP);
	bfin_write(&group_regs[group]->data_ilat, value);
	SSYNC();
}
EXPORT_SYMBOL(set_gptimer_status);
#else
uint32_t get_gptimer_status(unsigned int group)
{
	tassert(group < BFIN_TIMER_NUM_GROUP);
	return bfin_read(&group_regs[group]->status);
}
EXPORT_SYMBOL(get_gptimer_status);

void set_gptimer_status(unsigned int group, uint32_t value)
{
	tassert(group < BFIN_TIMER_NUM_GROUP);
	bfin_write(&group_regs[group]->status, value);
	SSYNC();
}
EXPORT_SYMBOL(set_gptimer_status);

static uint32_t read_gptimer_status(unsigned int timer_id)
{
	return bfin_read(&group_regs[BFIN_TIMER_OCTET(timer_id)]->status);
}

int get_gptimer_intr(unsigned int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	return !!(read_gptimer_status(timer_id) & timil_mask[timer_id]);
}
EXPORT_SYMBOL(get_gptimer_intr);

void clear_gptimer_intr(unsigned int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	bfin_write(&group_regs[BFIN_TIMER_OCTET(timer_id)]->status, timil_mask[timer_id]);
}
EXPORT_SYMBOL(clear_gptimer_intr);

int get_gptimer_over(unsigned int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	return !!(read_gptimer_status(timer_id) & tovf_mask[timer_id]);
}
EXPORT_SYMBOL(get_gptimer_over);

void clear_gptimer_over(unsigned int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	bfin_write(&group_regs[BFIN_TIMER_OCTET(timer_id)]->status, tovf_mask[timer_id]);
}
EXPORT_SYMBOL(clear_gptimer_over);

int get_gptimer_run(unsigned int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	return !!(read_gptimer_status(timer_id) & trun_mask[timer_id]);
}
EXPORT_SYMBOL(get_gptimer_run);
#endif

void set_gptimer_config(unsigned int timer_id, uint16_t config)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	bfin_write(&timer_regs[timer_id]->config, config);
	SSYNC();
}
EXPORT_SYMBOL(set_gptimer_config);

uint16_t get_gptimer_config(unsigned int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	return bfin_read(&timer_regs[timer_id]->config);
}
EXPORT_SYMBOL(get_gptimer_config);

void enable_gptimers(uint16_t mask)
{
	int i;
#ifdef CONFIG_BF60x
	uint16_t imask;
	imask = bfin_read16(TIMER_DATA_IMSK);
	imask &= ~mask;
	bfin_write16(TIMER_DATA_IMSK, imask);
#endif
	tassert((mask & ~BLACKFIN_GPTIMER_IDMASK) == 0);
	for (i = 0; i < BFIN_TIMER_NUM_GROUP; ++i) {
		bfin_write(&group_regs[i]->enable, mask & 0xFF);
		mask >>= 8;
	}
	SSYNC();
}
EXPORT_SYMBOL(enable_gptimers);

static void _disable_gptimers(uint16_t mask)
{
	int i;
	uint16_t m = mask;
	tassert((mask & ~BLACKFIN_GPTIMER_IDMASK) == 0);
	for (i = 0; i < BFIN_TIMER_NUM_GROUP; ++i) {
		bfin_write(&group_regs[i]->disable, m & 0xFF);
		m >>= 8;
	}
}

void disable_gptimers(uint16_t mask)
{
#ifndef CONFIG_BF60x
	int i;
	_disable_gptimers(mask);
	for (i = 0; i < MAX_BLACKFIN_GPTIMERS; ++i)
		if (mask & (1 << i))
			bfin_write(&group_regs[BFIN_TIMER_OCTET(i)]->status, trun_mask[i]);
	SSYNC();
#else
	_disable_gptimers(mask);
#endif
}
EXPORT_SYMBOL(disable_gptimers);

void disable_gptimers_sync(uint16_t mask)
{
	_disable_gptimers(mask);
	SSYNC();
}
EXPORT_SYMBOL(disable_gptimers_sync);

void set_gptimer_pulse_hi(unsigned int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	bfin_write_or(&timer_regs[timer_id]->config, TIMER_PULSE_HI);
	SSYNC();
}
EXPORT_SYMBOL(set_gptimer_pulse_hi);

void clear_gptimer_pulse_hi(unsigned int timer_id)
{
	tassert(timer_id < MAX_BLACKFIN_GPTIMERS);
	bfin_write_and(&timer_regs[timer_id]->config, ~TIMER_PULSE_HI);
	SSYNC();
}
EXPORT_SYMBOL(clear_gptimer_pulse_hi);

uint16_t get_enabled_gptimers(void)
{
	int i;
	uint16_t result = 0;
	for (i = 0; i < BFIN_TIMER_NUM_GROUP; ++i)
		result |= (bfin_read(&group_regs[i]->enable) << (i << 3));
	return result;
}
EXPORT_SYMBOL(get_enabled_gptimers);

MODULE_AUTHOR("Axel Weiss (awe@aglaia-gmbh.de)");
MODULE_DESCRIPTION("Blackfin General Purpose Timers API");
MODULE_LICENSE("GPL");
