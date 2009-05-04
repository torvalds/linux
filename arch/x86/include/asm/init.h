#ifndef _ASM_X86_INIT_32_H
#define _ASM_X86_INIT_32_H

#ifdef CONFIG_X86_32
extern void __init early_ioremap_page_table_range_init(void);
#endif

extern unsigned long __init
kernel_physical_mapping_init(unsigned long start,
			     unsigned long end,
			     unsigned long page_size_mask);


extern unsigned long __initdata e820_table_start;
extern unsigned long __meminitdata e820_table_end;
extern unsigned long __meminitdata e820_table_top;

#endif /* _ASM_X86_INIT_32_H */
