/* SPDX-License-Identifier: GPL-2.0 */
#ifndef SUN3X_TIME_H
#define SUN3X_TIME_H

extern int sun3x_hwclk(int set, struct rtc_time *t);
void sun3x_sched_init(void);

struct mostek_dt {
	volatile unsigned char csr;
	volatile unsigned char sec;
	volatile unsigned char min;
	volatile unsigned char hour;
	volatile unsigned char wday;
	volatile unsigned char mday;
	volatile unsigned char month;
	volatile unsigned char year;
};

#endif
