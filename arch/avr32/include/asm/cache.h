#ifndef __ASM_AVR32_CACHE_H
#define __ASM_AVR32_CACHE_H

#define L1_CACHE_SHIFT 5
#define L1_CACHE_BYTES (1 << L1_CACHE_SHIFT)

/*
 * Memory returned by kmalloc() may be used for DMA, so we must make
 * sure that all such allocations are cache aligned. Otherwise,
 * unrelated code may cause parts of the buffer to be read into the
 * cache before the transfer is done, causing old data to be seen by
 * the CPU.
 */
#define ARCH_DMA_MINALIGN	L1_CACHE_BYTES

#ifndef __ASSEMBLER__
struct cache_info {
	unsigned int ways;
	unsigned int sets;
	unsigned int linesz;
};
#endif /* __ASSEMBLER */

/* Cache operation constants */
#define ICACHE_FLUSH		0x00
#define ICACHE_INVALIDATE	0x01
#define ICACHE_LOCK		0x02
#define ICACHE_UNLOCK		0x03
#define ICACHE_PREFETCH		0x04

#define DCACHE_FLUSH		0x08
#define DCACHE_LOCK		0x09
#define DCACHE_UNLOCK		0x0a
#define DCACHE_INVALIDATE	0x0b
#define DCACHE_CLEAN		0x0c
#define DCACHE_CLEAN_INVAL	0x0d

#endif /* __ASM_AVR32_CACHE_H */
