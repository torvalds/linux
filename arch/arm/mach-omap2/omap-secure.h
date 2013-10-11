/*
 * omap-secure.h: OMAP Secure infrastructure header.
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 * Copyright (C) 2012 Ivaylo Dimitrov <freemangordon@abv.bg>
 * Copyright (C) 2013 Pali Rohár <pali.rohar@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef OMAP_ARCH_OMAP_SECURE_H
#define OMAP_ARCH_OMAP_SECURE_H

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

/* Secure PPA(Primary Protected Application) APIs */
#define OMAP4_PPA_L2_POR_INDEX		0x23
#define OMAP4_PPA_CPU_ACTRL_SMP_INDEX	0x25

/* Secure RX-51 PPA (Primary Protected Application) APIs */
#define RX51_PPA_HWRNG			29
#define RX51_PPA_L2_INVAL		40
#define RX51_PPA_WRITE_ACR		42

#ifndef __ASSEMBLER__

extern u32 omap_secure_dispatcher(u32 idx, u32 flag, u32 nargs,
				u32 arg1, u32 arg2, u32 arg3, u32 arg4);
extern u32 omap_smc2(u32 id, u32 falg, u32 pargs);
extern u32 omap_smc3(u32 id, u32 process, u32 flag, u32 pargs);
extern phys_addr_t omap_secure_ram_mempool_base(void);
extern int omap_secure_ram_reserve_memblock(void);

extern u32 rx51_secure_dispatcher(u32 idx, u32 process, u32 flag, u32 nargs,
				  u32 arg1, u32 arg2, u32 arg3, u32 arg4);
extern u32 rx51_secure_update_aux_cr(u32 set_bits, u32 clear_bits);
extern u32 rx51_secure_rng_call(u32 ptr, u32 count, u32 flag);

#ifdef CONFIG_OMAP4_ERRATA_I688
extern int omap_barrier_reserve_memblock(void);
#else
static inline void omap_barrier_reserve_memblock(void)
{ }
#endif
#endif /* __ASSEMBLER__ */
#endif /* OMAP_ARCH_OMAP_SECURE_H */
