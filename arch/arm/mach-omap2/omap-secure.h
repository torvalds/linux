/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * omap-secure.h: OMAP Secure infrastructure header.
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 * Copyright (C) 2012 Ivaylo Dimitrov <freemangordon@abv.bg>
 * Copyright (C) 2013 Pali Rohár <pali.rohar@gmail.com>
 */
#ifndef OMAP_ARCH_OMAP_SECURE_H
#define OMAP_ARCH_OMAP_SECURE_H

#include <linux/types.h>

/* Monitor error code */
#define  API_HAL_RET_VALUE_NS2S_CONVERSION_ERROR	0xFFFFFFFE
#define  API_HAL_RET_VALUE_SERVICE_UNKNWON		0xFFFFFFFF

/* HAL API error codes */
#define  API_HAL_RET_VALUE_OK		0x00
#define  API_HAL_RET_VALUE_FAIL		0x01

/* Secure HAL API flags */
#define FLAG_START_CRITICAL		0x4
#define FLAG_IRQFIQ_MASK		0x3
#define FLAG_IRQ_ENABLE			0x2
#define FLAG_FIQ_ENABLE			0x1
#define NO_FLAG				0x0

/* Maximum Secure memory storage size */
#define OMAP_SECURE_RAM_STORAGE	(88 * SZ_1K)

#define OMAP3_SAVE_SECURE_RAM_SZ	0x803F

/* Secure low power HAL API index */
#define OMAP4_HAL_SAVESECURERAM_INDEX	0x1a
#define OMAP4_HAL_SAVEHW_INDEX		0x1b
#define OMAP4_HAL_SAVEALL_INDEX		0x1c
#define OMAP4_HAL_SAVEGIC_INDEX		0x1d

/* Secure Monitor mode APIs */
#define OMAP4_MON_SCU_PWR_INDEX		0x108
#define OMAP4_MON_L2X0_DBG_CTRL_INDEX	0x100
#define OMAP4_MON_L2X0_CTRL_INDEX	0x102
#define OMAP4_MON_L2X0_AUXCTRL_INDEX	0x109
#define OMAP4_MON_L2X0_PREFETCH_INDEX	0x113

#define OMAP5_DRA7_MON_SET_CNTFRQ_INDEX	0x109
#define OMAP5_MON_AMBA_IF_INDEX		0x108
#define OMAP5_DRA7_MON_SET_ACR_INDEX	0x107

/* Secure PPA(Primary Protected Application) APIs */
#define OMAP4_PPA_L2_POR_INDEX		0x23
#define OMAP4_PPA_CPU_ACTRL_SMP_INDEX	0x25

#define AM43xx_PPA_SVC_PM_SUSPEND	0x71
#define AM43xx_PPA_SVC_PM_RESUME	0x72

/* Secure RX-51 PPA (Primary Protected Application) APIs */
#define RX51_PPA_HWRNG			29
#define RX51_PPA_L2_INVAL		40
#define RX51_PPA_WRITE_ACR		42

#ifndef __ASSEMBLER__

extern u32 omap_secure_dispatcher(u32 idx, u32 flag, u32 nargs,
				u32 arg1, u32 arg2, u32 arg3, u32 arg4);
extern void omap_smccc_smc(u32 fn, u32 arg);
extern void omap_smc1(u32 fn, u32 arg);
extern u32 omap_smc2(u32 id, u32 falg, u32 pargs);
extern u32 omap_smc3(u32 id, u32 process, u32 flag, u32 pargs);
extern phys_addr_t omap_secure_ram_mempool_base(void);
extern int omap_secure_ram_reserve_memblock(void);
extern u32 save_secure_ram_context(u32 args_pa);
extern u32 omap3_save_secure_ram(void __iomem *save_regs, int size);

extern u32 rx51_secure_dispatcher(u32 idx, u32 process, u32 flag, u32 nargs,
				  u32 arg1, u32 arg2, u32 arg3, u32 arg4);
extern u32 rx51_secure_update_aux_cr(u32 set_bits, u32 clear_bits);
extern u32 rx51_secure_rng_call(u32 ptr, u32 count, u32 flag);

extern bool optee_available;
void omap_secure_init(void);

#ifdef CONFIG_SOC_HAS_REALTIME_COUNTER
void set_cntfreq(void);
#else
static inline void set_cntfreq(void)
{
}
#endif

#endif /* __ASSEMBLER__ */
#endif /* OMAP_ARCH_OMAP_SECURE_H */
