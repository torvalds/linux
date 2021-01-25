// SPDX-License-Identifier: GPL-2.0
/*
 * LiteX SoC Controller Driver
 *
 * Copyright (C) 2020 Antmicro <www.antmicro.com>
 *
 */

#include <linux/litex.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/reboot.h>

/* reset register located at the base address */
#define RESET_REG_OFF           0x00
#define RESET_REG_VALUE         0x00000001

#define SCRATCH_REG_OFF         0x04
#define SCRATCH_REG_VALUE       0x12345678
#define SCRATCH_TEST_VALUE      0xdeadbeef

/*
 * Check LiteX CSR read/write access
 *
 * This function reads and writes a scratch register in order to verify if CSR
 * access works.
 *
 * In case any problems are detected, the driver should panic.
 *
 * Access to the LiteX CSR is, by design, done in CPU native endianness.
 * The driver should not dynamically configure access functions when
 * the endianness mismatch is detected. Such situation indicates problems in
 * the soft SoC design and should be solved at the LiteX generator level,
 * not in the software.
 */
static int litex_check_csr_access(void __iomem *reg_addr)
{
	unsigned long reg;

	reg = litex_read32(reg_addr + SCRATCH_REG_OFF);

	if (reg != SCRATCH_REG_VALUE) {
		panic("Scratch register read error - the system is probably broken! Expected: 0x%x but got: 0x%lx",
			SCRATCH_REG_VALUE, reg);
		return -EINVAL;
	}

	litex_write32(reg_addr + SCRATCH_REG_OFF, SCRATCH_TEST_VALUE);
	reg = litex_read32(reg_addr + SCRATCH_REG_OFF);

	if (reg != SCRATCH_TEST_VALUE) {
		panic("Scratch register write error - the system is probably broken! Expected: 0x%x but got: 0x%lx",
			SCRATCH_TEST_VALUE, reg);
		return -EINVAL;
	}

	/* restore original value of the SCRATCH register */
	litex_write32(reg_addr + SCRATCH_REG_OFF, SCRATCH_REG_VALUE);

	pr_info("LiteX SoC Controller driver initialized: subreg:%d, align:%d",
		LITEX_SUBREG_SIZE, LITEX_SUBREG_ALIGN);

	return 0;
}

struct litex_soc_ctrl_device {
	void __iomem *base;
	struct notifier_block reset_nb;
};

static int litex_reset_handler(struct notifier_block *this, unsigned long mode,
			       void *cmd)
{
	struct litex_soc_ctrl_device *soc_ctrl_dev =
		container_of(this, struct litex_soc_ctrl_device, reset_nb);

	litex_write32(soc_ctrl_dev->base + RESET_REG_OFF, RESET_REG_VALUE);
	return NOTIFY_DONE;
}

#ifdef CONFIG_OF
static const struct of_device_id litex_soc_ctrl_of_match[] = {
	{.compatible = "litex,soc-controller"},
	{},
};
MODULE_DEVICE_TABLE(of, litex_soc_ctrl_of_match);
#endif /* CONFIG_OF */

static int litex_soc_ctrl_probe(struct platform_device *pdev)
{
	struct litex_soc_ctrl_device *soc_ctrl_dev;
	int error;

	soc_ctrl_dev = devm_kzalloc(&pdev->dev, sizeof(*soc_ctrl_dev), GFP_KERNEL);
	if (!soc_ctrl_dev)
		return -ENOMEM;

	soc_ctrl_dev->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(soc_ctrl_dev->base))
		return PTR_ERR(soc_ctrl_dev->base);

	error = litex_check_csr_access(soc_ctrl_dev->base);
	if (error)
		return error;

	platform_set_drvdata(pdev, soc_ctrl_dev);

	soc_ctrl_dev->reset_nb.notifier_call = litex_reset_handler;
	soc_ctrl_dev->reset_nb.priority = 128;
	error = register_restart_handler(&soc_ctrl_dev->reset_nb);
	if (error) {
		dev_warn(&pdev->dev, "cannot register restart handler: %d\n",
			 error);
	}

	return 0;
}

static int litex_soc_ctrl_remove(struct platform_device *pdev)
{
	struct litex_soc_ctrl_device *soc_ctrl_dev = platform_get_drvdata(pdev);

	unregister_restart_handler(&soc_ctrl_dev->reset_nb);
	return 0;
}

static struct platform_driver litex_soc_ctrl_driver = {
	.driver = {
		.name = "litex-soc-controller",
		.of_match_table = of_match_ptr(litex_soc_ctrl_of_match)
	},
	.probe = litex_soc_ctrl_probe,
	.remove = litex_soc_ctrl_remove,
};

module_platform_driver(litex_soc_ctrl_driver);
MODULE_DESCRIPTION("LiteX SoC Controller driver");
MODULE_AUTHOR("Antmicro <www.antmicro.com>");
MODULE_LICENSE("GPL v2");
