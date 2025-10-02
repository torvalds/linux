// SPDX-License-Identifier: GPL-2.0
/*
 * kexec_file for LoongArch
 *
 * Author: Youling Tang <tangyouling@kylinos.cn>
 * Copyright (C) 2025 KylinSoft Corporation.
 *
 * Most code is derived from LoongArch port of kexec-tools
 */

#define pr_fmt(fmt) "kexec_file: " fmt

#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <asm/bootinfo.h>

const struct kexec_file_ops * const kexec_file_loaders[] = {
	NULL
};

int arch_kimage_file_post_load_cleanup(struct kimage *image)
{
	vfree(image->elf_headers);
	image->elf_headers = NULL;
	image->elf_headers_sz = 0;

	return kexec_image_post_load_cleanup_default(image);
}

/* Add the "kexec_file" command line parameter to command line. */
static void cmdline_add_loader(unsigned long *cmdline_tmplen, char *modified_cmdline)
{
	int loader_strlen;

	loader_strlen = sprintf(modified_cmdline + (*cmdline_tmplen), "kexec_file ");
	*cmdline_tmplen += loader_strlen;
}

/* Add the "initrd=start,size" command line parameter to command line. */
static void cmdline_add_initrd(struct kimage *image, unsigned long *cmdline_tmplen,
				char *modified_cmdline, unsigned long initrd)
{
	int initrd_strlen;

	initrd_strlen = sprintf(modified_cmdline + (*cmdline_tmplen), "initrd=0x%lx,0x%lx ",
		initrd, image->initrd_buf_len);
	*cmdline_tmplen += initrd_strlen;
}

/*
 * Try to add the initrd to the image. If it is not possible to find valid
 * locations, this function will undo changes to the image and return non zero.
 */
int load_other_segments(struct kimage *image,
			unsigned long kernel_load_addr, unsigned long kernel_size,
			char *initrd, unsigned long initrd_len, char *cmdline, unsigned long cmdline_len)
{
	int ret = 0;
	unsigned long cmdline_tmplen = 0;
	unsigned long initrd_load_addr = 0;
	unsigned long orig_segments = image->nr_segments;
	char *modified_cmdline = NULL;
	struct kexec_buf kbuf;

	kbuf.image = image;
	/* Don't allocate anything below the kernel */
	kbuf.buf_min = kernel_load_addr + kernel_size;

	modified_cmdline = kzalloc(COMMAND_LINE_SIZE, GFP_KERNEL);
	if (!modified_cmdline)
		return -EINVAL;

	cmdline_add_loader(&cmdline_tmplen, modified_cmdline);
	/* Ensure it's null terminated */
	modified_cmdline[COMMAND_LINE_SIZE - 1] = '\0';

	/* Load initrd */
	if (initrd) {
		kbuf.buffer = initrd;
		kbuf.bufsz = initrd_len;
		kbuf.mem = KEXEC_BUF_MEM_UNKNOWN;
		kbuf.memsz = initrd_len;
		kbuf.buf_align = 0;
		/* within 1GB-aligned window of up to 32GB in size */
		kbuf.buf_max = round_down(kernel_load_addr, SZ_1G) + (unsigned long)SZ_1G * 32;
		kbuf.top_down = false;

		ret = kexec_add_buffer(&kbuf);
		if (ret < 0)
			goto out_err;
		initrd_load_addr = kbuf.mem;

		kexec_dprintk("Loaded initrd at 0x%lx bufsz=0x%lx memsz=0x%lx\n",
			      initrd_load_addr, kbuf.bufsz, kbuf.memsz);

		/* Add the initrd=start,size parameter to the command line */
		cmdline_add_initrd(image, &cmdline_tmplen, modified_cmdline, initrd_load_addr);
	}

	if (cmdline_len + cmdline_tmplen > COMMAND_LINE_SIZE) {
		pr_err("Appending command line exceeds COMMAND_LINE_SIZE\n");
		ret = -EINVAL;
		goto out_err;
	}

	memcpy(modified_cmdline + cmdline_tmplen, cmdline, cmdline_len);
	cmdline = modified_cmdline;
	image->arch.cmdline_ptr = (unsigned long)cmdline;

	return 0;

out_err:
	image->nr_segments = orig_segments;
	kfree(modified_cmdline);
	return ret;
}
