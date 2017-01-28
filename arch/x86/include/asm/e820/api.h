#ifndef _ASM_E820_API_H
#define _ASM_E820_API_H

#include <asm/e820/types.h>

extern struct e820_table *e820_table;
extern struct e820_table *e820_table_firmware;

extern unsigned long pci_mem_start;

extern int  e820__mapped_any(u64 start, u64 end, unsigned type);
extern int  e820__mapped_all(u64 start, u64 end, unsigned type);

extern void e820__range_add   (u64 start, u64 size, int type);
extern u64  e820__range_update(u64 start, u64 size, unsigned old_type, unsigned new_type);
extern u64  e820__range_remove(u64 start, u64 size, unsigned old_type, int checktype);

extern void e820_print_map(char *who);
extern int  e820__update_table(struct e820_entry *biosmap, int max_nr_map, u32 *pnr_map);
extern void e820__update_table_print(void);
extern void e820__setup_pci_gap(void);
extern void e820__memory_setup_extended(u64 phys_addr, u32 data_len);
extern unsigned long e820_end_of_ram_pfn(void);
extern unsigned long e820_end_of_low_ram_pfn(void);
extern u64  e820__memblock_alloc_reserved(u64 sizet, u64 align);
extern void e820__memblock_setup(void);
extern void e820_reserve_setup_data(void);
extern void e820__finish_early_params(void);
extern void e820_reserve_resources(void);
extern void e820_reserve_resources_late(void);
extern void e820__memory_setup(void);
extern char *e820__memory_setup_default(void);
extern void e820_reallocate_tables(void);
extern void e820_mark_nosave_regions(unsigned long limit_pfn);

/*
 * Returns true iff the specified range [start,end) is completely contained inside
 * the ISA region.
 */
static inline bool is_ISA_range(u64 start, u64 end)
{
	return start >= ISA_START_ADDRESS && end <= ISA_END_ADDRESS;
}

#endif /* _ASM_E820_API_H */
