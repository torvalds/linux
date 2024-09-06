/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Rivos, Inc
 */

#ifndef _ASM_VENDOR_EXTENSIONS_H
#define _ASM_VENDOR_EXTENSIONS_H

#include <asm/cpufeature.h>

#include <linux/array_size.h>
#include <linux/types.h>

/*
 * The extension keys of each vendor must be strictly less than this value.
 */
#define RISCV_ISA_VENDOR_EXT_MAX 32

struct riscv_isavendorinfo {
	DECLARE_BITMAP(isa, RISCV_ISA_VENDOR_EXT_MAX);
};

struct riscv_isa_vendor_ext_data_list {
	bool is_initialized;
	const size_t ext_data_count;
	const struct riscv_isa_ext_data *ext_data;
	struct riscv_isavendorinfo per_hart_isa_bitmap[NR_CPUS];
	struct riscv_isavendorinfo all_harts_isa_bitmap;
};

extern struct riscv_isa_vendor_ext_data_list *riscv_isa_vendor_ext_list[];

extern const size_t riscv_isa_vendor_ext_list_size;

/*
 * The alternatives need some way of distinguishing between vendor extensions
 * and errata. Incrementing all of the vendor extension keys so they are at
 * least 0x8000 accomplishes that.
 */
#define RISCV_VENDOR_EXT_ALTERNATIVES_BASE	0x8000

#define VENDOR_EXT_ALL_CPUS			-1

bool __riscv_isa_vendor_extension_available(int cpu, unsigned long vendor, unsigned int bit);
#define riscv_cpu_isa_vendor_extension_available(cpu, vendor, ext)	\
	__riscv_isa_vendor_extension_available(cpu, vendor, RISCV_ISA_VENDOR_EXT_##ext)
#define riscv_isa_vendor_extension_available(vendor, ext)	\
	__riscv_isa_vendor_extension_available(VENDOR_EXT_ALL_CPUS, vendor, \
					       RISCV_ISA_VENDOR_EXT_##ext)

static __always_inline bool riscv_has_vendor_extension_likely(const unsigned long vendor,
							      const unsigned long ext)
{
	if (!IS_ENABLED(CONFIG_RISCV_ISA_VENDOR_EXT))
		return false;

	if (IS_ENABLED(CONFIG_RISCV_ALTERNATIVE))
		return __riscv_has_extension_likely(vendor,
						    ext + RISCV_VENDOR_EXT_ALTERNATIVES_BASE);

	return __riscv_isa_vendor_extension_available(VENDOR_EXT_ALL_CPUS, vendor, ext);
}

static __always_inline bool riscv_has_vendor_extension_unlikely(const unsigned long vendor,
								const unsigned long ext)
{
	if (!IS_ENABLED(CONFIG_RISCV_ISA_VENDOR_EXT))
		return false;

	if (IS_ENABLED(CONFIG_RISCV_ALTERNATIVE))
		return __riscv_has_extension_unlikely(vendor,
						      ext + RISCV_VENDOR_EXT_ALTERNATIVES_BASE);

	return __riscv_isa_vendor_extension_available(VENDOR_EXT_ALL_CPUS, vendor, ext);
}

static __always_inline bool riscv_cpu_has_vendor_extension_likely(const unsigned long vendor,
								  int cpu, const unsigned long ext)
{
	if (!IS_ENABLED(CONFIG_RISCV_ISA_VENDOR_EXT))
		return false;

	if (IS_ENABLED(CONFIG_RISCV_ALTERNATIVE) &&
	    __riscv_has_extension_likely(vendor, ext + RISCV_VENDOR_EXT_ALTERNATIVES_BASE))
		return true;

	return __riscv_isa_vendor_extension_available(cpu, vendor, ext);
}

static __always_inline bool riscv_cpu_has_vendor_extension_unlikely(const unsigned long vendor,
								    int cpu,
								    const unsigned long ext)
{
	if (!IS_ENABLED(CONFIG_RISCV_ISA_VENDOR_EXT))
		return false;

	if (IS_ENABLED(CONFIG_RISCV_ALTERNATIVE) &&
	    __riscv_has_extension_unlikely(vendor, ext + RISCV_VENDOR_EXT_ALTERNATIVES_BASE))
		return true;

	return __riscv_isa_vendor_extension_available(cpu, vendor, ext);
}

#endif /* _ASM_VENDOR_EXTENSIONS_H */
