/* SPDX-License-Identifier: GPL-2.0 */

struct rtc_time;

/* baboon.c */
void baboon_init(void);

/* iop.c */
void iop_init(void);

/* misc.c */
int mac_hwclk(int op, struct rtc_time *t);

/* macboing.c */
void mac_mksound(unsigned int freq, unsigned int length);

/* oss.c */
void oss_init(void);

/* psc.c */
void psc_init(void);

/* via.c */
void via_init(void);
void via_init_clock(void);
