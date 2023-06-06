// SPDX-License-Identifier: GPL-2.0-only

#include <linux/efi.h>
#include <linux/memblock.h>
#include <linux/spinlock.h>
#include <asm/unaccepted_memory.h>

/* Protects unaccepted memory bitmap */
static DEFINE_SPINLOCK(unaccepted_memory_lock);

/*
 * accept_memory() -- Consult bitmap and accept the memory if needed.
 *
 * Only memory that is explicitly marked as unaccepted in the bitmap requires
 * an action. All the remaining memory is implicitly accepted and doesn't need
 * acceptance.
 *
 * No need to accept:
 *  - anything if the system has no unaccepted table;
 *  - memory that is below phys_base;
 *  - memory that is above the memory that addressable by the bitmap;
 */
void accept_memory(phys_addr_t start, phys_addr_t end)
{
	struct efi_unaccepted_memory *unaccepted;
	unsigned long range_start, range_end;
	unsigned long flags;
	u64 unit_size;

	unaccepted = efi_get_unaccepted_table();
	if (!unaccepted)
		return;

	unit_size = unaccepted->unit_size;

	/*
	 * Only care for the part of the range that is represented
	 * in the bitmap.
	 */
	if (start < unaccepted->phys_base)
		start = unaccepted->phys_base;
	if (end < unaccepted->phys_base)
		return;

	/* Translate to offsets from the beginning of the bitmap */
	start -= unaccepted->phys_base;
	end -= unaccepted->phys_base;

	/* Make sure not to overrun the bitmap */
	if (end > unaccepted->size * unit_size * BITS_PER_BYTE)
		end = unaccepted->size * unit_size * BITS_PER_BYTE;

	range_start = start / unit_size;

	spin_lock_irqsave(&unaccepted_memory_lock, flags);
	for_each_set_bitrange_from(range_start, range_end, unaccepted->bitmap,
				   DIV_ROUND_UP(end, unit_size)) {
		unsigned long phys_start, phys_end;
		unsigned long len = range_end - range_start;

		phys_start = range_start * unit_size + unaccepted->phys_base;
		phys_end = range_end * unit_size + unaccepted->phys_base;

		arch_accept_memory(phys_start, phys_end);
		bitmap_clear(unaccepted->bitmap, range_start, len);
	}
	spin_unlock_irqrestore(&unaccepted_memory_lock, flags);
}

bool range_contains_unaccepted_memory(phys_addr_t start, phys_addr_t end)
{
	struct efi_unaccepted_memory *unaccepted;
	unsigned long flags;
	bool ret = false;
	u64 unit_size;

	unaccepted = efi_get_unaccepted_table();
	if (!unaccepted)
		return false;

	unit_size = unaccepted->unit_size;

	/*
	 * Only care for the part of the range that is represented
	 * in the bitmap.
	 */
	if (start < unaccepted->phys_base)
		start = unaccepted->phys_base;
	if (end < unaccepted->phys_base)
		return false;

	/* Translate to offsets from the beginning of the bitmap */
	start -= unaccepted->phys_base;
	end -= unaccepted->phys_base;

	/* Make sure not to overrun the bitmap */
	if (end > unaccepted->size * unit_size * BITS_PER_BYTE)
		end = unaccepted->size * unit_size * BITS_PER_BYTE;

	spin_lock_irqsave(&unaccepted_memory_lock, flags);
	while (start < end) {
		if (test_bit(start / unit_size, unaccepted->bitmap)) {
			ret = true;
			break;
		}

		start += unit_size;
	}
	spin_unlock_irqrestore(&unaccepted_memory_lock, flags);

	return ret;
}
