// SPDX-License-Identifier: GPL-2.0
/*
 * Load ELF vmlinux file for the kexec_file_load syscall.
 *
 * Copyright (c) 2019 Sven Schnelle <svens@stackframe.org>
 *
 */
#include <linux/elf.h>
#include <linux/kexec.h>
#include <linux/libfdt.h>
#include <linux/module.h>
#include <linux/of_fdt.h>
#include <linux/slab.h>
#include <linux/types.h>

static void *elf_load(struct kimage *image, char *kernel_buf,
			unsigned long kernel_len, char *initrd,
			unsigned long initrd_len, char *cmdline,
			unsigned long cmdline_len)
{
	int ret, i;
	unsigned long kernel_load_addr;
	struct elfhdr ehdr;
	struct kexec_elf_info elf_info;
	struct kexec_buf kbuf = { .image = image, .buf_min = 0,
				  .buf_max = -1UL, };

	ret = kexec_build_elf_info(kernel_buf, kernel_len, &ehdr, &elf_info);
	if (ret)
		goto out;

	ret = kexec_elf_load(image, &ehdr, &elf_info, &kbuf, &kernel_load_addr);
	if (ret)
		goto out;

	image->start = __pa(elf_info.ehdr->e_entry);

	for (i = 0; i < image->nr_segments; i++)
		image->segment[i].mem = __pa(image->segment[i].mem);

	pr_debug("Loaded the kernel at 0x%lx, entry at 0x%lx\n",
		 kernel_load_addr, image->start);

	if (initrd != NULL) {
		kbuf.buffer = initrd;
		kbuf.bufsz = kbuf.memsz = initrd_len;
		kbuf.buf_align = PAGE_SIZE;
		kbuf.top_down = false;
		kbuf.mem = KEXEC_BUF_MEM_UNKNOWN;
		ret = kexec_add_buffer(&kbuf);
		if (ret)
			goto out;

		pr_debug("Loaded initrd at 0x%lx\n", kbuf.mem);
		image->arch.initrd_start = kbuf.mem;
		image->arch.initrd_end = kbuf.mem + initrd_len;
	}

	if (cmdline != NULL) {
		kbuf.buffer = cmdline;
		kbuf.bufsz = kbuf.memsz = ALIGN(cmdline_len, 8);
		kbuf.buf_align = PAGE_SIZE;
		kbuf.top_down = false;
		kbuf.buf_min = PAGE0->mem_free + PAGE_SIZE;
		kbuf.buf_max = kernel_load_addr;
		kbuf.mem = KEXEC_BUF_MEM_UNKNOWN;
		ret = kexec_add_buffer(&kbuf);
		if (ret)
			goto out;

		pr_debug("Loaded cmdline at 0x%lx\n", kbuf.mem);
		image->arch.cmdline = kbuf.mem;
	}
out:
	return NULL;
}

const struct kexec_file_ops kexec_elf_ops = {
	.probe = kexec_elf_probe,
	.load = elf_load,
};

const struct kexec_file_ops * const kexec_file_loaders[] = {
	&kexec_elf_ops,
	NULL
};
