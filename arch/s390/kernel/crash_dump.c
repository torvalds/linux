// SPDX-License-Identifier: GPL-2.0
/*
 * S390 kdump implementation
 *
 * Copyright IBM Corp. 2011
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include <linux/crash_dump.h>
#include <asm/lowcore.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/memblock.h>
#include <linux/elf.h>
#include <asm/asm-offsets.h>
#include <asm/os_info.h>
#include <asm/elf.h>
#include <asm/ipl.h>
#include <asm/sclp.h>

#define PTR_ADD(x, y) (((char *) (x)) + ((unsigned long) (y)))
#define PTR_SUB(x, y) (((char *) (x)) - ((unsigned long) (y)))
#define PTR_DIFF(x, y) ((unsigned long)(((char *) (x)) - ((unsigned long) (y))))

static struct memblock_region oldmem_region;

static struct memblock_type oldmem_type = {
	.cnt = 1,
	.max = 1,
	.total_size = 0,
	.regions = &oldmem_region,
	.name = "oldmem",
};

struct save_area {
	struct list_head list;
	u64 psw[2];
	u64 ctrs[16];
	u64 gprs[16];
	u32 acrs[16];
	u64 fprs[16];
	u32 fpc;
	u32 prefix;
	u64 todpreg;
	u64 timer;
	u64 todcmp;
	u64 vxrs_low[16];
	__vector128 vxrs_high[16];
};

static LIST_HEAD(dump_save_areas);

/*
 * Allocate a save area
 */
struct save_area * __init save_area_alloc(bool is_boot_cpu)
{
	struct save_area *sa;

	sa = memblock_alloc(sizeof(*sa), 8);
	if (!sa)
		panic("Failed to allocate save area\n");

	if (is_boot_cpu)
		list_add(&sa->list, &dump_save_areas);
	else
		list_add_tail(&sa->list, &dump_save_areas);
	return sa;
}

/*
 * Return the address of the save area for the boot CPU
 */
struct save_area * __init save_area_boot_cpu(void)
{
	return list_first_entry_or_null(&dump_save_areas, struct save_area, list);
}

/*
 * Copy CPU registers into the save area
 */
void __init save_area_add_regs(struct save_area *sa, void *regs)
{
	struct lowcore *lc;

	lc = (struct lowcore *)(regs - __LC_FPREGS_SAVE_AREA);
	memcpy(&sa->psw, &lc->psw_save_area, sizeof(sa->psw));
	memcpy(&sa->ctrs, &lc->cregs_save_area, sizeof(sa->ctrs));
	memcpy(&sa->gprs, &lc->gpregs_save_area, sizeof(sa->gprs));
	memcpy(&sa->acrs, &lc->access_regs_save_area, sizeof(sa->acrs));
	memcpy(&sa->fprs, &lc->floating_pt_save_area, sizeof(sa->fprs));
	memcpy(&sa->fpc, &lc->fpt_creg_save_area, sizeof(sa->fpc));
	memcpy(&sa->prefix, &lc->prefixreg_save_area, sizeof(sa->prefix));
	memcpy(&sa->todpreg, &lc->tod_progreg_save_area, sizeof(sa->todpreg));
	memcpy(&sa->timer, &lc->cpu_timer_save_area, sizeof(sa->timer));
	memcpy(&sa->todcmp, &lc->clock_comp_save_area, sizeof(sa->todcmp));
}

/*
 * Copy vector registers into the save area
 */
void __init save_area_add_vxrs(struct save_area *sa, __vector128 *vxrs)
{
	int i;

	/* Copy lower halves of vector registers 0-15 */
	for (i = 0; i < 16; i++)
		memcpy(&sa->vxrs_low[i], &vxrs[i].u[2], 8);
	/* Copy vector registers 16-31 */
	memcpy(sa->vxrs_high, vxrs + 16, 16 * sizeof(__vector128));
}

/*
 * Return physical address for virtual address
 */
static inline void *load_real_addr(void *addr)
{
	unsigned long real_addr;

	asm volatile(
		   "	lra     %0,0(%1)\n"
		   "	jz	0f\n"
		   "	la	%0,0\n"
		   "0:"
		   : "=a" (real_addr) : "a" (addr) : "cc");
	return (void *)real_addr;
}

/*
 * Copy memory of the old, dumped system to a kernel space virtual address
 */
int copy_oldmem_kernel(void *dst, void *src, size_t count)
{
	unsigned long from, len;
	void *ra;
	int rc;

	while (count) {
		from = __pa(src);
		if (!oldmem_data.start && from < sclp.hsa_size) {
			/* Copy from zfcp/nvme dump HSA area */
			len = min(count, sclp.hsa_size - from);
			rc = memcpy_hsa_kernel(dst, from, len);
			if (rc)
				return rc;
		} else {
			/* Check for swapped kdump oldmem areas */
			if (oldmem_data.start && from - oldmem_data.start < oldmem_data.size) {
				from -= oldmem_data.start;
				len = min(count, oldmem_data.size - from);
			} else if (oldmem_data.start && from < oldmem_data.size) {
				len = min(count, oldmem_data.size - from);
				from += oldmem_data.start;
			} else {
				len = count;
			}
			if (is_vmalloc_or_module_addr(dst)) {
				ra = load_real_addr(dst);
				len = min(PAGE_SIZE - offset_in_page(ra), len);
			} else {
				ra = dst;
			}
			if (memcpy_real(ra, (void *) from, len))
				return -EFAULT;
		}
		dst += len;
		src += len;
		count -= len;
	}
	return 0;
}

/*
 * Copy memory of the old, dumped system to a user space virtual address
 */
static int copy_oldmem_user(void __user *dst, void *src, size_t count)
{
	unsigned long from, len;
	int rc;

	while (count) {
		from = __pa(src);
		if (!oldmem_data.start && from < sclp.hsa_size) {
			/* Copy from zfcp/nvme dump HSA area */
			len = min(count, sclp.hsa_size - from);
			rc = memcpy_hsa_user(dst, from, len);
			if (rc)
				return rc;
		} else {
			/* Check for swapped kdump oldmem areas */
			if (oldmem_data.start && from - oldmem_data.start < oldmem_data.size) {
				from -= oldmem_data.start;
				len = min(count, oldmem_data.size - from);
			} else if (oldmem_data.start && from < oldmem_data.size) {
				len = min(count, oldmem_data.size - from);
				from += oldmem_data.start;
			} else {
				len = count;
			}
			rc = copy_to_user_real(dst, (void *) from, count);
			if (rc)
				return rc;
		}
		dst += len;
		src += len;
		count -= len;
	}
	return 0;
}

/*
 * Copy one page from "oldmem"
 */
ssize_t copy_oldmem_page(unsigned long pfn, char *buf, size_t csize,
			 unsigned long offset, int userbuf)
{
	void *src;
	int rc;

	if (!csize)
		return 0;
	src = (void *) (pfn << PAGE_SHIFT) + offset;
	if (userbuf)
		rc = copy_oldmem_user((void __force __user *) buf, src, csize);
	else
		rc = copy_oldmem_kernel((void *) buf, src, csize);
	return rc;
}

/*
 * Remap "oldmem" for kdump
 *
 * For the kdump reserved memory this functions performs a swap operation:
 * [0 - OLDMEM_SIZE] is mapped to [OLDMEM_BASE - OLDMEM_BASE + OLDMEM_SIZE]
 */
static int remap_oldmem_pfn_range_kdump(struct vm_area_struct *vma,
					unsigned long from, unsigned long pfn,
					unsigned long size, pgprot_t prot)
{
	unsigned long size_old;
	int rc;

	if (pfn < oldmem_data.size >> PAGE_SHIFT) {
		size_old = min(size, oldmem_data.size - (pfn << PAGE_SHIFT));
		rc = remap_pfn_range(vma, from,
				     pfn + (oldmem_data.start >> PAGE_SHIFT),
				     size_old, prot);
		if (rc || size == size_old)
			return rc;
		size -= size_old;
		from += size_old;
		pfn += size_old >> PAGE_SHIFT;
	}
	return remap_pfn_range(vma, from, pfn, size, prot);
}

/*
 * Remap "oldmem" for zfcp/nvme dump
 *
 * We only map available memory above HSA size. Memory below HSA size
 * is read on demand using the copy_oldmem_page() function.
 */
static int remap_oldmem_pfn_range_zfcpdump(struct vm_area_struct *vma,
					   unsigned long from,
					   unsigned long pfn,
					   unsigned long size, pgprot_t prot)
{
	unsigned long hsa_end = sclp.hsa_size;
	unsigned long size_hsa;

	if (pfn < hsa_end >> PAGE_SHIFT) {
		size_hsa = min(size, hsa_end - (pfn << PAGE_SHIFT));
		if (size == size_hsa)
			return 0;
		size -= size_hsa;
		from += size_hsa;
		pfn += size_hsa >> PAGE_SHIFT;
	}
	return remap_pfn_range(vma, from, pfn, size, prot);
}

/*
 * Remap "oldmem" for kdump or zfcp/nvme dump
 */
int remap_oldmem_pfn_range(struct vm_area_struct *vma, unsigned long from,
			   unsigned long pfn, unsigned long size, pgprot_t prot)
{
	if (oldmem_data.start)
		return remap_oldmem_pfn_range_kdump(vma, from, pfn, size, prot);
	else
		return remap_oldmem_pfn_range_zfcpdump(vma, from, pfn, size,
						       prot);
}

static const char *nt_name(Elf64_Word type)
{
	const char *name = "LINUX";

	if (type == NT_PRPSINFO || type == NT_PRSTATUS || type == NT_PRFPREG)
		name = KEXEC_CORE_NOTE_NAME;
	return name;
}

/*
 * Initialize ELF note
 */
static void *nt_init_name(void *buf, Elf64_Word type, void *desc, int d_len,
			  const char *name)
{
	Elf64_Nhdr *note;
	u64 len;

	note = (Elf64_Nhdr *)buf;
	note->n_namesz = strlen(name) + 1;
	note->n_descsz = d_len;
	note->n_type = type;
	len = sizeof(Elf64_Nhdr);

	memcpy(buf + len, name, note->n_namesz);
	len = roundup(len + note->n_namesz, 4);

	memcpy(buf + len, desc, note->n_descsz);
	len = roundup(len + note->n_descsz, 4);

	return PTR_ADD(buf, len);
}

static inline void *nt_init(void *buf, Elf64_Word type, void *desc, int d_len)
{
	return nt_init_name(buf, type, desc, d_len, nt_name(type));
}

/*
 * Calculate the size of ELF note
 */
static size_t nt_size_name(int d_len, const char *name)
{
	size_t size;

	size = sizeof(Elf64_Nhdr);
	size += roundup(strlen(name) + 1, 4);
	size += roundup(d_len, 4);

	return size;
}

static inline size_t nt_size(Elf64_Word type, int d_len)
{
	return nt_size_name(d_len, nt_name(type));
}

/*
 * Fill ELF notes for one CPU with save area registers
 */
static void *fill_cpu_elf_notes(void *ptr, int cpu, struct save_area *sa)
{
	struct elf_prstatus nt_prstatus;
	elf_fpregset_t nt_fpregset;

	/* Prepare prstatus note */
	memset(&nt_prstatus, 0, sizeof(nt_prstatus));
	memcpy(&nt_prstatus.pr_reg.gprs, sa->gprs, sizeof(sa->gprs));
	memcpy(&nt_prstatus.pr_reg.psw, sa->psw, sizeof(sa->psw));
	memcpy(&nt_prstatus.pr_reg.acrs, sa->acrs, sizeof(sa->acrs));
	nt_prstatus.common.pr_pid = cpu;
	/* Prepare fpregset (floating point) note */
	memset(&nt_fpregset, 0, sizeof(nt_fpregset));
	memcpy(&nt_fpregset.fpc, &sa->fpc, sizeof(sa->fpc));
	memcpy(&nt_fpregset.fprs, &sa->fprs, sizeof(sa->fprs));
	/* Create ELF notes for the CPU */
	ptr = nt_init(ptr, NT_PRSTATUS, &nt_prstatus, sizeof(nt_prstatus));
	ptr = nt_init(ptr, NT_PRFPREG, &nt_fpregset, sizeof(nt_fpregset));
	ptr = nt_init(ptr, NT_S390_TIMER, &sa->timer, sizeof(sa->timer));
	ptr = nt_init(ptr, NT_S390_TODCMP, &sa->todcmp, sizeof(sa->todcmp));
	ptr = nt_init(ptr, NT_S390_TODPREG, &sa->todpreg, sizeof(sa->todpreg));
	ptr = nt_init(ptr, NT_S390_CTRS, &sa->ctrs, sizeof(sa->ctrs));
	ptr = nt_init(ptr, NT_S390_PREFIX, &sa->prefix, sizeof(sa->prefix));
	if (MACHINE_HAS_VX) {
		ptr = nt_init(ptr, NT_S390_VXRS_HIGH,
			      &sa->vxrs_high, sizeof(sa->vxrs_high));
		ptr = nt_init(ptr, NT_S390_VXRS_LOW,
			      &sa->vxrs_low, sizeof(sa->vxrs_low));
	}
	return ptr;
}

/*
 * Calculate size of ELF notes per cpu
 */
static size_t get_cpu_elf_notes_size(void)
{
	struct save_area *sa = NULL;
	size_t size;

	size =	nt_size(NT_PRSTATUS, sizeof(struct elf_prstatus));
	size +=  nt_size(NT_PRFPREG, sizeof(elf_fpregset_t));
	size +=  nt_size(NT_S390_TIMER, sizeof(sa->timer));
	size +=  nt_size(NT_S390_TODCMP, sizeof(sa->todcmp));
	size +=  nt_size(NT_S390_TODPREG, sizeof(sa->todpreg));
	size +=  nt_size(NT_S390_CTRS, sizeof(sa->ctrs));
	size +=  nt_size(NT_S390_PREFIX, sizeof(sa->prefix));
	if (MACHINE_HAS_VX) {
		size += nt_size(NT_S390_VXRS_HIGH, sizeof(sa->vxrs_high));
		size += nt_size(NT_S390_VXRS_LOW, sizeof(sa->vxrs_low));
	}

	return size;
}

/*
 * Initialize prpsinfo note (new kernel)
 */
static void *nt_prpsinfo(void *ptr)
{
	struct elf_prpsinfo prpsinfo;

	memset(&prpsinfo, 0, sizeof(prpsinfo));
	prpsinfo.pr_sname = 'R';
	strcpy(prpsinfo.pr_fname, "vmlinux");
	return nt_init(ptr, NT_PRPSINFO, &prpsinfo, sizeof(prpsinfo));
}

/*
 * Get vmcoreinfo using lowcore->vmcore_info (new kernel)
 */
static void *get_vmcoreinfo_old(unsigned long *size)
{
	char nt_name[11], *vmcoreinfo;
	Elf64_Nhdr note;
	void *addr;

	if (copy_oldmem_kernel(&addr, &S390_lowcore.vmcore_info, sizeof(addr)))
		return NULL;
	memset(nt_name, 0, sizeof(nt_name));
	if (copy_oldmem_kernel(&note, addr, sizeof(note)))
		return NULL;
	if (copy_oldmem_kernel(nt_name, addr + sizeof(note),
			       sizeof(nt_name) - 1))
		return NULL;
	if (strcmp(nt_name, VMCOREINFO_NOTE_NAME) != 0)
		return NULL;
	vmcoreinfo = kzalloc(note.n_descsz, GFP_KERNEL);
	if (!vmcoreinfo)
		return NULL;
	if (copy_oldmem_kernel(vmcoreinfo, addr + 24, note.n_descsz)) {
		kfree(vmcoreinfo);
		return NULL;
	}
	*size = note.n_descsz;
	return vmcoreinfo;
}

/*
 * Initialize vmcoreinfo note (new kernel)
 */
static void *nt_vmcoreinfo(void *ptr)
{
	const char *name = VMCOREINFO_NOTE_NAME;
	unsigned long size;
	void *vmcoreinfo;

	vmcoreinfo = os_info_old_entry(OS_INFO_VMCOREINFO, &size);
	if (vmcoreinfo)
		return nt_init_name(ptr, 0, vmcoreinfo, size, name);

	vmcoreinfo = get_vmcoreinfo_old(&size);
	if (!vmcoreinfo)
		return ptr;
	ptr = nt_init_name(ptr, 0, vmcoreinfo, size, name);
	kfree(vmcoreinfo);
	return ptr;
}

static size_t nt_vmcoreinfo_size(void)
{
	const char *name = VMCOREINFO_NOTE_NAME;
	unsigned long size;
	void *vmcoreinfo;

	vmcoreinfo = os_info_old_entry(OS_INFO_VMCOREINFO, &size);
	if (vmcoreinfo)
		return nt_size_name(size, name);

	vmcoreinfo = get_vmcoreinfo_old(&size);
	if (!vmcoreinfo)
		return 0;

	kfree(vmcoreinfo);
	return nt_size_name(size, name);
}

/*
 * Initialize final note (needed for /proc/vmcore code)
 */
static void *nt_final(void *ptr)
{
	Elf64_Nhdr *note;

	note = (Elf64_Nhdr *) ptr;
	note->n_namesz = 0;
	note->n_descsz = 0;
	note->n_type = 0;
	return PTR_ADD(ptr, sizeof(Elf64_Nhdr));
}

/*
 * Initialize ELF header (new kernel)
 */
static void *ehdr_init(Elf64_Ehdr *ehdr, int mem_chunk_cnt)
{
	memset(ehdr, 0, sizeof(*ehdr));
	memcpy(ehdr->e_ident, ELFMAG, SELFMAG);
	ehdr->e_ident[EI_CLASS] = ELFCLASS64;
	ehdr->e_ident[EI_DATA] = ELFDATA2MSB;
	ehdr->e_ident[EI_VERSION] = EV_CURRENT;
	memset(ehdr->e_ident + EI_PAD, 0, EI_NIDENT - EI_PAD);
	ehdr->e_type = ET_CORE;
	ehdr->e_machine = EM_S390;
	ehdr->e_version = EV_CURRENT;
	ehdr->e_phoff = sizeof(Elf64_Ehdr);
	ehdr->e_ehsize = sizeof(Elf64_Ehdr);
	ehdr->e_phentsize = sizeof(Elf64_Phdr);
	ehdr->e_phnum = mem_chunk_cnt + 1;
	return ehdr + 1;
}

/*
 * Return CPU count for ELF header (new kernel)
 */
static int get_cpu_cnt(void)
{
	struct save_area *sa;
	int cpus = 0;

	list_for_each_entry(sa, &dump_save_areas, list)
		if (sa->prefix != 0)
			cpus++;
	return cpus;
}

/*
 * Return memory chunk count for ELF header (new kernel)
 */
static int get_mem_chunk_cnt(void)
{
	int cnt = 0;
	u64 idx;

	for_each_physmem_range(idx, &oldmem_type, NULL, NULL)
		cnt++;
	return cnt;
}

/*
 * Initialize ELF loads (new kernel)
 */
static void loads_init(Elf64_Phdr *phdr, u64 loads_offset)
{
	phys_addr_t start, end;
	u64 idx;

	for_each_physmem_range(idx, &oldmem_type, &start, &end) {
		phdr->p_filesz = end - start;
		phdr->p_type = PT_LOAD;
		phdr->p_offset = start;
		phdr->p_vaddr = start;
		phdr->p_paddr = start;
		phdr->p_memsz = end - start;
		phdr->p_flags = PF_R | PF_W | PF_X;
		phdr->p_align = PAGE_SIZE;
		phdr++;
	}
}

/*
 * Initialize notes (new kernel)
 */
static void *notes_init(Elf64_Phdr *phdr, void *ptr, u64 notes_offset)
{
	struct save_area *sa;
	void *ptr_start = ptr;
	int cpu;

	ptr = nt_prpsinfo(ptr);

	cpu = 1;
	list_for_each_entry(sa, &dump_save_areas, list)
		if (sa->prefix != 0)
			ptr = fill_cpu_elf_notes(ptr, cpu++, sa);
	ptr = nt_vmcoreinfo(ptr);
	ptr = nt_final(ptr);
	memset(phdr, 0, sizeof(*phdr));
	phdr->p_type = PT_NOTE;
	phdr->p_offset = notes_offset;
	phdr->p_filesz = (unsigned long) PTR_SUB(ptr, ptr_start);
	phdr->p_memsz = phdr->p_filesz;
	return ptr;
}

static size_t get_elfcorehdr_size(int mem_chunk_cnt)
{
	size_t size;

	size = sizeof(Elf64_Ehdr);
	/* PT_NOTES */
	size += sizeof(Elf64_Phdr);
	/* nt_prpsinfo */
	size += nt_size(NT_PRPSINFO, sizeof(struct elf_prpsinfo));
	/* regsets */
	size += get_cpu_cnt() * get_cpu_elf_notes_size();
	/* nt_vmcoreinfo */
	size += nt_vmcoreinfo_size();
	/* nt_final */
	size += sizeof(Elf64_Nhdr);
	/* PT_LOADS */
	size += mem_chunk_cnt * sizeof(Elf64_Phdr);

	return size;
}

/*
 * Create ELF core header (new kernel)
 */
int elfcorehdr_alloc(unsigned long long *addr, unsigned long long *size)
{
	Elf64_Phdr *phdr_notes, *phdr_loads;
	int mem_chunk_cnt;
	void *ptr, *hdr;
	u32 alloc_size;
	u64 hdr_off;

	/* If we are not in kdump or zfcp/nvme dump mode return */
	if (!oldmem_data.start && !is_ipl_type_dump())
		return 0;
	/* If we cannot get HSA size for zfcp/nvme dump return error */
	if (is_ipl_type_dump() && !sclp.hsa_size)
		return -ENODEV;

	/* For kdump, exclude previous crashkernel memory */
	if (oldmem_data.start) {
		oldmem_region.base = oldmem_data.start;
		oldmem_region.size = oldmem_data.size;
		oldmem_type.total_size = oldmem_data.size;
	}

	mem_chunk_cnt = get_mem_chunk_cnt();

	alloc_size = get_elfcorehdr_size(mem_chunk_cnt);

	hdr = kzalloc(alloc_size, GFP_KERNEL);

	/* Without elfcorehdr /proc/vmcore cannot be created. Thus creating
	 * a dump with this crash kernel will fail. Panic now to allow other
	 * dump mechanisms to take over.
	 */
	if (!hdr)
		panic("s390 kdump allocating elfcorehdr failed");

	/* Init elf header */
	ptr = ehdr_init(hdr, mem_chunk_cnt);
	/* Init program headers */
	phdr_notes = ptr;
	ptr = PTR_ADD(ptr, sizeof(Elf64_Phdr));
	phdr_loads = ptr;
	ptr = PTR_ADD(ptr, sizeof(Elf64_Phdr) * mem_chunk_cnt);
	/* Init notes */
	hdr_off = PTR_DIFF(ptr, hdr);
	ptr = notes_init(phdr_notes, ptr, ((unsigned long) hdr) + hdr_off);
	/* Init loads */
	hdr_off = PTR_DIFF(ptr, hdr);
	loads_init(phdr_loads, hdr_off);
	*addr = (unsigned long long) hdr;
	*size = (unsigned long long) hdr_off;
	BUG_ON(elfcorehdr_size > alloc_size);
	return 0;
}

/*
 * Free ELF core header (new kernel)
 */
void elfcorehdr_free(unsigned long long addr)
{
	kfree((void *)(unsigned long)addr);
}

/*
 * Read from ELF header
 */
ssize_t elfcorehdr_read(char *buf, size_t count, u64 *ppos)
{
	void *src = (void *)(unsigned long)*ppos;

	memcpy(buf, src, count);
	*ppos += count;
	return count;
}

/*
 * Read from ELF notes data
 */
ssize_t elfcorehdr_read_notes(char *buf, size_t count, u64 *ppos)
{
	void *src = (void *)(unsigned long)*ppos;

	memcpy(buf, src, count);
	*ppos += count;
	return count;
}
