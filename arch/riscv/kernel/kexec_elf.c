// SPDX-License-Identifier: GPL-2.0-only
/*
 * Load ELF vmlinux file for the kexec_file_load syscall.
 *
 * Copyright (C) 2021 Huawei Technologies Co, Ltd.
 *
 * Author: Liao Chang (liaochang1@huawei.com)
 *
 * Based on kexec-tools' kexec-elf-riscv.c, heavily modified
 * for kernel.
 */

#define pr_fmt(fmt)	"kexec_image: " fmt

#include <linux/elf.h>
#include <linux/kexec.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/libfdt.h>
#include <linux/types.h>
#include <linux/memblock.h>
#include <asm/setup.h>

static int riscv_kexec_elf_load(struct kimage *image, struct elfhdr *ehdr,
				struct kexec_elf_info *elf_info, unsigned long old_pbase,
				unsigned long new_pbase)
{
	int i;
	int ret = 0;
	size_t size;
	struct kexec_buf kbuf = {};
	const struct elf_phdr *phdr;

	kbuf.image = image;

	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = &elf_info->proghdrs[i];
		if (phdr->p_type != PT_LOAD)
			continue;

		size = phdr->p_filesz;
		if (size > phdr->p_memsz)
			size = phdr->p_memsz;

		kbuf.buffer = (void *) elf_info->buffer + phdr->p_offset;
		kbuf.bufsz = size;
		kbuf.buf_align = phdr->p_align;
		kbuf.mem = phdr->p_paddr - old_pbase + new_pbase;
		kbuf.memsz = phdr->p_memsz;
		kbuf.top_down = false;
		ret = kexec_add_buffer(&kbuf);
		if (ret)
			break;
	}

	return ret;
}

/*
 * Go through the available phsyical memory regions and find one that hold
 * an image of the specified size.
 */
static int elf_find_pbase(struct kimage *image, unsigned long kernel_len,
			  struct elfhdr *ehdr, struct kexec_elf_info *elf_info,
			  unsigned long *old_pbase, unsigned long *new_pbase)
{
	int i;
	int ret;
	struct kexec_buf kbuf = {};
	const struct elf_phdr *phdr;
	unsigned long lowest_paddr = ULONG_MAX;
	unsigned long lowest_vaddr = ULONG_MAX;

	for (i = 0; i < ehdr->e_phnum; i++) {
		phdr = &elf_info->proghdrs[i];
		if (phdr->p_type != PT_LOAD)
			continue;

		if (lowest_paddr > phdr->p_paddr)
			lowest_paddr = phdr->p_paddr;

		if (lowest_vaddr > phdr->p_vaddr)
			lowest_vaddr = phdr->p_vaddr;
	}

	kbuf.image = image;
	kbuf.buf_min = lowest_paddr;
	kbuf.buf_max = ULONG_MAX;

	/*
	 * Current riscv boot protocol requires 2MB alignment for
	 * RV64 and 4MB alignment for RV32
	 *
	 */
	kbuf.buf_align = PMD_SIZE;
	kbuf.mem = KEXEC_BUF_MEM_UNKNOWN;
	kbuf.memsz = ALIGN(kernel_len, PAGE_SIZE);
	kbuf.cma = NULL;
	kbuf.top_down = false;
	ret = arch_kexec_locate_mem_hole(&kbuf);
	if (!ret) {
		*old_pbase = lowest_paddr;
		*new_pbase = kbuf.mem;
		image->start = ehdr->e_entry - lowest_vaddr + kbuf.mem;
	}
	return ret;
}

static void *elf_kexec_load(struct kimage *image, char *kernel_buf,
			    unsigned long kernel_len, char *initrd,
			    unsigned long initrd_len, char *cmdline,
			    unsigned long cmdline_len)
{
	int ret;
	unsigned long old_kernel_pbase = ULONG_MAX;
	unsigned long new_kernel_pbase = 0UL;
	struct elfhdr ehdr;
	struct kexec_elf_info elf_info;

	ret = kexec_build_elf_info(kernel_buf, kernel_len, &ehdr, &elf_info);
	if (ret)
		return ERR_PTR(ret);

	ret = elf_find_pbase(image, kernel_len, &ehdr, &elf_info,
			     &old_kernel_pbase, &new_kernel_pbase);
	if (ret)
		goto out;

	/* Add the kernel binary to the image */
	ret = riscv_kexec_elf_load(image, &ehdr, &elf_info,
				   old_kernel_pbase, new_kernel_pbase);
	if (ret)
		goto out;

	ret = load_extra_segments(image, image->start, kernel_len,
				  initrd, initrd_len, cmdline, cmdline_len);
out:
	kexec_free_elf_info(&elf_info);
	return ret ? ERR_PTR(ret) : NULL;
}

const struct kexec_file_ops elf_kexec_ops = {
	.probe = kexec_elf_probe,
	.load  = elf_kexec_load,
};
