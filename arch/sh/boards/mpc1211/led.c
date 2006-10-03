/*
 * linux/arch/sh/boards/mpc1211/led.c
 *
 * Copyright (C) 2001  Saito.K & Jeanne
 *
 * This file contains Interface MPC-1211 specific LED code.
 */


static void mach_led(int position, int value)
{
	volatile unsigned char* p = (volatile unsigned char*)0xa2000000;

	if (value) {
		*p |= 1;
	} else {
		*p &= ~1;
	}
}

#ifdef CONFIG_HEARTBEAT

#include <linux/sched.h>

/* Cycle the LED's in the clasic Knightrider/Sun pattern */
void heartbeat_mpc1211(void)
{
	static unsigned int cnt = 0, period = 0;
	volatile unsigned char* p = (volatile unsigned char*)0xa2000000;
	static unsigned bit = 0, up = 1;

	cnt += 1;
	if (cnt < period) {
		return;
	}

	cnt = 0;

	/* Go through the points (roughly!):
	 * f(0)=10, f(1)=16, f(2)=20, f(5)=35,f(inf)->110
	 */
	period = 110 - ( (300<<FSHIFT)/
			 ((avenrun[0]/5) + (3<<FSHIFT)) );

	if (up) {
		if (bit == 7) {
			bit--;
			up=0;
		} else {
			bit ++;
		}
	} else {
		if (bit == 0) {
			bit++;
			up=1;
		} else {
			bit--;
		}
	}
	*p = 1<<bit;

}
#endif /* CONFIG_HEARTBEAT */
