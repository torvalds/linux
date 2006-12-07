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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/mutex.h>

#include <asm/mtrr.h>

#include <asm/uaccess.h>
#include <asm/processor.h>
#include <asm/msr.h>
#include "mtrr.h"

u32 num_var_ranges = 0;

unsigned int *usage_table;
static DEFINE_MUTEX(mtrr_mutex);

u32 size_or_mask, size_and_mask;

static struct mtrr_ops * mtrr_ops[X86_VENDOR_NUM] = {};

struct mtrr_ops * mtrr_if = NULL;

static void set_mtrr(unsigned int reg, unsigned long base,
		     unsigned long size, mtrr_type type);

#ifndef CONFIG_X86_64
extern int arr3_protected;
#else
#define arr3_protected 0
#endif

void set_mtrr_ops(struct mtrr_ops * ops)
{
	if (ops->vendor && ops->vendor < X86_VENDOR_NUM)
		mtrr_ops[ops->vendor] = ops;
}

/*  Returns non-zero if we have the write-combining memory type  */
static int have_wrcomb(void)
{
	struct pci_dev *dev;
	u8 rev;
	
	if ((dev = pci_get_class(PCI_CLASS_BRIDGE_HOST << 8, NULL)) != NULL) {
		/* ServerWorks LE chipsets < rev 6 have problems with write-combining
		   Don't allow it and leave room for other chipsets to be tagged */
		if (dev->vendor == PCI_VENDOR_ID_SERVERWORKS &&
		    dev->device == PCI_DEVICE_ID_SERVERWORKS_LE) {
			pci_read_config_byte(dev, PCI_CLASS_REVISION, &rev);
			if (rev <= 5) {
				printk(KERN_INFO "mtrr: Serverworks LE rev < 6 detected. Write-combining disabled.\n");
				pci_dev_put(dev);
				return 0;
			}
		}
		/* Intel 450NX errata # 23. Non ascending cacheline evictions to
		   write combining memory may resulting in data corruption */
		if (dev->vendor == PCI_VENDOR_ID_INTEL &&
		    dev->device == PCI_DEVICE_ID_INTEL_82451NX) {
			printk(KERN_INFO "mtrr: Intel 450NX MMC detected. Write-combining disabled.\n");
			pci_dev_put(dev);
			return 0;
		}
		pci_dev_put(dev);
	}		
	return (mtrr_if->have_wrcomb ? mtrr_if->have_wrcomb() : 0);
}

/*  This function returns the number of variable MTRRs  */
static void __init set_num_var_ranges(void)
{
	unsigned long config = 0, dummy;

	if (use_intel()) {
		rdmsr(MTRRcap_MSR, config, dummy);
	} else if (is_cpu(AMD))
		config = 2;
	else if (is_cpu(CYRIX) || is_cpu(CENTAUR))
		config = 8;
	num_var_ranges = config & 0xff;
}

static void __init init_table(void)
{
	int i, max;

	max = num_var_ranges;
	if ((usage_table = kmalloc(max * sizeof *usage_table, GFP_KERNEL))
	    == NULL) {
		printk(KERN_ERR "mtrr: could not allocate\n");
		return;
	}
	for (i = 0; i < max; i++)
		usage_table[i] = 1;
}

struct set_mtrr_data {
	atomic_t	count;
	atomic_t	gate;
	unsigned long	smp_base;
	unsigned long	smp_size;
	unsigned int	smp_reg;
	mtrr_type	smp_type;
};

#ifdef CONFIG_SMP

static void ipi_handler(void *info)
/*  [SUMMARY] Synchronisation handler. Executed by "other" CPUs.
    [RETURNS] Nothing.
*/
{
	struct set_mtrr_data *data = info;
	unsigned long flags;

	local_irq_save(flags);

	atomic_dec(&data->count);
	while(!atomic_read(&data->gate))
		cpu_relax();

	/*  The master has cleared me to execute  */
	if (data->smp_reg != ~0U) 
		mtrr_if->set(data->smp_reg, data->smp_base, 
			     data->smp_size, data->smp_type);
	else
		mtrr_if->set_all();

	atomic_dec(&data->count);
	while(atomic_read(&data->gate))
		cpu_relax();

	atomic_dec(&data->count);
	local_irq_restore(flags);
}

#endif

/**
 * set_mtrr - update mtrrs on all processors
 * @reg:	mtrr in question
 * @base:	mtrr base
 * @size:	mtrr size
 * @type:	mtrr type
 *
 * This is kinda tricky, but fortunately, Intel spelled it out for us cleanly:
 * 
 * 1. Send IPI to do the following:
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
 * What does that mean for us? Well, first we set data.count to the number
 * of CPUs. As each CPU disables interrupts, it'll decrement it once. We wait
 * until it hits 0 and proceed. We set the data.gate flag and reset data.count.
 * Meanwhile, they are waiting for that flag to be set. Once it's set, each 
 * CPU goes through the transition of updating MTRRs. The CPU vendors may each do it 
 * differently, so we call mtrr_if->set() callback and let them take care of it.
 * When they're done, they again decrement data->count and wait for data.gate to 
 * be reset. 
 * When we finish, we wait for data.count to hit 0 and toggle the data.gate flag.
 * Everyone then enables interrupts and we all continue on.
 *
 * Note that the mechanism is the same for UP systems, too; all the SMP stuff
 * becomes nops.
 */
static void set_mtrr(unsigned int reg, unsigned long base,
		     unsigned long size, mtrr_type type)
{
	struct set_mtrr_data data;
	unsigned long flags;

	data.smp_reg = reg;
	data.smp_base = base;
	data.smp_size = size;
	data.smp_type = type;
	atomic_set(&data.count, num_booting_cpus() - 1);
	atomic_set(&data.gate,0);

	/*  Start the ball rolling on other CPUs  */
	if (smp_call_function(ipi_handler, &data, 1, 0) != 0)
		panic("mtrr: timed out waiting for other CPUs\n");

	local_irq_save(flags);

	while(atomic_read(&data.count))
		cpu_relax();

	/* ok, reset count and toggle gate */
	atomic_set(&data.count, num_booting_cpus() - 1);
	atomic_set(&data.gate,1);

	/* do our MTRR business */

	/* HACK!
	 * We use this same function to initialize the mtrrs on boot.
	 * The state of the boot cpu's mtrrs has been saved, and we want
	 * to replicate across all the APs. 
	 * If we're doing that @reg is set to something special...
	 */
	if (reg != ~0U) 
		mtrr_if->set(reg,base,size,type);

	/* wait for the others */
	while(atomic_read(&data.count))
		cpu_relax();

	atomic_set(&data.count, num_booting_cpus() - 1);
	atomic_set(&data.gate,0);

	/*
	 * Wait here for everyone to have seen the gate change
	 * So we're the last ones to touch 'data'
	 */
	while(atomic_read(&data.count))
		cpu_relax();

	local_irq_restore(flags);
}

/**
 *	mtrr_add_page - Add a memory type region
 *	@base: Physical base address of region in pages (in units of 4 kB!)
 *	@size: Physical size of region in pages (4 kB)
 *	@type: Type of MTRR desired
 *	@increment: If this is true do usage counting on the region
 *
 *	Memory type region registers control the caching on newer Intel and
 *	non Intel processors. This function allows drivers to request an
 *	MTRR is added. The details and hardware specifics of each processor's
 *	implementation are hidden from the caller, but nevertheless the 
 *	caller should expect to need to provide a power of two size on an
 *	equivalent power of two boundary.
 *
 *	If the region cannot be added either because all regions are in use
 *	or the CPU cannot support it a negative value is returned. On success
 *	the register number for this entry is returned, but should be treated
 *	as a cookie only.
 *
 *	On a multiprocessor machine the changes are made to all processors.
 *	This is required on x86 by the Intel processors.
 *
 *	The available types are
 *
 *	%MTRR_TYPE_UNCACHABLE	-	No caching
 *
 *	%MTRR_TYPE_WRBACK	-	Write data back in bursts whenever
 *
 *	%MTRR_TYPE_WRCOMB	-	Write data back soon but allow bursts
 *
 *	%MTRR_TYPE_WRTHROUGH	-	Cache reads but not writes
 *
 *	BUGS: Needs a quiet flag for the cases where drivers do not mind
 *	failures and do not wish system log messages to be sent.
 */

int mtrr_add_page(unsigned long base, unsigned long size, 
		  unsigned int type, char increment)
{
	int i;
	mtrr_type ltype;
	unsigned long lbase;
	unsigned int lsize;
	int error;

	if (!mtrr_if)
		return -ENXIO;
		
	if ((error = mtrr_if->validate_add_page(base,size,type)))
		return error;

	if (type >= MTRR_NUM_TYPES) {
		printk(KERN_WARNING "mtrr: type: %u invalid\n", type);
		return -EINVAL;
	}

	/*  If the type is WC, check that this processor supports it  */
	if ((type == MTRR_TYPE_WRCOMB) && !have_wrcomb()) {
		printk(KERN_WARNING
		       "mtrr: your processor doesn't support write-combining\n");
		return -ENOSYS;
	}

	if (base & size_or_mask || size & size_or_mask) {
		printk(KERN_WARNING "mtrr: base or size exceeds the MTRR width\n");
		return -EINVAL;
	}

	error = -EINVAL;

	/* No CPU hotplug when we change MTRR entries */
	lock_cpu_hotplug();
	/*  Search for existing MTRR  */
	mutex_lock(&mtrr_mutex);
	for (i = 0; i < num_var_ranges; ++i) {
		mtrr_if->get(i, &lbase, &lsize, &ltype);
		if (base >= lbase + lsize)
			continue;
		if ((base < lbase) && (base + size <= lbase))
			continue;
		/*  At this point we know there is some kind of overlap/enclosure  */
		if ((base < lbase) || (base + size > lbase + lsize)) {
			printk(KERN_WARNING
			       "mtrr: 0x%lx000,0x%lx000 overlaps existing"
			       " 0x%lx000,0x%x000\n", base, size, lbase,
			       lsize);
			goto out;
		}
		/*  New region is enclosed by an existing region  */
		if (ltype != type) {
			if (type == MTRR_TYPE_UNCACHABLE)
				continue;
			printk (KERN_WARNING "mtrr: type mismatch for %lx000,%lx000 old: %s new: %s\n",
			     base, size, mtrr_attrib_to_str(ltype),
			     mtrr_attrib_to_str(type));
			goto out;
		}
		if (increment)
			++usage_table[i];
		error = i;
		goto out;
	}
	/*  Search for an empty MTRR  */
	i = mtrr_if->get_free_region(base, size);
	if (i >= 0) {
		set_mtrr(i, base, size, type);
		usage_table[i] = 1;
	} else
		printk(KERN_INFO "mtrr: no more MTRRs available\n");
	error = i;
 out:
	mutex_unlock(&mtrr_mutex);
	unlock_cpu_hotplug();
	return error;
}

static int mtrr_check(unsigned long base, unsigned long size)
{
	if ((base & (PAGE_SIZE - 1)) || (size & (PAGE_SIZE - 1))) {
		printk(KERN_WARNING
			"mtrr: size and base must be multiples of 4 kiB\n");
		printk(KERN_DEBUG
			"mtrr: size: 0x%lx  base: 0x%lx\n", size, base);
		dump_stack();
		return -1;
	}
	return 0;
}

/**
 *	mtrr_add - Add a memory type region
 *	@base: Physical base address of region
 *	@size: Physical size of region
 *	@type: Type of MTRR desired
 *	@increment: If this is true do usage counting on the region
 *
 *	Memory type region registers control the caching on newer Intel and
 *	non Intel processors. This function allows drivers to request an
 *	MTRR is added. The details and hardware specifics of each processor's
 *	implementation are hidden from the caller, but nevertheless the 
 *	caller should expect to need to provide a power of two size on an
 *	equivalent power of two boundary.
 *
 *	If the region cannot be added either because all regions are in use
 *	or the CPU cannot support it a negative value is returned. On success
 *	the register number for this entry is returned, but should be treated
 *	as a cookie only.
 *
 *	On a multiprocessor machine the changes are made to all processors.
 *	This is required on x86 by the Intel processors.
 *
 *	The available types are
 *
 *	%MTRR_TYPE_UNCACHABLE	-	No caching
 *
 *	%MTRR_TYPE_WRBACK	-	Write data back in bursts whenever
 *
 *	%MTRR_TYPE_WRCOMB	-	Write data back soon but allow bursts
 *
 *	%MTRR_TYPE_WRTHROUGH	-	Cache reads but not writes
 *
 *	BUGS: Needs a quiet flag for the cases where drivers do not mind
 *	failures and do not wish system log messages to be sent.
 */

int
mtrr_add(unsigned long base, unsigned long size, unsigned int type,
	 char increment)
{
	if (mtrr_check(base, size))
		return -EINVAL;
	return mtrr_add_page(base >> PAGE_SHIFT, size >> PAGE_SHIFT, type,
			     increment);
}

/**
 *	mtrr_del_page - delete a memory type region
 *	@reg: Register returned by mtrr_add
 *	@base: Physical base address
 *	@size: Size of region
 *
 *	If register is supplied then base and size are ignored. This is
 *	how drivers should call it.
 *
 *	Releases an MTRR region. If the usage count drops to zero the 
 *	register is freed and the region returns to default state.
 *	On success the register is returned, on failure a negative error
 *	code.
 */

int mtrr_del_page(int reg, unsigned long base, unsigned long size)
{
	int i, max;
	mtrr_type ltype;
	unsigned long lbase;
	unsigned int lsize;
	int error = -EINVAL;

	if (!mtrr_if)
		return -ENXIO;

	max = num_var_ranges;
	/* No CPU hotplug when we change MTRR entries */
	lock_cpu_hotplug();
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
			printk(KERN_DEBUG "mtrr: no MTRR for %lx000,%lx000 found\n", base,
			       size);
			goto out;
		}
	}
	if (reg >= max) {
		printk(KERN_WARNING "mtrr: register: %d too big\n", reg);
		goto out;
	}
	if (is_cpu(CYRIX) && !use_intel()) {
		if ((reg == 3) && arr3_protected) {
			printk(KERN_WARNING "mtrr: ARR3 cannot be changed\n");
			goto out;
		}
	}
	mtrr_if->get(reg, &lbase, &lsize, &ltype);
	if (lsize < 1) {
		printk(KERN_WARNING "mtrr: MTRR %d not used\n", reg);
		goto out;
	}
	if (usage_table[reg] < 1) {
		printk(KERN_WARNING "mtrr: reg: %d has count=0\n", reg);
		goto out;
	}
	if (--usage_table[reg] < 1)
		set_mtrr(reg, 0, 0, 0);
	error = reg;
 out:
	mutex_unlock(&mtrr_mutex);
	unlock_cpu_hotplug();
	return error;
}
/**
 *	mtrr_del - delete a memory type region
 *	@reg: Register returned by mtrr_add
 *	@base: Physical base address
 *	@size: Size of region
 *
 *	If register is supplied then base and size are ignored. This is
 *	how drivers should call it.
 *
 *	Releases an MTRR region. If the usage count drops to zero the 
 *	register is freed and the region returns to default state.
 *	On success the register is returned, on failure a negative error
 *	code.
 */

int
mtrr_del(int reg, unsigned long base, unsigned long size)
{
	if (mtrr_check(base, size))
		return -EINVAL;
	return mtrr_del_page(reg, base >> PAGE_SHIFT, size >> PAGE_SHIFT);
}

EXPORT_SYMBOL(mtrr_add);
EXPORT_SYMBOL(mtrr_del);

/* HACK ALERT!
 * These should be called implicitly, but we can't yet until all the initcall
 * stuff is done...
 */
extern void amd_init_mtrr(void);
extern void cyrix_init_mtrr(void);
extern void centaur_init_mtrr(void);

static void __init init_ifs(void)
{
#ifndef CONFIG_X86_64
	amd_init_mtrr();
	cyrix_init_mtrr();
	centaur_init_mtrr();
#endif
}

/* The suspend/resume methods are only for CPU without MTRR. CPU using generic
 * MTRR driver doesn't require this
 */
struct mtrr_value {
	mtrr_type	ltype;
	unsigned long	lbase;
	unsigned int	lsize;
};

static struct mtrr_value * mtrr_state;

static int mtrr_save(struct sys_device * sysdev, pm_message_t state)
{
	int i;
	int size = num_var_ranges * sizeof(struct mtrr_value);

	mtrr_state = kmalloc(size,GFP_ATOMIC);
	if (mtrr_state)
		memset(mtrr_state,0,size);
	else
		return -ENOMEM;

	for (i = 0; i < num_var_ranges; i++) {
		mtrr_if->get(i,
			     &mtrr_state[i].lbase,
			     &mtrr_state[i].lsize,
			     &mtrr_state[i].ltype);
	}
	return 0;
}

static int mtrr_restore(struct sys_device * sysdev)
{
	int i;

	for (i = 0; i < num_var_ranges; i++) {
		if (mtrr_state[i].lsize) 
			set_mtrr(i,
				 mtrr_state[i].lbase,
				 mtrr_state[i].lsize,
				 mtrr_state[i].ltype);
	}
	kfree(mtrr_state);
	return 0;
}



static struct sysdev_driver mtrr_sysdev_driver = {
	.suspend	= mtrr_save,
	.resume		= mtrr_restore,
};


/**
 * mtrr_bp_init - initialize mtrrs on the boot CPU
 *
 * This needs to be called early; before any of the other CPUs are 
 * initialized (i.e. before smp_init()).
 * 
 */
void __init mtrr_bp_init(void)
{
	init_ifs();

	if (cpu_has_mtrr) {
		mtrr_if = &generic_mtrr_ops;
		size_or_mask = 0xff000000;	/* 36 bits */
		size_and_mask = 0x00f00000;

		/* This is an AMD specific MSR, but we assume(hope?) that
		   Intel will implement it to when they extend the address
		   bus of the Xeon. */
		if (cpuid_eax(0x80000000) >= 0x80000008) {
			u32 phys_addr;
			phys_addr = cpuid_eax(0x80000008) & 0xff;
			/* CPUID workaround for Intel 0F33/0F34 CPU */
			if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL &&
			    boot_cpu_data.x86 == 0xF &&
			    boot_cpu_data.x86_model == 0x3 &&
			    (boot_cpu_data.x86_mask == 0x3 ||
			     boot_cpu_data.x86_mask == 0x4))
				phys_addr = 36;

			size_or_mask = ~((1 << (phys_addr - PAGE_SHIFT)) - 1);
			size_and_mask = ~size_or_mask & 0xfff00000;
		} else if (boot_cpu_data.x86_vendor == X86_VENDOR_CENTAUR &&
			   boot_cpu_data.x86 == 6) {
			/* VIA C* family have Intel style MTRRs, but
			   don't support PAE */
			size_or_mask = 0xfff00000;	/* 32 bits */
			size_and_mask = 0;
		}
	} else {
		switch (boot_cpu_data.x86_vendor) {
		case X86_VENDOR_AMD:
			if (cpu_has_k6_mtrr) {
				/* Pre-Athlon (K6) AMD CPU MTRRs */
				mtrr_if = mtrr_ops[X86_VENDOR_AMD];
				size_or_mask = 0xfff00000;	/* 32 bits */
				size_and_mask = 0;
			}
			break;
		case X86_VENDOR_CENTAUR:
			if (cpu_has_centaur_mcr) {
				mtrr_if = mtrr_ops[X86_VENDOR_CENTAUR];
				size_or_mask = 0xfff00000;	/* 32 bits */
				size_and_mask = 0;
			}
			break;
		case X86_VENDOR_CYRIX:
			if (cpu_has_cyrix_arr) {
				mtrr_if = mtrr_ops[X86_VENDOR_CYRIX];
				size_or_mask = 0xfff00000;	/* 32 bits */
				size_and_mask = 0;
			}
			break;
		default:
			break;
		}
	}

	if (mtrr_if) {
		set_num_var_ranges();
		init_table();
		if (use_intel())
			get_mtrr_state();
	}
}

void mtrr_ap_init(void)
{
	unsigned long flags;

	if (!mtrr_if || !use_intel())
		return;
	/*
	 * Ideally we should hold mtrr_mutex here to avoid mtrr entries changed,
	 * but this routine will be called in cpu boot time, holding the lock
	 * breaks it. This routine is called in two cases: 1.very earily time
	 * of software resume, when there absolutely isn't mtrr entry changes;
	 * 2.cpu hotadd time. We let mtrr_add/del_page hold cpuhotplug lock to
	 * prevent mtrr entry changes
	 */
	local_irq_save(flags);

	mtrr_if->set_all();

	local_irq_restore(flags);
}

static int __init mtrr_init_finialize(void)
{
	if (!mtrr_if)
		return 0;
	if (use_intel())
		mtrr_state_warn();
	else {
		/* The CPUs haven't MTRR and seemes not support SMP. They have
		 * specific drivers, we use a tricky method to support
		 * suspend/resume for them.
		 * TBD: is there any system with such CPU which supports
		 * suspend/resume?  if no, we should remove the code.
		 */
		sysdev_driver_register(&cpu_sysdev_class,
			&mtrr_sysdev_driver);
	}
	return 0;
}
subsys_initcall(mtrr_init_finialize);
