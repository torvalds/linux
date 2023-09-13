/* SPDX-License-Identifier: GPL-2.0-only */

struct rtc_time;

/* ataints.c */
void atari_init_IRQ(void);

/* atasound.c */
void atari_microwire_cmd(int cmd);
void atari_mksound(unsigned int hz, unsigned int ticks);

/* time.c */
void atari_sched_init(void);
int atari_mste_hwclk(int op, struct rtc_time *t);
int atari_tt_hwclk(int op, struct rtc_time *t);
