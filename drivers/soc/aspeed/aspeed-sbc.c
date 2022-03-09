// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright 2022 IBM Corp. */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/debugfs.h>

#define SEC_STATUS		0x14
#define ABR_IMAGE_SOURCE	BIT(13)
#define OTP_PROTECTED		BIT(8)
#define LOW_SEC_KEY		BIT(7)
#define SECURE_BOOT		BIT(6)
#define UART_BOOT		BIT(5)

struct sbe {
	u8 abr_image;
	u8 low_security_key;
	u8 otp_protected;
	u8 secure_boot;
	u8 invert;
	u8 uart_boot;
};

static struct sbe sbe;

static int __init aspeed_sbc_init(void)
{
	struct device_node *np;
	void __iomem *base;
	struct dentry *sbc_dir;
	u32 security_status;

	/* AST2600 only */
	np = of_find_compatible_node(NULL, NULL, "aspeed,ast2600-sbc");
	if (!of_device_is_available(np))
		return -ENODEV;

	base = of_iomap(np, 0);
	if (!base) {
		of_node_put(np);
		return -ENODEV;
	}

	security_status = readl(base + SEC_STATUS);

	iounmap(base);
	of_node_put(np);

	sbe.abr_image = !!(security_status & ABR_IMAGE_SOURCE);
	sbe.low_security_key = !!(security_status & LOW_SEC_KEY);
	sbe.otp_protected = !!(security_status & OTP_PROTECTED);
	sbe.secure_boot = !!(security_status & SECURE_BOOT);
	/* Invert the bit, as 1 is boot from SPI/eMMC */
	sbe.uart_boot =  !(security_status & UART_BOOT);

	pr_info("AST2600 secure boot %s\n", sbe.secure_boot ? "enabled" : "disabled");

	sbc_dir = debugfs_create_dir("sbc", arch_debugfs_dir);
	if (IS_ERR(sbc_dir))
		return PTR_ERR(sbc_dir);

	debugfs_create_u8("abr_image", 0444, sbc_dir, &sbe.abr_image);
	debugfs_create_u8("low_security_key", 0444, sbc_dir, &sbe.low_security_key);
	debugfs_create_u8("otp_protected", 0444, sbc_dir, &sbe.otp_protected);
	debugfs_create_u8("uart_boot", 0444, sbc_dir, &sbe.uart_boot);
	debugfs_create_u8("secure_boot", 0444, sbc_dir, &sbe.secure_boot);

	return 0;
}

subsys_initcall(aspeed_sbc_init);
