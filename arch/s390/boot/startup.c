// SPDX-License-Identifier: GPL-2.0
#include <linux/string.h>
#include "compressed/decompressor.h"
#include "boot.h"

void startup_kernel(void)
{
	void (*startup_continue)(void) = (void *)0x100000;
	unsigned long uncompressed_size;
	void *uncompressed_img;

	if (!IS_ENABLED(CONFIG_KERNEL_UNCOMPRESSED)) {
		uncompressed_img = decompress_kernel(&uncompressed_size);
		memmove(startup_continue, uncompressed_img, uncompressed_size);
	}
	startup_continue();
}
