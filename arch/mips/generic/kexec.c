/*
 * Copyright (C) 2016 Imagination Technologies
 * Author: Marcin Nowakowski <marcin.nowakowski@mips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/kexec.h>
#include <linux/libfdt.h>
#include <linux/uaccess.h>

static int generic_kexec_prepare(struct kimage *image)
{
	int i;

	for (i = 0; i < image->nr_segments; i++) {
		struct fdt_header fdt;

		if (image->segment[i].memsz <= sizeof(fdt))
			continue;

		if (copy_from_user(&fdt, image->segment[i].buf, sizeof(fdt)))
			continue;

		if (fdt_check_header(&fdt))
			continue;

		kexec_args[0] = -2;
		kexec_args[1] = (unsigned long)
			phys_to_virt((unsigned long)image->segment[i].mem);
		break;
	}
	return 0;
}

static int __init register_generic_kexec(void)
{
	_machine_kexec_prepare = generic_kexec_prepare;
	return 0;
}
arch_initcall(register_generic_kexec);
