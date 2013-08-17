#ifndef __H8300_TIMER_H
#define __H8300_TIMER_H

void h8300_timer_tick(void);
void h8300_timer_setup(void);
void h8300_gettod(unsigned int *year, unsigned int *mon, unsigned int *day,
		   unsigned int *hour, unsigned int *min, unsigned int *sec);

#define TIMER_FREQ (CONFIG_CPU_CLOCK*10000) /* Timer input freq. */

#define calc_param(cnt, div, rate, limit)			\
do {								\
	cnt = TIMER_FREQ / HZ;					\
	for (div = 0; div < ARRAY_SIZE(divide_rate); div++) {	\
		if (rate[div] == 0)				\
			continue;				\
		if ((cnt / rate[div]) > limit)			\
			break;					\
	}							\
	if (div == ARRAY_SIZE(divide_rate))			\
		panic("Timer counter overflow");		\
	cnt /= divide_rate[div];				\
} while(0)

#endif
