/*
 * S390 kdump implementation
 *
 * Copyright IBM Corp. 2011
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include <linux/crash_dump.h>
#include <asm/lowcore.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/crash_dump.h>
#include <linux/bootmem.h>
#include <linux/elf.h>
#include <asm/ipl.h>

#define PTR_ADD(x, y) (((char *) (x)) + ((unsigned long) (y)))
#define PTR_SUB(x, y) (((char *) (x)) - ((unsigned long) (y)))
#define PTR_DIFF(x, y) ((unsigned long)(((char *) (x)) - ((unsigned long) (y))))

/*
 * Copy one page from "oldmem"
 *
 * For the kdump reserved memory this functions performs a swap operation:
 *  - [OLDMEM_BASE - OLDMEM_BASE + OLDMEM_SIZE] is mapped to [0 - OLDMEM_SIZE].
 *  - [0 - OLDMEM_SIZE] is mapped to [OLDMEM_BASE - OLDMEM_BASE + OLDMEM_SIZE]
 */
ssize_t copy_oldmem_page(unsigned long pfn, char *buf,
			 size_t csize, unsigned long offset, int userbuf)
{
	unsigned long src;

	if (!csize)
		return 0;

	src = (pfn << PAGE_SHIFT) + offset;
	if (src < OLDMEM_SIZE)
		src += OLDMEM_BASE;
	else if (src > OLDMEM_BASE &&
		 src < OLDMEM_BASE + OLDMEM_SIZE)
		src -= OLDMEM_BASE;
	if (userbuf)
		copy_to_user_real((void __force __user *) buf, (void *) src,
				  csize);
	else
		memcpy_real(buf, (void *) src, csize);
	return csize;
}

/*
 * Copy memory from old kernel
 */
static int copy_from_oldmem(void *dest, void *src, size_t count)
{
	unsigned long copied = 0;
	int rc;

	if ((unsigned long) src < OLDMEM_SIZE) {
		copied = min(count, OLDMEM_SIZE - (unsigned long) src);
		rc = memcpy_real(dest, src + OLDMEM_BASE, copied);
		if (rc)
			return rc;
	}
	return memcpy_real(dest + copied, src + copied, count - copied);
}

/*
 * Alloc memory and panic in case of ENOMEM
 */
static void *kzalloc_panic(int len)
{
	void *rc;

	rc = kzalloc(len, GFP_KERNEL);
	if (!rc)
		panic("s390 kdump kzalloc (%d) failed", len);
	return rc;
}

/*
 * Get memory layout and create hole for oldmem
 */
static struct mem_chunk *get_memory_layout(void)
{
	struct mem_chunk *chunk_array;

	chunk_array = kzalloc_panic(MEMORY_CHUNKS * sizeof(struct mem_chunk));
	detect_memory_layout(chunk_array);
	create_mem_hole(chunk_array, OLDMEM_BASE, OLDMEM_SIZE, CHUNK_CRASHK);
	return chunk_array;
}

/*
 * Initialize ELF note
 */
static void *nt_init(void *buf, Elf64_Word type, void *desc, int d_len,
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

/*
 * Initialize prstatus note
 */
static void *nt_prstatus(void *ptr, struct save_area *sa)
{
	struct elf_prstatus nt_prstatus;
	static int cpu_nr = 1;

	memset(&nt_prstatus, 0, sizeof(nt_prstatus));
	memcpy(&nt_prstatus.pr_reg.gprs, sa->gp_regs, sizeof(sa->gp_regs));
	memcpy(&nt_prstatus.pr_reg.psw, sa->psw, sizeof(sa->psw));
	memcpy(&nt_prstatus.pr_reg.acrs, sa->acc_regs, sizeof(sa->acc_regs));
	nt_prstatus.pr_pid = cpu_nr;
	cpu_nr++;

	return nt_init(ptr, NT_PRSTATUS, &nt_prstatus, sizeof(nt_prstatus),
			 "CORE");
}

/*
 * Initialize fpregset (floating point) note
 */
static void *nt_fpregset(void *ptr, struct save_area *sa)
{
	elf_fpregset_t nt_fpregset;

	memset(&nt_fpregset, 0, sizeof(nt_fpregset));
	memcpy(&nt_fpregset.fpc, &sa->fp_ctrl_reg, sizeof(sa->fp_ctrl_reg));
	memcpy(&nt_fpregset.fprs, &sa->fp_regs, sizeof(sa->fp_regs));

	return nt_init(ptr, NT_PRFPREG, &nt_fpregset, sizeof(nt_fpregset),
		       "CORE");
}

/*
 * Initialize timer note
 */
static void *nt_s390_timer(void *ptr, struct save_area *sa)
{
	return nt_init(ptr, NT_S390_TIMER, &sa->timer, sizeof(sa->timer),
			 KEXEC_CORE_NOTE_NAME);
}

/*
 * Initialize TOD clock comparator note
 */
static void *nt_s390_tod_cmp(void *ptr, struct save_area *sa)
{
	return nt_init(ptr, NT_S390_TODCMP, &sa->clk_cmp,
		       sizeof(sa->clk_cmp), KEXEC_CORE_NOTE_NAME);
}

/*
 * Initialize TOD programmable register note
 */
static void *nt_s390_tod_preg(void *ptr, struct save_area *sa)
{
	return nt_init(ptr, NT_S390_TODPREG, &sa->tod_reg,
		       sizeof(sa->tod_reg), KEXEC_CORE_NOTE_NAME);
}

/*
 * Initialize control register note
 */
static void *nt_s390_ctrs(void *ptr, struct save_area *sa)
{
	return nt_init(ptr, NT_S390_CTRS, &sa->ctrl_regs,
		       sizeof(sa->ctrl_regs), KEXEC_CORE_NOTE_NAME);
}

/*
 * Initialize prefix register note
 */
static void *nt_s390_prefix(void *ptr, struct save_area *sa)
{
	return nt_init(ptr, NT_S390_PREFIX, &sa->pref_reg,
			 sizeof(sa->pref_reg), KEXEC_CORE_NOTE_NAME);
}

/*
 * Fill ELF notes for one CPU with save area registers
 */
void *fill_cpu_elf_notes(void *ptr, struct save_area *sa)
{
	ptr = nt_prstatus(ptr, sa);
	ptr = nt_fpregset(ptr, sa);
	ptr = nt_s390_timer(ptr, sa);
	ptr = nt_s390_tod_cmp(ptr, sa);
	ptr = nt_s390_tod_preg(ptr, sa);
	ptr = nt_s390_ctrs(ptr, sa);
	ptr = nt_s390_prefix(ptr, sa);
	return ptr;
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
	return nt_init(ptr, NT_PRPSINFO, &prpsinfo, sizeof(prpsinfo),
		       KEXEC_CORE_NOTE_NAME);
}

/*
 * Initialize vmcoreinfo note (new kernel)
 */
static void *nt_vmcoreinfo(void *ptr)
{
	char nt_name[11], *vmcoreinfo;
	Elf64_Nhdr note;
	void *addr;

	if (copy_from_oldmem(&addr, &S390_lowcore.vmcore_info, sizeof(addr)))
		return ptr;
	memset(nt_name, 0, sizeof(nt_name));
	if (copy_from_oldmem(&note, addr, sizeof(note)))
		return ptr;
	if (copy_from_oldmem(nt_name, addr + sizeof(note), sizeof(nt_name) - 1))
		return ptr;
	if (strcmp(nt_name, "VMCOREINFO") != 0)
		return ptr;
	vmcoreinfo = kzalloc_panic(note.n_descsz + 1);
	if (copy_from_oldmem(vmcoreinfo, addr + 24, note.n_descsz))
		return ptr;
	vmcoreinfo[note.n_descsz + 1] = 0;
	return nt_init(ptr, 0, vmcoreinfo, note.n_descsz, "VMCOREINFO");
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
	int i, cpus = 0;

	for (i = 0; zfcpdump_save_areas[i]; i++) {
		if (zfcpdump_save_areas[i]->pref_reg == 0)
			continue;
		cpus++;
	}
	return cpus;
}

/*
 * Return memory chunk count for ELF header (new kernel)
 */
static int get_mem_chunk_cnt(void)
{
	struct mem_chunk *chunk_array, *mem_chunk;
	int i, cnt = 0;

	chunk_array = get_memory_layout();
	for (i = 0; i < MEMORY_CHUNKS; i++) {
		mem_chunk = &chunk_array[i];
		if (chunk_array[i].type != CHUNK_READ_WRITE &&
		    chunk_array[i].type != CHUNK_READ_ONLY)
			continue;
		if (mem_chunk->size == 0)
			continue;
		cnt++;
	}
	kfree(chunk_array);
	return cnt;
}

/*
 * Relocate pointer in order to allow vmcore code access the data
 */
static inline unsigned long relocate(unsigned long addr)
{
	return OLDMEM_BASE + addr;
}

/*
 * Initialize ELF loads (new kernel)
 */
static int loads_init(Elf64_Phdr *phdr, u64 loads_offset)
{
	struct mem_chunk *chunk_array, *mem_chunk;
	int i;

	chunk_array = get_memory_layout();
	for (i = 0; i < MEMORY_CHUNKS; i++) {
		mem_chunk = &chunk_array[i];
		if (mem_chunk->size == 0)
			break;
		if (chunk_array[i].type != CHUNK_READ_WRITE &&
		    chunk_array[i].type != CHUNK_READ_ONLY)
			continue;
		else
			phdr->p_filesz = mem_chunk->size;
		phdr->p_type = PT_LOAD;
		phdr->p_offset = mem_chunk->addr;
		phdr->p_vaddr = mem_chunk->addr;
		phdr->p_paddr = mem_chunk->addr;
		phdr->p_memsz = mem_chunk->size;
		phdr->p_flags = PF_R | PF_W | PF_X;
		phdr->p_align = PAGE_SIZE;
		phdr++;
	}
	kfree(chunk_array);
	return i;
}

/*
 * Initialize notes (new kernel)
 */
static void *notes_init(Elf64_Phdr *phdr, void *ptr, u64 notes_offset)
{
	struct save_area *sa;
	void *ptr_start = ptr;
	int i;

	ptr = nt_prpsinfo(ptr);

	for (i = 0; zfcpdump_save_areas[i]; i++) {
		sa = zfcpdump_save_areas[i];
		if (sa->pref_reg == 0)
			continue;
		ptr = fill_cpu_elf_notes(ptr, sa);
	}
	ptr = nt_vmcoreinfo(ptr);
	memset(phdr, 0, sizeof(*phdr));
	phdr->p_type = PT_NOTE;
	phdr->p_offset = relocate(notes_offset);
	phdr->p_filesz = (unsigned long) PTR_SUB(ptr, ptr_start);
	phdr->p_memsz = phdr->p_filesz;
	return ptr;
}

/*
 * Create ELF core header (new kernel)
 */
static void s390_elf_corehdr_create(char **elfcorebuf, size_t *elfcorebuf_sz)
{
	Elf64_Phdr *phdr_notes, *phdr_loads;
	int mem_chunk_cnt;
	void *ptr, *hdr;
	u32 alloc_size;
	u64 hdr_off;

	mem_chunk_cnt = get_mem_chunk_cnt();

	alloc_size = 0x1000 + get_cpu_cnt() * 0x300 +
		mem_chunk_cnt * sizeof(Elf64_Phdr);
	hdr = kzalloc_panic(alloc_size);
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
	loads_init(phdr_loads, ((unsigned long) hdr) + hdr_off);
	*elfcorebuf_sz = hdr_off;
	*elfcorebuf = (void *) relocate((unsigned long) hdr);
	BUG_ON(*elfcorebuf_sz > alloc_size);
}

/*
 * Create kdump ELF core header in new kernel, if it has not been passed via
 * the "elfcorehdr" kernel parameter
 */
static int setup_kdump_elfcorehdr(void)
{
	size_t elfcorebuf_sz;
	char *elfcorebuf;

	if (!OLDMEM_BASE || is_kdump_kernel())
		return -EINVAL;
	s390_elf_corehdr_create(&elfcorebuf, &elfcorebuf_sz);
	elfcorehdr_addr = (unsigned long long) elfcorebuf;
	elfcorehdr_size = elfcorebuf_sz;
	return 0;
}

subsys_initcall(setup_kdump_elfcorehdr);
