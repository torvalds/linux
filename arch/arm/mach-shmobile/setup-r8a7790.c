/*
 * r8a7790 processor support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
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

#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/platform_data/gpio-rcar.h>
#include <linux/platform_data/irq-renesas-irqc.h>
#include <linux/serial_sci.h>
#include <linux/sh_dma.h>
#include <linux/sh_timer.h>

#include <asm/mach/arch.h>

#include "common.h"
#include "dma-register.h"
#include "irqs.h"
#include "r8a7790.h"
#include "rcar-gen2.h"

/* Audio-DMAC */
#define AUDIO_DMAC_SLAVE(_id, _addr, t, r)			\
{								\
	.slave_id	= AUDIO_DMAC_SLAVE_## _id ##_TX,	\
	.addr		= _addr + 0x8,				\
	.chcr		= CHCR_TX(XMIT_SZ_32BIT),		\
	.mid_rid	= t,					\
}, {								\
	.slave_id	= AUDIO_DMAC_SLAVE_## _id ##_RX,	\
	.addr		= _addr + 0xc,				\
	.chcr		= CHCR_RX(XMIT_SZ_32BIT),		\
	.mid_rid	= r,					\
}

static const struct sh_dmae_slave_config r8a7790_audio_dmac_slaves[] = {
	AUDIO_DMAC_SLAVE(SSI0, 0xec241000, 0x01, 0x02),
	AUDIO_DMAC_SLAVE(SSI1, 0xec241040, 0x03, 0x04),
	AUDIO_DMAC_SLAVE(SSI2, 0xec241080, 0x05, 0x06),
	AUDIO_DMAC_SLAVE(SSI3, 0xec2410c0, 0x07, 0x08),
	AUDIO_DMAC_SLAVE(SSI4, 0xec241100, 0x09, 0x0a),
	AUDIO_DMAC_SLAVE(SSI5, 0xec241140, 0x0b, 0x0c),
	AUDIO_DMAC_SLAVE(SSI6, 0xec241180, 0x0d, 0x0e),
	AUDIO_DMAC_SLAVE(SSI7, 0xec2411c0, 0x0f, 0x10),
	AUDIO_DMAC_SLAVE(SSI8, 0xec241200, 0x11, 0x12),
	AUDIO_DMAC_SLAVE(SSI9, 0xec241240, 0x13, 0x14),
};

#define DMAE_CHANNEL(a, b)			\
{						\
	.offset		= (a) - 0x20,		\
	.dmars		= (a) - 0x20 + 0x40,	\
	.chclr_bit	= (b),			\
	.chclr_offset	= 0x80 - 0x20,		\
}

static const struct sh_dmae_channel r8a7790_audio_dmac_channels[] = {
	DMAE_CHANNEL(0x8000, 0),
	DMAE_CHANNEL(0x8080, 1),
	DMAE_CHANNEL(0x8100, 2),
	DMAE_CHANNEL(0x8180, 3),
	DMAE_CHANNEL(0x8200, 4),
	DMAE_CHANNEL(0x8280, 5),
	DMAE_CHANNEL(0x8300, 6),
	DMAE_CHANNEL(0x8380, 7),
	DMAE_CHANNEL(0x8400, 8),
	DMAE_CHANNEL(0x8480, 9),
	DMAE_CHANNEL(0x8500, 10),
	DMAE_CHANNEL(0x8580, 11),
	DMAE_CHANNEL(0x8600, 12),
};

static struct sh_dmae_pdata r8a7790_audio_dmac_platform_data = {
	.slave		= r8a7790_audio_dmac_slaves,
	.slave_num	= ARRAY_SIZE(r8a7790_audio_dmac_slaves),
	.channel	= r8a7790_audio_dmac_channels,
	.channel_num	= ARRAY_SIZE(r8a7790_audio_dmac_channels),
	.ts_low_shift	= TS_LOW_SHIFT,
	.ts_low_mask	= TS_LOW_BIT << TS_LOW_SHIFT,
	.ts_high_shift	= TS_HI_SHIFT,
	.ts_high_mask	= TS_HI_BIT << TS_HI_SHIFT,
	.ts_shift	= dma_ts_shift,
	.ts_shift_num	= ARRAY_SIZE(dma_ts_shift),
	.dmaor_init	= DMAOR_DME,
	.chclr_present	= 1,
	.chclr_bitwise	= 1,
};

static struct resource r8a7790_audio_dmac_resources[] = {
	/* Channel registers and DMAOR for low */
	DEFINE_RES_MEM(0xec700020, 0x8663 - 0x20),
	DEFINE_RES_IRQ(gic_spi(346)),
	DEFINE_RES_NAMED(gic_spi(320), 13, NULL, IORESOURCE_IRQ),

	/* Channel registers and DMAOR for hi */
	DEFINE_RES_MEM(0xec720020, 0x8663 - 0x20), /* hi */
	DEFINE_RES_IRQ(gic_spi(347)),
	DEFINE_RES_NAMED(gic_spi(333), 13, NULL, IORESOURCE_IRQ),
};

#define r8a7790_register_audio_dmac(id)				\
	platform_device_register_resndata(			\
		NULL, "sh-dma-engine", id,			\
		&r8a7790_audio_dmac_resources[id * 3],	3,	\
		&r8a7790_audio_dmac_platform_data,		\
		sizeof(r8a7790_audio_dmac_platform_data))

static const struct resource pfc_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6060000, 0x250),
};

#define r8a7790_register_pfc()						\
	platform_device_register_simple("pfc-r8a7790", -1, pfc_resources, \
					ARRAY_SIZE(pfc_resources))

#define R8A7790_GPIO(idx)						\
static const struct resource r8a7790_gpio##idx##_resources[] __initconst = { \
	DEFINE_RES_MEM(0xe6050000 + 0x1000 * (idx), 0x50),		\
	DEFINE_RES_IRQ(gic_spi(4 + (idx))),				\
};									\
									\
static const struct gpio_rcar_config					\
r8a7790_gpio##idx##_platform_data __initconst = {			\
	.gpio_base	= 32 * (idx),					\
	.irq_base	= 0,						\
	.number_of_pins	= 32,						\
	.pctl_name	= "pfc-r8a7790",				\
	.has_both_edge_trigger = 1,					\
};									\

R8A7790_GPIO(0);
R8A7790_GPIO(1);
R8A7790_GPIO(2);
R8A7790_GPIO(3);
R8A7790_GPIO(4);
R8A7790_GPIO(5);

#define r8a7790_register_gpio(idx)					\
	platform_device_register_resndata(NULL, "gpio_rcar", idx,	\
		r8a7790_gpio##idx##_resources,				\
		ARRAY_SIZE(r8a7790_gpio##idx##_resources),		\
		&r8a7790_gpio##idx##_platform_data,			\
		sizeof(r8a7790_gpio##idx##_platform_data))

static struct resource i2c_resources[] __initdata = {
	/* I2C0 */
	DEFINE_RES_MEM(0xE6508000, 0x40),
	DEFINE_RES_IRQ(gic_spi(287)),
	/* I2C1 */
	DEFINE_RES_MEM(0xE6518000, 0x40),
	DEFINE_RES_IRQ(gic_spi(288)),
	/* I2C2 */
	DEFINE_RES_MEM(0xE6530000, 0x40),
	DEFINE_RES_IRQ(gic_spi(286)),
	/* I2C3 */
	DEFINE_RES_MEM(0xE6540000, 0x40),
	DEFINE_RES_IRQ(gic_spi(290)),

};

#define r8a7790_register_i2c(idx)		\
	platform_device_register_simple(	\
		"i2c-rcar_gen2", idx,		\
		i2c_resources + (2 * idx), 2);	\

void __init r8a7790_pinmux_init(void)
{
	r8a7790_register_pfc();
	r8a7790_register_gpio(0);
	r8a7790_register_gpio(1);
	r8a7790_register_gpio(2);
	r8a7790_register_gpio(3);
	r8a7790_register_gpio(4);
	r8a7790_register_gpio(5);
}

#define __R8A7790_SCIF(scif_type, _scscr, index, baseaddr, irq)		\
static struct plat_sci_port scif##index##_platform_data = {		\
	.type		= scif_type,					\
	.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,		\
	.scscr		= _scscr,					\
};									\
									\
static struct resource scif##index##_resources[] = {			\
	DEFINE_RES_MEM(baseaddr, 0x100),				\
	DEFINE_RES_IRQ(irq),						\
}

#define R8A7790_SCIF(index, baseaddr, irq)				\
	__R8A7790_SCIF(PORT_SCIF, SCSCR_RE | SCSCR_TE,			\
		       index, baseaddr, irq)

#define R8A7790_SCIFA(index, baseaddr, irq)				\
	__R8A7790_SCIF(PORT_SCIFA, SCSCR_RE | SCSCR_TE | SCSCR_CKE0,	\
		       index, baseaddr, irq)

#define R8A7790_SCIFB(index, baseaddr, irq)				\
	__R8A7790_SCIF(PORT_SCIFB, SCSCR_RE | SCSCR_TE,			\
		       index, baseaddr, irq)

#define R8A7790_HSCIF(index, baseaddr, irq)				\
	__R8A7790_SCIF(PORT_HSCIF, SCSCR_RE | SCSCR_TE,			\
		       index, baseaddr, irq)

R8A7790_SCIFA(0, 0xe6c40000, gic_spi(144)); /* SCIFA0 */
R8A7790_SCIFA(1, 0xe6c50000, gic_spi(145)); /* SCIFA1 */
R8A7790_SCIFB(2, 0xe6c20000, gic_spi(148)); /* SCIFB0 */
R8A7790_SCIFB(3, 0xe6c30000, gic_spi(149)); /* SCIFB1 */
R8A7790_SCIFB(4, 0xe6ce0000, gic_spi(150)); /* SCIFB2 */
R8A7790_SCIFA(5, 0xe6c60000, gic_spi(151)); /* SCIFA2 */
R8A7790_SCIF(6,  0xe6e60000, gic_spi(152)); /* SCIF0 */
R8A7790_SCIF(7,  0xe6e68000, gic_spi(153)); /* SCIF1 */
R8A7790_HSCIF(8, 0xe62c0000, gic_spi(154)); /* HSCIF0 */
R8A7790_HSCIF(9, 0xe62c8000, gic_spi(155)); /* HSCIF1 */

#define r8a7790_register_scif(index)					       \
	platform_device_register_resndata(NULL, "sh-sci", index,  	       \
					  scif##index##_resources,	       \
					  ARRAY_SIZE(scif##index##_resources), \
					  &scif##index##_platform_data,	       \
					  sizeof(scif##index##_platform_data))

static const struct renesas_irqc_config irqc0_data __initconst = {
	.irq_base = irq_pin(0), /* IRQ0 -> IRQ3 */
};

static const struct resource irqc0_resources[] __initconst = {
	DEFINE_RES_MEM(0xe61c0000, 0x200), /* IRQC Event Detector Block_0 */
	DEFINE_RES_IRQ(gic_spi(0)), /* IRQ0 */
	DEFINE_RES_IRQ(gic_spi(1)), /* IRQ1 */
	DEFINE_RES_IRQ(gic_spi(2)), /* IRQ2 */
	DEFINE_RES_IRQ(gic_spi(3)), /* IRQ3 */
};

#define r8a7790_register_irqc(idx)					\
	platform_device_register_resndata(NULL, "renesas_irqc",		\
					  idx, irqc##idx##_resources,	\
					  ARRAY_SIZE(irqc##idx##_resources), \
					  &irqc##idx##_data,		\
					  sizeof(struct renesas_irqc_config))

static const struct resource thermal_resources[] __initconst = {
	DEFINE_RES_MEM(0xe61f0000, 0x14),
	DEFINE_RES_MEM(0xe61f0100, 0x38),
	DEFINE_RES_IRQ(gic_spi(69)),
};

#define r8a7790_register_thermal()					\
	platform_device_register_simple("rcar_thermal", -1,		\
					thermal_resources,		\
					ARRAY_SIZE(thermal_resources))

static struct sh_timer_config cmt0_platform_data = {
	.channels_mask = 0x60,
};

static struct resource cmt0_resources[] = {
	DEFINE_RES_MEM(0xffca0000, 0x1004),
	DEFINE_RES_IRQ(gic_spi(142)),
};

#define r8a7790_register_cmt(idx)					\
	platform_device_register_resndata(NULL, "sh-cmt-48-gen2",	\
					  idx, cmt##idx##_resources,	\
					  ARRAY_SIZE(cmt##idx##_resources), \
					  &cmt##idx##_platform_data,	\
					  sizeof(struct sh_timer_config))

void __init r8a7790_add_dt_devices(void)
{
	r8a7790_register_cmt(0);
}

void __init r8a7790_add_standard_devices(void)
{
	r8a7790_register_scif(0);
	r8a7790_register_scif(1);
	r8a7790_register_scif(2);
	r8a7790_register_scif(3);
	r8a7790_register_scif(4);
	r8a7790_register_scif(5);
	r8a7790_register_scif(6);
	r8a7790_register_scif(7);
	r8a7790_register_scif(8);
	r8a7790_register_scif(9);
	r8a7790_add_dt_devices();
	r8a7790_register_irqc(0);
	r8a7790_register_thermal();
	r8a7790_register_i2c(0);
	r8a7790_register_i2c(1);
	r8a7790_register_i2c(2);
	r8a7790_register_i2c(3);
	r8a7790_register_audio_dmac(0);
	r8a7790_register_audio_dmac(1);
}

#ifdef CONFIG_USE_OF

static const char * const r8a7790_boards_compat_dt[] __initconst = {
	"renesas,r8a7790",
	NULL,
};

DT_MACHINE_START(R8A7790_DT, "Generic R8A7790 (Flattened Device Tree)")
	.smp		= smp_ops(r8a7790_smp_ops),
	.init_early	= shmobile_init_delay,
	.init_time	= rcar_gen2_timer_init,
	.init_late	= shmobile_init_late,
	.reserve	= rcar_gen2_reserve,
	.dt_compat	= r8a7790_boards_compat_dt,
MACHINE_END
#endif /* CONFIG_USE_OF */
