/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_E820_API_H
#define _ASM_E820_API_H

#include <asm/e820/types.h>

extern struct e820_table *e820_table;
extern struct e820_table *e820_table_kexec;
extern struct e820_table *e820_table_firmware;

extern unsigned long pci_mem_start;

extern bool e820__mapped_raw_any(u64 start, u64 end, enum e820_type type);
extern bool e820__mapped_any(u64 start, u64 end, enum e820_type type);
extern bool e820__mapped_all(u64 start, u64 end, enum e820_type type);

extern void e820__range_add   (u64 start, u64 size, enum e820_type type);
extern u64  e820__range_update(u64 start, u64 size, enum e820_type old_type, enum e820_type new_type);
extern u64  e820__range_remove(u64 start, u64 size, enum e820_type old_type, bool check_type);
extern u64  e820__range_update_table(struct e820_table *t, u64 start, u64 size, enum e820_type old_type, enum e820_type new_type);

extern void e820__print_table(char *who);
extern int  e820__update_table(struct e820_table *table);
extern void e820__update_table_print(void);

extern unsigned long e820__end_of_ram_pfn(void);
extern unsigned long e820__end_of_low_ram_pfn(void);

extern u64  e820__memblock_alloc_reserved(u64 size, u64 align);
extern void e820__memblock_setup(void);

extern void e820__finish_early_params(void);
extern void e820__reserve_resources(void);
extern void e820__reserve_resources_late(void);

extern void e820__memory_setup(void);
extern void e820__memory_setup_extended(u64 phys_addr, u32 data_len);
extern char *e820__memory_setup_default(void);
extern void e820__setup_pci_gap(void);

extern void e820__reallocate_tables(void);
extern void e820__register_nosave_regions(unsigned long limit_pfn);

extern int  e820__get_entry_type(u64 start, u64 end);

/*
 * Returns true iff the specified range [start,end) is completely contained inside
 * the ISA region.
 */
static inline bool is_ISA_range(u64 start, u64 end)
{
	return start >= ISA_START_ADDRESS && end <= ISA_END_ADDRESS;
}

#endif /* _ASM_E820_API_H */
