/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_S390_DMA_TYPES_H_
#define _ASM_S390_DMA_TYPES_H_

#include <linux/types.h>
#include <linux/io.h>

/*
 * typedef dma32_t
 * Contains a 31 bit absolute address to a DMA capable piece of storage.
 *
 * For CIO, DMA addresses are always absolute addresses. These addresses tend
 * to be used in architectured memory blocks (like ORB, IDAW, MIDAW). Under
 * certain circumstances 31 bit wide addresses must be used because the
 * address must fit in 31 bits.
 *
 * This type is to be used when such fields can be modelled as 32 bit wide.
 */
typedef u32 __bitwise dma32_t;

/*
 * typedef dma64_t
 * Contains a 64 bit absolute address to a DMA capable piece of storage.
 *
 * For CIO, DMA addresses are always absolute addresses. These addresses tend
 * to be used in architectured memory blocks (like ORB, IDAW, MIDAW).
 *
 * This type is to be used to model such 64 bit wide fields.
 */
typedef u64 __bitwise dma64_t;

/*
 * Although DMA addresses should be obtained using the DMA API, in cases when
 * it is known that the first argument holds a virtual address that points to
 * DMA-able 31 bit addressable storage, then this function can be safely used.
 */
static inline dma32_t virt_to_dma32(void *ptr)
{
	return (__force dma32_t)__pa32(ptr);
}

static inline void *dma32_to_virt(dma32_t addr)
{
	return __va((__force unsigned long)addr);
}

static inline dma32_t u32_to_dma32(u32 addr)
{
	return (__force dma32_t)addr;
}

static inline u32 dma32_to_u32(dma32_t addr)
{
	return (__force u32)addr;
}

static inline dma32_t dma32_add(dma32_t a, u32 b)
{
	return (__force dma32_t)((__force u32)a + b);
}

static inline dma32_t dma32_and(dma32_t a, u32 b)
{
	return (__force dma32_t)((__force u32)a & b);
}

/*
 * Although DMA addresses should be obtained using the DMA API, in cases when
 * it is known that the first argument holds a virtual address that points to
 * DMA-able storage, then this function can be safely used.
 */
static inline dma64_t virt_to_dma64(void *ptr)
{
	return (__force dma64_t)__pa(ptr);
}

static inline void *dma64_to_virt(dma64_t addr)
{
	return __va((__force unsigned long)addr);
}

static inline dma64_t u64_to_dma64(u64 addr)
{
	return (__force dma64_t)addr;
}

static inline u64 dma64_to_u64(dma64_t addr)
{
	return (__force u64)addr;
}

static inline dma64_t dma64_add(dma64_t a, u64 b)
{
	return (__force dma64_t)((__force u64)a + b);
}

static inline dma64_t dma64_and(dma64_t a, u64 b)
{
	return (__force dma64_t)((__force u64)a & b);
}

#endif /* _ASM_S390_DMA_TYPES_H_ */
