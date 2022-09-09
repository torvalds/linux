/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This contains all required hardware related helper functions for
 * Trace Buffer Extension (TRBE) driver in the coresight framework.
 *
 * Copyright (C) 2020 ARM Ltd.
 *
 * Author: Anshuman Khandual <anshuman.khandual@arm.com>
 */
#include <linux/coresight.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/smp.h>

#include "coresight-etm-perf.h"

static inline bool is_trbe_available(void)
{
	u64 aa64dfr0 = read_sysreg_s(SYS_ID_AA64DFR0_EL1);
	unsigned int trbe = cpuid_feature_extract_unsigned_field(aa64dfr0, ID_AA64DFR0_TRBE_SHIFT);

	return trbe >= 0b0001;
}

static inline bool is_trbe_enabled(void)
{
	u64 trblimitr = read_sysreg_s(SYS_TRBLIMITR_EL1);

	return trblimitr & TRBLIMITR_ENABLE;
}

#define TRBE_EC_OTHERS		0
#define TRBE_EC_STAGE1_ABORT	36
#define TRBE_EC_STAGE2_ABORT	37

static inline int get_trbe_ec(u64 trbsr)
{
	return (trbsr >> TRBSR_EC_SHIFT) & TRBSR_EC_MASK;
}

#define TRBE_BSC_NOT_STOPPED 0
#define TRBE_BSC_FILLED      1
#define TRBE_BSC_TRIGGERED   2

static inline int get_trbe_bsc(u64 trbsr)
{
	return (trbsr >> TRBSR_BSC_SHIFT) & TRBSR_BSC_MASK;
}

static inline void clr_trbe_irq(void)
{
	u64 trbsr = read_sysreg_s(SYS_TRBSR_EL1);

	trbsr &= ~TRBSR_IRQ;
	write_sysreg_s(trbsr, SYS_TRBSR_EL1);
}

static inline bool is_trbe_irq(u64 trbsr)
{
	return trbsr & TRBSR_IRQ;
}

static inline bool is_trbe_trg(u64 trbsr)
{
	return trbsr & TRBSR_TRG;
}

static inline bool is_trbe_wrap(u64 trbsr)
{
	return trbsr & TRBSR_WRAP;
}

static inline bool is_trbe_abort(u64 trbsr)
{
	return trbsr & TRBSR_ABORT;
}

static inline bool is_trbe_running(u64 trbsr)
{
	return !(trbsr & TRBSR_STOP);
}

#define TRBE_TRIG_MODE_STOP		0
#define TRBE_TRIG_MODE_IRQ		1
#define TRBE_TRIG_MODE_IGNORE		3

#define TRBE_FILL_MODE_FILL		0
#define TRBE_FILL_MODE_WRAP		1
#define TRBE_FILL_MODE_CIRCULAR_BUFFER	3

static inline bool get_trbe_flag_update(u64 trbidr)
{
	return trbidr & TRBIDR_FLAG;
}

static inline bool is_trbe_programmable(u64 trbidr)
{
	return !(trbidr & TRBIDR_PROG);
}

static inline int get_trbe_address_align(u64 trbidr)
{
	return (trbidr >> TRBIDR_ALIGN_SHIFT) & TRBIDR_ALIGN_MASK;
}

static inline unsigned long get_trbe_write_pointer(void)
{
	return read_sysreg_s(SYS_TRBPTR_EL1);
}

static inline void set_trbe_write_pointer(unsigned long addr)
{
	WARN_ON(is_trbe_enabled());
	write_sysreg_s(addr, SYS_TRBPTR_EL1);
}

static inline unsigned long get_trbe_limit_pointer(void)
{
	u64 trblimitr = read_sysreg_s(SYS_TRBLIMITR_EL1);
	unsigned long addr = trblimitr & (TRBLIMITR_LIMIT_MASK << TRBLIMITR_LIMIT_SHIFT);

	WARN_ON(!IS_ALIGNED(addr, PAGE_SIZE));
	return addr;
}

static inline unsigned long get_trbe_base_pointer(void)
{
	u64 trbbaser = read_sysreg_s(SYS_TRBBASER_EL1);
	unsigned long addr = trbbaser & (TRBBASER_BASE_MASK << TRBBASER_BASE_SHIFT);

	WARN_ON(!IS_ALIGNED(addr, PAGE_SIZE));
	return addr;
}

static inline void set_trbe_base_pointer(unsigned long addr)
{
	WARN_ON(is_trbe_enabled());
	WARN_ON(!IS_ALIGNED(addr, (1UL << TRBBASER_BASE_SHIFT)));
	WARN_ON(!IS_ALIGNED(addr, PAGE_SIZE));
	write_sysreg_s(addr, SYS_TRBBASER_EL1);
}
