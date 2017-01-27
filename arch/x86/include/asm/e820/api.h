#ifndef _ASM_E820_API_H
#define _ASM_E820_API_H

#include <asm/e820/types.h>

/* see comment in arch/x86/kernel/e820.c */
extern struct e820_array *e820_array;
extern struct e820_array *e820_array_saved;

extern unsigned long pci_mem_start;

extern int  e820_any_mapped(u64 start, u64 end, unsigned type);
extern int  e820_all_mapped(u64 start, u64 end, unsigned type);
extern void e820_add_region(u64 start, u64 size, int type);
extern void e820_print_map(char *who);
extern int  sanitize_e820_array(struct e820_entry *biosmap, int max_nr_map, u32 *pnr_map);
extern u64  e820_update_range(u64 start, u64 size, unsigned old_type, unsigned new_type);
extern u64  e820_remove_range(u64 start, u64 size, unsigned old_type, int checktype);
extern void update_e820(void);
extern void e820_setup_gap(void);
extern void parse_e820_ext(u64 phys_addr, u32 data_len);
extern unsigned long e820_end_of_ram_pfn(void);
extern unsigned long e820_end_of_low_ram_pfn(void);
extern u64  early_reserve_e820(u64 sizet, u64 align);
extern void memblock_x86_fill(void);
extern void memblock_find_dma_reserve(void);
extern void finish_e820_parsing(void);
extern void e820_reserve_resources(void);
extern void e820_reserve_resources_late(void);
extern void setup_memory_map(void);
extern char *default_machine_specific_memory_setup(void);
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
