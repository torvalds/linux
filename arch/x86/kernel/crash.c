// SPDX-License-Identifier: GPL-2.0-only
/*
 * Architecture specific (i386/x86_64) functions for kexec based crash dumps.
 *
 * Created by: Hariprasad Nellitheertha (hari@in.ibm.com)
 *
 * Copyright (C) IBM Corporation, 2004. All rights reserved.
 * Copyright (C) Red Hat Inc., 2014. All rights reserved.
 * Authors:
 *      Vivek Goyal <vgoyal@redhat.com>
 *
 */

#define pr_fmt(fmt)	"kexec: " fmt

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/reboot.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <linux/elf.h>
#include <linux/elfcore.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/memblock.h>

#include <asm/processor.h>
#include <asm/hardirq.h>
#include <asm/nmi.h>
#include <asm/hw_irq.h>
#include <asm/apic.h>
#include <asm/e820/types.h>
#include <asm/io_apic.h>
#include <asm/hpet.h>
#include <linux/kdebug.h>
#include <asm/cpu.h>
#include <asm/reboot.h>
#include <asm/virtext.h>
#include <asm/intel_pt.h>
#include <asm/crash.h>
#include <asm/cmdline.h>

/* Used while preparing memory map entries for second kernel */
struct crash_memmap_data {
	struct boot_params *params;
	/* Type of memory */
	unsigned int type;
};

/*
 * This is used to VMCLEAR all VMCSs loaded on the
 * processor. And when loading kvm_intel module, the
 * callback function pointer will be assigned.
 *
 * protected by rcu.
 */
crash_vmclear_fn __rcu *crash_vmclear_loaded_vmcss = NULL;
EXPORT_SYMBOL_GPL(crash_vmclear_loaded_vmcss);

static inline void cpu_crash_vmclear_loaded_vmcss(void)
{
	crash_vmclear_fn *do_vmclear_operation = NULL;

	rcu_read_lock();
	do_vmclear_operation = rcu_dereference(crash_vmclear_loaded_vmcss);
	if (do_vmclear_operation)
		do_vmclear_operation();
	rcu_read_unlock();
}

#if defined(CONFIG_SMP) && defined(CONFIG_X86_LOCAL_APIC)

static void kdump_nmi_callback(int cpu, struct pt_regs *regs)
{
	crash_save_cpu(regs, cpu);

	/*
	 * VMCLEAR VMCSs loaded on all cpus if needed.
	 */
	cpu_crash_vmclear_loaded_vmcss();

	/* Disable VMX or SVM if needed.
	 *
	 * We need to disable virtualization on all CPUs.
	 * Having VMX or SVM enabled on any CPU may break rebooting
	 * after the kdump kernel has finished its task.
	 */
	cpu_emergency_vmxoff();
	cpu_emergency_svm_disable();

	/*
	 * Disable Intel PT to stop its logging
	 */
	cpu_emergency_stop_pt();

	disable_local_APIC();
}

void kdump_nmi_shootdown_cpus(void)
{
	nmi_shootdown_cpus(kdump_nmi_callback);

	disable_local_APIC();
}

/* Override the weak function in kernel/panic.c */
void crash_smp_send_stop(void)
{
	static int cpus_stopped;

	if (cpus_stopped)
		return;

	if (smp_ops.crash_stop_other_cpus)
		smp_ops.crash_stop_other_cpus();
	else
		smp_send_stop();

	cpus_stopped = 1;
}

#else
void crash_smp_send_stop(void)
{
	/* There are no cpus to shootdown */
}
#endif

void native_machine_crash_shutdown(struct pt_regs *regs)
{
	/* This function is only called after the system
	 * has panicked or is otherwise in a critical state.
	 * The minimum amount of code to allow a kexec'd kernel
	 * to run successfully needs to happen here.
	 *
	 * In practice this means shooting down the other cpus in
	 * an SMP system.
	 */
	/* The kernel is broken so disable interrupts */
	local_irq_disable();

	crash_smp_send_stop();

	/*
	 * VMCLEAR VMCSs loaded on this cpu if needed.
	 */
	cpu_crash_vmclear_loaded_vmcss();

	/* Booting kdump kernel with VMX or SVM enabled won't work,
	 * because (among other limitations) we can't disable paging
	 * with the virt flags.
	 */
	cpu_emergency_vmxoff();
	cpu_emergency_svm_disable();

	/*
	 * Disable Intel PT to stop its logging
	 */
	cpu_emergency_stop_pt();

#ifdef CONFIG_X86_IO_APIC
	/* Prevent crash_kexec() from deadlocking on ioapic_lock. */
	ioapic_zap_locks();
	clear_IO_APIC();
#endif
	lapic_shutdown();
	restore_boot_irq_mode();
#ifdef CONFIG_HPET_TIMER
	hpet_disable();
#endif
	crash_save_cpu(regs, safe_smp_processor_id());
}

#ifdef CONFIG_KEXEC_FILE

static int get_nr_ram_ranges_callback(struct resource *res, void *arg)
{
	unsigned int *nr_ranges = arg;

	(*nr_ranges)++;
	return 0;
}

/* Gather all the required information to prepare elf headers for ram regions */
static struct crash_mem *fill_up_crash_elf_data(void)
{
	unsigned int nr_ranges = 0;
	struct crash_mem *cmem;

	walk_system_ram_res(0, -1, &nr_ranges, get_nr_ram_ranges_callback);
	if (!nr_ranges)
		return NULL;

	/*
	 * Exclusion of crash region and/or crashk_low_res may cause
	 * another range split. So add extra two slots here.
	 */
	nr_ranges += 2;
	cmem = vzalloc(struct_size(cmem, ranges, nr_ranges));
	if (!cmem)
		return NULL;

	cmem->max_nr_ranges = nr_ranges;
	cmem->nr_ranges = 0;

	return cmem;
}

/*
 * Look for any unwanted ranges between mstart, mend and remove them. This
 * might lead to split and split ranges are put in cmem->ranges[] array
 */
static int elf_header_exclude_ranges(struct crash_mem *cmem)
{
	int ret = 0;

	/* Exclude the low 1M because it is always reserved */
	ret = crash_exclude_mem_range(cmem, 0, (1<<20)-1);
	if (ret)
		return ret;

	/* Exclude crashkernel region */
	ret = crash_exclude_mem_range(cmem, crashk_res.start, crashk_res.end);
	if (ret)
		return ret;

	if (crashk_low_res.end)
		ret = crash_exclude_mem_range(cmem, crashk_low_res.start,
					      crashk_low_res.end);

	return ret;
}

static int prepare_elf64_ram_headers_callback(struct resource *res, void *arg)
{
	struct crash_mem *cmem = arg;

	cmem->ranges[cmem->nr_ranges].start = res->start;
	cmem->ranges[cmem->nr_ranges].end = res->end;
	cmem->nr_ranges++;

	return 0;
}

/* Prepare elf headers. Return addr and size */
static int prepare_elf_headers(struct kimage *image, void **addr,
					unsigned long *sz)
{
	struct crash_mem *cmem;
	int ret;

	cmem = fill_up_crash_elf_data();
	if (!cmem)
		return -ENOMEM;

	ret = walk_system_ram_res(0, -1, cmem, prepare_elf64_ram_headers_callback);
	if (ret)
		goto out;

	/* Exclude unwanted mem ranges */
	ret = elf_header_exclude_ranges(cmem);
	if (ret)
		goto out;

	/* By default prepare 64bit headers */
	ret =  crash_prepare_elf64_headers(cmem, IS_ENABLED(CONFIG_X86_64), addr, sz);

out:
	vfree(cmem);
	return ret;
}

static int add_e820_entry(struct boot_params *params, struct e820_entry *entry)
{
	unsigned int nr_e820_entries;

	nr_e820_entries = params->e820_entries;
	if (nr_e820_entries >= E820_MAX_ENTRIES_ZEROPAGE)
		return 1;

	memcpy(&params->e820_table[nr_e820_entries], entry, sizeof(struct e820_entry));
	params->e820_entries++;
	return 0;
}

static int memmap_entry_callback(struct resource *res, void *arg)
{
	struct crash_memmap_data *cmd = arg;
	struct boot_params *params = cmd->params;
	struct e820_entry ei;

	ei.addr = res->start;
	ei.size = resource_size(res);
	ei.type = cmd->type;
	add_e820_entry(params, &ei);

	return 0;
}

static int memmap_exclude_ranges(struct kimage *image, struct crash_mem *cmem,
				 unsigned long long mstart,
				 unsigned long long mend)
{
	unsigned long start, end;

	cmem->ranges[0].start = mstart;
	cmem->ranges[0].end = mend;
	cmem->nr_ranges = 1;

	/* Exclude elf header region */
	start = image->elf_load_addr;
	end = start + image->elf_headers_sz - 1;
	return crash_exclude_mem_range(cmem, start, end);
}

/* Prepare memory map for crash dump kernel */
int crash_setup_memmap_entries(struct kimage *image, struct boot_params *params)
{
	int i, ret = 0;
	unsigned long flags;
	struct e820_entry ei;
	struct crash_memmap_data cmd;
	struct crash_mem *cmem;

	cmem = vzalloc(struct_size(cmem, ranges, 1));
	if (!cmem)
		return -ENOMEM;

	memset(&cmd, 0, sizeof(struct crash_memmap_data));
	cmd.params = params;

	/* Add the low 1M */
	cmd.type = E820_TYPE_RAM;
	flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
	walk_iomem_res_desc(IORES_DESC_NONE, flags, 0, (1<<20)-1, &cmd,
			    memmap_entry_callback);

	/* Add ACPI tables */
	cmd.type = E820_TYPE_ACPI;
	flags = IORESOURCE_MEM | IORESOURCE_BUSY;
	walk_iomem_res_desc(IORES_DESC_ACPI_TABLES, flags, 0, -1, &cmd,
			    memmap_entry_callback);

	/* Add ACPI Non-volatile Storage */
	cmd.type = E820_TYPE_NVS;
	walk_iomem_res_desc(IORES_DESC_ACPI_NV_STORAGE, flags, 0, -1, &cmd,
			    memmap_entry_callback);

	/* Add e820 reserved ranges */
	cmd.type = E820_TYPE_RESERVED;
	flags = IORESOURCE_MEM;
	walk_iomem_res_desc(IORES_DESC_RESERVED, flags, 0, -1, &cmd,
			    memmap_entry_callback);

	/* Add crashk_low_res region */
	if (crashk_low_res.end) {
		ei.addr = crashk_low_res.start;
		ei.size = resource_size(&crashk_low_res);
		ei.type = E820_TYPE_RAM;
		add_e820_entry(params, &ei);
	}

	/* Exclude some ranges from crashk_res and add rest to memmap */
	ret = memmap_exclude_ranges(image, cmem, crashk_res.start, crashk_res.end);
	if (ret)
		goto out;

	for (i = 0; i < cmem->nr_ranges; i++) {
		ei.size = cmem->ranges[i].end - cmem->ranges[i].start + 1;

		/* If entry is less than a page, skip it */
		if (ei.size < PAGE_SIZE)
			continue;
		ei.addr = cmem->ranges[i].start;
		ei.type = E820_TYPE_RAM;
		add_e820_entry(params, &ei);
	}

out:
	vfree(cmem);
	return ret;
}

int crash_load_segments(struct kimage *image)
{
	int ret;
	struct kexec_buf kbuf = { .image = image, .buf_min = 0,
				  .buf_max = ULONG_MAX, .top_down = false };

	/* Prepare elf headers and add a segment */
	ret = prepare_elf_headers(image, &kbuf.buffer, &kbuf.bufsz);
	if (ret)
		return ret;

	image->elf_headers = kbuf.buffer;
	image->elf_headers_sz = kbuf.bufsz;

	kbuf.memsz = kbuf.bufsz;
	kbuf.buf_align = ELF_CORE_HEADER_ALIGN;
	kbuf.mem = KEXEC_BUF_MEM_UNKNOWN;
	ret = kexec_add_buffer(&kbuf);
	if (ret) {
		vfree((void *)image->elf_headers);
		return ret;
	}
	image->elf_load_addr = kbuf.mem;
	pr_debug("Loaded ELF headers at 0x%lx bufsz=0x%lx memsz=0x%lx\n",
		 image->elf_load_addr, kbuf.bufsz, kbuf.bufsz);

	return ret;
}
#endif /* CONFIG_KEXEC_FILE */
