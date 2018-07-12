// SPDX-License-Identifier: GPL-2.0
/*
 * Image loader for kexec_file_load system call.
 *
 * Copyright IBM Corp. 2018
 *
 * Author(s): Philipp Rudo <prudo@linux.vnet.ibm.com>
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <asm/setup.h>

static int kexec_file_add_image_kernel(struct kimage *image,
				       struct s390_load_data *data,
				       char *kernel, unsigned long kernel_len)
{
	struct kexec_buf buf;
	int ret;

	buf.image = image;

	buf.buffer = kernel + STARTUP_NORMAL_OFFSET;
	buf.bufsz = kernel_len - STARTUP_NORMAL_OFFSET;

	buf.mem = STARTUP_NORMAL_OFFSET;
	if (image->type == KEXEC_TYPE_CRASH)
		buf.mem += crashk_res.start;
	buf.memsz = buf.bufsz;

	ret = kexec_add_buffer(&buf);

	data->kernel_buf = kernel;
	data->memsz += buf.memsz + STARTUP_NORMAL_OFFSET;

	return ret;
}

static void *s390_image_load(struct kimage *image,
			     char *kernel, unsigned long kernel_len,
			     char *initrd, unsigned long initrd_len,
			     char *cmdline, unsigned long cmdline_len)
{
	struct s390_load_data data = {0};
	int ret;

	ret = kexec_file_add_image_kernel(image, &data, kernel, kernel_len);
	if (ret)
		return ERR_PTR(ret);

	if (initrd) {
		ret = kexec_file_add_initrd(image, &data, initrd, initrd_len);
		if (ret)
			return ERR_PTR(ret);
	}

	ret = kexec_file_add_purgatory(image, &data);
	if (ret)
		return ERR_PTR(ret);

	return kexec_file_update_kernel(image, &data);
}

static int s390_image_probe(const char *buf, unsigned long len)
{
	/* Can't reliably tell if an image is valid.  Therefore give the
	 * user whatever he wants.
	 */
	return 0;
}

const struct kexec_file_ops s390_kexec_image_ops = {
	.probe = s390_image_probe,
	.load = s390_image_load,
};
