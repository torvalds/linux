#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/intrinsics.h>
#include <linux/module.h>
#include <linux/bitops.h>

/*
 * Find next zero bit in a bitmap reasonably efficiently..
 */

int __find_next_zero_bit (const void *addr, unsigned long size, unsigned long offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 6);
	unsigned long result = offset & ~63UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 63UL;
	if (offset) {
		tmp = *(p++);
		tmp |= ~0UL >> (64-offset);
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
	if (tmp == ~0UL)		/* any bits zero? */
		return result + size;	/* nope */
found_middle:
	return result + ffz(tmp);
}
EXPORT_SYMBOL(__find_next_zero_bit);

/*
 * Find next bit in a bitmap reasonably efficiently..
 */
int __find_next_bit(const void *addr, unsigned long size, unsigned long offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 6);
	unsigned long result = offset & ~63UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 63UL;
	if (offset) {
		tmp = *(p++);
		tmp &= ~0UL << offset;
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
	tmp &= ~0UL >> (64-size);
	if (tmp == 0UL)		/* Are any bits set? */
		return result + size; /* Nope. */
  found_middle:
	return result + __ffs(tmp);
}
EXPORT_SYMBOL(__find_next_bit);
