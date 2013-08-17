/* This handles the memory map.. */

#if defined(CONFIG_RAMBASE)
#define PAGE_OFFSET_RAW		CONFIG_RAMBASE
#elif defined(CONFIG_SUN3)
#define PAGE_OFFSET_RAW		0x0E000000
#else
#define PAGE_OFFSET_RAW		0x00000000
#endif
