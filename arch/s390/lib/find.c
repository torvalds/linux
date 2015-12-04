/*
 * MSB0 numbered special bitops handling.
 *
 * The bits are numbered:
 *   |0..............63|64............127|128...........191|192...........255|
 *
 * The reason for this bit numbering is the fact that the hardware sets bits
 * in a bitmap starting at bit 0 (MSB) and we don't want to scan the bitmap
 * from the 'wrong end'.
 */

#include <linux/compiler.h>
#include <linux/bitops.h>
#include <linux/export.h>

unsigned long find_first_bit_inv(const unsigned long *addr, unsigned long size)
{
	const unsigned long *p = addr;
	unsigned long result = 0;
	unsigned long tmp;

	while (size & ~(BITS_PER_LONG - 1)) {
		if ((tmp = *(p++)))
			goto found;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = (*p) & (~0UL << (BITS_PER_LONG - size));
	if (!tmp)		/* Are any bits set? */
		return result + size;	/* Nope. */
found:
	return result + (__fls(tmp) ^ (BITS_PER_LONG - 1));
}
EXPORT_SYMBOL(find_first_bit_inv);

unsigned long find_next_bit_inv(const unsigned long *addr, unsigned long size,
				unsigned long offset)
{
	const unsigned long *p = addr + (offset / BITS_PER_LONG);
	unsigned long result = offset & ~(BITS_PER_LONG - 1);
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset %= BITS_PER_LONG;
	if (offset) {
		tmp = *(p++);
		tmp &= (~0UL >> offset);
		if (size < BITS_PER_LONG)
			goto found_first;
		if (tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}
	while (size & ~(BITS_PER_LONG-1)) {
		if ((tmp = *(p++)))
			goto found_middle;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = *p;
found_first:
	tmp &= (~0UL << (BITS_PER_LONG - size));
	if (!tmp)		/* Are any bits set? */
		return result + size;	/* Nope. */
found_middle:
	return result + (__fls(tmp) ^ (BITS_PER_LONG - 1));
}
EXPORT_SYMBOL(find_next_bit_inv);
