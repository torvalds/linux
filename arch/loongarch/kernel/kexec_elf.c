// SPDX-License-Identifier: GPL-2.0-only
/*
 * Load ELF vmlinux file for the kexec_file_load syscall.
 *
 * Author: Youling Tang <tangyouling@kylinos.cn>
 * Copyright (C) 2025 KylinSoft Corporation.
 */

#define pr_fmt(fmt)	"kexec_file(ELF): " fmt

#include <linux/elf.h>
#include <linux/kexec.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/memblock.h>
#include <asm/setup.h>

#define elf_kexec_probe kexec_elf_probe

static int _elf_kexec_load(struct kimage *image,
			   struct elfhdr *ehdr, struct kexec_elf_info *elf_info,
			   struct kexec_buf *kbuf, unsigned long *text_offset)
{
	int i, ret = -1;

	/* Read in the PT_LOAD segments. */
	for (i = 0; i < ehdr->e_phnum; i++) {
		size_t size;
		const struct elf_phdr *phdr;

		phdr = &elf_info->proghdrs[i];
		if (phdr->p_type != PT_LOAD)
			continue;

		size = phdr->p_filesz;
		if (size > phdr->p_memsz)
			size = phdr->p_memsz;

		kbuf->buffer = (void *)elf_info->buffer + phdr->p_offset;
		kbuf->bufsz = size;
		kbuf->buf_align = phdr->p_align;
		*text_offset = __pa(phdr->p_paddr);
		kbuf->buf_min = *text_offset;
		kbuf->memsz = ALIGN(phdr->p_memsz, SZ_64K);
		kbuf->mem = KEXEC_BUF_MEM_UNKNOWN;
		ret = kexec_add_buffer(kbuf);
		if (ret < 0)
			break;
	}

	return ret;
}

static void *elf_kexec_load(struct kimage *image,
			    char *kernel, unsigned long kernel_len,
			    char *initrd, unsigned long initrd_len,
			    char *cmdline, unsigned long cmdline_len)
{
	int ret;
	unsigned long text_offset, kernel_segment_number;
	struct elfhdr ehdr;
	struct kexec_buf kbuf = {};
	struct kexec_elf_info elf_info;
	struct kexec_segment *kernel_segment;

	ret = kexec_build_elf_info(kernel, kernel_len, &ehdr, &elf_info);
	if (ret < 0)
		return ERR_PTR(ret);

	/*
	 * Load the kernel
	 * FIXME: Non-relocatable kernel rejected for kexec_file (require CONFIG_RELOCATABLE)
	 */
	kbuf.image = image;
	kbuf.buf_max = ULONG_MAX;
	kbuf.top_down = false;

	kernel_segment_number = image->nr_segments;

	ret = _elf_kexec_load(image, &ehdr, &elf_info, &kbuf, &text_offset);
	if (ret < 0)
		goto out;

	/* Load additional data */
	kernel_segment = &image->segment[kernel_segment_number];
	ret = load_other_segments(image, kernel_segment->mem, kernel_segment->memsz,
				  initrd, initrd_len, cmdline, cmdline_len);
	if (ret < 0)
		goto out;

	/* Make sure the second kernel jumps to the correct "kernel_entry". */
	image->start = kernel_segment->mem + __pa(ehdr.e_entry) - text_offset;

	kexec_dprintk("Loaded kernel at 0x%lx bufsz=0x%lx memsz=0x%lx\n",
		      kernel_segment->mem, kbuf.bufsz, kernel_segment->memsz);

out:
	kexec_free_elf_info(&elf_info);
	return ret ? ERR_PTR(ret) : NULL;
}

const struct kexec_file_ops kexec_elf_ops = {
	.probe = elf_kexec_probe,
	.load  = elf_kexec_load,
};
