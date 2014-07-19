/*
 * r8a7778 processor support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 * Copyright (C) 2013  Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_data/dma-rcar-hpbdma.h>
#include <linux/platform_data/gpio-rcar.h>
#include <linux/platform_data/irq-renesas-intc-irqpin.h>
#include <linux/platform_device.h>
#include <linux/irqchip.h>
#include <linux/serial_sci.h>
#include <linux/sh_timer.h>
#include <linux/pm_runtime.h>
#include <linux/usb/phy.h>
#include <linux/usb/hcd.h>
#include <linux/usb/ehci_pdriver.h>
#include <linux/usb/ohci_pdriver.h>
#include <linux/dma-mapping.h>
#include <mach/irqs.h>
#include <mach/r8a7778.h>
#include <mach/common.h>
#include <asm/mach/arch.h>
#include <asm/hardware/cache-l2x0.h>

/* SCIF */
#define R8A7778_SCIF(index, baseaddr, irq)			\
static struct plat_sci_port scif##index##_platform_data = {	\
	.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,	\
	.scscr		= SCSCR_RE | SCSCR_TE | SCSCR_CKE1,	\
	.type		= PORT_SCIF,				\
};								\
								\
static struct resource scif##index##_resources[] = {		\
	DEFINE_RES_MEM(baseaddr, 0x100),			\
	DEFINE_RES_IRQ(irq),					\
}

R8A7778_SCIF(0, 0xffe40000, gic_iid(0x66));
R8A7778_SCIF(1, 0xffe41000, gic_iid(0x67));
R8A7778_SCIF(2, 0xffe42000, gic_iid(0x68));
R8A7778_SCIF(3, 0xffe43000, gic_iid(0x69));
R8A7778_SCIF(4, 0xffe44000, gic_iid(0x6a));
R8A7778_SCIF(5, 0xffe45000, gic_iid(0x6b));

#define r8a7778_register_scif(index)					       \
	platform_device_register_resndata(&platform_bus, "sh-sci", index,      \
					  scif##index##_resources,	       \
					  ARRAY_SIZE(scif##index##_resources), \
					  &scif##index##_platform_data,	       \
					  sizeof(scif##index##_platform_data))

/* TMU */
static struct sh_timer_config sh_tmu0_platform_data = {
	.channels_mask = 7,
};

static struct resource sh_tmu0_resources[] = {
	DEFINE_RES_MEM(0xffd80000, 0x30),
	DEFINE_RES_IRQ(gic_iid(0x40)),
	DEFINE_RES_IRQ(gic_iid(0x41)),
	DEFINE_RES_IRQ(gic_iid(0x42)),
};

#define r8a7778_register_tmu(idx)			\
	platform_device_register_resndata(		\
		&platform_bus, "sh-tmu", idx,		\
		sh_tmu##idx##_resources,		\
		ARRAY_SIZE(sh_tmu##idx##_resources),	\
		&sh_tmu##idx##_platform_data,		\
		sizeof(sh_tmu##idx##_platform_data))

int r8a7778_usb_phy_power(bool enable)
{
	static struct usb_phy *phy = NULL;
	int ret = 0;

	if (!phy)
		phy = usb_get_phy(USB_PHY_TYPE_USB2);

	if (IS_ERR(phy)) {
		pr_err("kernel doesn't have usb phy driver\n");
		return PTR_ERR(phy);
	}

	if (enable)
		ret = usb_phy_init(phy);
	else
		usb_phy_shutdown(phy);

	return ret;
}

/* USB */
static int usb_power_on(struct platform_device *pdev)
{
	int ret = r8a7778_usb_phy_power(true);

	if (ret)
		return ret;

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	return 0;
}

static void usb_power_off(struct platform_device *pdev)
{
	if (r8a7778_usb_phy_power(false))
		return;

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
}

static int ehci_init_internal_buffer(struct usb_hcd *hcd)
{
	/*
	 * Below are recommended values from the datasheet;
	 * see [USB :: Setting of EHCI Internal Buffer].
	 */
	/* EHCI IP internal buffer setting */
	iowrite32(0x00ff0040, hcd->regs + 0x0094);
	/* EHCI IP internal buffer enable */
	iowrite32(0x00000001, hcd->regs + 0x009C);

	return 0;
}

static struct usb_ehci_pdata ehci_pdata __initdata = {
	.power_on	= usb_power_on,
	.power_off	= usb_power_off,
	.power_suspend	= usb_power_off,
	.pre_setup	= ehci_init_internal_buffer,
};

static struct resource ehci_resources[] __initdata = {
	DEFINE_RES_MEM(0xffe70000, 0x400),
	DEFINE_RES_IRQ(gic_iid(0x4c)),
};

static struct usb_ohci_pdata ohci_pdata __initdata = {
	.power_on	= usb_power_on,
	.power_off	= usb_power_off,
	.power_suspend	= usb_power_off,
};

static struct resource ohci_resources[] __initdata = {
	DEFINE_RES_MEM(0xffe70400, 0x400),
	DEFINE_RES_IRQ(gic_iid(0x4c)),
};

#define USB_PLATFORM_INFO(hci)					\
static struct platform_device_info hci##_info __initdata = {	\
	.parent		= &platform_bus,			\
	.name		= #hci "-platform",			\
	.id		= -1,					\
	.res		= hci##_resources,			\
	.num_res	= ARRAY_SIZE(hci##_resources),		\
	.data		= &hci##_pdata,				\
	.size_data	= sizeof(hci##_pdata),			\
	.dma_mask	= DMA_BIT_MASK(32),			\
}

USB_PLATFORM_INFO(ehci);
USB_PLATFORM_INFO(ohci);

/* PFC/GPIO */
static struct resource pfc_resources[] __initdata = {
	DEFINE_RES_MEM(0xfffc0000, 0x118),
};

#define R8A7778_GPIO(idx)						\
static struct resource r8a7778_gpio##idx##_resources[] __initdata = {	\
	DEFINE_RES_MEM(0xffc40000 + 0x1000 * (idx), 0x30),		\
	DEFINE_RES_IRQ(gic_iid(0x87)),					\
};									\
									\
static struct gpio_rcar_config r8a7778_gpio##idx##_platform_data __initdata = { \
	.gpio_base	= 32 * (idx),					\
	.irq_base	= GPIO_IRQ_BASE(idx),				\
	.number_of_pins	= 32,						\
	.pctl_name	= "pfc-r8a7778",				\
}

R8A7778_GPIO(0);
R8A7778_GPIO(1);
R8A7778_GPIO(2);
R8A7778_GPIO(3);
R8A7778_GPIO(4);

#define r8a7778_register_gpio(idx)				\
	platform_device_register_resndata(			\
		&platform_bus, "gpio_rcar", idx,		\
		r8a7778_gpio##idx##_resources,			\
		ARRAY_SIZE(r8a7778_gpio##idx##_resources),	\
		&r8a7778_gpio##idx##_platform_data,		\
		sizeof(r8a7778_gpio##idx##_platform_data))

void __init r8a7778_pinmux_init(void)
{
	platform_device_register_simple(
		"pfc-r8a7778", -1,
		pfc_resources,
		ARRAY_SIZE(pfc_resources));

	r8a7778_register_gpio(0);
	r8a7778_register_gpio(1);
	r8a7778_register_gpio(2);
	r8a7778_register_gpio(3);
	r8a7778_register_gpio(4);
};

/* I2C */
static struct resource i2c_resources[] __initdata = {
	/* I2C0 */
	DEFINE_RES_MEM(0xffc70000, 0x1000),
	DEFINE_RES_IRQ(gic_iid(0x63)),
	/* I2C1 */
	DEFINE_RES_MEM(0xffc71000, 0x1000),
	DEFINE_RES_IRQ(gic_iid(0x6e)),
	/* I2C2 */
	DEFINE_RES_MEM(0xffc72000, 0x1000),
	DEFINE_RES_IRQ(gic_iid(0x6c)),
	/* I2C3 */
	DEFINE_RES_MEM(0xffc73000, 0x1000),
	DEFINE_RES_IRQ(gic_iid(0x6d)),
};

static void __init r8a7778_register_i2c(int id)
{
	BUG_ON(id < 0 || id > 3);

	platform_device_register_simple(
		"i2c-rcar", id,
		i2c_resources + (2 * id), 2);
}

/* HSPI */
static struct resource hspi_resources[] __initdata = {
	/* HSPI0 */
	DEFINE_RES_MEM(0xfffc7000, 0x18),
	DEFINE_RES_IRQ(gic_iid(0x5f)),
	/* HSPI1 */
	DEFINE_RES_MEM(0xfffc8000, 0x18),
	DEFINE_RES_IRQ(gic_iid(0x74)),
	/* HSPI2 */
	DEFINE_RES_MEM(0xfffc6000, 0x18),
	DEFINE_RES_IRQ(gic_iid(0x75)),
};

static void __init r8a7778_register_hspi(int id)
{
	BUG_ON(id < 0 || id > 2);

	platform_device_register_simple(
		"sh-hspi", id,
		hspi_resources + (2 * id), 2);
}

void __init r8a7778_add_dt_devices(void)
{
#ifdef CONFIG_CACHE_L2X0
	void __iomem *base = ioremap_nocache(0xf0100000, 0x1000);
	if (base) {
		/*
		 * Shared attribute override enable, 64K*16way
		 * don't call iounmap(base)
		 */
		l2x0_init(base, 0x00400000, 0xc20f0fff);
	}
#endif

	r8a7778_register_tmu(0);
}

/* HPB-DMA */

/* Asynchronous mode register (ASYNCMDR) bits */
#define HPB_DMAE_ASYNCMDR_ASMD22_MASK	BIT(2)	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASMD22_SINGLE	BIT(2)	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASMD22_MULTI	0	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASMD21_MASK	BIT(1)	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASMD21_SINGLE	BIT(1)	/* SDHI0 */
#define HPB_DMAE_ASYNCMDR_ASMD21_MULTI	0	/* SDHI0 */

#define HPBDMA_SSI(_id)				\
{						\
	.id	= HPBDMA_SLAVE_SSI## _id ##_TX,	\
	.addr	= 0xffd91008 + (_id * 0x40),	\
	.dcr	= HPB_DMAE_DCR_CT |		\
		  HPB_DMAE_DCR_DIP |		\
		  HPB_DMAE_DCR_SPDS_32BIT |	\
		  HPB_DMAE_DCR_DMDL |		\
		  HPB_DMAE_DCR_DPDS_32BIT,	\
	.port   = _id + (_id << 8),		\
	.dma_ch = (28 + _id),			\
}, {						\
	.id	= HPBDMA_SLAVE_SSI## _id ##_RX,	\
	.addr	= 0xffd9100c + (_id * 0x40),	\
	.dcr	= HPB_DMAE_DCR_CT |		\
		  HPB_DMAE_DCR_DIP |		\
		  HPB_DMAE_DCR_SMDL |		\
		  HPB_DMAE_DCR_SPDS_32BIT |	\
		  HPB_DMAE_DCR_DPDS_32BIT,	\
	.port   = _id + (_id << 8),		\
	.dma_ch = (28 + _id),			\
}

#define HPBDMA_HPBIF(_id)				\
{							\
	.id	= HPBDMA_SLAVE_HPBIF## _id ##_TX,	\
	.addr	= 0xffda0000 + (_id * 0x1000),		\
	.dcr	= HPB_DMAE_DCR_CT |			\
		  HPB_DMAE_DCR_DIP |			\
		  HPB_DMAE_DCR_SPDS_32BIT |		\
		  HPB_DMAE_DCR_DMDL |			\
		  HPB_DMAE_DCR_DPDS_32BIT,		\
	.port   = 0x1111,				\
	.dma_ch = (28 + _id),				\
}, {							\
	.id	= HPBDMA_SLAVE_HPBIF## _id ##_RX,	\
	.addr	= 0xffda0000 + (_id * 0x1000),		\
	.dcr	= HPB_DMAE_DCR_CT |			\
		  HPB_DMAE_DCR_DIP |			\
		  HPB_DMAE_DCR_SMDL |			\
		  HPB_DMAE_DCR_SPDS_32BIT |		\
		  HPB_DMAE_DCR_DPDS_32BIT,		\
	.port   = 0x1111,				\
	.dma_ch = (28 + _id),				\
}

static const struct hpb_dmae_slave_config hpb_dmae_slaves[] = {
	{
		.id	= HPBDMA_SLAVE_SDHI0_TX,
		.addr	= 0xffe4c000 + 0x30,
		.dcr	= HPB_DMAE_DCR_SPDS_16BIT |
			  HPB_DMAE_DCR_DMDL |
			  HPB_DMAE_DCR_DPDS_16BIT,
		.rstr	= HPB_DMAE_ASYNCRSTR_ASRST21 |
			  HPB_DMAE_ASYNCRSTR_ASRST22 |
			  HPB_DMAE_ASYNCRSTR_ASRST23,
		.mdr	= HPB_DMAE_ASYNCMDR_ASMD21_MULTI,
		.mdm	= HPB_DMAE_ASYNCMDR_ASMD21_MASK,
		.port	= 0x0D0C,
		.flags	= HPB_DMAE_SET_ASYNC_RESET | HPB_DMAE_SET_ASYNC_MODE,
		.dma_ch	= 21,
	}, {
		.id	= HPBDMA_SLAVE_SDHI0_RX,
		.addr	= 0xffe4c000 + 0x30,
		.dcr	= HPB_DMAE_DCR_SMDL |
			  HPB_DMAE_DCR_SPDS_16BIT |
			  HPB_DMAE_DCR_DPDS_16BIT,
		.rstr	= HPB_DMAE_ASYNCRSTR_ASRST21 |
			  HPB_DMAE_ASYNCRSTR_ASRST22 |
			  HPB_DMAE_ASYNCRSTR_ASRST23,
		.mdr	= HPB_DMAE_ASYNCMDR_ASMD22_MULTI,
		.mdm	= HPB_DMAE_ASYNCMDR_ASMD22_MASK,
		.port	= 0x0D0C,
		.flags	= HPB_DMAE_SET_ASYNC_RESET | HPB_DMAE_SET_ASYNC_MODE,
		.dma_ch	= 22,
	}, {
		.id	= HPBDMA_SLAVE_USBFUNC_TX, /* for D0 */
		.addr	= 0xffe60018,
		.dcr	= HPB_DMAE_DCR_SPDS_32BIT |
			  HPB_DMAE_DCR_DMDL |
			  HPB_DMAE_DCR_DPDS_32BIT,
		.port	= 0x0000,
		.dma_ch	= 14,
	}, {
		.id	= HPBDMA_SLAVE_USBFUNC_RX, /* for D1 */
		.addr	= 0xffe6001c,
		.dcr	= HPB_DMAE_DCR_SMDL |
			  HPB_DMAE_DCR_SPDS_32BIT |
			  HPB_DMAE_DCR_DPDS_32BIT,
		.port	= 0x0101,
		.dma_ch	= 15,
	},

	HPBDMA_SSI(0),
	HPBDMA_SSI(1),
	HPBDMA_SSI(2),
	HPBDMA_SSI(3),
	HPBDMA_SSI(4),
	HPBDMA_SSI(5),
	HPBDMA_SSI(6),
	HPBDMA_SSI(7),
	HPBDMA_SSI(8),

	HPBDMA_HPBIF(0),
	HPBDMA_HPBIF(1),
	HPBDMA_HPBIF(2),
	HPBDMA_HPBIF(3),
	HPBDMA_HPBIF(4),
	HPBDMA_HPBIF(5),
	HPBDMA_HPBIF(6),
	HPBDMA_HPBIF(7),
	HPBDMA_HPBIF(8),
};

static const struct hpb_dmae_channel hpb_dmae_channels[] = {
	HPB_DMAE_CHANNEL(0x7c, HPBDMA_SLAVE_USBFUNC_TX), /* ch. 14 */
	HPB_DMAE_CHANNEL(0x7c, HPBDMA_SLAVE_USBFUNC_RX), /* ch. 15 */
	HPB_DMAE_CHANNEL(0x7e, HPBDMA_SLAVE_SDHI0_TX), /* ch. 21 */
	HPB_DMAE_CHANNEL(0x7e, HPBDMA_SLAVE_SDHI0_RX), /* ch. 22 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_SSI0_TX),   /* ch. 28 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_SSI0_RX),   /* ch. 28 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_HPBIF0_TX), /* ch. 28 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_HPBIF0_RX), /* ch. 28 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_SSI1_TX),   /* ch. 29 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_SSI1_RX),   /* ch. 29 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_HPBIF1_TX), /* ch. 29 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_HPBIF1_RX), /* ch. 29 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_SSI2_TX),   /* ch. 30 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_SSI2_RX),   /* ch. 30 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_HPBIF2_TX), /* ch. 30 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_HPBIF2_RX), /* ch. 30 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_SSI3_TX),   /* ch. 31 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_SSI3_RX),   /* ch. 31 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_HPBIF3_TX), /* ch. 31 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_HPBIF3_RX), /* ch. 31 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_SSI4_TX),   /* ch. 32 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_SSI4_RX),   /* ch. 32 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_HPBIF4_TX), /* ch. 32 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_HPBIF4_RX), /* ch. 32 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_SSI5_TX),   /* ch. 33 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_SSI5_RX),   /* ch. 33 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_HPBIF5_TX), /* ch. 33 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_HPBIF5_RX), /* ch. 33 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_SSI6_TX),   /* ch. 34 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_SSI6_RX),   /* ch. 34 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_HPBIF6_TX), /* ch. 34 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_HPBIF6_RX), /* ch. 34 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_SSI7_TX),   /* ch. 35 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_SSI7_RX),   /* ch. 35 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_HPBIF7_TX), /* ch. 35 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_HPBIF7_RX), /* ch. 35 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_SSI8_TX),   /* ch. 36 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_SSI8_RX),   /* ch. 36 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_HPBIF8_TX), /* ch. 36 */
	HPB_DMAE_CHANNEL(0x7f, HPBDMA_SLAVE_HPBIF8_RX), /* ch. 36 */
};

static struct hpb_dmae_pdata dma_platform_data __initdata = {
	.slaves			= hpb_dmae_slaves,
	.num_slaves		= ARRAY_SIZE(hpb_dmae_slaves),
	.channels		= hpb_dmae_channels,
	.num_channels		= ARRAY_SIZE(hpb_dmae_channels),
	.ts_shift		= {
		[XMIT_SZ_8BIT]	= 0,
		[XMIT_SZ_16BIT]	= 1,
		[XMIT_SZ_32BIT]	= 2,
	},
	.num_hw_channels	= 39,
};

static struct resource hpb_dmae_resources[] __initdata = {
	/* Channel registers */
	DEFINE_RES_MEM(0xffc08000, 0x1000),
	/* Common registers */
	DEFINE_RES_MEM(0xffc09000, 0x170),
	/* Asynchronous reset registers */
	DEFINE_RES_MEM(0xffc00300, 4),
	/* Asynchronous mode registers */
	DEFINE_RES_MEM(0xffc00400, 4),
	/* IRQ for DMA channels */
	DEFINE_RES_NAMED(gic_iid(0x7b), 5, NULL, IORESOURCE_IRQ),
};

static void __init r8a7778_register_hpb_dmae(void)
{
	platform_device_register_resndata(&platform_bus, "hpb-dma-engine", -1,
					  hpb_dmae_resources,
					  ARRAY_SIZE(hpb_dmae_resources),
					  &dma_platform_data,
					  sizeof(dma_platform_data));
}

void __init r8a7778_add_standard_devices(void)
{
	r8a7778_add_dt_devices();
	r8a7778_register_scif(0);
	r8a7778_register_scif(1);
	r8a7778_register_scif(2);
	r8a7778_register_scif(3);
	r8a7778_register_scif(4);
	r8a7778_register_scif(5);
	r8a7778_register_i2c(0);
	r8a7778_register_i2c(1);
	r8a7778_register_i2c(2);
	r8a7778_register_i2c(3);
	r8a7778_register_hspi(0);
	r8a7778_register_hspi(1);
	r8a7778_register_hspi(2);

	r8a7778_register_hpb_dmae();
}

void __init r8a7778_init_late(void)
{
	platform_device_register_full(&ehci_info);
	platform_device_register_full(&ohci_info);
}

static struct renesas_intc_irqpin_config irqpin_platform_data __initdata = {
	.irq_base = irq_pin(0), /* IRQ0 -> IRQ3 */
	.sense_bitfield_width = 2,
};

static struct resource irqpin_resources[] __initdata = {
	DEFINE_RES_MEM(0xfe78001c, 4), /* ICR1 */
	DEFINE_RES_MEM(0xfe780010, 4), /* INTPRI */
	DEFINE_RES_MEM(0xfe780024, 4), /* INTREQ */
	DEFINE_RES_MEM(0xfe780044, 4), /* INTMSK0 */
	DEFINE_RES_MEM(0xfe780064, 4), /* INTMSKCLR0 */
	DEFINE_RES_IRQ(gic_iid(0x3b)), /* IRQ0 */
	DEFINE_RES_IRQ(gic_iid(0x3c)), /* IRQ1 */
	DEFINE_RES_IRQ(gic_iid(0x3d)), /* IRQ2 */
	DEFINE_RES_IRQ(gic_iid(0x3e)), /* IRQ3 */
};

void __init r8a7778_init_irq_extpin_dt(int irlm)
{
	void __iomem *icr0 = ioremap_nocache(0xfe780000, PAGE_SIZE);
	unsigned long tmp;

	if (!icr0) {
		pr_warn("r8a7778: unable to setup external irq pin mode\n");
		return;
	}

	tmp = ioread32(icr0);
	if (irlm)
		tmp |= 1 << 23; /* IRQ0 -> IRQ3 as individual pins */
	else
		tmp &= ~(1 << 23); /* IRL mode - not supported */
	tmp |= (1 << 21); /* LVLMODE = 1 */
	iowrite32(tmp, icr0);
	iounmap(icr0);
}

void __init r8a7778_init_irq_extpin(int irlm)
{
	r8a7778_init_irq_extpin_dt(irlm);
	if (irlm)
		platform_device_register_resndata(
			&platform_bus, "renesas_intc_irqpin", -1,
			irqpin_resources, ARRAY_SIZE(irqpin_resources),
			&irqpin_platform_data, sizeof(irqpin_platform_data));
}

void __init r8a7778_init_delay(void)
{
	shmobile_setup_delay(800, 1, 3); /* Cortex-A9 @ 800MHz */
}

#ifdef CONFIG_USE_OF
#define INT2SMSKCR0	0x82288 /* 0xfe782288 */
#define INT2SMSKCR1	0x8228c /* 0xfe78228c */

#define INT2NTSR0	0x00018 /* 0xfe700018 */
#define INT2NTSR1	0x0002c /* 0xfe70002c */
void __init r8a7778_init_irq_dt(void)
{
	void __iomem *base = ioremap_nocache(0xfe700000, 0x00100000);

	BUG_ON(!base);

	irqchip_init();

	/* route all interrupts to ARM */
	__raw_writel(0x73ffffff, base + INT2NTSR0);
	__raw_writel(0xffffffff, base + INT2NTSR1);

	/* unmask all known interrupts in INTCS2 */
	__raw_writel(0x08330773, base + INT2SMSKCR0);
	__raw_writel(0x00311110, base + INT2SMSKCR1);

	iounmap(base);
}

static const char *r8a7778_compat_dt[] __initdata = {
	"renesas,r8a7778",
	NULL,
};

DT_MACHINE_START(R8A7778_DT, "Generic R8A7778 (Flattened Device Tree)")
	.init_early	= r8a7778_init_delay,
	.init_irq	= r8a7778_init_irq_dt,
	.dt_compat	= r8a7778_compat_dt,
	.init_late      = r8a7778_init_late,
MACHINE_END

#endif /* CONFIG_USE_OF */
