/*
 * omap-secure.h: OMAP Secure infrastructure header.
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
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


/* Secure low power HAL API index */
#define OMAP4_HAL_SAVESECURERAM_INDEX	0x1a
#define OMAP4_HAL_SAVEHW_INDEX		0x1b
#define OMAP4_HAL_SAVEALL_INDEX		0x1c
#define OMAP4_HAL_SAVEGIC_INDEX		0x1d

extern u32 omap_secure_dispatcher(u32 idx, u32 flag, u32 nargs,
				u32 arg1, u32 arg2, u32 arg3, u32 arg4);
extern u32 omap_smc2(u32 id, u32 falg, u32 pargs);

#endif /* OMAP_ARCH_OMAP_SECURE_H */
