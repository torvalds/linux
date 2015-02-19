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
#include <linux/module.h>
#include <linux/slab.h>

#include <asm/processor.h>
#include <asm/hardirq.h>
#include <asm/nmi.h>
#include <asm/hw_irq.h>
#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/hpet.h>
#include <linux/kdebug.h>
#include <asm/cpu.h>
#include <asm/reboot.h>
#include <asm/virtext.h>

/* Alignment required for elf header segment */
#define ELF_CORE_HEADER_ALIGN   4096

/* This primarily represents number of split ranges due to exclusion */
#define CRASH_MAX_RANGES	16

struct crash_mem_range {
	u64 start, end;
};

struct crash_mem {
	unsigned int nr_ranges;
	struct crash_mem_range ranges[CRASH_MAX_RANGES];
};

/* Misc data about ram ranges needed to prepare elf headers */
struct crash_elf_data {
	struct kimage *image;
	/*
	 * Total number of ram ranges we have after various adjustments for
	 * GART, crash reserved region etc.
	 */
	unsigned int max_nr_ranges;
	unsigned long gart_start, gart_end;

	/* Pointer to elf header */
	void *ehdr;
	/* Pointer to next phdr */
	void *bufp;
	struct crash_mem mem;
};

/* Used while preparing memory map entries for second kernel */
struct crash_memmap_data {
	struct boot_params *params;
	/* Type of memory */
	unsigned int type;
};

int in_crash_kexec;

/*
 * This is used to VMCLEAR all VMCSs loaded on the
 * processor. And when loading kvm_intel module, the
 * callback function pointer will be assigned.
 *
 * protected by rcu.
 */
crash_vmclear_fn __rcu *crash_vmclear_loaded_vmcss = NULL;
EXPORT_SYMBOL_GPL(crash_vmclear_loaded_vmcss);
unsigned long crash_zero_bytes;

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
#ifdef CONFIG_X86_32
	struct pt_regs fixed_regs;

	if (!user_mode_vm(regs)) {
		crash_fixup_ss_esp(&fixed_regs, regs);
		regs = &fixed_regs;
	}
#endif
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

	disable_local_APIC();
}

static void kdump_nmi_shootdown_cpus(void)
{
	in_crash_kexec = 1;
	nmi_shootdown_cpus(kdump_nmi_callback);

	disable_local_APIC();
}

#else
static void kdump_nmi_shootdown_cpus(void)
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

	kdump_nmi_shootdown_cpus();

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

#ifdef CONFIG_X86_IO_APIC
	/* Prevent crash_kexec() from deadlocking on ioapic_lock. */
	ioapic_zap_locks();
	disable_IO_APIC();
#endif
	lapic_shutdown();
#ifdef CONFIG_HPET_TIMER
	hpet_disable();
#endif
	crash_save_cpu(regs, safe_smp_processor_id());
}

#ifdef CONFIG_KEXEC_FILE
static int get_nr_ram_ranges_callback(unsigned long start_pfn,
				unsigned long nr_pfn, void *arg)
{
	int *nr_ranges = arg;

	(*nr_ranges)++;
	return 0;
}

static int get_gart_ranges_callback(u64 start, u64 end, void *arg)
{
	struct crash_elf_data *ced = arg;

	ced->gart_start = start;
	ced->gart_end = end;

	/* Not expecting more than 1 gart aperture */
	return 1;
}


/* Gather all the required information to prepare elf headers for ram regions */
static void fill_up_crash_elf_data(struct crash_elf_data *ced,
				   struct kimage *image)
{
	unsigned int nr_ranges = 0;

	ced->image = image;

	walk_system_ram_range(0, -1, &nr_ranges,
				get_nr_ram_ranges_callback);

	ced->max_nr_ranges = nr_ranges;

	/*
	 * We don't create ELF headers for GART aperture as an attempt
	 * to dump this memory in second kernel leads to hang/crash.
	 * If gart aperture is present, one needs to exclude that region
	 * and that could lead to need of extra phdr.
	 */
	walk_iomem_res("GART", IORESOURCE_MEM, 0, -1,
				ced, get_gart_ranges_callback);

	/*
	 * If we have gart region, excluding that could potentially split
	 * a memory range, resulting in extra header. Account for  that.
	 */
	if (ced->gart_end)
		ced->max_nr_ranges++;

	/* Exclusion of crash region could split memory ranges */
	ced->max_nr_ranges++;

	/* If crashk_low_res is not 0, another range split possible */
	if (crashk_low_res.end)
		ced->max_nr_ranges++;
}

static int exclude_mem_range(struct crash_mem *mem,
		unsigned long long mstart, unsigned long long mend)
{
	int i, j;
	unsigned long long start, end;
	struct crash_mem_range temp_range = {0, 0};

	for (i = 0; i < mem->nr_ranges; i++) {
		start = mem->ranges[i].start;
		end = mem->ranges[i].end;

		if (mstart > end || mend < start)
			continue;

		/* Truncate any area outside of range */
		if (mstart < start)
			mstart = start;
		if (mend > end)
			mend = end;

		/* Found completely overlapping range */
		if (mstart == start && mend == end) {
			mem->ranges[i].start = 0;
			mem->ranges[i].end = 0;
			if (i < mem->nr_ranges - 1) {
				/* Shift rest of the ranges to left */
				for (j = i; j < mem->nr_ranges - 1; j++) {
					mem->ranges[j].start =
						mem->ranges[j+1].start;
					mem->ranges[j].end =
							mem->ranges[j+1].end;
				}
			}
			mem->nr_ranges--;
			return 0;
		}

		if (mstart > start && mend < end) {
			/* Split original range */
			mem->ranges[i].end = mstart - 1;
			temp_range.start = mend + 1;
			temp_range.end = end;
		} else if (mstart != start)
			mem->ranges[i].end = mstart - 1;
		else
			mem->ranges[i].start = mend + 1;
		break;
	}

	/* If a split happend, add the split to array */
	if (!temp_range.end)
		return 0;

	/* Split happened */
	if (i == CRASH_MAX_RANGES - 1) {
		pr_err("Too many crash ranges after split\n");
		return -ENOMEM;
	}

	/* Location where new range should go */
	j = i + 1;
	if (j < mem->nr_ranges) {
		/* Move over all ranges one slot towards the end */
		for (i = mem->nr_ranges - 1; i >= j; i--)
			mem->ranges[i + 1] = mem->ranges[i];
	}

	mem->ranges[j].start = temp_range.start;
	mem->ranges[j].end = temp_range.end;
	mem->nr_ranges++;
	return 0;
}

/*
 * Look for any unwanted ranges between mstart, mend and remove them. This
 * might lead to split and split ranges are put in ced->mem.ranges[] array
 */
static int elf_header_exclude_ranges(struct crash_elf_data *ced,
		unsigned long long mstart, unsigned long long mend)
{
	struct crash_mem *cmem = &ced->mem;
	int ret = 0;

	memset(cmem->ranges, 0, sizeof(cmem->ranges));

	cmem->ranges[0].start = mstart;
	cmem->ranges[0].end = mend;
	cmem->nr_ranges = 1;

	/* Exclude crashkernel region */
	ret = exclude_mem_range(cmem, crashk_res.start, crashk_res.end);
	if (ret)
		return ret;

	if (crashk_low_res.end) {
		ret = exclude_mem_range(cmem, crashk_low_res.start, crashk_low_res.end);
		if (ret)
			return ret;
	}

	/* Exclude GART region */
	if (ced->gart_end) {
		ret = exclude_mem_range(cmem, ced->gart_start, ced->gart_end);
		if (ret)
			return ret;
	}

	return ret;
}

static int prepare_elf64_ram_headers_callback(u64 start, u64 end, void *arg)
{
	struct crash_elf_data *ced = arg;
	Elf64_Ehdr *ehdr;
	Elf64_Phdr *phdr;
	unsigned long mstart, mend;
	struct kimage *image = ced->image;
	struct crash_mem *cmem;
	int ret, i;

	ehdr = ced->ehdr;

	/* Exclude unwanted mem ranges */
	ret = elf_header_exclude_ranges(ced, start, end);
	if (ret)
		return ret;

	/* Go through all the ranges in ced->mem.ranges[] and prepare phdr */
	cmem = &ced->mem;

	for (i = 0; i < cmem->nr_ranges; i++) {
		mstart = cmem->ranges[i].start;
		mend = cmem->ranges[i].end;

		phdr = ced->bufp;
		ced->bufp += sizeof(Elf64_Phdr);

		phdr->p_type = PT_LOAD;
		phdr->p_flags = PF_R|PF_W|PF_X;
		phdr->p_offset  = mstart;

		/*
		 * If a range matches backup region, adjust offset to backup
		 * segment.
		 */
		if (mstart == image->arch.backup_src_start &&
		    (mend - mstart + 1) == image->arch.backup_src_sz)
			phdr->p_offset = image->arch.backup_load_addr;

		phdr->p_paddr = mstart;
		phdr->p_vaddr = (unsigned long long) __va(mstart);
		phdr->p_filesz = phdr->p_memsz = mend - mstart + 1;
		phdr->p_align = 0;
		ehdr->e_phnum++;
		pr_debug("Crash PT_LOAD elf header. phdr=%p vaddr=0x%llx, paddr=0x%llx, sz=0x%llx e_phnum=%d p_offset=0x%llx\n",
			phdr, phdr->p_vaddr, phdr->p_paddr, phdr->p_filesz,
			ehdr->e_phnum, phdr->p_offset);
	}

	return ret;
}

static int prepare_elf64_headers(struct crash_elf_data *ced,
		void **addr, unsigned long *sz)
{
	Elf64_Ehdr *ehdr;
	Elf64_Phdr *phdr;
	unsigned long nr_cpus = num_possible_cpus(), nr_phdr, elf_sz;
	unsigned char *buf, *bufp;
	unsigned int cpu;
	unsigned long long notes_addr;
	int ret;

	/* extra phdr for vmcoreinfo elf note */
	nr_phdr = nr_cpus + 1;
	nr_phdr += ced->max_nr_ranges;

	/*
	 * kexec-tools creates an extra PT_LOAD phdr for kernel text mapping
	 * area on x86_64 (ffffffff80000000 - ffffffffa0000000).
	 * I think this is required by tools like gdb. So same physical
	 * memory will be mapped in two elf headers. One will contain kernel
	 * text virtual addresses and other will have __va(physical) addresses.
	 */

	nr_phdr++;
	elf_sz = sizeof(Elf64_Ehdr) + nr_phdr * sizeof(Elf64_Phdr);
	elf_sz = ALIGN(elf_sz, ELF_CORE_HEADER_ALIGN);

	buf = vzalloc(elf_sz);
	if (!buf)
		return -ENOMEM;

	bufp = buf;
	ehdr = (Elf64_Ehdr *)bufp;
	bufp += sizeof(Elf64_Ehdr);
	memcpy(ehdr->e_ident, ELFMAG, SELFMAG);
	ehdr->e_ident[EI_CLASS] = ELFCLASS64;
	ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
	ehdr->e_ident[EI_VERSION] = EV_CURRENT;
	ehdr->e_ident[EI_OSABI] = ELF_OSABI;
	memset(ehdr->e_ident + EI_PAD, 0, EI_NIDENT - EI_PAD);
	ehdr->e_type = ET_CORE;
	ehdr->e_machine = ELF_ARCH;
	ehdr->e_version = EV_CURRENT;
	ehdr->e_phoff = sizeof(Elf64_Ehdr);
	ehdr->e_ehsize = sizeof(Elf64_Ehdr);
	ehdr->e_phentsize = sizeof(Elf64_Phdr);

	/* Prepare one phdr of type PT_NOTE for each present cpu */
	for_each_present_cpu(cpu) {
		phdr = (Elf64_Phdr *)bufp;
		bufp += sizeof(Elf64_Phdr);
		phdr->p_type = PT_NOTE;
		notes_addr = per_cpu_ptr_to_phys(per_cpu_ptr(crash_notes, cpu));
		phdr->p_offset = phdr->p_paddr = notes_addr;
		phdr->p_filesz = phdr->p_memsz = sizeof(note_buf_t);
		(ehdr->e_phnum)++;
	}

	/* Prepare one PT_NOTE header for vmcoreinfo */
	phdr = (Elf64_Phdr *)bufp;
	bufp += sizeof(Elf64_Phdr);
	phdr->p_type = PT_NOTE;
	phdr->p_offset = phdr->p_paddr = paddr_vmcoreinfo_note();
	phdr->p_filesz = phdr->p_memsz = sizeof(vmcoreinfo_note);
	(ehdr->e_phnum)++;

#ifdef CONFIG_X86_64
	/* Prepare PT_LOAD type program header for kernel text region */
	phdr = (Elf64_Phdr *)bufp;
	bufp += sizeof(Elf64_Phdr);
	phdr->p_type = PT_LOAD;
	phdr->p_flags = PF_R|PF_W|PF_X;
	phdr->p_vaddr = (Elf64_Addr)_text;
	phdr->p_filesz = phdr->p_memsz = _end - _text;
	phdr->p_offset = phdr->p_paddr = __pa_symbol(_text);
	(ehdr->e_phnum)++;
#endif

	/* Prepare PT_LOAD headers for system ram chunks. */
	ced->ehdr = ehdr;
	ced->bufp = bufp;
	ret = walk_system_ram_res(0, -1, ced,
			prepare_elf64_ram_headers_callback);
	if (ret < 0)
		return ret;

	*addr = buf;
	*sz = elf_sz;
	return 0;
}

/* Prepare elf headers. Return addr and size */
static int prepare_elf_headers(struct kimage *image, void **addr,
					unsigned long *sz)
{
	struct crash_elf_data *ced;
	int ret;

	ced = kzalloc(sizeof(*ced), GFP_KERNEL);
	if (!ced)
		return -ENOMEM;

	fill_up_crash_elf_data(ced, image);

	/* By default prepare 64bit headers */
	ret =  prepare_elf64_headers(ced, addr, sz);
	kfree(ced);
	return ret;
}

static int add_e820_entry(struct boot_params *params, struct e820entry *entry)
{
	unsigned int nr_e820_entries;

	nr_e820_entries = params->e820_entries;
	if (nr_e820_entries >= E820MAX)
		return 1;

	memcpy(&params->e820_map[nr_e820_entries], entry,
			sizeof(struct e820entry));
	params->e820_entries++;
	return 0;
}

static int memmap_entry_callback(u64 start, u64 end, void *arg)
{
	struct crash_memmap_data *cmd = arg;
	struct boot_params *params = cmd->params;
	struct e820entry ei;

	ei.addr = start;
	ei.size = end - start + 1;
	ei.type = cmd->type;
	add_e820_entry(params, &ei);

	return 0;
}

static int memmap_exclude_ranges(struct kimage *image, struct crash_mem *cmem,
				 unsigned long long mstart,
				 unsigned long long mend)
{
	unsigned long start, end;
	int ret = 0;

	cmem->ranges[0].start = mstart;
	cmem->ranges[0].end = mend;
	cmem->nr_ranges = 1;

	/* Exclude Backup region */
	start = image->arch.backup_load_addr;
	end = start + image->arch.backup_src_sz - 1;
	ret = exclude_mem_range(cmem, start, end);
	if (ret)
		return ret;

	/* Exclude elf header region */
	start = image->arch.elf_load_addr;
	end = start + image->arch.elf_headers_sz - 1;
	return exclude_mem_range(cmem, start, end);
}

/* Prepare memory map for crash dump kernel */
int crash_setup_memmap_entries(struct kimage *image, struct boot_params *params)
{
	int i, ret = 0;
	unsigned long flags;
	struct e820entry ei;
	struct crash_memmap_data cmd;
	struct crash_mem *cmem;

	cmem = vzalloc(sizeof(struct crash_mem));
	if (!cmem)
		return -ENOMEM;

	memset(&cmd, 0, sizeof(struct crash_memmap_data));
	cmd.params = params;

	/* Add first 640K segment */
	ei.addr = image->arch.backup_src_start;
	ei.size = image->arch.backup_src_sz;
	ei.type = E820_RAM;
	add_e820_entry(params, &ei);

	/* Add ACPI tables */
	cmd.type = E820_ACPI;
	flags = IORESOURCE_MEM | IORESOURCE_BUSY;
	walk_iomem_res("ACPI Tables", flags, 0, -1, &cmd,
		       memmap_entry_callback);

	/* Add ACPI Non-volatile Storage */
	cmd.type = E820_NVS;
	walk_iomem_res("ACPI Non-volatile Storage", flags, 0, -1, &cmd,
			memmap_entry_callback);

	/* Add crashk_low_res region */
	if (crashk_low_res.end) {
		ei.addr = crashk_low_res.start;
		ei.size = crashk_low_res.end - crashk_low_res.start + 1;
		ei.type = E820_RAM;
		add_e820_entry(params, &ei);
	}

	/* Exclude some ranges from crashk_res and add rest to memmap */
	ret = memmap_exclude_ranges(image, cmem, crashk_res.start,
						crashk_res.end);
	if (ret)
		goto out;

	for (i = 0; i < cmem->nr_ranges; i++) {
		ei.size = cmem->ranges[i].end - cmem->ranges[i].start + 1;

		/* If entry is less than a page, skip it */
		if (ei.size < PAGE_SIZE)
			continue;
		ei.addr = cmem->ranges[i].start;
		ei.type = E820_RAM;
		add_e820_entry(params, &ei);
	}

out:
	vfree(cmem);
	return ret;
}

static int determine_backup_region(u64 start, u64 end, void *arg)
{
	struct kimage *image = arg;

	image->arch.backup_src_start = start;
	image->arch.backup_src_sz = end - start + 1;

	/* Expecting only one range for backup region */
	return 1;
}

int crash_load_segments(struct kimage *image)
{
	unsigned long src_start, src_sz, elf_sz;
	void *elf_addr;
	int ret;

	/*
	 * Determine and load a segment for backup area. First 640K RAM
	 * region is backup source
	 */

	ret = walk_system_ram_res(KEXEC_BACKUP_SRC_START, KEXEC_BACKUP_SRC_END,
				image, determine_backup_region);

	/* Zero or postive return values are ok */
	if (ret < 0)
		return ret;

	src_start = image->arch.backup_src_start;
	src_sz = image->arch.backup_src_sz;

	/* Add backup segment. */
	if (src_sz) {
		/*
		 * Ideally there is no source for backup segment. This is
		 * copied in purgatory after crash. Just add a zero filled
		 * segment for now to make sure checksum logic works fine.
		 */
		ret = kexec_add_buffer(image, (char *)&crash_zero_bytes,
				       sizeof(crash_zero_bytes), src_sz,
				       PAGE_SIZE, 0, -1, 0,
				       &image->arch.backup_load_addr);
		if (ret)
			return ret;
		pr_debug("Loaded backup region at 0x%lx backup_start=0x%lx memsz=0x%lx\n",
			 image->arch.backup_load_addr, src_start, src_sz);
	}

	/* Prepare elf headers and add a segment */
	ret = prepare_elf_headers(image, &elf_addr, &elf_sz);
	if (ret)
		return ret;

	image->arch.elf_headers = elf_addr;
	image->arch.elf_headers_sz = elf_sz;

	ret = kexec_add_buffer(image, (char *)elf_addr, elf_sz, elf_sz,
			ELF_CORE_HEADER_ALIGN, 0, -1, 0,
			&image->arch.elf_load_addr);
	if (ret) {
		vfree((void *)image->arch.elf_headers);
		return ret;
	}
	pr_debug("Loaded ELF headers at 0x%lx bufsz=0x%lx memsz=0x%lx\n",
		 image->arch.elf_load_addr, elf_sz, elf_sz);

	return ret;
}
#endif /* CONFIG_KEXEC_FILE */
