/*
 * safe read and write memory routines callable while atomic
 *
 * Copyright 2012 Imagination Technologies
 */

#include <linux/uaccess.h>
#include <asm/io.h>

/*
 * The generic probe_kernel_write() uses the user copy code which can split the
 * writes if the source is unaligned, and repeats writes to make exceptions
 * precise. We override it here to avoid these things happening to memory mapped
 * IO memory where they could have undesired effects.
 * Due to the use of CACHERD instruction this only works on Meta2 onwards.
 */
#ifdef CONFIG_METAG_META21
long probe_kernel_write(void *dst, const void *src, size_t size)
{
	unsigned long ldst = (unsigned long)dst;
	void __iomem *iodst = (void __iomem *)dst;
	unsigned long lsrc = (unsigned long)src;
	const u8 *psrc = (u8 *)src;
	unsigned int pte, i;
	u8 bounce[8] __aligned(8);

	if (!size)
		return 0;

	/* Use the write combine bit to decide is the destination is MMIO. */
	pte = __builtin_meta2_cacherd(dst);

	/* Check the mapping is valid and writeable. */
	if ((pte & (MMCU_ENTRY_WR_BIT | MMCU_ENTRY_VAL_BIT))
	    != (MMCU_ENTRY_WR_BIT | MMCU_ENTRY_VAL_BIT))
		return -EFAULT;

	/* Fall back to generic version for cases we're not interested in. */
	if (pte & MMCU_ENTRY_WRC_BIT	|| /* write combined memory */
	    (ldst & (size - 1))		|| /* destination unaligned */
	    size > 8			|| /* more than max write size */
	    (size & (size - 1)))	   /* non power of 2 size */
		return __probe_kernel_write(dst, src, size);

	/* If src is unaligned, copy to the aligned bounce buffer first. */
	if (lsrc & (size - 1)) {
		for (i = 0; i < size; ++i)
			bounce[i] = psrc[i];
		psrc = bounce;
	}

	switch (size) {
	case 1:
		writeb(*psrc, iodst);
		break;
	case 2:
		writew(*(const u16 *)psrc, iodst);
		break;
	case 4:
		writel(*(const u32 *)psrc, iodst);
		break;
	case 8:
		writeq(*(const u64 *)psrc, iodst);
		break;
	}
	return 0;
}
#endif
