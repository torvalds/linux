/*
 * arch/sh/boards/se/7343/led.c
 *
 */
#include <linux/sched.h>
#include <asm/mach/se7343.h>

/* Cycle the LED's in the clasic Knightrider/Sun pattern */
void heartbeat_7343se(void)
{
	static unsigned int cnt = 0, period = 0;
	volatile unsigned short *p = (volatile unsigned short *) PA_LED;
	static unsigned bit = 0, up = 1;

	cnt += 1;
	if (cnt < period) {
		return;
	}

	cnt = 0;

	/* Go through the points (roughly!):
	 * f(0)=10, f(1)=16, f(2)=20, f(5)=35,f(inf)->110
	 */
	period = 110 - ((300 << FSHIFT) / ((avenrun[0] / 5) + (3 << FSHIFT)));

	if (up) {
		if (bit == 7) {
			bit--;
			up = 0;
		} else {
			bit++;
		}
	} else {
		if (bit == 0) {
			bit++;
			up = 1;
		} else {
			bit--;
		}
	}
	*p = 1 << (bit + LED_SHIFT);

}
