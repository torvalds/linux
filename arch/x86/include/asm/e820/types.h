/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_E820_TYPES_H
#define _ASM_E820_TYPES_H

#include <uapi/asm/bootparam.h>

/*
 * These are the E820 types known to the kernel:
 */
enum e820_type {
	E820_TYPE_RAM		= 1,
	E820_TYPE_RESERVED	= 2,
	E820_TYPE_ACPI		= 3,
	E820_TYPE_NVS		= 4,
	E820_TYPE_UNUSABLE	= 5,
	E820_TYPE_PMEM		= 7,

	/*
	 * This is a non-standardized way to represent ADR or
	 * NVDIMM regions that persist over a reboot.
	 *
	 * The kernel will ignore their special capabilities
	 * unless the CONFIG_X86_PMEM_LEGACY=y option is set.
	 *
	 * ( Note that older platforms also used 6 for the same
	 *   type of memory, but newer versions switched to 12 as
	 *   6 was assigned differently. Some time they will learn... )
	 */
	E820_TYPE_PRAM		= 12,

	/*
	 * Reserved RAM used by the kernel itself if
	 * CONFIG_INTEL_TXT=y is enabled, memory of this type
	 * will be included in the S3 integrity calculation
	 * and so should not include any memory that the BIOS
	 * might alter over the S3 transition:
	 */
	E820_TYPE_RESERVED_KERN	= 128,
};

/*
 * A single E820 map entry, describing a memory range of [addr...addr+size-1],
 * of 'type' memory type:
 *
 * (We pack it because there can be thousands of them on large systems.)
 */
struct e820_entry {
	u64			addr;
	u64			size;
	enum e820_type		type;
} __attribute__((packed));

/*
 * The legacy E820 BIOS limits us to 128 (E820_MAX_ENTRIES_ZEROPAGE) nodes
 * due to the constrained space in the zeropage.
 *
 * On large systems we can easily have thousands of nodes with RAM,
 * which cannot be fit into so few entries - so we have a mechanism
 * to extend the e820 table size at build-time, via the E820_MAX_ENTRIES
 * define below.
 *
 * ( Those extra entries are enumerated via the EFI memory map, not
 *   via the legacy zeropage mechanism. )
 *
 * Size our internal memory map tables to have room for these additional
 * entries, based on a heuristic calculation: up to three entries per
 * NUMA node, plus E820_MAX_ENTRIES_ZEROPAGE for some extra space.
 *
 * This allows for bootstrap/firmware quirks such as possible duplicate
 * E820 entries that might need room in the same arrays, prior to the
 * call to e820__update_table() to remove duplicates.  The allowance
 * of three memory map entries per node is "enough" entries for
 * the initial hardware platform motivating this mechanism to make
 * use of additional EFI map entries.  Future platforms may want
 * to allow more than three entries per node or otherwise refine
 * this size.
 */

#include <linux/numa.h>

#define E820_MAX_ENTRIES	(E820_MAX_ENTRIES_ZEROPAGE + 3*MAX_NUMNODES)

/*
 * The whole array of E820 entries:
 */
struct e820_table {
	__u32 nr_entries;
	struct e820_entry entries[E820_MAX_ENTRIES];
};

/*
 * Various well-known legacy memory ranges in physical memory:
 */
#define ISA_START_ADDRESS	0x000a0000
#define ISA_END_ADDRESS		0x00100000

#define BIOS_BEGIN		0x000a0000
#define BIOS_END		0x00100000

#define HIGH_MEMORY		0x00100000

#define BIOS_ROM_BASE		0xffe00000
#define BIOS_ROM_END		0xffffffff

#endif /* _ASM_E820_TYPES_H */
