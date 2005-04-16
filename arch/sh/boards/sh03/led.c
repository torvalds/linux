/*
 * linux/arch/sh/boards/sh03/led.c
 *
 * Copyright (C) 2004  Saito.K Interface Corporation.
 *
 * This file contains Interface CTP/PCI-SH03 specific LED code.
 */

#include <linux/config.h>
#include <linux/sched.h>

/* Cycle the LED's in the clasic Knightrider/Sun pattern */
void heartbeat_sh03(void)
{
	static unsigned int cnt = 0, period = 0;
	volatile unsigned char* p = (volatile unsigned char*)0xa0800000;
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
