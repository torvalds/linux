// SPDX-License-Identifier: GPL-2.0-only
/*
 * ppc64 code to implement the kexec_file_load syscall
 *
 * Copyright (C) 2004  Adam Litke (agl@us.ibm.com)
 * Copyright (C) 2004  IBM Corp.
 * Copyright (C) 2004,2005  Milton D Miller II, IBM Corporation
 * Copyright (C) 2005  R Sharada (sharada@in.ibm.com)
 * Copyright (C) 2006  Mohan Kumar M (mohan@in.ibm.com)
 * Copyright (C) 2020  IBM Corporation
 *
 * Based on kexec-tools' kexec-ppc64.c, kexec-elf-rel-ppc64.c, fs2dt.c.
 * Heavily modified for the kernel by
 * Hari Bathini, IBM Corporation.
 */

#include <linux/kexec.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>

const struct kexec_file_ops * const kexec_file_loaders[] = {
	&kexec_elf64_ops,
	NULL
};

/**
 * setup_purgatory_ppc64 - initialize PPC64 specific purgatory's global
 *                         variables and call setup_purgatory() to initialize
 *                         common global variable.
 * @image:                 kexec image.
 * @slave_code:            Slave code for the purgatory.
 * @fdt:                   Flattened device tree for the next kernel.
 * @kernel_load_addr:      Address where the kernel is loaded.
 * @fdt_load_addr:         Address where the flattened device tree is loaded.
 *
 * Returns 0 on success, negative errno on error.
 */
int setup_purgatory_ppc64(struct kimage *image, const void *slave_code,
			  const void *fdt, unsigned long kernel_load_addr,
			  unsigned long fdt_load_addr)
{
	int ret;

	ret = setup_purgatory(image, slave_code, fdt, kernel_load_addr,
			      fdt_load_addr);
	if (ret)
		pr_err("Failed to setup purgatory symbols");
	return ret;
}

/**
 * setup_new_fdt_ppc64 - Update the flattend device-tree of the kernel
 *                       being loaded.
 * @image:               kexec image being loaded.
 * @fdt:                 Flattened device tree for the next kernel.
 * @initrd_load_addr:    Address where the next initrd will be loaded.
 * @initrd_len:          Size of the next initrd, or 0 if there will be none.
 * @cmdline:             Command line for the next kernel, or NULL if there will
 *                       be none.
 *
 * Returns 0 on success, negative errno on error.
 */
int setup_new_fdt_ppc64(const struct kimage *image, void *fdt,
			unsigned long initrd_load_addr,
			unsigned long initrd_len, const char *cmdline)
{
	return setup_new_fdt(image, fdt, initrd_load_addr, initrd_len, cmdline);
}

/**
 * arch_kexec_kernel_image_probe - Does additional handling needed to setup
 *                                 kexec segments.
 * @image:                         kexec image being loaded.
 * @buf:                           Buffer pointing to elf data.
 * @buf_len:                       Length of the buffer.
 *
 * Returns 0 on success, negative errno on error.
 */
int arch_kexec_kernel_image_probe(struct kimage *image, void *buf,
				  unsigned long buf_len)
{
	/* We don't support crash kernels yet. */
	if (image->type == KEXEC_TYPE_CRASH)
		return -EOPNOTSUPP;

	return kexec_image_probe_default(image, buf, buf_len);
}
