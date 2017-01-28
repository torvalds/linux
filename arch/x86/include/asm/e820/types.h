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
 * call to e820__update_table() to remove duplicates.  The allowance
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

/*
 * The whole array of E820 entries:
 */
struct e820_table {
	__u32 nr_entries;
	struct e820_entry entries[E820_X_MAX];
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
