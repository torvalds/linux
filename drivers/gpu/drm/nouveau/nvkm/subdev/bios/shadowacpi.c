/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "priv.h"

#if defined(CONFIG_ACPI) && defined(CONFIG_X86)
int nouveau_acpi_get_bios_chunk(uint8_t *bios, int offset, int len);
bool nouveau_acpi_rom_supported(struct pci_dev *pdev);
#else
static inline bool
nouveau_acpi_rom_supported(struct pci_dev *pdev)
{
	return false;
}

static inline int
nouveau_acpi_get_bios_chunk(uint8_t *bios, int offset, int len)
{
	return -EINVAL;
}
#endif

/* This version of the shadow function disobeys the ACPI spec and tries
 * to fetch in units of more than 4KiB at a time.  This is a LOT faster
 * on some systems, such as Lenovo W530.
 */
static u32
acpi_read_fast(void *data, u32 offset, u32 length, struct nouveau_bios *bios)
{
	u32 limit = (offset + length + 0xfff) & ~0xfff;
	u32 start = offset & ~0x00000fff;
	u32 fetch = limit - start;

	if (nvbios_extend(bios, limit) > 0) {
		int ret = nouveau_acpi_get_bios_chunk(bios->data, start, fetch);
		if (ret == fetch)
			return fetch;
	}

	return 0;
}

/* Other systems, such as the one in fdo#55948, will report a success
 * but only return 4KiB of data.  The common bios fetching logic will
 * detect an invalid image, and fall back to this version of the read
 * function.
 */
static u32
acpi_read_slow(void *data, u32 offset, u32 length, struct nouveau_bios *bios)
{
	u32 limit = (offset + length + 0xfff) & ~0xfff;
	u32 start = offset & ~0xfff;
	u32 fetch = 0;

	if (nvbios_extend(bios, limit) > 0) {
		while (start + fetch < limit) {
			int ret = nouveau_acpi_get_bios_chunk(bios->data,
							      start + fetch,
							      0x1000);
			if (ret != 0x1000)
				break;
			fetch += 0x1000;
		}
	}

	return fetch;
}

static void *
acpi_init(struct nouveau_bios *bios, const char *name)
{
	if (!nouveau_acpi_rom_supported(nv_device(bios)->pdev))
		return ERR_PTR(-ENODEV);
	return NULL;
}

const struct nvbios_source
nvbios_acpi_fast = {
	.name = "ACPI",
	.init = acpi_init,
	.read = acpi_read_fast,
	.rw = false,
};

const struct nvbios_source
nvbios_acpi_slow = {
	.name = "ACPI",
	.init = acpi_init,
	.read = acpi_read_slow,
	.rw = false,
};
