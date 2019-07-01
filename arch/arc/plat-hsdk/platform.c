// SPDX-License-Identifier: GPL-2.0-only
/*
 * ARC HSDK Platform support code
 *
 * Copyright (C) 2017 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <asm/arcregs.h>
#include <asm/io.h>
#include <asm/mach_desc.h>

#define ARC_CCM_UNUSED_ADDR	0x60000000

static void __init hsdk_init_per_cpu(unsigned int cpu)
{
	/*
	 * By default ICCM is mapped to 0x7z while this area is used for
	 * kernel virtual mappings, so move it to currently unused area.
	 */
	if (cpuinfo_arc700[cpu].iccm.sz)
		write_aux_reg(ARC_REG_AUX_ICCM, ARC_CCM_UNUSED_ADDR);

	/*
	 * By default DCCM is mapped to 0x8z while this area is used by kernel,
	 * so move it to currently unused area.
	 */
	if (cpuinfo_arc700[cpu].dccm.sz)
		write_aux_reg(ARC_REG_AUX_DCCM, ARC_CCM_UNUSED_ADDR);
}

#define ARC_PERIPHERAL_BASE	0xf0000000
#define CREG_BASE		(ARC_PERIPHERAL_BASE + 0x1000)

#define SDIO_BASE		(ARC_PERIPHERAL_BASE + 0xA000)
#define SDIO_UHS_REG_EXT	(SDIO_BASE + 0x108)
#define SDIO_UHS_REG_EXT_DIV_2	(2 << 30)

#define HSDK_GPIO_INTC          (ARC_PERIPHERAL_BASE + 0x3000)

static void __init hsdk_enable_gpio_intc_wire(void)
{
	/*
	 * Peripherals on CPU Card are wired to cpu intc via intermediate
	 * DW APB GPIO blocks (mainly for debouncing)
	 *
	 *         ---------------------
	 *        |  snps,archs-intc  |
	 *        ---------------------
	 *                  |
	 *        ----------------------
	 *        | snps,archs-idu-intc |
	 *        ----------------------
	 *         |   |     |   |    |
	 *         | [eth] [USB]    [... other peripherals]
	 *         |
	 * -------------------
	 * | snps,dw-apb-intc |
	 * -------------------
	 *  |      |   |   |
	 * [Bt] [HAPS]   [... other peripherals]
	 *
	 * Current implementation of "irq-dw-apb-ictl" driver doesn't work well
	 * with stacked INTCs. In particular problem happens if its master INTC
	 * not yet instantiated. See discussion here -
	 * https://lkml.org/lkml/2015/3/4/755
	 *
	 * So setup the first gpio block as a passive pass thru and hide it from
	 * DT hardware topology - connect intc directly to cpu intc
	 * The GPIO "wire" needs to be init nevertheless (here)
	 *
	 * One side adv is that peripheral interrupt handling avoids one nested
	 * intc ISR hop
	 *
	 * According to HSDK User's Manual [1], "Table 2 Interrupt Mapping"
	 * we have the following GPIO input lines used as sources of interrupt:
	 * - GPIO[0] - Bluetooth interrupt of RS9113 module
	 * - GPIO[2] - HAPS interrupt (on HapsTrak 3 connector)
	 * - GPIO[3] - Audio codec (MAX9880A) interrupt
	 * - GPIO[8-23] - Available on Arduino and PMOD_x headers
	 * For now there's no use of Arduino and PMOD_x headers in Linux
	 * use-case so we only enable lines 0, 2 and 3.
	 *
	 * [1] https://github.com/foss-for-synopsys-dwc-arc-processors/ARC-Development-Systems-Forum/wiki/docs/ARC_HSDK_User_Guide.pdf
	 */
#define GPIO_INTEN              (HSDK_GPIO_INTC + 0x30)
#define GPIO_INTMASK            (HSDK_GPIO_INTC + 0x34)
#define GPIO_INTTYPE_LEVEL      (HSDK_GPIO_INTC + 0x38)
#define GPIO_INT_POLARITY       (HSDK_GPIO_INTC + 0x3c)
#define GPIO_INT_CONNECTED_MASK	0x0d

	iowrite32(0xffffffff, (void __iomem *) GPIO_INTMASK);
	iowrite32(~GPIO_INT_CONNECTED_MASK, (void __iomem *) GPIO_INTMASK);
	iowrite32(0x00000000, (void __iomem *) GPIO_INTTYPE_LEVEL);
	iowrite32(0xffffffff, (void __iomem *) GPIO_INT_POLARITY);
	iowrite32(GPIO_INT_CONNECTED_MASK, (void __iomem *) GPIO_INTEN);
}

enum hsdk_axi_masters {
	M_HS_CORE = 0,
	M_HS_RTT,
	M_AXI_TUN,
	M_HDMI_VIDEO,
	M_HDMI_AUDIO,
	M_USB_HOST,
	M_ETHERNET,
	M_SDIO,
	M_GPU,
	M_DMAC_0,
	M_DMAC_1,
	M_DVFS
};

#define UPDATE_VAL	1

/*
 * This is modified configuration of AXI bridge. Default settings
 * are specified in "Table 111 CREG Address Decoder register reset values".
 *
 * AXI_M_m_SLV{0|1} - Slave Select register for master 'm'.
 * Possible slaves are:
 *  - 0  => no slave selected
 *  - 1  => DDR controller port #1
 *  - 2  => SRAM controller
 *  - 3  => AXI tunnel
 *  - 4  => EBI controller
 *  - 5  => ROM controller
 *  - 6  => AXI2APB bridge
 *  - 7  => DDR controller port #2
 *  - 8  => DDR controller port #3
 *  - 9  => HS38x4 IOC
 *  - 10 => HS38x4 DMI
 * AXI_M_m_OFFSET{0|1} - Addr Offset register for master 'm'
 *
 * Please read ARC HS Development IC Specification, section 17.2 for more
 * information about apertures configuration.
 *
 * m	master		AXI_M_m_SLV0	AXI_M_m_SLV1	AXI_M_m_OFFSET0	AXI_M_m_OFFSET1
 * 0	HS (CBU)	0x11111111	0x63111111	0xFEDCBA98	0x0E543210
 * 1	HS (RTT)	0x77777777	0x77777777	0xFEDCBA98	0x76543210
 * 2	AXI Tunnel	0x88888888	0x88888888	0xFEDCBA98	0x76543210
 * 3	HDMI-VIDEO	0x77777777	0x77777777	0xFEDCBA98	0x76543210
 * 4	HDMI-ADUIO	0x77777777	0x77777777	0xFEDCBA98	0x76543210
 * 5	USB-HOST	0x77777777	0x77999999	0xFEDCBA98	0x76DCBA98
 * 6	ETHERNET	0x77777777	0x77999999	0xFEDCBA98	0x76DCBA98
 * 7	SDIO		0x77777777	0x77999999	0xFEDCBA98	0x76DCBA98
 * 8	GPU		0x77777777	0x77777777	0xFEDCBA98	0x76543210
 * 9	DMAC (port #1)	0x77777777	0x77777777	0xFEDCBA98	0x76543210
 * 10	DMAC (port #2)	0x77777777	0x77777777	0xFEDCBA98	0x76543210
 * 11	DVFS		0x00000000	0x60000000	0x00000000	0x00000000
 */

#define CREG_AXI_M_SLV0(m)  ((void __iomem *)(CREG_BASE + 0x20 * (m)))
#define CREG_AXI_M_SLV1(m)  ((void __iomem *)(CREG_BASE + 0x20 * (m) + 0x04))
#define CREG_AXI_M_OFT0(m)  ((void __iomem *)(CREG_BASE + 0x20 * (m) + 0x08))
#define CREG_AXI_M_OFT1(m)  ((void __iomem *)(CREG_BASE + 0x20 * (m) + 0x0C))
#define CREG_AXI_M_UPDT(m)  ((void __iomem *)(CREG_BASE + 0x20 * (m) + 0x14))

#define CREG_AXI_M_HS_CORE_BOOT	((void __iomem *)(CREG_BASE + 0x010))

#define CREG_PAE		((void __iomem *)(CREG_BASE + 0x180))
#define CREG_PAE_UPDT		((void __iomem *)(CREG_BASE + 0x194))

static void __init hsdk_init_memory_bridge(void)
{
	u32 reg;

	/*
	 * M_HS_CORE has one unique register - BOOT.
	 * We need to clean boot mirror (BOOT[1:0]) bits in them to avoid first
	 * aperture to be masked by 'boot mirror'.
	 */
	reg = readl(CREG_AXI_M_HS_CORE_BOOT) & (~0x3);
	writel(reg, CREG_AXI_M_HS_CORE_BOOT);
	writel(0x11111111, CREG_AXI_M_SLV0(M_HS_CORE));
	writel(0x63111111, CREG_AXI_M_SLV1(M_HS_CORE));
	writel(0xFEDCBA98, CREG_AXI_M_OFT0(M_HS_CORE));
	writel(0x0E543210, CREG_AXI_M_OFT1(M_HS_CORE));
	writel(UPDATE_VAL, CREG_AXI_M_UPDT(M_HS_CORE));

	writel(0x77777777, CREG_AXI_M_SLV0(M_HS_RTT));
	writel(0x77777777, CREG_AXI_M_SLV1(M_HS_RTT));
	writel(0xFEDCBA98, CREG_AXI_M_OFT0(M_HS_RTT));
	writel(0x76543210, CREG_AXI_M_OFT1(M_HS_RTT));
	writel(UPDATE_VAL, CREG_AXI_M_UPDT(M_HS_RTT));

	writel(0x88888888, CREG_AXI_M_SLV0(M_AXI_TUN));
	writel(0x88888888, CREG_AXI_M_SLV1(M_AXI_TUN));
	writel(0xFEDCBA98, CREG_AXI_M_OFT0(M_AXI_TUN));
	writel(0x76543210, CREG_AXI_M_OFT1(M_AXI_TUN));
	writel(UPDATE_VAL, CREG_AXI_M_UPDT(M_AXI_TUN));

	writel(0x77777777, CREG_AXI_M_SLV0(M_HDMI_VIDEO));
	writel(0x77777777, CREG_AXI_M_SLV1(M_HDMI_VIDEO));
	writel(0xFEDCBA98, CREG_AXI_M_OFT0(M_HDMI_VIDEO));
	writel(0x76543210, CREG_AXI_M_OFT1(M_HDMI_VIDEO));
	writel(UPDATE_VAL, CREG_AXI_M_UPDT(M_HDMI_VIDEO));

	writel(0x77777777, CREG_AXI_M_SLV0(M_HDMI_AUDIO));
	writel(0x77777777, CREG_AXI_M_SLV1(M_HDMI_AUDIO));
	writel(0xFEDCBA98, CREG_AXI_M_OFT0(M_HDMI_AUDIO));
	writel(0x76543210, CREG_AXI_M_OFT1(M_HDMI_AUDIO));
	writel(UPDATE_VAL, CREG_AXI_M_UPDT(M_HDMI_AUDIO));

	writel(0x77777777, CREG_AXI_M_SLV0(M_USB_HOST));
	writel(0x77999999, CREG_AXI_M_SLV1(M_USB_HOST));
	writel(0xFEDCBA98, CREG_AXI_M_OFT0(M_USB_HOST));
	writel(0x76DCBA98, CREG_AXI_M_OFT1(M_USB_HOST));
	writel(UPDATE_VAL, CREG_AXI_M_UPDT(M_USB_HOST));

	writel(0x77777777, CREG_AXI_M_SLV0(M_ETHERNET));
	writel(0x77999999, CREG_AXI_M_SLV1(M_ETHERNET));
	writel(0xFEDCBA98, CREG_AXI_M_OFT0(M_ETHERNET));
	writel(0x76DCBA98, CREG_AXI_M_OFT1(M_ETHERNET));
	writel(UPDATE_VAL, CREG_AXI_M_UPDT(M_ETHERNET));

	writel(0x77777777, CREG_AXI_M_SLV0(M_SDIO));
	writel(0x77999999, CREG_AXI_M_SLV1(M_SDIO));
	writel(0xFEDCBA98, CREG_AXI_M_OFT0(M_SDIO));
	writel(0x76DCBA98, CREG_AXI_M_OFT1(M_SDIO));
	writel(UPDATE_VAL, CREG_AXI_M_UPDT(M_SDIO));

	writel(0x77777777, CREG_AXI_M_SLV0(M_GPU));
	writel(0x77777777, CREG_AXI_M_SLV1(M_GPU));
	writel(0xFEDCBA98, CREG_AXI_M_OFT0(M_GPU));
	writel(0x76543210, CREG_AXI_M_OFT1(M_GPU));
	writel(UPDATE_VAL, CREG_AXI_M_UPDT(M_GPU));

	writel(0x77777777, CREG_AXI_M_SLV0(M_DMAC_0));
	writel(0x77777777, CREG_AXI_M_SLV1(M_DMAC_0));
	writel(0xFEDCBA98, CREG_AXI_M_OFT0(M_DMAC_0));
	writel(0x76543210, CREG_AXI_M_OFT1(M_DMAC_0));
	writel(UPDATE_VAL, CREG_AXI_M_UPDT(M_DMAC_0));

	writel(0x77777777, CREG_AXI_M_SLV0(M_DMAC_1));
	writel(0x77777777, CREG_AXI_M_SLV1(M_DMAC_1));
	writel(0xFEDCBA98, CREG_AXI_M_OFT0(M_DMAC_1));
	writel(0x76543210, CREG_AXI_M_OFT1(M_DMAC_1));
	writel(UPDATE_VAL, CREG_AXI_M_UPDT(M_DMAC_1));

	writel(0x00000000, CREG_AXI_M_SLV0(M_DVFS));
	writel(0x60000000, CREG_AXI_M_SLV1(M_DVFS));
	writel(0x00000000, CREG_AXI_M_OFT0(M_DVFS));
	writel(0x00000000, CREG_AXI_M_OFT1(M_DVFS));
	writel(UPDATE_VAL, CREG_AXI_M_UPDT(M_DVFS));

	/*
	 * PAE remapping for DMA clients does not work due to an RTL bug, so
	 * CREG_PAE register must be programmed to all zeroes, otherwise it
	 * will cause problems with DMA to/from peripherals even if PAE40 is
	 * not used.
	 */
	writel(0x00000000, CREG_PAE);
	writel(UPDATE_VAL, CREG_PAE_UPDT);
}

static void __init hsdk_init_early(void)
{
	hsdk_init_memory_bridge();

	/*
	 * Switch SDIO external ciu clock divider from default div-by-8 to
	 * minimum possible div-by-2.
	 */
	iowrite32(SDIO_UHS_REG_EXT_DIV_2, (void __iomem *) SDIO_UHS_REG_EXT);

	hsdk_enable_gpio_intc_wire();
}

static const char *hsdk_compat[] __initconst = {
	"snps,hsdk",
	NULL,
};

MACHINE_START(SIMULATION, "hsdk")
	.dt_compat	= hsdk_compat,
	.init_early     = hsdk_init_early,
	.init_per_cpu	= hsdk_init_per_cpu,
MACHINE_END
