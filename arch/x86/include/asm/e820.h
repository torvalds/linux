#ifndef _ASM_X86_E820_H
#define _ASM_X86_E820_H

#ifdef CONFIG_EFI
#include <linux/numa.h>
#define E820_X_MAX (E820MAX + 3 * MAX_NUMNODES)
#else	/* ! CONFIG_EFI */
#define E820_X_MAX E820MAX
#endif
#include <uapi/asm/e820.h>
#ifndef __ASSEMBLY__
/* see comment in arch/x86/kernel/e820.c */
extern struct e820map e820;
extern struct e820map e820_saved;

extern unsigned long pci_mem_start;
extern int e820_any_mapped(u64 start, u64 end, unsigned type);
extern int e820_all_mapped(u64 start, u64 end, unsigned type);
extern void e820_add_region(u64 start, u64 size, int type);
extern void e820_print_map(char *who);
extern int
sanitize_e820_map(struct e820entry *biosmap, int max_nr_map, u32 *pnr_map);
extern u64 e820_update_range(u64 start, u64 size, unsigned old_type,
			       unsigned new_type);
extern u64 e820_remove_range(u64 start, u64 size, unsigned old_type,
			     int checktype);
extern void update_e820(void);
extern void e820_setup_gap(void);
extern int e820_search_gap(unsigned long *gapstart, unsigned long *gapsize,
			unsigned long start_addr, unsigned long long end_addr);
struct setup_data;
extern void parse_e820_ext(struct setup_data *data);

#if defined(CONFIG_X86_64) || \
	(defined(CONFIG_X86_32) && defined(CONFIG_HIBERNATION))
extern void e820_mark_nosave_regions(unsigned long limit_pfn);
#else
static inline void e820_mark_nosave_regions(unsigned long limit_pfn)
{
}
#endif

#ifdef CONFIG_MEMTEST
extern void early_memtest(unsigned long start, unsigned long end);
#else
static inline void early_memtest(unsigned long start, unsigned long end)
{
}
#endif

extern unsigned long e820_end_of_ram_pfn(void);
extern unsigned long e820_end_of_low_ram_pfn(void);
extern u64 early_reserve_e820(u64 sizet, u64 align);

void memblock_x86_fill(void);
void memblock_find_dma_reserve(void);

extern void finish_e820_parsing(void);
extern void e820_reserve_resources(void);
extern void e820_reserve_resources_late(void);
extern void setup_memory_map(void);
extern char *default_machine_specific_memory_setup(void);

/*
 * Returns true iff the specified range [s,e) is completely contained inside
 * the ISA region.
 */
static inline bool is_ISA_range(u64 s, u64 e)
{
	return s >= ISA_START_ADDRESS && e <= ISA_END_ADDRESS;
}

#endif /* __ASSEMBLY__ */
#include <linux/ioport.h>

#define HIGH_MEMORY	(1024*1024)
#endif /* _ASM_X86_E820_H */
