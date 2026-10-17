// SPDX-License-Identifier: GPL-2.0
/*
 * Decompression and self-relocation for the compressed kernel boot stub,
 * based on arch/parisc/boot/compressed/misc.c.
 *
 * aboot loads the stub as an ordinary ELF at START_ADDR and jumps to it
 * with the parameter page and stack already set up; vmlinux.lds.S
 * explains why the stub has to be linked there. We decompress above our
 * own bss and never write below START_ADDR, so what aboot prepared stays
 * valid for the real kernel.
 */

#include <linux/elf.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#include <asm/hwrpb.h>
#include <asm/page.h>
#include <asm/pal.h>
#include <asm/setup.h>

#define STATIC static
#define memzero(s, n) memset((s), 0, (n))

extern char input_data[];
extern int input_len;
/* output_len is inserted by the linker, possibly at an unaligned address */
extern char output_len;

extern char _ebss[];
extern char trampoline_start[], trampoline_end[];

static unsigned long free_mem_ptr;
static unsigned long free_mem_end_ptr;
static unsigned long mem_end;

/* The stub has no console, so the message is only for debuggers. */
void __noreturn error(char *m)
{
	while (1)
		__halt();
}

#ifdef CONFIG_KERNEL_GZIP
#include "../../../../lib/decompress_inflate.c"
#endif

/* Scratch heap for the decompressors, larger than any of them needs. */
#define STUB_HEAP_SIZE	(4 * 1024 * 1024)

/*
 * Type punning through a packed struct, to avoid the warnings the regular
 * get_unaligned_le32() produces when reading a char-typed symbol as an
 * integer.
 */
static u32 punned_get_unaligned_le32(const void *p)
{
	const struct { __le32 x; } __packed *pp = p;

	return le32_to_cpu(pp->x);
}

/*
 * End of the usable memory cluster that holds the kernel, or 0 if the
 * HWRPB does not describe one. Used to check that the decompressed image
 * and the stub's scratch space fit before anything is written.
 */
static unsigned long usable_memory_end(void)
{
	struct hwrpb_struct *hwrpb;
	struct memdesc_struct *memdesc;
	unsigned long pfn = (START_ADDR - PAGE_OFFSET) >> PAGE_SHIFT;
	unsigned long i;

	hwrpb = (struct hwrpb_struct *)(PAGE_OFFSET + INIT_HWRPB->phys_addr);
	memdesc = (struct memdesc_struct *)((unsigned long)hwrpb +
					    hwrpb->mddt_offset);

	for (i = 0; i < memdesc->numclusters; i++) {
		struct memclust_struct *cluster = &memdesc->cluster[i];
		unsigned long end_pfn = cluster->start_pfn + cluster->numpages;

		if (cluster->usage)
			continue;
		if (pfn >= cluster->start_pfn && pfn < end_pfn)
			return PAGE_OFFSET + (end_pfn << PAGE_SHIFT);
	}

	return 0;
}

/*
 * Move the decompressed kernel's single PT_LOAD segment down onto
 * START_ADDR and jump to its entry point. The kernel is always linked
 * with exactly one PT_LOAD whose p_vaddr equals its e_entry equals
 * START_ADDR (see arch/alpha/kernel/vmlinux.lds.S); anything else we do
 * not know how to boot.
 *
 * The copy cannot run from this stub, which is itself executing at
 * START_ADDR; see trampoline.S.
 */
static void __noreturn relocate_and_jump(void *image)
{
	void (*trampoline)(unsigned long dest, unsigned long src,
			   unsigned long filesz, unsigned long memsz,
			   unsigned long entry);
	unsigned long tramp_len = trampoline_end - trampoline_start;
	Elf64_Phdr phdr, load_phdr = {};
	Elf64_Ehdr ehdr;
	int i, found_load = 0;
	unsigned long tramp;

	memcpy(&ehdr, image, sizeof(ehdr));

	if (ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
	    ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
	    ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
	    ehdr.e_ident[EI_MAG3] != ELFMAG3 ||
	    ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
	    ehdr.e_ident[EI_DATA] != ELFDATA2LSB ||
	    ehdr.e_machine != EM_ALPHA ||
	    ehdr.e_type != ET_EXEC ||
	    ehdr.e_entry != START_ADDR)
		error("Not a bootable Alpha kernel image");

	for (i = 0; i < ehdr.e_phnum; i++) {
		memcpy(&phdr, image + ehdr.e_phoff + i * ehdr.e_phentsize,
		       sizeof(phdr));

		if (phdr.p_type != PT_LOAD)
			continue;
		if (found_load || phdr.p_vaddr != START_ADDR)
			error("Unexpected kernel segment layout");
		found_load = 1;
		load_phdr = phdr;
	}
	if (!found_load)
		error("Kernel image has no PT_LOAD segment");

	/*
	 * The trampoline has to outlive both the scratch heap it is copied
	 * into and the region it is about to write, which extends past the
	 * kernel's file image to the end of its bss.
	 */
	tramp = ALIGN(max(free_mem_end_ptr, START_ADDR + load_phdr.p_memsz), 8);
	if (mem_end && tramp + tramp_len > mem_end)
		error("Not enough memory to relocate the kernel");

	memcpy((void *)tramp, trampoline_start, tramp_len);
	imb();

	trampoline = (void *)tramp;
	trampoline(load_phdr.p_vaddr,
		   (unsigned long)image + load_phdr.p_offset,
		   load_phdr.p_filesz, load_phdr.p_memsz, ehdr.e_entry);
	unreachable();
}

void __noreturn decompress_and_boot(void)
{
	unsigned long image_len = punned_get_unaligned_le32(&output_len);
	void *image = (void *)ALIGN((unsigned long)_ebss, PAGE_SIZE);

	free_mem_ptr = ALIGN((unsigned long)image + image_len, PAGE_SIZE);
	free_mem_end_ptr = free_mem_ptr + STUB_HEAP_SIZE;

	mem_end = usable_memory_end();
	if (mem_end && free_mem_end_ptr > mem_end)
		error("Not enough memory to decompress the kernel");

	/* Some decompressors report failure only through the return value. */
	if (__decompress(input_data, input_len, NULL, NULL, image, image_len,
			 NULL, error))
		error("Kernel decompression failed");

	relocate_and_jump(image);
}
