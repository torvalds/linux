#ifndef __PPC_FSL_SOC_H
#define __PPC_FSL_SOC_H
#ifdef __KERNEL__

#include <asm/mmu.h>

extern phys_addr_t get_immrbase(void);
extern u32 get_brgfreq(void);
extern u32 get_baudrate(void);

#endif
#endif
