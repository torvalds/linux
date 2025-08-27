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
#include <asm/ipl.h>
#include <asm/setup.h>

static int kexec_file_add_kernel_image(struct kimage *image,
				       struct s390_load_data *data)
{
	struct kexec_buf buf = {};

	buf.image = image;

	buf.buffer = image->kernel_buf;
	buf.bufsz = image->kernel_buf_len;

	buf.mem = 0;
#ifdef CONFIG_CRASH_DUMP
	if (image->type == KEXEC_TYPE_CRASH)
		buf.mem += crashk_res.start;
#endif
	buf.memsz = buf.bufsz;

	data->kernel_buf = image->kernel_buf;
	data->kernel_mem = buf.mem;
	data->parm = image->kernel_buf + PARMAREA;
	data->memsz += buf.memsz;

	ipl_report_add_component(data->report, &buf,
				 IPL_RB_COMPONENT_FLAG_SIGNED |
				 IPL_RB_COMPONENT_FLAG_VERIFIED,
				 IPL_RB_CERT_UNKNOWN);
	return kexec_add_buffer(&buf);
}

static void *s390_image_load(struct kimage *image,
			     char *kernel, unsigned long kernel_len,
			     char *initrd, unsigned long initrd_len,
			     char *cmdline, unsigned long cmdline_len)
{
	return kexec_file_add_components(image, kexec_file_add_kernel_image);
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
#ifdef CONFIG_KEXEC_SIG
	.verify_sig = s390_verify_sig,
#endif /* CONFIG_KEXEC_SIG */
};
