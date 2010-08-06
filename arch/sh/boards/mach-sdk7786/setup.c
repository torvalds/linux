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
#include <linux/clk.h>
#include <mach/fpga.h>
#include <mach/irq.h>
#include <asm/machvec.h>
#include <asm/heartbeat.h>
#include <asm/sizes.h>
#include <asm/reboot.h>
#include <asm/smp-ops.h>

static struct resource heartbeat_resource = {
	.start		= 0x07fff8b0,
	.end		= 0x07fff8b0 + sizeof(u16) - 1,
	.flags		= IORESOURCE_MEM | IORESOURCE_MEM_16BIT,
};

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,
	.num_resources	= 1,
	.resource	= &heartbeat_resource,
};

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
	&heartbeat_device,
	&smsc911x_device,
	&smbus_fpga_device,
	&smbus_pcie_device,
};

static int sdk7786_i2c_setup(void)
{
	unsigned int tmp;

	/*
	 * Hand over I2C control to the FPGA.
	 */
	tmp = fpga_read_reg(SBCR);
	tmp &= ~SCBR_I2CCEN;
	tmp |= SCBR_I2CMEN;
	fpga_write_reg(tmp, SBCR);

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

static int sdk7786_mode_pins(void)
{
	return fpga_read_reg(MODSWR);
}

static int sdk7786_clk_init(void)
{
	struct clk *clk;
	int ret;

	/*
	 * Only handle the EXTAL case, anyone interfacing a crystal
	 * resonator will need to provide their own input clock.
	 */
	if (test_mode_pin(MODE_PIN9))
		return -EINVAL;

	clk = clk_get(NULL, "extal");
	if (!clk || IS_ERR(clk))
		return PTR_ERR(clk);
	ret = clk_set_rate(clk, 33333333);
	clk_put(clk);

	return ret;
}

static void sdk7786_restart(char *cmd)
{
	fpga_write_reg(0xa5a5, SRSTR);
}

static void sdk7786_power_off(void)
{
	fpga_write_reg(fpga_read_reg(PWRCR) | PWRCR_PDWNREQ, PWRCR);

	/*
	 * It can take up to 20us for the R8C to do its job, back off and
	 * wait a bit until we've been shut off. Even though newer FPGA
	 * versions don't set the ACK bit, the latency issue remains.
	 */
	while ((fpga_read_reg(PWRCR) & PWRCR_PDWNACK) == 0)
		cpu_sleep();
}

/* Initialize the board */
static void __init sdk7786_setup(char **cmdline_p)
{
	pr_info("Renesas Technology Europe SDK7786 support:\n");

	sdk7786_fpga_init();

	pr_info("\tPCB revision:\t%d\n", fpga_read_reg(PCBRR) & 0xf);

	machine_ops.restart = sdk7786_restart;
	pm_power_off = sdk7786_power_off;

	register_smp_ops(&shx3_smp_ops);
}

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_sdk7786 __initmv = {
	.mv_name		= "SDK7786",
	.mv_setup		= sdk7786_setup,
	.mv_mode_pins		= sdk7786_mode_pins,
	.mv_clk_init		= sdk7786_clk_init,
	.mv_init_irq		= sdk7786_init_irq,
};
