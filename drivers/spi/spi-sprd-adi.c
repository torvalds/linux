/*
 * Copyright (C) 2017 Spreadtrum Communications Inc.
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/delay.h>
#include <linux/hwspinlock.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/spi/spi.h>
#include <linux/sizes.h>

/* Registers definitions for ADI controller */
#define REG_ADI_CTRL0			0x4
#define REG_ADI_CHN_PRIL		0x8
#define REG_ADI_CHN_PRIH		0xc
#define REG_ADI_INT_EN			0x10
#define REG_ADI_INT_RAW			0x14
#define REG_ADI_INT_MASK		0x18
#define REG_ADI_INT_CLR			0x1c
#define REG_ADI_GSSI_CFG0		0x20
#define REG_ADI_GSSI_CFG1		0x24
#define REG_ADI_RD_CMD			0x28
#define REG_ADI_RD_DATA			0x2c
#define REG_ADI_ARM_FIFO_STS		0x30
#define REG_ADI_STS			0x34
#define REG_ADI_EVT_FIFO_STS		0x38
#define REG_ADI_ARM_CMD_STS		0x3c
#define REG_ADI_CHN_EN			0x40
#define REG_ADI_CHN_ADDR(id)		(0x44 + (id - 2) * 4)
#define REG_ADI_CHN_EN1			0x20c

/* Bits definitions for register REG_ADI_GSSI_CFG0 */
#define BIT_CLK_ALL_ON			BIT(30)

/* Bits definitions for register REG_ADI_RD_DATA */
#define BIT_RD_CMD_BUSY			BIT(31)
#define RD_ADDR_SHIFT			16
#define RD_VALUE_MASK			GENMASK(15, 0)
#define RD_ADDR_MASK			GENMASK(30, 16)

/* Bits definitions for register REG_ADI_ARM_FIFO_STS */
#define BIT_FIFO_FULL			BIT(11)
#define BIT_FIFO_EMPTY			BIT(10)

/*
 * ADI slave devices include RTC, ADC, regulator, charger, thermal and so on.
 * The slave devices address offset is always 0x8000 and size is 4K.
 */
#define ADI_SLAVE_ADDR_SIZE		SZ_4K
#define ADI_SLAVE_OFFSET		0x8000

/* Timeout (ms) for the trylock of hardware spinlocks */
#define ADI_HWSPINLOCK_TIMEOUT		5000
/*
 * ADI controller has 50 channels including 2 software channels
 * and 48 hardware channels.
 */
#define ADI_HW_CHNS			50

#define ADI_FIFO_DRAIN_TIMEOUT		1000
#define ADI_READ_TIMEOUT		2000
#define REG_ADDR_LOW_MASK		GENMASK(11, 0)

/* Registers definitions for PMIC watchdog controller */
#define REG_WDG_LOAD_LOW		0x80
#define REG_WDG_LOAD_HIGH		0x84
#define REG_WDG_CTRL			0x88
#define REG_WDG_LOCK			0xa0

/* Bits definitions for register REG_WDG_CTRL */
#define BIT_WDG_RUN			BIT(1)
#define BIT_WDG_NEW			BIT(2)
#define BIT_WDG_RST			BIT(3)

/* Registers definitions for PMIC */
#define PMIC_RST_STATUS			0xee8
#define PMIC_MODULE_EN			0xc08
#define PMIC_CLK_EN			0xc18
#define BIT_WDG_EN			BIT(2)

/* Definition of PMIC reset status register */
#define HWRST_STATUS_SECURITY		0x02
#define HWRST_STATUS_RECOVERY		0x20
#define HWRST_STATUS_NORMAL		0x40
#define HWRST_STATUS_ALARM		0x50
#define HWRST_STATUS_SLEEP		0x60
#define HWRST_STATUS_FASTBOOT		0x30
#define HWRST_STATUS_SPECIAL		0x70
#define HWRST_STATUS_PANIC		0x80
#define HWRST_STATUS_CFTREBOOT		0x90
#define HWRST_STATUS_AUTODLOADER	0xa0
#define HWRST_STATUS_IQMODE		0xb0
#define HWRST_STATUS_SPRDISK		0xc0
#define HWRST_STATUS_FACTORYTEST	0xe0
#define HWRST_STATUS_WATCHDOG		0xf0

/* Use default timeout 50 ms that converts to watchdog values */
#define WDG_LOAD_VAL			((50 * 1000) / 32768)
#define WDG_LOAD_MASK			GENMASK(15, 0)
#define WDG_UNLOCK_KEY			0xe551

struct sprd_adi {
	struct spi_controller	*ctlr;
	struct device		*dev;
	void __iomem		*base;
	struct hwspinlock	*hwlock;
	unsigned long		slave_vbase;
	unsigned long		slave_pbase;
	struct notifier_block	restart_handler;
};

static int sprd_adi_check_paddr(struct sprd_adi *sadi, u32 paddr)
{
	if (paddr < sadi->slave_pbase || paddr >
	    (sadi->slave_pbase + ADI_SLAVE_ADDR_SIZE)) {
		dev_err(sadi->dev,
			"slave physical address is incorrect, addr = 0x%x\n",
			paddr);
		return -EINVAL;
	}

	return 0;
}

static unsigned long sprd_adi_to_vaddr(struct sprd_adi *sadi, u32 paddr)
{
	return (paddr - sadi->slave_pbase + sadi->slave_vbase);
}

static int sprd_adi_drain_fifo(struct sprd_adi *sadi)
{
	u32 timeout = ADI_FIFO_DRAIN_TIMEOUT;
	u32 sts;

	do {
		sts = readl_relaxed(sadi->base + REG_ADI_ARM_FIFO_STS);
		if (sts & BIT_FIFO_EMPTY)
			break;

		cpu_relax();
	} while (--timeout);

	if (timeout == 0) {
		dev_err(sadi->dev, "drain write fifo timeout\n");
		return -EBUSY;
	}

	return 0;
}

static int sprd_adi_fifo_is_full(struct sprd_adi *sadi)
{
	return readl_relaxed(sadi->base + REG_ADI_ARM_FIFO_STS) & BIT_FIFO_FULL;
}

static int sprd_adi_read(struct sprd_adi *sadi, u32 reg_paddr, u32 *read_val)
{
	int read_timeout = ADI_READ_TIMEOUT;
	unsigned long flags;
	u32 val, rd_addr;
	int ret = 0;

	if (sadi->hwlock) {
		ret = hwspin_lock_timeout_irqsave(sadi->hwlock,
						  ADI_HWSPINLOCK_TIMEOUT,
						  &flags);
		if (ret) {
			dev_err(sadi->dev, "get the hw lock failed\n");
			return ret;
		}
	}

	/*
	 * Set the physical register address need to read into RD_CMD register,
	 * then ADI controller will start to transfer automatically.
	 */
	writel_relaxed(reg_paddr, sadi->base + REG_ADI_RD_CMD);

	/*
	 * Wait read operation complete, the BIT_RD_CMD_BUSY will be set
	 * simultaneously when writing read command to register, and the
	 * BIT_RD_CMD_BUSY will be cleared after the read operation is
	 * completed.
	 */
	do {
		val = readl_relaxed(sadi->base + REG_ADI_RD_DATA);
		if (!(val & BIT_RD_CMD_BUSY))
			break;

		cpu_relax();
	} while (--read_timeout);

	if (read_timeout == 0) {
		dev_err(sadi->dev, "ADI read timeout\n");
		ret = -EBUSY;
		goto out;
	}

	/*
	 * The return value includes data and read register address, from bit 0
	 * to bit 15 are data, and from bit 16 to bit 30 are read register
	 * address. Then we can check the returned register address to validate
	 * data.
	 */
	rd_addr = (val & RD_ADDR_MASK ) >> RD_ADDR_SHIFT;

	if (rd_addr != (reg_paddr & REG_ADDR_LOW_MASK)) {
		dev_err(sadi->dev, "read error, reg addr = 0x%x, val = 0x%x\n",
			reg_paddr, val);
		ret = -EIO;
		goto out;
	}

	*read_val = val & RD_VALUE_MASK;

out:
	if (sadi->hwlock)
		hwspin_unlock_irqrestore(sadi->hwlock, &flags);
	return ret;
}

static int sprd_adi_write(struct sprd_adi *sadi, u32 reg_paddr, u32 val)
{
	unsigned long reg = sprd_adi_to_vaddr(sadi, reg_paddr);
	u32 timeout = ADI_FIFO_DRAIN_TIMEOUT;
	unsigned long flags;
	int ret;

	if (sadi->hwlock) {
		ret = hwspin_lock_timeout_irqsave(sadi->hwlock,
						  ADI_HWSPINLOCK_TIMEOUT,
						  &flags);
		if (ret) {
			dev_err(sadi->dev, "get the hw lock failed\n");
			return ret;
		}
	}

	ret = sprd_adi_drain_fifo(sadi);
	if (ret < 0)
		goto out;

	/*
	 * we should wait for write fifo is empty before writing data to PMIC
	 * registers.
	 */
	do {
		if (!sprd_adi_fifo_is_full(sadi)) {
			writel_relaxed(val, (void __iomem *)reg);
			break;
		}

		cpu_relax();
	} while (--timeout);

	if (timeout == 0) {
		dev_err(sadi->dev, "write fifo is full\n");
		ret = -EBUSY;
	}

out:
	if (sadi->hwlock)
		hwspin_unlock_irqrestore(sadi->hwlock, &flags);
	return ret;
}

static int sprd_adi_transfer_one(struct spi_controller *ctlr,
				 struct spi_device *spi_dev,
				 struct spi_transfer *t)
{
	struct sprd_adi *sadi = spi_controller_get_devdata(ctlr);
	u32 phy_reg, val;
	int ret;

	if (t->rx_buf) {
		phy_reg = *(u32 *)t->rx_buf + sadi->slave_pbase;

		ret = sprd_adi_check_paddr(sadi, phy_reg);
		if (ret)
			return ret;

		ret = sprd_adi_read(sadi, phy_reg, &val);
		if (ret)
			return ret;

		*(u32 *)t->rx_buf = val;
	} else if (t->tx_buf) {
		u32 *p = (u32 *)t->tx_buf;

		/*
		 * Get the physical register address need to write and convert
		 * the physical address to virtual address. Since we need
		 * virtual register address to write.
		 */
		phy_reg = *p++ + sadi->slave_pbase;
		ret = sprd_adi_check_paddr(sadi, phy_reg);
		if (ret)
			return ret;

		val = *p;
		ret = sprd_adi_write(sadi, phy_reg, val);
		if (ret)
			return ret;
	} else {
		dev_err(sadi->dev, "no buffer for transfer\n");
		return -EINVAL;
	}

	return 0;
}

static void sprd_adi_set_wdt_rst_mode(struct sprd_adi *sadi)
{
#if IS_ENABLED(CONFIG_SPRD_WATCHDOG)
	u32 val;

	/* Set default watchdog reboot mode */
	sprd_adi_read(sadi, sadi->slave_pbase + PMIC_RST_STATUS, &val);
	val |= HWRST_STATUS_WATCHDOG;
	sprd_adi_write(sadi, sadi->slave_pbase + PMIC_RST_STATUS, val);
#endif
}

static int sprd_adi_restart_handler(struct notifier_block *this,
				    unsigned long mode, void *cmd)
{
	struct sprd_adi *sadi = container_of(this, struct sprd_adi,
					     restart_handler);
	u32 val, reboot_mode = 0;

	if (!cmd)
		reboot_mode = HWRST_STATUS_NORMAL;
	else if (!strncmp(cmd, "recovery", 8))
		reboot_mode = HWRST_STATUS_RECOVERY;
	else if (!strncmp(cmd, "alarm", 5))
		reboot_mode = HWRST_STATUS_ALARM;
	else if (!strncmp(cmd, "fastsleep", 9))
		reboot_mode = HWRST_STATUS_SLEEP;
	else if (!strncmp(cmd, "bootloader", 10))
		reboot_mode = HWRST_STATUS_FASTBOOT;
	else if (!strncmp(cmd, "panic", 5))
		reboot_mode = HWRST_STATUS_PANIC;
	else if (!strncmp(cmd, "special", 7))
		reboot_mode = HWRST_STATUS_SPECIAL;
	else if (!strncmp(cmd, "cftreboot", 9))
		reboot_mode = HWRST_STATUS_CFTREBOOT;
	else if (!strncmp(cmd, "autodloader", 11))
		reboot_mode = HWRST_STATUS_AUTODLOADER;
	else if (!strncmp(cmd, "iqmode", 6))
		reboot_mode = HWRST_STATUS_IQMODE;
	else if (!strncmp(cmd, "sprdisk", 7))
		reboot_mode = HWRST_STATUS_SPRDISK;
	else if (!strncmp(cmd, "tospanic", 8))
		reboot_mode = HWRST_STATUS_SECURITY;
	else if (!strncmp(cmd, "factorytest", 11))
		reboot_mode = HWRST_STATUS_FACTORYTEST;
	else
		reboot_mode = HWRST_STATUS_NORMAL;

	/* Record the reboot mode */
	sprd_adi_read(sadi, sadi->slave_pbase + PMIC_RST_STATUS, &val);
	val &= ~HWRST_STATUS_WATCHDOG;
	val |= reboot_mode;
	sprd_adi_write(sadi, sadi->slave_pbase + PMIC_RST_STATUS, val);

	/* Enable the interface clock of the watchdog */
	sprd_adi_read(sadi, sadi->slave_pbase + PMIC_MODULE_EN, &val);
	val |= BIT_WDG_EN;
	sprd_adi_write(sadi, sadi->slave_pbase + PMIC_MODULE_EN, val);

	/* Enable the work clock of the watchdog */
	sprd_adi_read(sadi, sadi->slave_pbase + PMIC_CLK_EN, &val);
	val |= BIT_WDG_EN;
	sprd_adi_write(sadi, sadi->slave_pbase + PMIC_CLK_EN, val);

	/* Unlock the watchdog */
	sprd_adi_write(sadi, sadi->slave_pbase + REG_WDG_LOCK, WDG_UNLOCK_KEY);

	sprd_adi_read(sadi, sadi->slave_pbase + REG_WDG_CTRL, &val);
	val |= BIT_WDG_NEW;
	sprd_adi_write(sadi, sadi->slave_pbase + REG_WDG_CTRL, val);

	/* Load the watchdog timeout value, 50ms is always enough. */
	sprd_adi_write(sadi, sadi->slave_pbase + REG_WDG_LOAD_HIGH, 0);
	sprd_adi_write(sadi, sadi->slave_pbase + REG_WDG_LOAD_LOW,
		       WDG_LOAD_VAL & WDG_LOAD_MASK);

	/* Start the watchdog to reset system */
	sprd_adi_read(sadi, sadi->slave_pbase + REG_WDG_CTRL, &val);
	val |= BIT_WDG_RUN | BIT_WDG_RST;
	sprd_adi_write(sadi, sadi->slave_pbase + REG_WDG_CTRL, val);

	/* Lock the watchdog */
	sprd_adi_write(sadi, sadi->slave_pbase + REG_WDG_LOCK, ~WDG_UNLOCK_KEY);

	mdelay(1000);

	dev_emerg(sadi->dev, "Unable to restart system\n");
	return NOTIFY_DONE;
}

static void sprd_adi_hw_init(struct sprd_adi *sadi)
{
	struct device_node *np = sadi->dev->of_node;
	int i, size, chn_cnt;
	const __be32 *list;
	u32 tmp;

	/* Set all channels as default priority */
	writel_relaxed(0, sadi->base + REG_ADI_CHN_PRIL);
	writel_relaxed(0, sadi->base + REG_ADI_CHN_PRIH);

	/* Set clock auto gate mode */
	tmp = readl_relaxed(sadi->base + REG_ADI_GSSI_CFG0);
	tmp &= ~BIT_CLK_ALL_ON;
	writel_relaxed(tmp, sadi->base + REG_ADI_GSSI_CFG0);

	/* Set hardware channels setting */
	list = of_get_property(np, "sprd,hw-channels", &size);
	if (!list || !size) {
		dev_info(sadi->dev, "no hw channels setting in node\n");
		return;
	}

	chn_cnt = size / 8;
	for (i = 0; i < chn_cnt; i++) {
		u32 value;
		u32 chn_id = be32_to_cpu(*list++);
		u32 chn_config = be32_to_cpu(*list++);

		/* Channel 0 and 1 are software channels */
		if (chn_id < 2)
			continue;

		writel_relaxed(chn_config, sadi->base +
			       REG_ADI_CHN_ADDR(chn_id));

		if (chn_id < 32) {
			value = readl_relaxed(sadi->base + REG_ADI_CHN_EN);
			value |= BIT(chn_id);
			writel_relaxed(value, sadi->base + REG_ADI_CHN_EN);
		} else if (chn_id < ADI_HW_CHNS) {
			value = readl_relaxed(sadi->base + REG_ADI_CHN_EN1);
			value |= BIT(chn_id - 32);
			writel_relaxed(value, sadi->base + REG_ADI_CHN_EN1);
		}
	}
}

static int sprd_adi_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct spi_controller *ctlr;
	struct sprd_adi *sadi;
	struct resource *res;
	u32 num_chipselect;
	int ret;

	if (!np) {
		dev_err(&pdev->dev, "can not find the adi bus node\n");
		return -ENODEV;
	}

	pdev->id = of_alias_get_id(np, "spi");
	num_chipselect = of_get_child_count(np);

	ctlr = spi_alloc_master(&pdev->dev, sizeof(struct sprd_adi));
	if (!ctlr)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, ctlr);
	sadi = spi_controller_get_devdata(ctlr);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sadi->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(sadi->base)) {
		ret = PTR_ERR(sadi->base);
		goto put_ctlr;
	}

	sadi->slave_vbase = (unsigned long)sadi->base + ADI_SLAVE_OFFSET;
	sadi->slave_pbase = res->start + ADI_SLAVE_OFFSET;
	sadi->ctlr = ctlr;
	sadi->dev = &pdev->dev;
	ret = of_hwspin_lock_get_id(np, 0);
	if (ret > 0 || (IS_ENABLED(CONFIG_HWSPINLOCK) && ret == 0)) {
		sadi->hwlock =
			devm_hwspin_lock_request_specific(&pdev->dev, ret);
		if (!sadi->hwlock) {
			ret = -ENXIO;
			goto put_ctlr;
		}
	} else {
		switch (ret) {
		case -ENOENT:
			dev_info(&pdev->dev, "no hardware spinlock supplied\n");
			break;
		default:
			dev_err(&pdev->dev,
				"failed to find hwlock id, %d\n", ret);
			fallthrough;
		case -EPROBE_DEFER:
			goto put_ctlr;
		}
	}

	sprd_adi_hw_init(sadi);
	sprd_adi_set_wdt_rst_mode(sadi);

	ctlr->dev.of_node = pdev->dev.of_node;
	ctlr->bus_num = pdev->id;
	ctlr->num_chipselect = num_chipselect;
	ctlr->flags = SPI_MASTER_HALF_DUPLEX;
	ctlr->bits_per_word_mask = 0;
	ctlr->transfer_one = sprd_adi_transfer_one;

	ret = devm_spi_register_controller(&pdev->dev, ctlr);
	if (ret) {
		dev_err(&pdev->dev, "failed to register SPI controller\n");
		goto put_ctlr;
	}

	sadi->restart_handler.notifier_call = sprd_adi_restart_handler;
	sadi->restart_handler.priority = 128;
	ret = register_restart_handler(&sadi->restart_handler);
	if (ret) {
		dev_err(&pdev->dev, "can not register restart handler\n");
		goto put_ctlr;
	}

	return 0;

put_ctlr:
	spi_controller_put(ctlr);
	return ret;
}

static int sprd_adi_remove(struct platform_device *pdev)
{
	struct spi_controller *ctlr = dev_get_drvdata(&pdev->dev);
	struct sprd_adi *sadi = spi_controller_get_devdata(ctlr);

	unregister_restart_handler(&sadi->restart_handler);
	return 0;
}

static const struct of_device_id sprd_adi_of_match[] = {
	{
		.compatible = "sprd,sc9860-adi",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, sprd_adi_of_match);

static struct platform_driver sprd_adi_driver = {
	.driver = {
		.name = "sprd-adi",
		.of_match_table = sprd_adi_of_match,
	},
	.probe = sprd_adi_probe,
	.remove = sprd_adi_remove,
};
module_platform_driver(sprd_adi_driver);

MODULE_DESCRIPTION("Spreadtrum ADI Controller Driver");
MODULE_AUTHOR("Baolin Wang <Baolin.Wang@spreadtrum.com>");
MODULE_LICENSE("GPL v2");
