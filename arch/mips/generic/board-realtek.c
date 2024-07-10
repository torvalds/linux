// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Allied Telesis
 */

#include <linux/errno.h>
#include <linux/libfdt.h>
#include <linux/printk.h>
#include <linux/types.h>

#include <asm/fw/fw.h>
#include <asm/machine.h>

static __init int realtek_add_initrd(void *fdt)
{
	int node, err;
	u32 start, size;

	node = fdt_path_offset(fdt, "/chosen");
	if (node < 0) {
		pr_err("/chosen node not found\n");
		return -ENOENT;
	}

	start = fw_getenvl("initrd_start");
	size = fw_getenvl("initrd_size");

	if (start == 0 && size == 0)
		return 0;

	pr_info("Adding initrd info from environment\n");

	err = fdt_setprop_u32(fdt, node, "linux,initrd-start", start);
	if (err) {
		pr_err("unable to set initrd-start: %d\n", err);
		return err;
	}

	err = fdt_setprop_u32(fdt, node, "linux,initrd-end", start + size);
	if (err) {
		pr_err("unable to set initrd-end: %d\n", err);
		return err;
	}

	return 0;
}

static const struct mips_fdt_fixup realtek_fdt_fixups[] __initconst = {
	{ realtek_add_initrd, "add initrd" },
	{},
};

static __init const void *realtek_fixup_fdt(const void *fdt, const void *match_data)
{
	static unsigned char fdt_buf[16 << 10] __initdata;
	int err;

	if (fdt_check_header(fdt))
		panic("Corrupt DT");

	fw_init_cmdline();

	err = apply_mips_fdt_fixups(fdt_buf, sizeof(fdt_buf), fdt, realtek_fdt_fixups);
	if (err)
		panic("Unable to fixup FDT: %d", err);

	return fdt_buf;

}

static const struct of_device_id realtek_of_match[] __initconst = {
	{ .compatible = "realtek,rtl9302-soc" },
	{}
};

MIPS_MACHINE(realtek) = {
	.matches = realtek_of_match,
	.fixup_fdt = realtek_fixup_fdt,
};
