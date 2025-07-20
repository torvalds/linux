// SPDX-License-Identifier: GPL-2.0-only
/*
 * Architecture specific (PPC64) functions for kexec based crash dumps.
 *
 * Copyright (C) 2005, IBM Corp.
 *
 * Created by: Haren Myneni
 */

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/reboot.h>
#include <linux/kexec.h>
#include <linux/export.h>
#include <linux/crash_dump.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/types.h>
#include <linux/libfdt.h>
#include <linux/memory.h>

#include <asm/processor.h>
#include <asm/machdep.h>
#include <asm/kexec.h>
#include <asm/smp.h>
#include <asm/setjmp.h>
#include <asm/debug.h>
#include <asm/interrupt.h>
#include <asm/kexec_ranges.h>

/*
 * The primary CPU waits a while for all secondary CPUs to enter. This is to
 * avoid sending an IPI if the secondary CPUs are entering
 * crash_kexec_secondary on their own (eg via a system reset).
 *
 * The secondary timeout has to be longer than the primary. Both timeouts are
 * in milliseconds.
 */
#define PRIMARY_TIMEOUT		500
#define SECONDARY_TIMEOUT	1000

#define IPI_TIMEOUT		10000
#define REAL_MODE_TIMEOUT	10000

static int time_to_dump;

/*
 * In case of system reset, secondary CPUs enter crash_kexec_secondary with out
 * having to send an IPI explicitly. So, indicate if the crash is via
 * system reset to avoid sending another IPI.
 */
static int is_via_system_reset;

/*
 * crash_wake_offline should be set to 1 by platforms that intend to wake
 * up offline cpus prior to jumping to a kdump kernel. Currently powernv
 * sets it to 1, since we want to avoid things from happening when an
 * offline CPU wakes up due to something like an HMI (malfunction error),
 * which propagates to all threads.
 */
int crash_wake_offline;

#define CRASH_HANDLER_MAX 3
/* List of shutdown handles */
static crash_shutdown_t crash_shutdown_handles[CRASH_HANDLER_MAX];
static DEFINE_SPINLOCK(crash_handlers_lock);

static unsigned long crash_shutdown_buf[JMP_BUF_LEN];
static int crash_shutdown_cpu = -1;

static int handle_fault(struct pt_regs *regs)
{
	if (crash_shutdown_cpu == smp_processor_id())
		longjmp(crash_shutdown_buf, 1);
	return 0;
}

#ifdef CONFIG_SMP

static atomic_t cpus_in_crash;
void crash_ipi_callback(struct pt_regs *regs)
{
	static cpumask_t cpus_state_saved = CPU_MASK_NONE;

	int cpu = smp_processor_id();

	hard_irq_disable();
	if (!cpumask_test_cpu(cpu, &cpus_state_saved)) {
		crash_save_cpu(regs, cpu);
		cpumask_set_cpu(cpu, &cpus_state_saved);
	}

	atomic_inc(&cpus_in_crash);
	smp_mb__after_atomic();

	/*
	 * Starting the kdump boot.
	 * This barrier is needed to make sure that all CPUs are stopped.
	 */
	while (!time_to_dump)
		cpu_relax();

	if (ppc_md.kexec_cpu_down)
		ppc_md.kexec_cpu_down(1, 1);

#ifdef CONFIG_PPC64
	kexec_smp_wait();
#else
	for (;;);	/* FIXME */
#endif

	/* NOTREACHED */
}

static void crash_kexec_prepare_cpus(void)
{
	unsigned int msecs;
	volatile unsigned int ncpus = num_online_cpus() - 1;/* Excluding the panic cpu */
	volatile int tries = 0;
	int (*old_handler)(struct pt_regs *regs);

	printk(KERN_EMERG "Sending IPI to other CPUs\n");

	if (crash_wake_offline)
		ncpus = num_present_cpus() - 1;

	/*
	 * If we came in via system reset, secondaries enter via crash_kexec_secondary().
	 * So, wait a while for the secondary CPUs to enter for that case.
	 * Else, send IPI to all other CPUs.
	 */
	if (is_via_system_reset)
		mdelay(PRIMARY_TIMEOUT);
	else
		crash_send_ipi(crash_ipi_callback);
	smp_wmb();

again:
	/*
	 * FIXME: Until we will have the way to stop other CPUs reliably,
	 * the crash CPU will send an IPI and wait for other CPUs to
	 * respond.
	 */
	msecs = IPI_TIMEOUT;
	while ((atomic_read(&cpus_in_crash) < ncpus) && (--msecs > 0))
		mdelay(1);

	/* Would it be better to replace the trap vector here? */

	if (atomic_read(&cpus_in_crash) >= ncpus) {
		printk(KERN_EMERG "IPI complete\n");
		return;
	}

	printk(KERN_EMERG "ERROR: %d cpu(s) not responding\n",
		ncpus - atomic_read(&cpus_in_crash));

	/*
	 * If we have a panic timeout set then we can't wait indefinitely
	 * for someone to activate system reset. We also give up on the
	 * second time through if system reset fail to work.
	 */
	if ((panic_timeout > 0) || (tries > 0))
		return;

	/*
	 * A system reset will cause all CPUs to take an 0x100 exception.
	 * The primary CPU returns here via setjmp, and the secondary
	 * CPUs reexecute the crash_kexec_secondary path.
	 */
	old_handler = __debugger;
	__debugger = handle_fault;
	crash_shutdown_cpu = smp_processor_id();

	if (setjmp(crash_shutdown_buf) == 0) {
		printk(KERN_EMERG "Activate system reset (dumprestart) "
				  "to stop other cpu(s)\n");

		/*
		 * A system reset will force all CPUs to execute the
		 * crash code again. We need to reset cpus_in_crash so we
		 * wait for everyone to do this.
		 */
		atomic_set(&cpus_in_crash, 0);
		smp_mb();

		while (atomic_read(&cpus_in_crash) < ncpus)
			cpu_relax();
	}

	crash_shutdown_cpu = -1;
	__debugger = old_handler;

	tries++;
	goto again;
}

/*
 * This function will be called by secondary cpus.
 */
void crash_kexec_secondary(struct pt_regs *regs)
{
	unsigned long flags;
	int msecs = SECONDARY_TIMEOUT;

	local_irq_save(flags);

	/* Wait for the primary crash CPU to signal its progress */
	while (crashing_cpu < 0) {
		if (--msecs < 0) {
			/* No response, kdump image may not have been loaded */
			local_irq_restore(flags);
			return;
		}

		mdelay(1);
	}

	crash_ipi_callback(regs);
}

#else	/* ! CONFIG_SMP */

static void crash_kexec_prepare_cpus(void)
{
	/*
	 * move the secondaries to us so that we can copy
	 * the new kernel 0-0x100 safely
	 *
	 * do this if kexec in setup.c ?
	 */
#ifdef CONFIG_PPC64
	smp_release_cpus();
#else
	/* FIXME */
#endif
}

void crash_kexec_secondary(struct pt_regs *regs)
{
}
#endif	/* CONFIG_SMP */

/* wait for all the CPUs to hit real mode but timeout if they don't come in */
#if defined(CONFIG_SMP) && defined(CONFIG_PPC64)
noinstr static void __maybe_unused crash_kexec_wait_realmode(int cpu)
{
	unsigned int msecs;
	int i;

	msecs = REAL_MODE_TIMEOUT;
	for (i=0; i < nr_cpu_ids && msecs > 0; i++) {
		if (i == cpu)
			continue;

		while (paca_ptrs[i]->kexec_state < KEXEC_STATE_REAL_MODE) {
			barrier();
			if (!cpu_possible(i) || !cpu_online(i) || (msecs <= 0))
				break;
			msecs--;
			mdelay(1);
		}
	}
	mb();
}
#else
static inline void crash_kexec_wait_realmode(int cpu) {}
#endif	/* CONFIG_SMP && CONFIG_PPC64 */

void crash_kexec_prepare(void)
{
	/* Avoid hardlocking with irresponsive CPU holding logbuf_lock */
	printk_deferred_enter();

	/*
	 * This function is only called after the system
	 * has panicked or is otherwise in a critical state.
	 * The minimum amount of code to allow a kexec'd kernel
	 * to run successfully needs to happen here.
	 *
	 * In practice this means stopping other cpus in
	 * an SMP system.
	 * The kernel is broken so disable interrupts.
	 */
	hard_irq_disable();

	/*
	 * Make a note of crashing cpu. Will be used in machine_kexec
	 * such that another IPI will not be sent.
	 */
	crashing_cpu = smp_processor_id();

	crash_kexec_prepare_cpus();
}

/*
 * Register a function to be called on shutdown.  Only use this if you
 * can't reset your device in the second kernel.
 */
int crash_shutdown_register(crash_shutdown_t handler)
{
	unsigned int i, rc;

	spin_lock(&crash_handlers_lock);
	for (i = 0 ; i < CRASH_HANDLER_MAX; i++)
		if (!crash_shutdown_handles[i]) {
			/* Insert handle at first empty entry */
			crash_shutdown_handles[i] = handler;
			rc = 0;
			break;
		}

	if (i == CRASH_HANDLER_MAX) {
		printk(KERN_ERR "Crash shutdown handles full, "
		       "not registered.\n");
		rc = 1;
	}

	spin_unlock(&crash_handlers_lock);
	return rc;
}
EXPORT_SYMBOL(crash_shutdown_register);

int crash_shutdown_unregister(crash_shutdown_t handler)
{
	unsigned int i, rc;

	spin_lock(&crash_handlers_lock);
	for (i = 0 ; i < CRASH_HANDLER_MAX; i++)
		if (crash_shutdown_handles[i] == handler)
			break;

	if (i == CRASH_HANDLER_MAX) {
		printk(KERN_ERR "Crash shutdown handle not found\n");
		rc = 1;
	} else {
		/* Shift handles down */
		for (; i < (CRASH_HANDLER_MAX - 1); i++)
			crash_shutdown_handles[i] =
				crash_shutdown_handles[i+1];
		/*
		 * Reset last entry to NULL now that it has been shifted down,
		 * this will allow new handles to be added here.
		 */
		crash_shutdown_handles[i] = NULL;
		rc = 0;
	}

	spin_unlock(&crash_handlers_lock);
	return rc;
}
EXPORT_SYMBOL(crash_shutdown_unregister);

void default_machine_crash_shutdown(struct pt_regs *regs)
{
	volatile unsigned int i;
	int (*old_handler)(struct pt_regs *regs);

	if (TRAP(regs) == INTERRUPT_SYSTEM_RESET)
		is_via_system_reset = 1;

	if (IS_ENABLED(CONFIG_SMP))
		crash_smp_send_stop();
	else
		crash_kexec_prepare();

	crash_save_cpu(regs, crashing_cpu);

	time_to_dump = 1;

	crash_kexec_wait_realmode(crashing_cpu);

	machine_kexec_mask_interrupts();

	/*
	 * Call registered shutdown routines safely.  Swap out
	 * __debugger_fault_handler, and replace on exit.
	 */
	old_handler = __debugger_fault_handler;
	__debugger_fault_handler = handle_fault;
	crash_shutdown_cpu = smp_processor_id();
	for (i = 0; i < CRASH_HANDLER_MAX && crash_shutdown_handles[i]; i++) {
		if (setjmp(crash_shutdown_buf) == 0) {
			/*
			 * Insert syncs and delay to ensure
			 * instructions in the dangerous region don't
			 * leak away from this protected region.
			 */
			asm volatile("sync; isync");
			/* dangerous region */
			crash_shutdown_handles[i]();
			asm volatile("sync; isync");
		}
	}
	crash_shutdown_cpu = -1;
	__debugger_fault_handler = old_handler;

	if (ppc_md.kexec_cpu_down)
		ppc_md.kexec_cpu_down(1, 0);
}

#ifdef CONFIG_CRASH_HOTPLUG
#undef pr_fmt
#define pr_fmt(fmt) "crash hp: " fmt

/*
 * Advertise preferred elfcorehdr size to userspace via
 * /sys/kernel/crash_elfcorehdr_size sysfs interface.
 */
unsigned int arch_crash_get_elfcorehdr_size(void)
{
	unsigned long phdr_cnt;

	/* A program header for possible CPUs + vmcoreinfo */
	phdr_cnt = num_possible_cpus() + 1;
	if (IS_ENABLED(CONFIG_MEMORY_HOTPLUG))
		phdr_cnt += CONFIG_CRASH_MAX_MEMORY_RANGES;

	return sizeof(struct elfhdr) + (phdr_cnt * sizeof(Elf64_Phdr));
}

/**
 * update_crash_elfcorehdr() - Recreate the elfcorehdr and replace it with old
 *			       elfcorehdr in the kexec segment array.
 * @image: the active struct kimage
 * @mn: struct memory_notify data handler
 */
static void update_crash_elfcorehdr(struct kimage *image, struct memory_notify *mn)
{
	int ret;
	struct crash_mem *cmem = NULL;
	struct kexec_segment *ksegment;
	void *ptr, *mem, *elfbuf = NULL;
	unsigned long elfsz, memsz, base_addr, size;

	ksegment = &image->segment[image->elfcorehdr_index];
	mem = (void *) ksegment->mem;
	memsz = ksegment->memsz;

	ret = get_crash_memory_ranges(&cmem);
	if (ret) {
		pr_err("Failed to get crash mem range\n");
		return;
	}

	/*
	 * The hot unplugged memory is part of crash memory ranges,
	 * remove it here.
	 */
	if (image->hp_action == KEXEC_CRASH_HP_REMOVE_MEMORY) {
		base_addr = PFN_PHYS(mn->start_pfn);
		size = mn->nr_pages * PAGE_SIZE;
		ret = remove_mem_range(&cmem, base_addr, size);
		if (ret) {
			pr_err("Failed to remove hot-unplugged memory from crash memory ranges\n");
			goto out;
		}
	}

	ret = crash_prepare_elf64_headers(cmem, false, &elfbuf, &elfsz);
	if (ret) {
		pr_err("Failed to prepare elf header\n");
		goto out;
	}

	/*
	 * It is unlikely that kernel hit this because elfcorehdr kexec
	 * segment (memsz) is built with addition space to accommodate growing
	 * number of crash memory ranges while loading the kdump kernel. It is
	 * Just to avoid any unforeseen case.
	 */
	if (elfsz > memsz) {
		pr_err("Updated crash elfcorehdr elfsz %lu > memsz %lu", elfsz, memsz);
		goto out;
	}

	ptr = __va(mem);
	if (ptr) {
		/* Temporarily invalidate the crash image while it is replaced */
		xchg(&kexec_crash_image, NULL);

		/* Replace the old elfcorehdr with newly prepared elfcorehdr */
		memcpy((void *)ptr, elfbuf, elfsz);

		/* The crash image is now valid once again */
		xchg(&kexec_crash_image, image);
	}
out:
	kvfree(cmem);
	kvfree(elfbuf);
}

/**
 * get_fdt_index - Loop through the kexec segment array and find
 *		   the index of the FDT segment.
 * @image: a pointer to kexec_crash_image
 *
 * Returns the index of FDT segment in the kexec segment array
 * if found; otherwise -1.
 */
static int get_fdt_index(struct kimage *image)
{
	void *ptr;
	unsigned long mem;
	int i, fdt_index = -1;

	/* Find the FDT segment index in kexec segment array. */
	for (i = 0; i < image->nr_segments; i++) {
		mem = image->segment[i].mem;
		ptr = __va(mem);

		if (ptr && fdt_magic(ptr) == FDT_MAGIC) {
			fdt_index = i;
			break;
		}
	}

	return fdt_index;
}

/**
 * update_crash_fdt - updates the cpus node of the crash FDT.
 *
 * @image: a pointer to kexec_crash_image
 */
static void update_crash_fdt(struct kimage *image)
{
	void *fdt;
	int fdt_index;

	fdt_index = get_fdt_index(image);
	if (fdt_index < 0) {
		pr_err("Unable to locate FDT segment.\n");
		return;
	}

	fdt = __va((void *)image->segment[fdt_index].mem);

	/* Temporarily invalidate the crash image while it is replaced */
	xchg(&kexec_crash_image, NULL);

	/* update FDT to reflect changes in CPU resources */
	if (update_cpus_node(fdt))
		pr_err("Failed to update crash FDT");

	/* The crash image is now valid once again */
	xchg(&kexec_crash_image, image);
}

int arch_crash_hotplug_support(struct kimage *image, unsigned long kexec_flags)
{
#ifdef CONFIG_KEXEC_FILE
	if (image->file_mode)
		return 1;
#endif
	return kexec_flags & KEXEC_CRASH_HOTPLUG_SUPPORT;
}

/**
 * arch_crash_handle_hotplug_event - Handle crash CPU/Memory hotplug events to update the
 *				     necessary kexec segments based on the hotplug event.
 * @image: a pointer to kexec_crash_image
 * @arg: struct memory_notify handler for memory hotplug case and NULL for CPU hotplug case.
 *
 * Update the kdump image based on the type of hotplug event, represented by image->hp_action.
 * CPU add: Update the FDT segment to include the newly added CPU.
 * CPU remove: No action is needed, with the assumption that it's okay to have offline CPUs
 *	       part of the FDT.
 * Memory add/remove: No action is taken as this is not yet supported.
 */
void arch_crash_handle_hotplug_event(struct kimage *image, void *arg)
{
	struct memory_notify *mn;

	switch (image->hp_action) {
	case KEXEC_CRASH_HP_REMOVE_CPU:
		return;

	case KEXEC_CRASH_HP_ADD_CPU:
		update_crash_fdt(image);
		break;

	case KEXEC_CRASH_HP_REMOVE_MEMORY:
	case KEXEC_CRASH_HP_ADD_MEMORY:
		mn = (struct memory_notify *)arg;
		update_crash_elfcorehdr(image, mn);
		return;
	default:
		pr_warn_once("Unknown hotplug action\n");
	}
}
#endif /* CONFIG_CRASH_HOTPLUG */
