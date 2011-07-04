
#ifndef __DRIVER_IRDA_SMC_H
#define __DRIVER_IRDA_SMC_H

extern int smc0_init(u8 **base_addr);
extern void smc0_exit(void);
extern int smc0_write(u32 addr, u32 data);
extern int smc0_read(u32 addr);
extern int smc0_enable(int enable);
#endif

