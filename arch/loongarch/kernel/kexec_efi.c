// SPDX-License-Identifier: GPL-2.0
/*
 * Load EFI vmlinux file for the kexec_file_load syscall.
 *
 * Author: Youling Tang <tangyouling@kylinos.cn>
 * Copyright (C) 2025 KylinSoft Corporation.
 */

#define pr_fmt(fmt)	"kexec_file(EFI): " fmt

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/pe.h>
#include <linux/string.h>
#include <asm/byteorder.h>
#include <asm/cpufeature.h>
#include <asm/image.h>

static int efi_kexec_probe(const char *kernel_buf, unsigned long kernel_len)
{
	const struct loongarch_image_header *h = (const struct loongarch_image_header *)kernel_buf;

	if (!h || (kernel_len < sizeof(*h))) {
		kexec_dprintk("No LoongArch image header.\n");
		return -EINVAL;
	}

	if (!loongarch_header_check_dos_sig(h)) {
		kexec_dprintk("No LoongArch PE image header.\n");
		return -EINVAL;
	}

	return 0;
}

static void *efi_kexec_load(struct kimage *image,
				char *kernel, unsigned long kernel_len,
				char *initrd, unsigned long initrd_len,
				char *cmdline, unsigned long cmdline_len)
{
	int ret;
	unsigned long text_offset, kernel_segment_number;
	struct kexec_buf kbuf = {};
	struct kexec_segment *kernel_segment;
	struct loongarch_image_header *h;

	h = (struct loongarch_image_header *)kernel;
	if (!h->kernel_asize)
		return ERR_PTR(-EINVAL);

	/*
	 * Load the kernel
	 * FIXME: Non-relocatable kernel rejected for kexec_file (require CONFIG_RELOCATABLE)
	 */
	kbuf.image = image;
	kbuf.buf_max = ULONG_MAX;
	kbuf.top_down = false;

	kbuf.buffer = kernel;
	kbuf.bufsz = kernel_len;
	kbuf.mem = KEXEC_BUF_MEM_UNKNOWN;
	kbuf.memsz = le64_to_cpu(h->kernel_asize);
	text_offset = le64_to_cpu(h->text_offset);
	kbuf.buf_min = text_offset;
	kbuf.buf_align = SZ_2M;

	kernel_segment_number = image->nr_segments;

	/*
	 * The location of the kernel segment may make it impossible to
	 * satisfy the other segment requirements, so we try repeatedly
	 * to find a location that will work.
	 */
	while ((ret = kexec_add_buffer(&kbuf)) == 0) {
		/* Try to load additional data */
		kernel_segment = &image->segment[kernel_segment_number];
		ret = load_other_segments(image, kernel_segment->mem,
					  kernel_segment->memsz, initrd,
					  initrd_len, cmdline, cmdline_len);
		if (!ret)
			break;

		/*
		 * We couldn't find space for the other segments; erase the
		 * kernel segment and try the next available hole.
		 */
		image->nr_segments -= 1;
		kbuf.buf_min = kernel_segment->mem + kernel_segment->memsz;
		kbuf.mem = KEXEC_BUF_MEM_UNKNOWN;
	}

	if (ret < 0) {
		pr_err("Could not find any suitable kernel location!");
		return ERR_PTR(ret);
	}

	kernel_segment = &image->segment[kernel_segment_number];

	/* Make sure the second kernel jumps to the correct "kernel_entry" */
	image->start = kernel_segment->mem + h->kernel_entry - text_offset;

	kexec_dprintk("Loaded kernel at 0x%lx bufsz=0x%lx memsz=0x%lx\n",
		      kernel_segment->mem, kbuf.bufsz, kernel_segment->memsz);

	return NULL;
}

const struct kexec_file_ops kexec_efi_ops = {
	.probe = efi_kexec_probe,
	.load = efi_kexec_load,
};
