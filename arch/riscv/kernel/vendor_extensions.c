// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Rivos, Inc
 */

#include <asm/vendorid_list.h>
#include <asm/vendor_extensions.h>
#include <asm/vendor_extensions/andes.h>

#include <linux/array_size.h>
#include <linux/types.h>

struct riscv_isa_vendor_ext_data_list *riscv_isa_vendor_ext_list[] = {
#ifdef CONFIG_RISCV_ISA_VENDOR_EXT_ANDES
	&riscv_isa_vendor_ext_list_andes,
#endif
};

const size_t riscv_isa_vendor_ext_list_size = ARRAY_SIZE(riscv_isa_vendor_ext_list);

/**
 * __riscv_isa_vendor_extension_available() - Check whether given vendor
 * extension is available or not.
 *
 * @cpu: check if extension is available on this cpu
 * @vendor: vendor that the extension is a member of
 * @bit: bit position of the desired extension
 * Return: true or false
 *
 * NOTE: When cpu is -1, will check if extension is available on all cpus
 */
bool __riscv_isa_vendor_extension_available(int cpu, unsigned long vendor, unsigned int bit)
{
	struct riscv_isavendorinfo *bmap;
	struct riscv_isavendorinfo *cpu_bmap;

	switch (vendor) {
	#ifdef CONFIG_RISCV_ISA_VENDOR_EXT_ANDES
	case ANDES_VENDOR_ID:
		bmap = &riscv_isa_vendor_ext_list_andes.all_harts_isa_bitmap;
		cpu_bmap = riscv_isa_vendor_ext_list_andes.per_hart_isa_bitmap;
		break;
	#endif
	default:
		return false;
	}

	if (cpu != -1)
		bmap = &cpu_bmap[cpu];

	if (bit >= RISCV_ISA_VENDOR_EXT_MAX)
		return false;

	return test_bit(bit, bmap->isa) ? true : false;
}
EXPORT_SYMBOL_GPL(__riscv_isa_vendor_extension_available);
