#ifndef _ASM_X86_INIT_32_H
#define _ASM_X86_INIT_32_H

#ifdef CONFIG_X86_32
extern void __init early_ioremap_page_table_range_init(void);
#endif

extern void __init zone_sizes_init(void);

extern unsigned long __init
kernel_physical_mapping_init(unsigned long start,
			     unsigned long end,
			     unsigned long page_size_mask);

#endif /* _ASM_X86_INIT_32_H */
