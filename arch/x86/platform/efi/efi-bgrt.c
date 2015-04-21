/*
 * Copyright 2012 Intel Corporation
 * Author: Josh Triplett <josh@joshtriplett.org>
 *
 * Based on the bgrt driver:
 * Copyright 2012 Red Hat, Inc <mjg@redhat.com>
 * Author: Matthew Garrett
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/efi.h>
#include <linux/efi-bgrt.h>

struct acpi_table_bgrt *bgrt_tab;
void *__initdata bgrt_image;
size_t __initdata bgrt_image_size;

struct bmp_header {
	u16 id;
	u32 size;
} __packed;

void __init efi_bgrt_init(void)
{
	acpi_status status;
	void __iomem *image;
	bool ioremapped = false;
	struct bmp_header bmp_header;

	if (acpi_disabled)
		return;

	status = acpi_get_table("BGRT", 0,
	                        (struct acpi_table_header **)&bgrt_tab);
	if (ACPI_FAILURE(status))
		return;

	if (bgrt_tab->header.length < sizeof(*bgrt_tab)) {
		pr_err("Ignoring BGRT: invalid length %u (expected %zu)\n",
		       bgrt_tab->header.length, sizeof(*bgrt_tab));
		return;
	}
	if (bgrt_tab->version != 1) {
		pr_err("Ignoring BGRT: invalid version %u (expected 1)\n",
		       bgrt_tab->version);
		return;
	}
	if (bgrt_tab->status != 1) {
		pr_err("Ignoring BGRT: invalid status %u (expected 1)\n",
		       bgrt_tab->status);
		return;
	}
	if (bgrt_tab->image_type != 0) {
		pr_err("Ignoring BGRT: invalid image type %u (expected 0)\n",
		       bgrt_tab->image_type);
		return;
	}
	if (!bgrt_tab->image_address) {
		pr_err("Ignoring BGRT: null image address\n");
		return;
	}

	image = efi_lookup_mapped_addr(bgrt_tab->image_address);
	if (!image) {
		image = early_ioremap(bgrt_tab->image_address,
				       sizeof(bmp_header));
		ioremapped = true;
		if (!image) {
			pr_err("Ignoring BGRT: failed to map image header memory\n");
			return;
		}
	}

	memcpy_fromio(&bmp_header, image, sizeof(bmp_header));
	if (ioremapped)
		early_iounmap(image, sizeof(bmp_header));
	bgrt_image_size = bmp_header.size;

	bgrt_image = kmalloc(bgrt_image_size, GFP_KERNEL | __GFP_NOWARN);
	if (!bgrt_image) {
		pr_err("Ignoring BGRT: failed to allocate memory for image (wanted %zu bytes)\n",
		       bgrt_image_size);
		return;
	}

	if (ioremapped) {
		image = early_ioremap(bgrt_tab->image_address,
				       bmp_header.size);
		if (!image) {
			pr_err("Ignoring BGRT: failed to map image memory\n");
			kfree(bgrt_image);
			bgrt_image = NULL;
			return;
		}
	}

	memcpy_fromio(bgrt_image, image, bgrt_image_size);
	if (ioremapped)
		early_iounmap(image, bmp_header.size);
}
