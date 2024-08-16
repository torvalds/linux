/*  Generic MTRR (Memory Type Range Register) driver.

    Copyright (C) 1997-2000  Richard Gooch
    Copyright (c) 2002	     Patrick Mochel

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Richard Gooch may be reached by email at  rgooch@atnf.csiro.au
    The postal address is:
      Richard Gooch, c/o ATNF, P. O. Box 76, Epping, N.S.W., 2121, Australia.

    Source: "Pentium Pro Family Developer's Manual, Volume 3:
    Operating System Writer's Guide" (Intel document number 242692),
    section 11.11.7

    This was cleaned and made readable by Patrick Mochel <mochel@osdl.org>
    on 6-7 March 2002.
    Source: Intel Architecture Software Developers Manual, Volume 3:
    System Programming Guide; Section 9.11. (1997 edition - PPro).
*/

#include <linux/types.h> /* FIXME: kvm_para.h needs this */

#include <linux/stop_machine.h>
#include <linux/kvm_para.h>
#include <linux/uaccess.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/init.h>
#include <linux/sort.h>
#include <linux/cpu.h>
#include <linux/pci.h>
#include <linux/smp.h>
#include <linux/syscore_ops.h>
#include <linux/rcupdate.h>

#include <asm/cacheinfo.h>
#include <asm/cpufeature.h>
#include <asm/e820/api.h>
#include <asm/mtrr.h>
#include <asm/msr.h>
#include <asm/memtype.h>

#include "mtrr.h"

/* arch_phys_wc_add returns an MTRR register index plus this offset. */
#define MTRR_TO_PHYS_WC_OFFSET 1000

u32 num_var_ranges;

unsigned int mtrr_usage_table[MTRR_MAX_VAR_RANGES];
DEFINE_MUTEX(mtrr_mutex);

const struct mtrr_ops *mtrr_if;

/*  Returns non-zero if we have the write-combining memory type  */
static int have_wrcomb(void)
{
	struct pci_dev *dev;

	dev = pci_get_class(PCI_CLASS_BRIDGE_HOST << 8, NULL);
	if (dev != NULL) {
		/*
		 * ServerWorks LE chipsets < rev 6 have problems with
		 * write-combining. Don't allow it and leave room for other
		 * chipsets to be tagged
		 */
		if (dev->vendor == PCI_VENDOR_ID_SERVERWORKS &&
		    dev->device == PCI_DEVICE_ID_SERVERWORKS_LE &&
		    dev->revision <= 5) {
			pr_info("Serverworks LE rev < 6 detected. Write-combining disabled.\n");
			pci_dev_put(dev);
			return 0;
		}
		/*
		 * Intel 450NX errata # 23. Non ascending cacheline evictions to
		 * write combining memory may resulting in data corruption
		 */
		if (dev->vendor == PCI_VENDOR_ID_INTEL &&
		    dev->device == PCI_DEVICE_ID_INTEL_82451NX) {
			pr_info("Intel 450NX MMC detected. Write-combining disabled.\n");
			pci_dev_put(dev);
			return 0;
		}
		pci_dev_put(dev);
	}
	return mtrr_if->have_wrcomb ? mtrr_if->have_wrcomb() : 0;
}

static void __init init_table(void)
{
	int i, max;

	max = num_var_ranges;
	for (i = 0; i < max; i++)
		mtrr_usage_table[i] = 1;
}

struct set_mtrr_data {
	unsigned long	smp_base;
	unsigned long	smp_size;
	unsigned int	smp_reg;
	mtrr_type	smp_type;
};

/**
 * mtrr_rendezvous_handler - Work done in the synchronization handler. Executed
 * by all the CPUs.
 * @info: pointer to mtrr configuration data
 *
 * Returns nothing.
 */
static int mtrr_rendezvous_handler(void *info)
{
	struct set_mtrr_data *data = info;

	mtrr_if->set(data->smp_reg, data->smp_base,
		     data->smp_size, data->smp_type);
	return 0;
}

static inline int types_compatible(mtrr_type type1, mtrr_type type2)
{
	return type1 == MTRR_TYPE_UNCACHABLE ||
	       type2 == MTRR_TYPE_UNCACHABLE ||
	       (type1 == MTRR_TYPE_WRTHROUGH && type2 == MTRR_TYPE_WRBACK) ||
	       (type1 == MTRR_TYPE_WRBACK && type2 == MTRR_TYPE_WRTHROUGH);
}

/**
 * set_mtrr - update mtrrs on all processors
 * @reg:	mtrr in question
 * @base:	mtrr base
 * @size:	mtrr size
 * @type:	mtrr type
 *
 * This is kinda tricky, but fortunately, Intel spelled it out for us cleanly:
 *
 * 1. Queue work to do the following on all processors:
 * 2. Disable Interrupts
 * 3. Wait for all procs to do so
 * 4. Enter no-fill cache mode
 * 5. Flush caches
 * 6. Clear PGE bit
 * 7. Flush all TLBs
 * 8. Disable all range registers
 * 9. Update the MTRRs
 * 10. Enable all range registers
 * 11. Flush all TLBs and caches again
 * 12. Enter normal cache mode and reenable caching
 * 13. Set PGE
 * 14. Wait for buddies to catch up
 * 15. Enable interrupts.
 *
 * What does that mean for us? Well, stop_machine() will ensure that
 * the rendezvous handler is started on each CPU. And in lockstep they
 * do the state transition of disabling interrupts, updating MTRR's
 * (the CPU vendors may each do it differently, so we call mtrr_if->set()
 * callback and let them take care of it.) and enabling interrupts.
 *
 * Note that the mechanism is the same for UP systems, too; all the SMP stuff
 * becomes nops.
 */
static void set_mtrr(unsigned int reg, unsigned long base, unsigned long size,
		     mtrr_type type)
{
	struct set_mtrr_data data = { .smp_reg = reg,
				      .smp_base = base,
				      .smp_size = size,
				      .smp_type = type
				    };

	stop_machine_cpuslocked(mtrr_rendezvous_handler, &data, cpu_online_mask);

	generic_rebuild_map();
}

/**
 * mtrr_add_page - Add a memory type region
 * @base: Physical base address of region in pages (in units of 4 kB!)
 * @size: Physical size of region in pages (4 kB)
 * @type: Type of MTRR desired
 * @increment: If this is true do usage counting on the region
 *
 * Memory type region registers control the caching on newer Intel and
 * non Intel processors. This function allows drivers to request an
 * MTRR is added. The details and hardware specifics of each processor's
 * implementation are hidden from the caller, but nevertheless the
 * caller should expect to need to provide a power of two size on an
 * equivalent power of two boundary.
 *
 * If the region cannot be added either because all regions are in use
 * or the CPU cannot support it a negative value is returned. On success
 * the register number for this entry is returned, but should be treated
 * as a cookie only.
 *
 * On a multiprocessor machine the changes are made to all processors.
 * This is required on x86 by the Intel processors.
 *
 * The available types are
 *
 * %MTRR_TYPE_UNCACHABLE - No caching
 *
 * %MTRR_TYPE_WRBACK - Write data back in bursts whenever
 *
 * %MTRR_TYPE_WRCOMB - Write data back soon but allow bursts
 *
 * %MTRR_TYPE_WRTHROUGH - Cache reads but not writes
 *
 * BUGS: Needs a quiet flag for the cases where drivers do not mind
 * failures and do not wish system log messages to be sent.
 */
int mtrr_add_page(unsigned long base, unsigned long size,
		  unsigned int type, bool increment)
{
	unsigned long lbase, lsize;
	int i, replace, error;
	mtrr_type ltype;

	if (!mtrr_enabled())
		return -ENXIO;

	error = mtrr_if->validate_add_page(base, size, type);
	if (error)
		return error;

	if (type >= MTRR_NUM_TYPES) {
		pr_warn("type: %u invalid\n", type);
		return -EINVAL;
	}

	/* If the type is WC, check that this processor supports it */
	if ((type == MTRR_TYPE_WRCOMB) && !have_wrcomb()) {
		pr_warn("your processor doesn't support write-combining\n");
		return -ENOSYS;
	}

	if (!size) {
		pr_warn("zero sized request\n");
		return -EINVAL;
	}

	if ((base | (base + size - 1)) >>
	    (boot_cpu_data.x86_phys_bits - PAGE_SHIFT)) {
		pr_warn("base or size exceeds the MTRR width\n");
		return -EINVAL;
	}

	error = -EINVAL;
	replace = -1;

	/* No CPU hotplug when we change MTRR entries */
	cpus_read_lock();

	/* Search for existing MTRR  */
	mutex_lock(&mtrr_mutex);
	for (i = 0; i < num_var_ranges; ++i) {
		mtrr_if->get(i, &lbase, &lsize, &ltype);
		if (!lsize || base > lbase + lsize - 1 ||
		    base + size - 1 < lbase)
			continue;
		/*
		 * At this point we know there is some kind of
		 * overlap/enclosure
		 */
		if (base < lbase || base + size - 1 > lbase + lsize - 1) {
			if (base <= lbase &&
			    base + size - 1 >= lbase + lsize - 1) {
				/*  New region encloses an existing region  */
				if (type == ltype) {
					replace = replace == -1 ? i : -2;
					continue;
				} else if (types_compatible(type, ltype))
					continue;
			}
			pr_warn("0x%lx000,0x%lx000 overlaps existing 0x%lx000,0x%lx000\n", base, size, lbase,
				lsize);
			goto out;
		}
		/* New region is enclosed by an existing region */
		if (ltype != type) {
			if (types_compatible(type, ltype))
				continue;
			pr_warn("type mismatch for %lx000,%lx000 old: %s new: %s\n",
				base, size, mtrr_attrib_to_str(ltype),
				mtrr_attrib_to_str(type));
			goto out;
		}
		if (increment)
			++mtrr_usage_table[i];
		error = i;
		goto out;
	}
	/* Search for an empty MTRR */
	i = mtrr_if->get_free_region(base, size, replace);
	if (i >= 0) {
		set_mtrr(i, base, size, type);
		if (likely(replace < 0)) {
			mtrr_usage_table[i] = 1;
		} else {
			mtrr_usage_table[i] = mtrr_usage_table[replace];
			if (increment)
				mtrr_usage_table[i]++;
			if (unlikely(replace != i)) {
				set_mtrr(replace, 0, 0, 0);
				mtrr_usage_table[replace] = 0;
			}
		}
	} else {
		pr_info("no more MTRRs available\n");
	}
	error = i;
 out:
	mutex_unlock(&mtrr_mutex);
	cpus_read_unlock();
	return error;
}

static int mtrr_check(unsigned long base, unsigned long size)
{
	if ((base & (PAGE_SIZE - 1)) || (size & (PAGE_SIZE - 1))) {
		pr_warn("size and base must be multiples of 4 kiB\n");
		Dprintk("size: 0x%lx  base: 0x%lx\n", size, base);
		dump_stack();
		return -1;
	}
	return 0;
}

/**
 * mtrr_add - Add a memory type region
 * @base: Physical base address of region
 * @size: Physical size of region
 * @type: Type of MTRR desired
 * @increment: If this is true do usage counting on the region
 *
 * Memory type region registers control the caching on newer Intel and
 * non Intel processors. This function allows drivers to request an
 * MTRR is added. The details and hardware specifics of each processor's
 * implementation are hidden from the caller, but nevertheless the
 * caller should expect to need to provide a power of two size on an
 * equivalent power of two boundary.
 *
 * If the region cannot be added either because all regions are in use
 * or the CPU cannot support it a negative value is returned. On success
 * the register number for this entry is returned, but should be treated
 * as a cookie only.
 *
 * On a multiprocessor machine the changes are made to all processors.
 * This is required on x86 by the Intel processors.
 *
 * The available types are
 *
 * %MTRR_TYPE_UNCACHABLE - No caching
 *
 * %MTRR_TYPE_WRBACK - Write data back in bursts whenever
 *
 * %MTRR_TYPE_WRCOMB - Write data back soon but allow bursts
 *
 * %MTRR_TYPE_WRTHROUGH - Cache reads but not writes
 *
 * BUGS: Needs a quiet flag for the cases where drivers do not mind
 * failures and do not wish system log messages to be sent.
 */
int mtrr_add(unsigned long base, unsigned long size, unsigned int type,
	     bool increment)
{
	if (!mtrr_enabled())
		return -ENODEV;
	if (mtrr_check(base, size))
		return -EINVAL;
	return mtrr_add_page(base >> PAGE_SHIFT, size >> PAGE_SHIFT, type,
			     increment);
}

/**
 * mtrr_del_page - delete a memory type region
 * @reg: Register returned by mtrr_add
 * @base: Physical base address
 * @size: Size of region
 *
 * If register is supplied then base and size are ignored. This is
 * how drivers should call it.
 *
 * Releases an MTRR region. If the usage count drops to zero the
 * register is freed and the region returns to default state.
 * On success the register is returned, on failure a negative error
 * code.
 */
int mtrr_del_page(int reg, unsigned long base, unsigned long size)
{
	int i, max;
	mtrr_type ltype;
	unsigned long lbase, lsize;
	int error = -EINVAL;

	if (!mtrr_enabled())
		return -ENODEV;

	max = num_var_ranges;
	/* No CPU hotplug when we change MTRR entries */
	cpus_read_lock();
	mutex_lock(&mtrr_mutex);
	if (reg < 0) {
		/*  Search for existing MTRR  */
		for (i = 0; i < max; ++i) {
			mtrr_if->get(i, &lbase, &lsize, &ltype);
			if (lbase == base && lsize == size) {
				reg = i;
				break;
			}
		}
		if (reg < 0) {
			Dprintk("no MTRR for %lx000,%lx000 found\n", base, size);
			goto out;
		}
	}
	if (reg >= max) {
		pr_warn("register: %d too big\n", reg);
		goto out;
	}
	mtrr_if->get(reg, &lbase, &lsize, &ltype);
	if (lsize < 1) {
		pr_warn("MTRR %d not used\n", reg);
		goto out;
	}
	if (mtrr_usage_table[reg] < 1) {
		pr_warn("reg: %d has count=0\n", reg);
		goto out;
	}
	if (--mtrr_usage_table[reg] < 1)
		set_mtrr(reg, 0, 0, 0);
	error = reg;
 out:
	mutex_unlock(&mtrr_mutex);
	cpus_read_unlock();
	return error;
}

/**
 * mtrr_del - delete a memory type region
 * @reg: Register returned by mtrr_add
 * @base: Physical base address
 * @size: Size of region
 *
 * If register is supplied then base and size are ignored. This is
 * how drivers should call it.
 *
 * Releases an MTRR region. If the usage count drops to zero the
 * register is freed and the region returns to default state.
 * On success the register is returned, on failure a negative error
 * code.
 */
int mtrr_del(int reg, unsigned long base, unsigned long size)
{
	if (!mtrr_enabled())
		return -ENODEV;
	if (mtrr_check(base, size))
		return -EINVAL;
	return mtrr_del_page(reg, base >> PAGE_SHIFT, size >> PAGE_SHIFT);
}

/**
 * arch_phys_wc_add - add a WC MTRR and handle errors if PAT is unavailable
 * @base: Physical base address
 * @size: Size of region
 *
 * If PAT is available, this does nothing.  If PAT is unavailable, it
 * attempts to add a WC MTRR covering size bytes starting at base and
 * logs an error if this fails.
 *
 * The called should provide a power of two size on an equivalent
 * power of two boundary.
 *
 * Drivers must store the return value to pass to mtrr_del_wc_if_needed,
 * but drivers should not try to interpret that return value.
 */
int arch_phys_wc_add(unsigned long base, unsigned long size)
{
	int ret;

	if (pat_enabled() || !mtrr_enabled())
		return 0;  /* Success!  (We don't need to do anything.) */

	ret = mtrr_add(base, size, MTRR_TYPE_WRCOMB, true);
	if (ret < 0) {
		pr_warn("Failed to add WC MTRR for [%p-%p]; performance may suffer.",
			(void *)base, (void *)(base + size - 1));
		return ret;
	}
	return ret + MTRR_TO_PHYS_WC_OFFSET;
}
EXPORT_SYMBOL(arch_phys_wc_add);

/*
 * arch_phys_wc_del - undoes arch_phys_wc_add
 * @handle: Return value from arch_phys_wc_add
 *
 * This cleans up after mtrr_add_wc_if_needed.
 *
 * The API guarantees that mtrr_del_wc_if_needed(error code) and
 * mtrr_del_wc_if_needed(0) do nothing.
 */
void arch_phys_wc_del(int handle)
{
	if (handle >= 1) {
		WARN_ON(handle < MTRR_TO_PHYS_WC_OFFSET);
		mtrr_del(handle - MTRR_TO_PHYS_WC_OFFSET, 0, 0);
	}
}
EXPORT_SYMBOL(arch_phys_wc_del);

/*
 * arch_phys_wc_index - translates arch_phys_wc_add's return value
 * @handle: Return value from arch_phys_wc_add
 *
 * This will turn the return value from arch_phys_wc_add into an mtrr
 * index suitable for debugging.
 *
 * Note: There is no legitimate use for this function, except possibly
 * in printk line.  Alas there is an illegitimate use in some ancient
 * drm ioctls.
 */
int arch_phys_wc_index(int handle)
{
	if (handle < MTRR_TO_PHYS_WC_OFFSET)
		return -1;
	else
		return handle - MTRR_TO_PHYS_WC_OFFSET;
}
EXPORT_SYMBOL_GPL(arch_phys_wc_index);

int __initdata changed_by_mtrr_cleanup;

/**
 * mtrr_bp_init - initialize MTRRs on the boot CPU
 *
 * This needs to be called early; before any of the other CPUs are
 * initialized (i.e. before smp_init()).
 */
void __init mtrr_bp_init(void)
{
	bool generic_mtrrs = cpu_feature_enabled(X86_FEATURE_MTRR);
	const char *why = "(not available)";
	unsigned long config, dummy;

	phys_hi_rsvd = GENMASK(31, boot_cpu_data.x86_phys_bits - 32);

	if (!generic_mtrrs && mtrr_state.enabled) {
		/*
		 * Software overwrite of MTRR state, only for generic case.
		 * Note that X86_FEATURE_MTRR has been reset in this case.
		 */
		init_table();
		mtrr_build_map();
		pr_info("MTRRs set to read-only\n");

		return;
	}

	if (generic_mtrrs)
		mtrr_if = &generic_mtrr_ops;
	else
		mtrr_set_if();

	if (mtrr_enabled()) {
		/* Get the number of variable MTRR ranges. */
		if (mtrr_if == &generic_mtrr_ops)
			rdmsr(MSR_MTRRcap, config, dummy);
		else
			config = mtrr_if->var_regs;
		num_var_ranges = config & MTRR_CAP_VCNT;

		init_table();
		if (mtrr_if == &generic_mtrr_ops) {
			/* BIOS may override */
			if (get_mtrr_state()) {
				memory_caching_control |= CACHE_MTRR;
				changed_by_mtrr_cleanup = mtrr_cleanup();
				mtrr_build_map();
			} else {
				mtrr_if = NULL;
				why = "by BIOS";
			}
		}
	}

	if (!mtrr_enabled())
		pr_info("MTRRs disabled %s\n", why);
}

/**
 * mtrr_save_state - Save current fixed-range MTRR state of the first
 *	cpu in cpu_online_mask.
 */
void mtrr_save_state(void)
{
	int first_cpu;

	if (!mtrr_enabled() || !mtrr_state.have_fixed)
		return;

	first_cpu = cpumask_first(cpu_online_mask);
	smp_call_function_single(first_cpu, mtrr_save_fixed_ranges, NULL, 1);
}

static int __init mtrr_init_finalize(void)
{
	/*
	 * Map might exist if mtrr_overwrite_state() has been called or if
	 * mtrr_enabled() returns true.
	 */
	mtrr_copy_map();

	if (!mtrr_enabled())
		return 0;

	if (memory_caching_control & CACHE_MTRR) {
		if (!changed_by_mtrr_cleanup)
			mtrr_state_warn();
		return 0;
	}

	mtrr_register_syscore();

	return 0;
}
subsys_initcall(mtrr_init_finalize);
