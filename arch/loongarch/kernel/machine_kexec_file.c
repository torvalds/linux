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
	&kexec_efi_ops,
	&kexec_elf_ops,
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

#ifdef CONFIG_CRASH_DUMP

static int prepare_elf_headers(void **addr, unsigned long *sz)
{
	int ret, nr_ranges;
	uint64_t i;
	phys_addr_t start, end;
	struct crash_mem *cmem;

	nr_ranges = 2; /* for exclusion of crashkernel region */
	for_each_mem_range(i, &start, &end)
		nr_ranges++;

	cmem = kmalloc(struct_size(cmem, ranges, nr_ranges), GFP_KERNEL);
	if (!cmem)
		return -ENOMEM;

	cmem->max_nr_ranges = nr_ranges;
	cmem->nr_ranges = 0;
	for_each_mem_range(i, &start, &end) {
		cmem->ranges[cmem->nr_ranges].start = start;
		cmem->ranges[cmem->nr_ranges].end = end - 1;
		cmem->nr_ranges++;
	}

	/* Exclude crashkernel region */
	ret = crash_exclude_mem_range(cmem, crashk_res.start, crashk_res.end);
	if (ret < 0)
		goto out;

	if (crashk_low_res.end) {
		ret = crash_exclude_mem_range(cmem, crashk_low_res.start, crashk_low_res.end);
		if (ret < 0)
			goto out;
	}

	ret = crash_prepare_elf64_headers(cmem, true, addr, sz);

out:
	kfree(cmem);
	return ret;
}

/*
 * Add the "mem=size@start" command line parameter to command line, indicating the
 * memory region the new kernel can use to boot into.
 */
static void cmdline_add_mem(unsigned long *cmdline_tmplen, char *modified_cmdline)
{
	int mem_strlen = 0;

	mem_strlen = sprintf(modified_cmdline + (*cmdline_tmplen), "mem=0x%llx@0x%llx ",
		crashk_res.end - crashk_res.start + 1, crashk_res.start);
	*cmdline_tmplen += mem_strlen;

	if (crashk_low_res.end) {
		mem_strlen = sprintf(modified_cmdline + (*cmdline_tmplen), "mem=0x%llx@0x%llx ",
			crashk_low_res.end - crashk_low_res.start + 1, crashk_low_res.start);
		*cmdline_tmplen += mem_strlen;
	}
}

/* Add the "elfcorehdr=size@start" command line parameter to command line. */
static void cmdline_add_elfcorehdr(struct kimage *image, unsigned long *cmdline_tmplen,
				   char *modified_cmdline, unsigned long elfcorehdr_sz)
{
	int elfcorehdr_strlen = 0;

	elfcorehdr_strlen = sprintf(modified_cmdline + (*cmdline_tmplen), "elfcorehdr=0x%lx@0x%lx ",
		elfcorehdr_sz, image->elf_load_addr);
	*cmdline_tmplen += elfcorehdr_strlen;
}

#endif

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
	struct kexec_buf kbuf = {};

	kbuf.image = image;
	/* Don't allocate anything below the kernel */
	kbuf.buf_min = kernel_load_addr + kernel_size;

	modified_cmdline = kzalloc(COMMAND_LINE_SIZE, GFP_KERNEL);
	if (!modified_cmdline)
		return -EINVAL;

	cmdline_add_loader(&cmdline_tmplen, modified_cmdline);
	/* Ensure it's null terminated */
	modified_cmdline[COMMAND_LINE_SIZE - 1] = '\0';

#ifdef CONFIG_CRASH_DUMP
	/* Load elf core header */
	if (image->type == KEXEC_TYPE_CRASH) {
		void *headers;
		unsigned long headers_sz;

		ret = prepare_elf_headers(&headers, &headers_sz);
		if (ret < 0) {
			pr_err("Preparing elf core header failed\n");
			goto out_err;
		}

		kbuf.buffer = headers;
		kbuf.bufsz = headers_sz;
		kbuf.mem = KEXEC_BUF_MEM_UNKNOWN;
		kbuf.memsz = headers_sz;
		kbuf.buf_align = SZ_64K; /* largest supported page size */
		kbuf.buf_max = ULONG_MAX;
		kbuf.top_down = true;

		ret = kexec_add_buffer(&kbuf);
		if (ret < 0) {
			vfree(headers);
			goto out_err;
		}
		image->elf_headers = headers;
		image->elf_load_addr = kbuf.mem;
		image->elf_headers_sz = headers_sz;

		kexec_dprintk("Loaded elf core header at 0x%lx bufsz=0x%lx memsz=0x%lx\n",
			      image->elf_load_addr, kbuf.bufsz, kbuf.memsz);

		/* Add the mem=size@start parameter to the command line */
		cmdline_add_mem(&cmdline_tmplen, modified_cmdline);

		/* Add the elfcorehdr=size@start parameter to the command line */
		cmdline_add_elfcorehdr(image, &cmdline_tmplen, modified_cmdline, headers_sz);
	}
#endif

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
