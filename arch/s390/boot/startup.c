// SPDX-License-Identifier: GPL-2.0
#include <linux/string.h>
#include "compressed/decompressor.h"
#include "boot.h"

void startup_kernel(void)
{
	void *img;

	if (!IS_ENABLED(CONFIG_KERNEL_UNCOMPRESSED)) {
		img = decompress_kernel();
		memmove((void *)vmlinux.default_lma, img, vmlinux.image_size);
	}
	vmlinux.entry();
}
