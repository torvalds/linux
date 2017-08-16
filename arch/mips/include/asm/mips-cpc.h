/*
 * Copyright (C) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __MIPS_ASM_MIPS_CPC_H__
#define __MIPS_ASM_MIPS_CPC_H__

#include <linux/io.h>
#include <linux/types.h>

/* The base address of the CPC registers */
extern void __iomem *mips_cpc_base;

/**
 * mips_cpc_default_phys_base - retrieve the default physical base address of
 *                              the CPC
 *
 * Returns the default physical base address of the Cluster Power Controller
 * memory mapped registers. This is platform dependant & must therefore be
 * implemented per-platform.
 */
extern phys_addr_t mips_cpc_default_phys_base(void);

/**
 * mips_cpc_probe - probe for a Cluster Power Controller
 *
 * Attempt to detect the presence of a Cluster Power Controller. Returns 0 if
 * a CPC is successfully detected, else -errno.
 */
#ifdef CONFIG_MIPS_CPC
extern int mips_cpc_probe(void);
#else
static inline int mips_cpc_probe(void)
{
	return -ENODEV;
}
#endif

/**
 * mips_cpc_present - determine whether a Cluster Power Controller is present
 *
 * Returns true if a CPC is present in the system, else false.
 */
static inline bool mips_cpc_present(void)
{
#ifdef CONFIG_MIPS_CPC
	return mips_cpc_base != NULL;
#else
	return false;
#endif
}

/* Offsets from the CPC base address to various control blocks */
#define MIPS_CPC_GCB_OFS	0x0000
#define MIPS_CPC_CLCB_OFS	0x2000
#define MIPS_CPC_COCB_OFS	0x4000

/* Macros to ease the creation of register access functions */
#define BUILD_CPC_R_(name, off)					\
static inline u32 *addr_cpc_##name(void)			\
{								\
	return (u32 *)(mips_cpc_base + (off));			\
}								\
								\
static inline u32 read_cpc_##name(void)				\
{								\
	return __raw_readl(mips_cpc_base + (off));		\
}

#define BUILD_CPC__W(name, off) \
static inline void write_cpc_##name(u32 value)			\
{								\
	__raw_writel(value, mips_cpc_base + (off));		\
}

#define BUILD_CPC_RW(name, off)					\
	BUILD_CPC_R_(name, off)					\
	BUILD_CPC__W(name, off)

#define BUILD_CPC_Cx_R_(name, off)				\
	BUILD_CPC_R_(cl_##name, MIPS_CPC_CLCB_OFS + (off))	\
	BUILD_CPC_R_(co_##name, MIPS_CPC_COCB_OFS + (off))

#define BUILD_CPC_Cx__W(name, off)				\
	BUILD_CPC__W(cl_##name, MIPS_CPC_CLCB_OFS + (off))	\
	BUILD_CPC__W(co_##name, MIPS_CPC_COCB_OFS + (off))

#define BUILD_CPC_Cx_RW(name, off)				\
	BUILD_CPC_Cx_R_(name, off)				\
	BUILD_CPC_Cx__W(name, off)

/* GCB register accessor functions */
BUILD_CPC_RW(access,		MIPS_CPC_GCB_OFS + 0x00)
BUILD_CPC_RW(seqdel,		MIPS_CPC_GCB_OFS + 0x08)
BUILD_CPC_RW(rail,		MIPS_CPC_GCB_OFS + 0x10)
BUILD_CPC_RW(resetlen,		MIPS_CPC_GCB_OFS + 0x18)
BUILD_CPC_R_(revision,		MIPS_CPC_GCB_OFS + 0x20)

/* Core Local & Core Other accessor functions */
BUILD_CPC_Cx_RW(cmd,		0x00)
BUILD_CPC_Cx_RW(stat_conf,	0x08)
BUILD_CPC_Cx_RW(other,		0x10)
BUILD_CPC_Cx_RW(vp_stop,	0x20)
BUILD_CPC_Cx_RW(vp_run,		0x28)
BUILD_CPC_Cx_RW(vp_running,	0x30)

/* CPC_Cx_CMD register fields */
#define CPC_Cx_CMD_SHF				0
#define CPC_Cx_CMD_MSK				(_ULCAST_(0xf) << 0)
#define  CPC_Cx_CMD_CLOCKOFF			(_ULCAST_(0x1) << 0)
#define  CPC_Cx_CMD_PWRDOWN			(_ULCAST_(0x2) << 0)
#define  CPC_Cx_CMD_PWRUP			(_ULCAST_(0x3) << 0)
#define  CPC_Cx_CMD_RESET			(_ULCAST_(0x4) << 0)

/* CPC_Cx_STAT_CONF register fields */
#define CPC_Cx_STAT_CONF_PWRUPE_SHF		23
#define CPC_Cx_STAT_CONF_PWRUPE_MSK		(_ULCAST_(0x1) << 23)
#define CPC_Cx_STAT_CONF_SEQSTATE_SHF		19
#define CPC_Cx_STAT_CONF_SEQSTATE_MSK		(_ULCAST_(0xf) << 19)
#define  CPC_Cx_STAT_CONF_SEQSTATE_D0		(_ULCAST_(0x0) << 19)
#define  CPC_Cx_STAT_CONF_SEQSTATE_U0		(_ULCAST_(0x1) << 19)
#define  CPC_Cx_STAT_CONF_SEQSTATE_U1		(_ULCAST_(0x2) << 19)
#define  CPC_Cx_STAT_CONF_SEQSTATE_U2		(_ULCAST_(0x3) << 19)
#define  CPC_Cx_STAT_CONF_SEQSTATE_U3		(_ULCAST_(0x4) << 19)
#define  CPC_Cx_STAT_CONF_SEQSTATE_U4		(_ULCAST_(0x5) << 19)
#define  CPC_Cx_STAT_CONF_SEQSTATE_U5		(_ULCAST_(0x6) << 19)
#define  CPC_Cx_STAT_CONF_SEQSTATE_U6		(_ULCAST_(0x7) << 19)
#define  CPC_Cx_STAT_CONF_SEQSTATE_D1		(_ULCAST_(0x8) << 19)
#define  CPC_Cx_STAT_CONF_SEQSTATE_D3		(_ULCAST_(0x9) << 19)
#define  CPC_Cx_STAT_CONF_SEQSTATE_D2		(_ULCAST_(0xa) << 19)
#define CPC_Cx_STAT_CONF_CLKGAT_IMPL_SHF	17
#define CPC_Cx_STAT_CONF_CLKGAT_IMPL_MSK	(_ULCAST_(0x1) << 17)
#define CPC_Cx_STAT_CONF_PWRDN_IMPL_SHF		16
#define CPC_Cx_STAT_CONF_PWRDN_IMPL_MSK		(_ULCAST_(0x1) << 16)
#define CPC_Cx_STAT_CONF_EJTAG_PROBE_SHF	15
#define CPC_Cx_STAT_CONF_EJTAG_PROBE_MSK	(_ULCAST_(0x1) << 15)

/* CPC_Cx_OTHER register fields */
#define CPC_Cx_OTHER_CORENUM_SHF		16
#define CPC_Cx_OTHER_CORENUM_MSK		(_ULCAST_(0xff) << 16)

#ifdef CONFIG_MIPS_CPC

/**
 * mips_cpc_lock_other - lock access to another core
 * core: the other core to be accessed
 *
 * Call before operating upon a core via the 'other' register region in
 * order to prevent the region being moved during access. Must be called
 * within the bounds of a mips_cm_{lock,unlock}_other pair, and followed
 * by a call to mips_cpc_unlock_other.
 */
extern void mips_cpc_lock_other(unsigned int core);

/**
 * mips_cpc_unlock_other - unlock access to another core
 *
 * Call after operating upon another core via the 'other' register region.
 * Must be called after mips_cpc_lock_other.
 */
extern void mips_cpc_unlock_other(void);

#else /* !CONFIG_MIPS_CPC */

static inline void mips_cpc_lock_other(unsigned int core) { }
static inline void mips_cpc_unlock_other(void) { }

#endif /* !CONFIG_MIPS_CPC */

#endif /* __MIPS_ASM_MIPS_CPC_H__ */
