/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Use prescale timer at 25000 Hz instead of the baudrate timer at 
 * 19200 to get rid of the 64ppm to fast timer (and we get better 
 * resolution within a jiffie as well. 
 */
#ifndef _ASM_CRIS_ARCH_TIMEX_H
#define _ASM_CRIS_ARCH_TIMEX_H

/* The prescaler clock runs at 25MHz, we divide it by 1000 in the prescaler */
/* If you change anything here you must check time.c as well... */
#define PRESCALE_FREQ 25000000
#define PRESCALE_VALUE 1000
#define CLOCK_TICK_RATE 25000 /* Underlying frequency of the HZ timer */
/* The timer0 values gives 40us resolution (1/25000) but interrupts at HZ*/
#define TIMER0_FREQ (CLOCK_TICK_RATE)
#define TIMER0_CLKSEL flexible
#define TIMER0_DIV (TIMER0_FREQ/(HZ))


#define GET_JIFFIES_USEC() \
  ( (TIMER0_DIV - *R_TIMER0_DATA) * (1000000/HZ)/TIMER0_DIV )

unsigned long get_ns_in_jiffie(void);

static inline unsigned long get_us_in_jiffie_highres(void)
{
	return get_ns_in_jiffie()/1000;
}

#endif
