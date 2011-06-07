/*
 * gptimers.h - Blackfin General Purpose Timer structs/defines/prototypes
 *
 * Copyright (c) 2005-2008 Analog Devices Inc.
 * Copyright (C) 2005 John DeHority
 * Copyright (C) 2006 Hella Aglaia GmbH (awe@aglaia-gmbh.de)
 *
 * Licensed under the GPL-2.
 */

#ifndef _BLACKFIN_TIMERS_H_
#define _BLACKFIN_TIMERS_H_

#include <linux/types.h>
#include <asm/blackfin.h>

/*
 * BF51x/BF52x/BF537: 8 timers:
 */
#if defined(CONFIG_BF51x) || defined(CONFIG_BF52x) || defined(BF537_FAMILY)
# define MAX_BLACKFIN_GPTIMERS 8
# define TIMER0_GROUP_REG      TIMER_ENABLE
#endif
/*
 * BF54x: 11 timers (BF542: 8 timers):
 */
#if defined(CONFIG_BF54x)
# ifdef CONFIG_BF542
#  define MAX_BLACKFIN_GPTIMERS 8
# else
#  define MAX_BLACKFIN_GPTIMERS 11
#  define TIMER8_GROUP_REG      TIMER_ENABLE1
#  define TIMER_GROUP2          1
# endif
# define TIMER0_GROUP_REG       TIMER_ENABLE0
#endif
/*
 * BF561: 12 timers:
 */
#if defined(CONFIG_BF561)
# define MAX_BLACKFIN_GPTIMERS 12
# define TIMER0_GROUP_REG      TMRS8_ENABLE
# define TIMER8_GROUP_REG      TMRS4_ENABLE
# define TIMER_GROUP2          1
#endif
/*
 * All others: 3 timers:
 */
#define TIMER_GROUP1           0
#if !defined(MAX_BLACKFIN_GPTIMERS)
# define MAX_BLACKFIN_GPTIMERS 3
# define TIMER0_GROUP_REG      TIMER_ENABLE
#endif

#define BLACKFIN_GPTIMER_IDMASK ((1UL << MAX_BLACKFIN_GPTIMERS) - 1)
#define BFIN_TIMER_OCTET(x) ((x) >> 3)

/* used in masks for timer_enable() and timer_disable() */
#define TIMER0bit  0x0001  /*  0001b */
#define TIMER1bit  0x0002  /*  0010b */
#define TIMER2bit  0x0004  /*  0100b */
#define TIMER3bit  0x0008
#define TIMER4bit  0x0010
#define TIMER5bit  0x0020
#define TIMER6bit  0x0040
#define TIMER7bit  0x0080
#define TIMER8bit  0x0100
#define TIMER9bit  0x0200
#define TIMER10bit 0x0400
#define TIMER11bit 0x0800

#define TIMER0_id   0
#define TIMER1_id   1
#define TIMER2_id   2
#define TIMER3_id   3
#define TIMER4_id   4
#define TIMER5_id   5
#define TIMER6_id   6
#define TIMER7_id   7
#define TIMER8_id   8
#define TIMER9_id   9
#define TIMER10_id 10
#define TIMER11_id 11

/* associated timers for ppi framesync: */

#if defined(CONFIG_BF561)
# define FS0_1_TIMER_ID   TIMER8_id
# define FS0_2_TIMER_ID   TIMER9_id
# define FS1_1_TIMER_ID   TIMER10_id
# define FS1_2_TIMER_ID   TIMER11_id
# define FS0_1_TIMER_BIT  TIMER8bit
# define FS0_2_TIMER_BIT  TIMER9bit
# define FS1_1_TIMER_BIT  TIMER10bit
# define FS1_2_TIMER_BIT  TIMER11bit
# undef FS1_TIMER_ID
# undef FS2_TIMER_ID
# undef FS1_TIMER_BIT
# undef FS2_TIMER_BIT
#else
# define FS1_TIMER_ID  TIMER0_id
# define FS2_TIMER_ID  TIMER1_id
# define FS1_TIMER_BIT TIMER0bit
# define FS2_TIMER_BIT TIMER1bit
#endif

/*
 * Timer Configuration Register Bits
 */
#define TIMER_ERR           0xC000
#define TIMER_ERR_OVFL      0x4000
#define TIMER_ERR_PROG_PER  0x8000
#define TIMER_ERR_PROG_PW   0xC000
#define TIMER_EMU_RUN       0x0200
#define TIMER_TOGGLE_HI     0x0100
#define TIMER_CLK_SEL       0x0080
#define TIMER_OUT_DIS       0x0040
#define TIMER_TIN_SEL       0x0020
#define TIMER_IRQ_ENA       0x0010
#define TIMER_PERIOD_CNT    0x0008
#define TIMER_PULSE_HI      0x0004
#define TIMER_MODE          0x0003
#define TIMER_MODE_PWM      0x0001
#define TIMER_MODE_WDTH     0x0002
#define TIMER_MODE_EXT_CLK  0x0003

/*
 * Timer Status Register Bits
 */
#define TIMER_STATUS_TIMIL0  0x0001
#define TIMER_STATUS_TIMIL1  0x0002
#define TIMER_STATUS_TIMIL2  0x0004
#define TIMER_STATUS_TIMIL3  0x00000008
#define TIMER_STATUS_TIMIL4  0x00010000
#define TIMER_STATUS_TIMIL5  0x00020000
#define TIMER_STATUS_TIMIL6  0x00040000
#define TIMER_STATUS_TIMIL7  0x00080000
#define TIMER_STATUS_TIMIL8  0x0001
#define TIMER_STATUS_TIMIL9  0x0002
#define TIMER_STATUS_TIMIL10 0x0004
#define TIMER_STATUS_TIMIL11 0x0008

#define TIMER_STATUS_TOVF0   0x0010	/* timer 0 overflow error */
#define TIMER_STATUS_TOVF1   0x0020
#define TIMER_STATUS_TOVF2   0x0040
#define TIMER_STATUS_TOVF3   0x00000080
#define TIMER_STATUS_TOVF4   0x00100000
#define TIMER_STATUS_TOVF5   0x00200000
#define TIMER_STATUS_TOVF6   0x00400000
#define TIMER_STATUS_TOVF7   0x00800000
#define TIMER_STATUS_TOVF8   0x0010
#define TIMER_STATUS_TOVF9   0x0020
#define TIMER_STATUS_TOVF10  0x0040
#define TIMER_STATUS_TOVF11  0x0080

/*
 * Timer Slave Enable Status : write 1 to clear
 */
#define TIMER_STATUS_TRUN0  0x1000
#define TIMER_STATUS_TRUN1  0x2000
#define TIMER_STATUS_TRUN2  0x4000
#define TIMER_STATUS_TRUN3  0x00008000
#define TIMER_STATUS_TRUN4  0x10000000
#define TIMER_STATUS_TRUN5  0x20000000
#define TIMER_STATUS_TRUN6  0x40000000
#define TIMER_STATUS_TRUN7  0x80000000
#define TIMER_STATUS_TRUN   0xF000F000
#define TIMER_STATUS_TRUN8  0x1000
#define TIMER_STATUS_TRUN9  0x2000
#define TIMER_STATUS_TRUN10 0x4000
#define TIMER_STATUS_TRUN11 0x8000

/* The actual gptimer API */

void     set_gptimer_pwidth(unsigned int timer_id, uint32_t width);
uint32_t get_gptimer_pwidth(unsigned int timer_id);
void     set_gptimer_period(unsigned int timer_id, uint32_t period);
uint32_t get_gptimer_period(unsigned int timer_id);
uint32_t get_gptimer_count(unsigned int timer_id);
int      get_gptimer_intr(unsigned int timer_id);
void     clear_gptimer_intr(unsigned int timer_id);
int      get_gptimer_over(unsigned int timer_id);
void     clear_gptimer_over(unsigned int timer_id);
void     set_gptimer_config(unsigned int timer_id, uint16_t config);
uint16_t get_gptimer_config(unsigned int timer_id);
int      get_gptimer_run(unsigned int timer_id);
void     set_gptimer_pulse_hi(unsigned int timer_id);
void     clear_gptimer_pulse_hi(unsigned int timer_id);
void     enable_gptimers(uint16_t mask);
void     disable_gptimers(uint16_t mask);
void     disable_gptimers_sync(uint16_t mask);
uint16_t get_enabled_gptimers(void);
uint32_t get_gptimer_status(unsigned int group);
void     set_gptimer_status(unsigned int group, uint32_t value);

/*
 * All Blackfin system MMRs are padded to 32bits even if the register
 * itself is only 16bits.  So use a helper macro to streamline this.
 */
#define __BFP(m) u16 m; u16 __pad_##m

/*
 * bfin timer registers layout
 */
struct bfin_gptimer_regs {
	__BFP(config);
	u32 counter;
	u32 period;
	u32 width;
};

#undef __BFP

#endif
