#ifndef _ASM_E820_TYPES_H
#define _ASM_E820_TYPES_H

#include <uapi/asm/e820/types.h>

/*
 * The legacy E820 BIOS limits us to 128 (E820MAX) nodes due to the
 * constrained space in the zeropage.
 *
 * On large systems we can easily have thousands of nodes with RAM,
 * which cannot be fit into so few entries - so we have a mechanism
 * to extend the e820 table size at build-time, via the E820_X_MAX
 * define below.
 *
 * ( Those extra entries are enumerated via the EFI memory map, not
 *   via the legacy zeropage mechanism. )
 *
 * Size our internal memory map tables to have room for these additional
 * entries, based on a heuristic calculation: up to three entries per
 * NUMA node, plus E820MAX for some extra space.
 *
 * This allows for bootstrap/firmware quirks such as possible duplicate
 * E820 entries that might need room in the same arrays, prior to the
 * call to sanitize_e820_table() to remove duplicates.  The allowance
 * of three memory map entries per node is "enough" entries for
 * the initial hardware platform motivating this mechanism to make
 * use of additional EFI map entries.  Future platforms may want
 * to allow more than three entries per node or otherwise refine
 * this size.
 */

#include <linux/numa.h>

#define E820_X_MAX		(E820MAX + 3*MAX_NUMNODES)

/* Our map: */
#define E820MAP			0x2d0

/* Number of entries in E820MAP: */
#define E820NR			0x1e8

#define E820_RAM		1
#define E820_RESERVED		2
#define E820_ACPI		3
#define E820_NVS		4
#define E820_UNUSABLE		5
#define E820_PMEM		7

/*
 * This is a non-standardized way to represent ADR or NVDIMM regions that
 * persist over a reboot.  The kernel will ignore their special capabilities
 * unless the CONFIG_X86_PMEM_LEGACY option is set.
 *
 * ( Note that older platforms also used 6 for the same type of memory,
 *   but newer versions switched to 12 as 6 was assigned differently.  Some
 *   time they will learn... )
 */
#define E820_PRAM		12

/*
 * reserved RAM used by kernel itself
 * if CONFIG_INTEL_TXT is enabled, memory of this type will be
 * included in the S3 integrity calculation and so should not include
 * any memory that BIOS might alter over the S3 transition
 */
#define E820_RESERVED_KERN	128

/*
 * The whole array of E820 entries:
 */
struct e820_table {
	__u32 nr_map;
	struct e820_entry map[E820_X_MAX];
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
