// SPDX-License-Identifier: GPL-2.0
/*
 * kexec_file for arm64
 *
 * Copyright (C) 2018 Linaro Limited
 * Author: AKASHI Takahiro <takahiro.akashi@linaro.org>
 *
 * Most code is derived from arm64 port of kexec-tools
 */

#define pr_fmt(fmt) "kexec_file: " fmt

#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/libfdt.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>
#include <linux/random.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <asm/byteorder.h>

/* relevant device tree properties */
#define FDT_PROP_INITRD_START	"linux,initrd-start"
#define FDT_PROP_INITRD_END	"linux,initrd-end"
#define FDT_PROP_BOOTARGS	"bootargs"
#define FDT_PROP_KASLR_SEED	"kaslr-seed"

const struct kexec_file_ops * const kexec_file_loaders[] = {
	&kexec_image_ops,
	NULL
};

int arch_kimage_file_post_load_cleanup(struct kimage *image)
{
	vfree(image->arch.dtb);
	image->arch.dtb = NULL;

	return kexec_image_post_load_cleanup_default(image);
}

static int setup_dtb(struct kimage *image,
		     unsigned long initrd_load_addr, unsigned long initrd_len,
		     char *cmdline, void *dtb)
{
	int off, ret;

	ret = fdt_path_offset(dtb, "/chosen");
	if (ret < 0)
		goto out;

	off = ret;

	/* add bootargs */
	if (cmdline) {
		ret = fdt_setprop_string(dtb, off, FDT_PROP_BOOTARGS, cmdline);
		if (ret)
			goto out;
	} else {
		ret = fdt_delprop(dtb, off, FDT_PROP_BOOTARGS);
		if (ret && (ret != -FDT_ERR_NOTFOUND))
			goto out;
	}

	/* add initrd-* */
	if (initrd_load_addr) {
		ret = fdt_setprop_u64(dtb, off, FDT_PROP_INITRD_START,
				      initrd_load_addr);
		if (ret)
			goto out;

		ret = fdt_setprop_u64(dtb, off, FDT_PROP_INITRD_END,
				      initrd_load_addr + initrd_len);
		if (ret)
			goto out;
	} else {
		ret = fdt_delprop(dtb, off, FDT_PROP_INITRD_START);
		if (ret && (ret != -FDT_ERR_NOTFOUND))
			goto out;

		ret = fdt_delprop(dtb, off, FDT_PROP_INITRD_END);
		if (ret && (ret != -FDT_ERR_NOTFOUND))
			goto out;
	}

	/* add kaslr-seed */
	ret = fdt_delprop(dtb, off, FDT_PROP_KASLR_SEED);
	if  (ret == -FDT_ERR_NOTFOUND)
		ret = 0;
	else if (ret)
		goto out;

	if (rng_is_initialized()) {
		u64 seed = get_random_u64();
		ret = fdt_setprop_u64(dtb, off, FDT_PROP_KASLR_SEED, seed);
		if (ret)
			goto out;
	} else {
		pr_notice("RNG is not initialised: omitting \"%s\" property\n",
				FDT_PROP_KASLR_SEED);
	}

out:
	if (ret)
		return (ret == -FDT_ERR_NOSPACE) ? -ENOMEM : -EINVAL;

	return 0;
}

/*
 * More space needed so that we can add initrd, bootargs and kaslr-seed.
 */
#define DTB_EXTRA_SPACE 0x1000

static int create_dtb(struct kimage *image,
		      unsigned long initrd_load_addr, unsigned long initrd_len,
		      char *cmdline, void **dtb)
{
	void *buf;
	size_t buf_size;
	size_t cmdline_len;
	int ret;

	cmdline_len = cmdline ? strlen(cmdline) : 0;
	buf_size = fdt_totalsize(initial_boot_params)
			+ cmdline_len + DTB_EXTRA_SPACE;

	for (;;) {
		buf = vmalloc(buf_size);
		if (!buf)
			return -ENOMEM;

		/* duplicate a device tree blob */
		ret = fdt_open_into(initial_boot_params, buf, buf_size);
		if (ret)
			return -EINVAL;

		ret = setup_dtb(image, initrd_load_addr, initrd_len,
				cmdline, buf);
		if (ret) {
			vfree(buf);
			if (ret == -ENOMEM) {
				/* unlikely, but just in case */
				buf_size += DTB_EXTRA_SPACE;
				continue;
			} else {
				return ret;
			}
		}

		/* trim it */
		fdt_pack(buf);
		*dtb = buf;

		return 0;
	}
}

int load_other_segments(struct kimage *image,
			unsigned long kernel_load_addr,
			unsigned long kernel_size,
			char *initrd, unsigned long initrd_len,
			char *cmdline)
{
	struct kexec_buf kbuf;
	void *dtb = NULL;
	unsigned long initrd_load_addr = 0, dtb_len;
	int ret = 0;

	kbuf.image = image;
	/* not allocate anything below the kernel */
	kbuf.buf_min = kernel_load_addr + kernel_size;

	/* load initrd */
	if (initrd) {
		kbuf.buffer = initrd;
		kbuf.bufsz = initrd_len;
		kbuf.mem = 0;
		kbuf.memsz = initrd_len;
		kbuf.buf_align = 0;
		/* within 1GB-aligned window of up to 32GB in size */
		kbuf.buf_max = round_down(kernel_load_addr, SZ_1G)
						+ (unsigned long)SZ_1G * 32;
		kbuf.top_down = false;

		ret = kexec_add_buffer(&kbuf);
		if (ret)
			goto out_err;
		initrd_load_addr = kbuf.mem;

		pr_debug("Loaded initrd at 0x%lx bufsz=0x%lx memsz=0x%lx\n",
				initrd_load_addr, initrd_len, initrd_len);
	}

	/* load dtb */
	ret = create_dtb(image, initrd_load_addr, initrd_len, cmdline, &dtb);
	if (ret) {
		pr_err("Preparing for new dtb failed\n");
		goto out_err;
	}

	dtb_len = fdt_totalsize(dtb);
	kbuf.buffer = dtb;
	kbuf.bufsz = dtb_len;
	kbuf.mem = 0;
	kbuf.memsz = dtb_len;
	/* not across 2MB boundary */
	kbuf.buf_align = SZ_2M;
	kbuf.buf_max = ULONG_MAX;
	kbuf.top_down = true;

	ret = kexec_add_buffer(&kbuf);
	if (ret)
		goto out_err;
	image->arch.dtb = dtb;
	image->arch.dtb_mem = kbuf.mem;

	pr_debug("Loaded dtb at 0x%lx bufsz=0x%lx memsz=0x%lx\n",
			kbuf.mem, dtb_len, dtb_len);

	return 0;

out_err:
	vfree(dtb);
	return ret;
}
