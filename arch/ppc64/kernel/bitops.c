/*
 * These are too big to be inlined.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <asm/byteorder.h>

unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
				 unsigned long offset)
{
	const unsigned long *p = addr + (offset >> 6);
	unsigned long result = offset & ~63UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 63UL;
	if (offset) {
		tmp = *(p++);
		tmp |= ~0UL >> (64 - offset);
		if (size < 64)
			goto found_first;
		if (~tmp)
			goto found_middle;
		size -= 64;
		result += 64;
	}
	while (size & ~63UL) {
		if (~(tmp = *(p++)))
			goto found_middle;
		result += 64;
		size -= 64;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp |= ~0UL << size;
	if (tmp == ~0UL)	/* Are any bits zero? */
		return result + size;	/* Nope. */
found_middle:
	return result + ffz(tmp);
}

EXPORT_SYMBOL(find_next_zero_bit);

unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
			    unsigned long offset)
{
	const unsigned long *p = addr + (offset >> 6);
	unsigned long result = offset & ~63UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 63UL;
	if (offset) {
		tmp = *(p++);
		tmp &= (~0UL << offset);
		if (size < 64)
			goto found_first;
		if (tmp)
			goto found_middle;
		size -= 64;
		result += 64;
	}
	while (size & ~63UL) {
		if ((tmp = *(p++)))
			goto found_middle;
		result += 64;
		size -= 64;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp &= (~0UL >> (64 - size));
	if (tmp == 0UL)		/* Are any bits set? */
		return result + size;	/* Nope. */
found_middle:
	return result + __ffs(tmp);
}

EXPORT_SYMBOL(find_next_bit);

static inline unsigned int ext2_ilog2(unsigned int x)
{
	int lz;

	asm("cntlzw %0,%1": "=r"(lz):"r"(x));
	return 31 - lz;
}

static inline unsigned int ext2_ffz(unsigned int x)
{
	u32 rc;
	if ((x = ~x) == 0)
		return 32;
	rc = ext2_ilog2(x & -x);
	return rc;
}

unsigned long find_next_zero_le_bit(const unsigned long *addr, unsigned long size,
				    unsigned long offset)
{
	const unsigned int *p = ((const unsigned int *)addr) + (offset >> 5);
	unsigned int result = offset & ~31;
	unsigned int tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31;
	if (offset) {
		tmp = cpu_to_le32p(p++);
		tmp |= ~0U >> (32 - offset);	/* bug or feature ? */
		if (size < 32)
			goto found_first;
		if (tmp != ~0)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while (size >= 32) {
		if ((tmp = cpu_to_le32p(p++)) != ~0)
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if (!size)
		return result;
	tmp = cpu_to_le32p(p);
found_first:
	tmp |= ~0 << size;
	if (tmp == ~0)		/* Are any bits zero? */
		return result + size;	/* Nope. */
found_middle:
	return result + ext2_ffz(tmp);
}

EXPORT_SYMBOL(find_next_zero_le_bit);
