/*
 * safe read and write memory routines callable while atomic
 *
 * Copyright 2005-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/uaccess.h>
#include <asm/dma.h>

static int validate_memory_access_address(unsigned long addr, int size)
{
	if (size < 0 || addr == 0)
		return -EFAULT;
	return bfin_mem_access_type(addr, size);
}

long probe_kernel_read(void *dst, void *src, size_t size)
{
	unsigned long lsrc = (unsigned long)src;
	int mem_type;

	mem_type = validate_memory_access_address(lsrc, size);
	if (mem_type < 0)
		return mem_type;

	if (lsrc >= SYSMMR_BASE) {
		if (size == 2 && lsrc % 2 == 0) {
			u16 mmr = bfin_read16(src);
			memcpy(dst, &mmr, sizeof(mmr));
			return 0;
		} else if (size == 4 && lsrc % 4 == 0) {
			u32 mmr = bfin_read32(src);
			memcpy(dst, &mmr, sizeof(mmr));
			return 0;
		}
	} else {
		switch (mem_type) {
		case BFIN_MEM_ACCESS_CORE:
		case BFIN_MEM_ACCESS_CORE_ONLY:
			return __probe_kernel_read(dst, src, size);
			/* XXX: should support IDMA here with SMP */
		case BFIN_MEM_ACCESS_DMA:
			if (dma_memcpy(dst, src, size))
				return 0;
			break;
		case BFIN_MEM_ACCESS_ITEST:
			if (isram_memcpy(dst, src, size))
				return 0;
			break;
		}
	}

	return -EFAULT;
}

long probe_kernel_write(void *dst, void *src, size_t size)
{
	unsigned long ldst = (unsigned long)dst;
	int mem_type;

	mem_type = validate_memory_access_address(ldst, size);
	if (mem_type < 0)
		return mem_type;

	if (ldst >= SYSMMR_BASE) {
		if (size == 2 && ldst % 2 == 0) {
			u16 mmr;
			memcpy(&mmr, src, sizeof(mmr));
			bfin_write16(dst, mmr);
			return 0;
		} else if (size == 4 && ldst % 4 == 0) {
			u32 mmr;
			memcpy(&mmr, src, sizeof(mmr));
			bfin_write32(dst, mmr);
			return 0;
		}
	} else {
		switch (mem_type) {
		case BFIN_MEM_ACCESS_CORE:
		case BFIN_MEM_ACCESS_CORE_ONLY:
			return __probe_kernel_write(dst, src, size);
			/* XXX: should support IDMA here with SMP */
		case BFIN_MEM_ACCESS_DMA:
			if (dma_memcpy(dst, src, size))
				return 0;
			break;
		case BFIN_MEM_ACCESS_ITEST:
			if (isram_memcpy(dst, src, size))
				return 0;
			break;
		}
	}

	return -EFAULT;
}
