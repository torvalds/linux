#ifndef __LM7000_H
#define __LM7000_H

#define LM7000_DATA	(1 << 0)
#define LM7000_CLK	(1 << 1)
#define LM7000_CE	(1 << 2)

#define LM7000_FREQ_MASK 0x3fff
#define LM7000_BIT_T0	(1 << 14)
#define LM7000_BIT_T1	(1 << 15)
#define LM7000_BIT_B0	(1 << 16)
#define LM7000_BIT_B1	(1 << 17)
#define LM7000_BIT_B2	(1 << 18)
#define LM7000_BIT_TB	(1 << 19)
#define LM7000_FM_100	(0 << 20)
#define LM7000_FM_50	(1 << 20)
#define LM7000_FM_25	(2 << 20)
#define LM7000_AM_5	(3 << 20)
#define LM7000_AM_10	(4 << 20)
#define LM7000_AM_9	(5 << 20)
#define LM7000_AM_1	(6 << 20)
#define LM7000_AM_5_	(7 << 20)
#define LM7000_BIT_FM	(1 << 23)


struct lm7000 {
	void (*set_pins)(struct lm7000 *lm, u8 pins);
};

void lm7000_set_freq(struct lm7000 *lm, u32 freq);

#endif /* __LM7000_H */
