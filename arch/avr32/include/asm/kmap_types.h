#ifndef __ASM_AVR32_KMAP_TYPES_H
#define __ASM_AVR32_KMAP_TYPES_H

#ifdef CONFIG_DEBUG_HIGHMEM
# define KM_TYPE_NR 29
#else
# define KM_TYPE_NR 14
#endif

#endif /* __ASM_AVR32_KMAP_TYPES_H */
