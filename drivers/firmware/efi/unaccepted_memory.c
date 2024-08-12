// SPDX-License-Identifier: GPL-2.0-only

#include <linux/efi.h>
#include <linux/memblock.h>
#include <linux/spinlock.h>
#include <linux/crash_dump.h>
#include <linux/nmi.h>
#include <asm/unaccepted_memory.h>

/* Protects unaccepted memory bitmap and accepting_list */
static DEFINE_SPINLOCK(unaccepted_memory_lock);

struct accept_range {
	struct list_head list;
	unsigned long start;
	unsigned long end;
};

static LIST_HEAD(accepting_list);

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
	struct accept_range range, *entry;
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

	/*
	 * load_unaligned_zeropad() can lead to unwanted loads across page
	 * boundaries. The unwanted loads are typically harmless. But, they
	 * might be made to totally unrelated or even unmapped memory.
	 * load_unaligned_zeropad() relies on exception fixup (#PF, #GP and now
	 * #VE) to recover from these unwanted loads.
	 *
	 * But, this approach does not work for unaccepted memory. For TDX, a
	 * load from unaccepted memory will not lead to a recoverable exception
	 * within the guest. The guest will exit to the VMM where the only
	 * recourse is to terminate the guest.
	 *
	 * There are two parts to fix this issue and comprehensively avoid
	 * access to unaccepted memory. Together these ensure that an extra
	 * "guard" page is accepted in addition to the memory that needs to be
	 * used:
	 *
	 * 1. Implicitly extend the range_contains_unaccepted_memory(start, end)
	 *    checks up to end+unit_size if 'end' is aligned on a unit_size
	 *    boundary.
	 *
	 * 2. Implicitly extend accept_memory(start, end) to end+unit_size if
	 *    'end' is aligned on a unit_size boundary. (immediately following
	 *    this comment)
	 */
	if (!(end % unit_size))
		end += unit_size;

	/* Make sure not to overrun the bitmap */
	if (end > unaccepted->size * unit_size * BITS_PER_BYTE)
		end = unaccepted->size * unit_size * BITS_PER_BYTE;

	range.start = start / unit_size;
	range.end = DIV_ROUND_UP(end, unit_size);
retry:
	spin_lock_irqsave(&unaccepted_memory_lock, flags);

	/*
	 * Check if anybody works on accepting the same range of the memory.
	 *
	 * The check is done with unit_size granularity. It is crucial to catch
	 * all accept requests to the same unit_size block, even if they don't
	 * overlap on physical address level.
	 */
	list_for_each_entry(entry, &accepting_list, list) {
		if (entry->end <= range.start)
			continue;
		if (entry->start >= range.end)
			continue;

		/*
		 * Somebody else accepting the range. Or at least part of it.
		 *
		 * Drop the lock and retry until it is complete.
		 */
		spin_unlock_irqrestore(&unaccepted_memory_lock, flags);
		goto retry;
	}

	/*
	 * Register that the range is about to be accepted.
	 * Make sure nobody else will accept it.
	 */
	list_add(&range.list, &accepting_list);

	range_start = range.start;
	for_each_set_bitrange_from(range_start, range_end, unaccepted->bitmap,
				   range.end) {
		unsigned long phys_start, phys_end;
		unsigned long len = range_end - range_start;

		phys_start = range_start * unit_size + unaccepted->phys_base;
		phys_end = range_end * unit_size + unaccepted->phys_base;

		/*
		 * Keep interrupts disabled until the accept operation is
		 * complete in order to prevent deadlocks.
		 *
		 * Enabling interrupts before calling arch_accept_memory()
		 * creates an opportunity for an interrupt handler to request
		 * acceptance for the same memory. The handler will continuously
		 * spin with interrupts disabled, preventing other task from
		 * making progress with the acceptance process.
		 */
		spin_unlock(&unaccepted_memory_lock);

		arch_accept_memory(phys_start, phys_end);

		spin_lock(&unaccepted_memory_lock);
		bitmap_clear(unaccepted->bitmap, range_start, len);
	}

	list_del(&range.list);

	touch_softlockup_watchdog();

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

	/*
	 * Also consider the unaccepted state of the *next* page. See fix #1 in
	 * the comment on load_unaligned_zeropad() in accept_memory().
	 */
	if (!(end % unit_size))
		end += unit_size;

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

#ifdef CONFIG_PROC_VMCORE
static bool unaccepted_memory_vmcore_pfn_is_ram(struct vmcore_cb *cb,
						unsigned long pfn)
{
	return !pfn_is_unaccepted_memory(pfn);
}

static struct vmcore_cb vmcore_cb = {
	.pfn_is_ram = unaccepted_memory_vmcore_pfn_is_ram,
};

static int __init unaccepted_memory_init_kdump(void)
{
	register_vmcore_cb(&vmcore_cb);
	return 0;
}
core_initcall(unaccepted_memory_init_kdump);
#endif /* CONFIG_PROC_VMCORE */
