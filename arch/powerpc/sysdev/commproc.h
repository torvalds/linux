#ifndef _POWERPC_SYSDEV_COMMPROC_H
#define _POWERPC_SYSDEV_COMMPROC_H

extern void cpm_reset(void);
extern void mpc8xx_restart(char *cmd);
extern void mpc8xx_calibrate_decr(void);
extern int mpc8xx_set_rtc_time(struct rtc_time *tm);
extern void mpc8xx_get_rtc_time(struct rtc_time *tm);
extern void m8xx_pic_init(void);
extern unsigned int mpc8xx_get_irq(void);

#endif
