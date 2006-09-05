/*
 * PQ2 Device descriptions
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <asm/cpm2.h>
#include <asm/irq.h>
#include <asm/ppc_sys.h>
#include <asm/machdep.h>

struct platform_device ppc_sys_platform_devices[] = {
	[MPC82xx_CPM_FCC1] = {
		.name = "fsl-cpm-fcc",
		.id	= 1,
		.num_resources	 = 3,
		.resource = (struct resource[]) {
			{
				.name	= "fcc_regs",
				.start	= 0x11300,
				.end	= 0x1131f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "fcc_pram",
				.start	= 0x8400,
				.end	= 0x84ff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_FCC1,
				.end	= SIU_INT_FCC1,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC82xx_CPM_FCC2] = {
		.name = "fsl-cpm-fcc",
		.id	= 2,
		.num_resources	 = 3,
		.resource = (struct resource[]) {
			{
				.name	= "fcc_regs",
				.start	= 0x11320,
				.end	= 0x1133f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "fcc_pram",
				.start	= 0x8500,
				.end	= 0x85ff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_FCC2,
				.end	= SIU_INT_FCC2,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC82xx_CPM_FCC3] = {
		.name = "fsl-cpm-fcc",
		.id	= 3,
		.num_resources	 = 3,
		.resource = (struct resource[]) {
			{
				.name	= "fcc_regs",
				.start	= 0x11340,
				.end	= 0x1135f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "fcc_pram",
				.start	= 0x8600,
				.end	= 0x86ff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_FCC3,
				.end	= SIU_INT_FCC3,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC82xx_CPM_I2C] = {
		.name = "fsl-cpm-i2c",
		.id	= 1,
		.num_resources	 = 3,
		.resource = (struct resource[]) {
			{
				.name	= "i2c_mem",
				.start	= 0x11860,
				.end	= 0x118BF,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "i2c_pram",
				.start 	= 0x8afc,
				.end	= 0x8afd,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_I2C,
				.end	= SIU_INT_I2C,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC82xx_CPM_SCC1] = {
		.name = "fsl-cpm-scc",
		.id	= 1,
		.num_resources	 = 3,
		.resource = (struct resource[]) {
			{
				.name	= "regs",
				.start	= 0x11A00,
				.end	= 0x11A1F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "pram",
				.start	= 0x8000,
				.end	= 0x80ff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_SCC1,
				.end	= SIU_INT_SCC1,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC82xx_CPM_SCC2] = {
		.name = "fsl-cpm-scc",
		.id	= 2,
		.num_resources	 = 3,
		.resource = (struct resource[]) {
			{
				.name	= "regs",
				.start	= 0x11A20,
				.end	= 0x11A3F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "pram",
				.start	= 0x8100,
				.end	= 0x81ff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_SCC2,
				.end	= SIU_INT_SCC2,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC82xx_CPM_SCC3] = {
		.name = "fsl-cpm-scc",
		.id	= 3,
		.num_resources	 = 3,
		.resource = (struct resource[]) {
			{
				.name 	= "regs",
				.start	= 0x11A40,
				.end	= 0x11A5F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "pram",
				.start	= 0x8200,
				.end	= 0x82ff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_SCC3,
				.end	= SIU_INT_SCC3,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC82xx_CPM_SCC4] = {
		.name = "fsl-cpm-scc",
		.id	= 4,
		.num_resources	 = 3,
		.resource = (struct resource[]) {
			{
				.name	= "regs",
				.start	= 0x11A60,
				.end	= 0x11A7F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "pram",
				.start	= 0x8300,
				.end	= 0x83ff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_SCC4,
				.end	= SIU_INT_SCC4,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC82xx_CPM_SPI] = {
		.name = "fsl-cpm-spi",
		.id	= 1,
		.num_resources	 = 3,
		.resource = (struct resource[]) {
			{
				.name	= "spi_mem",
				.start	= 0x11AA0,
				.end	= 0x11AFF,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "spi_pram",
				.start	= 0x89fc,
				.end	= 0x89fd,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_SPI,
				.end	= SIU_INT_SPI,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC82xx_CPM_MCC1] = {
		.name = "fsl-cpm-mcc",
		.id	= 1,
		.num_resources	 = 3,
		.resource = (struct resource[]) {
			{
				.name	= "mcc_mem",
				.start	= 0x11B30,
				.end	= 0x11B3F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "mcc_pram",
				.start	= 0x8700,
				.end	= 0x877f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_MCC1,
				.end	= SIU_INT_MCC1,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC82xx_CPM_MCC2] = {
		.name = "fsl-cpm-mcc",
		.id	= 2,
		.num_resources	 = 3,
		.resource = (struct resource[]) {
			{
				.name	= "mcc_mem",
				.start	= 0x11B50,
				.end	= 0x11B5F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "mcc_pram",
				.start	= 0x8800,
				.end	= 0x887f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_MCC2,
				.end	= SIU_INT_MCC2,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC82xx_CPM_SMC1] = {
		.name = "fsl-cpm-smc",
		.id	= 1,
		.num_resources	 = 3,
		.resource = (struct resource[]) {
			{
				.name	= "smc_mem",
				.start	= 0x11A80,
				.end	= 0x11A8F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "smc_pram",
				.start	= 0x87fc,
				.end	= 0x87fd,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_SMC1,
				.end	= SIU_INT_SMC1,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC82xx_CPM_SMC2] = {
		.name = "fsl-cpm-smc",
		.id	= 2,
		.num_resources	 = 3,
		.resource = (struct resource[]) {
			{
				.name	= "smc_mem",
				.start	= 0x11A90,
				.end	= 0x11A9F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "smc_pram",
				.start	= 0x88fc,
				.end	= 0x88fd,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_SMC2,
				.end	= SIU_INT_SMC2,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC82xx_CPM_USB] = {
		.name = "fsl-cpm-usb",
		.id	= 1,
		.num_resources	= 3,
		.resource = (struct resource[]) {
			{
				.name	= "usb_mem",
				.start	= 0x11b60,
				.end	= 0x11b78,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "usb_pram",
				.start	= 0x8b00,
				.end	= 0x8bff,
				.flags 	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_USB,
				.end	= SIU_INT_USB,
				.flags	= IORESOURCE_IRQ,
			},

		},
	},
	[MPC82xx_SEC1] = {
		.name = "fsl-sec",
		.id = 1,
		.num_resources = 1,
		.resource = (struct resource[]) {
			{
				.name	= "sec_mem",
				.start	= 0x40000,
				.end	= 0x52fff,
				.flags	= IORESOURCE_MEM,
			},
		},
	},
	[MPC82xx_MDIO_BB] = {
		.name = "fsl-bb-mdio",
		.id = 0,
		.num_resources = 0,
	},
};

static int __init mach_mpc82xx_fixup(struct platform_device *pdev)
{
	ppc_sys_fixup_mem_resource(pdev, CPM_MAP_ADDR);
	return 0;
}

static int __init mach_mpc82xx_init(void)
{
	if (ppc_md.progress)
		ppc_md.progress("mach_mpc82xx_init:enter", 0);
	ppc_sys_device_fixup = mach_mpc82xx_fixup;
	return 0;
}

postcore_initcall(mach_mpc82xx_init);
