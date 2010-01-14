/*
 * Renesas Technology Europe SDK7786 Support.
 *
 * Copyright (C) 2010  Matt Fleming
 * Copyright (C) 2010  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/smsc911x.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <asm/machvec.h>
#include <asm/sizes.h>

static struct resource smsc911x_resources[] = {
	[0] = {
		.name		= "smsc911x-memory",
		.start		= 0x07ffff00,
		.end		= 0x07ffff00 + SZ_256 - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.name		= "smsc911x-irq",
		.start		= evt2irq(0x2c0),
		.end		= evt2irq(0x2c0),
		.flags		= IORESOURCE_IRQ,
	},
};

static struct smsc911x_platform_config smsc911x_config = {
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.flags		= SMSC911X_USE_32BIT,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
};

static struct platform_device smsc911x_device = {
	.name		= "smsc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smsc911x_resources),
	.resource	= smsc911x_resources,
	.dev = {
		.platform_data = &smsc911x_config,
	},
};

static struct resource smbus_fpga_resource = {
	.start		= 0x07fff9e0,
	.end		= 0x07fff9e0 + SZ_32 - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device smbus_fpga_device = {
	.name		= "i2c-sdk7786",
	.id		= 0,
	.num_resources	= 1,
	.resource	= &smbus_fpga_resource,
};

static struct resource smbus_pcie_resource = {
	.start		= 0x07fffc30,
	.end		= 0x07fffc30 + SZ_32 - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device smbus_pcie_device = {
	.name		= "i2c-sdk7786",
	.id		= 1,
	.num_resources	= 1,
	.resource	= &smbus_pcie_resource,
};

static struct i2c_board_info __initdata sdk7786_i2c_devices[] = {
	{
		I2C_BOARD_INFO("max6900", 0x68),
	},
};

static struct platform_device *sh7786_devices[] __initdata = {
	&smsc911x_device,
	&smbus_fpga_device,
	&smbus_pcie_device,
};

#define SBCR_REGS_BASE	0x07fff990

#define SCBR_I2CMEN	(1 << 0)	/* FPGA I2C master enable */
#define SCBR_I2CCEN	(1 << 1)	/* CPU I2C master enable */

static int sdk7786_i2c_setup(void)
{
	void __iomem *sbcr;
	unsigned int tmp;

	sbcr = ioremap_nocache(SBCR_REGS_BASE, SZ_16);

	/*
	 * Hand over I2C control to the FPGA.
	 */
	tmp = ioread16(sbcr);
	tmp &= ~SCBR_I2CCEN;
	tmp |= SCBR_I2CMEN;
	iowrite16(tmp, sbcr);

	iounmap(sbcr);

	return i2c_register_board_info(0, sdk7786_i2c_devices,
				       ARRAY_SIZE(sdk7786_i2c_devices));
}

static int __init sdk7786_devices_setup(void)
{
	int ret;

	ret = platform_add_devices(sh7786_devices, ARRAY_SIZE(sh7786_devices));
	if (unlikely(ret != 0))
		return ret;

	return sdk7786_i2c_setup();
}
__initcall(sdk7786_devices_setup);

#define FPGA_REGS_BASE	0x07fff800
#define FPGA_REGS_SIZE	1152

#define INTASR		0x010
#define INTAMR		0x020
#define INTBSR		0x090
#define INTBMR		0x0a0
#define INTMSR		0x130

#define IASELR1		0x210
#define IASELR2		0x220
#define IASELR3		0x230
#define IASELR4		0x240
#define IASELR5		0x250
#define IASELR6		0x260
#define IASELR7		0x270
#define IASELR8		0x280
#define IASELR9		0x290
#define IASELR10	0x2a0
#define IASELR11	0x2b0
#define IASELR12	0x2c0
#define IASELR13	0x2d0
#define IASELR14	0x2e0
#define IASELR15	0x2f0

static void __iomem *fpga_regs;

static u16 fpga_read_reg(unsigned int reg)
{
	return __raw_readw(fpga_regs + reg);
}

static void fpga_write_reg(u16 val, unsigned int reg)
{
	__raw_writew(val, fpga_regs + reg);
}

enum {
	ATA_IRQ_BIT		= 1,
	SPI_BUSY_BIT		= 2,
	LIRQ5_BIT		= 3,
	LIRQ6_BIT		= 4,
	LIRQ7_BIT		= 5,
	LIRQ8_BIT		= 6,
	KEY_IRQ_BIT		= 7,
	PEN_IRQ_BIT		= 8,
	ETH_IRQ_BIT		= 9,
	RTC_ALARM_BIT		= 10,
	CRYSTAL_FAIL_BIT	= 12,
	ETH_PME_BIT		= 14,
};

static void __init init_sdk7786_IRQ(void)
{
	unsigned int tmp;

	fpga_regs = ioremap_nocache(FPGA_REGS_BASE, FPGA_REGS_SIZE);
	if (!fpga_regs) {
		printk(KERN_ERR "Couldn't map FPGA registers\n");
		return;
	}

	/* Enable priority encoding for all IRLs */
	fpga_write_reg(fpga_read_reg(INTMSR) | 0x0303, INTMSR);

	/* Clear FPGA interrupt status registers */
	fpga_write_reg(0x0000, INTASR);
	fpga_write_reg(0x0000, INTBSR);

	/* Unmask FPGA interrupts */
	tmp = fpga_read_reg(INTAMR);
	tmp &= ~(1 << ETH_IRQ_BIT);
	fpga_write_reg(tmp, INTAMR);

	plat_irq_setup_pins(IRQ_MODE_IRL7654_MASK);
	plat_irq_setup_pins(IRQ_MODE_IRL3210_MASK);
}

/* Initialize the board */
static void __init sdk7786_setup(char **cmdline_p)
{
	printk(KERN_INFO "Renesas Technology Corp. SDK7786 support.\n");
}

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_sdk7786 __initmv = {
	.mv_name		= "SDK7786",
	.mv_setup		= sdk7786_setup,
	.mv_init_irq		= init_sdk7786_IRQ,
};
