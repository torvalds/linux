// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (c) 2004-2005 Simtec Electronics
//	http://www.simtec.co.uk/products/SWLINUX/
//	Ben Dooks <ben@simtec.co.uk>
//
// Common code for S3C24XX machines

#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <clocksource/samsung_pwm.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/platform_data/dma-s3c24xx.h>
#include <linux/dmaengine.h>

#include <mach/hardware.h>
#include <mach/regs-clock.h>
#include <asm/irq.h>
#include <asm/cacheflush.h>
#include <asm/system_info.h>
#include <asm/system_misc.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/regs-gpio.h>
#include <mach/dma.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/cpu-freq.h>
#include <plat/pwm-core.h>

#include "common.h"

/* table of supported CPUs */

static const char name_s3c2410[]  = "S3C2410";
static const char name_s3c2412[]  = "S3C2412";
static const char name_s3c2416[]  = "S3C2416/S3C2450";
static const char name_s3c2440[]  = "S3C2440";
static const char name_s3c2442[]  = "S3C2442";
static const char name_s3c2442b[]  = "S3C2442B";
static const char name_s3c2443[]  = "S3C2443";
static const char name_s3c2410a[] = "S3C2410A";
static const char name_s3c2440a[] = "S3C2440A";

static struct cpu_table cpu_ids[] __initdata = {
	{
		.idcode		= 0x32410000,
		.idmask		= 0xffffffff,
		.map_io		= s3c2410_map_io,
		.init_uarts	= s3c2410_init_uarts,
		.init		= s3c2410_init,
		.name		= name_s3c2410
	},
	{
		.idcode		= 0x32410002,
		.idmask		= 0xffffffff,
		.map_io		= s3c2410_map_io,
		.init_uarts	= s3c2410_init_uarts,
		.init		= s3c2410a_init,
		.name		= name_s3c2410a
	},
	{
		.idcode		= 0x32440000,
		.idmask		= 0xffffffff,
		.map_io		= s3c2440_map_io,
		.init_uarts	= s3c244x_init_uarts,
		.init		= s3c2440_init,
		.name		= name_s3c2440
	},
	{
		.idcode		= 0x32440001,
		.idmask		= 0xffffffff,
		.map_io		= s3c2440_map_io,
		.init_uarts	= s3c244x_init_uarts,
		.init		= s3c2440_init,
		.name		= name_s3c2440a
	},
	{
		.idcode		= 0x32440aaa,
		.idmask		= 0xffffffff,
		.map_io		= s3c2442_map_io,
		.init_uarts	= s3c244x_init_uarts,
		.init		= s3c2442_init,
		.name		= name_s3c2442
	},
	{
		.idcode		= 0x32440aab,
		.idmask		= 0xffffffff,
		.map_io		= s3c2442_map_io,
		.init_uarts	= s3c244x_init_uarts,
		.init		= s3c2442_init,
		.name		= name_s3c2442b
	},
	{
		.idcode		= 0x32412001,
		.idmask		= 0xffffffff,
		.map_io		= s3c2412_map_io,
		.init_uarts	= s3c2412_init_uarts,
		.init		= s3c2412_init,
		.name		= name_s3c2412,
	},
	{			/* a newer version of the s3c2412 */
		.idcode		= 0x32412003,
		.idmask		= 0xffffffff,
		.map_io		= s3c2412_map_io,
		.init_uarts	= s3c2412_init_uarts,
		.init		= s3c2412_init,
		.name		= name_s3c2412,
	},
	{			/* a strange version of the s3c2416 */
		.idcode		= 0x32450003,
		.idmask		= 0xffffffff,
		.map_io		= s3c2416_map_io,
		.init_uarts	= s3c2416_init_uarts,
		.init		= s3c2416_init,
		.name		= name_s3c2416,
	},
	{
		.idcode		= 0x32443001,
		.idmask		= 0xffffffff,
		.map_io		= s3c2443_map_io,
		.init_uarts	= s3c2443_init_uarts,
		.init		= s3c2443_init,
		.name		= name_s3c2443,
	},
};

/* minimal IO mapping */

static struct map_desc s3c_iodesc[] __initdata = {
	IODESC_ENT(GPIO),
	IODESC_ENT(IRQ),
	IODESC_ENT(MEMCTRL),
	IODESC_ENT(UART)
};

/* read cpu identificaiton code */

static unsigned long s3c24xx_read_idcode_v5(void)
{
#if defined(CONFIG_CPU_S3C2416)
	/* s3c2416 is v5, with S3C24XX_GSTATUS1 instead of S3C2412_GSTATUS1 */

	u32 gs = __raw_readl(S3C24XX_GSTATUS1);

	/* test for s3c2416 or similar device */
	if ((gs >> 16) == 0x3245)
		return gs;
#endif

#if defined(CONFIG_CPU_S3C2412)
	return __raw_readl(S3C2412_GSTATUS1);
#else
	return 1UL;	/* don't look like an 2400 */
#endif
}

static unsigned long s3c24xx_read_idcode_v4(void)
{
	return __raw_readl(S3C2410_GSTATUS1);
}

static void s3c24xx_default_idle(void)
{
	unsigned long tmp = 0;
	int i;

	/* idle the system by using the idle mode which will wait for an
	 * interrupt to happen before restarting the system.
	 */

	/* Warning: going into idle state upsets jtag scanning */

	__raw_writel(__raw_readl(S3C2410_CLKCON) | S3C2410_CLKCON_IDLE,
		     S3C2410_CLKCON);

	/* the samsung port seems to do a loop and then unset idle.. */
	for (i = 0; i < 50; i++)
		tmp += __raw_readl(S3C2410_CLKCON); /* ensure loop not optimised out */

	/* this bit is not cleared on re-start... */

	__raw_writel(__raw_readl(S3C2410_CLKCON) & ~S3C2410_CLKCON_IDLE,
		     S3C2410_CLKCON);
}

static struct samsung_pwm_variant s3c24xx_pwm_variant = {
	.bits		= 16,
	.div_base	= 1,
	.has_tint_cstat	= false,
	.tclk_mask	= (1 << 4),
};

void __init s3c24xx_init_io(struct map_desc *mach_desc, int size)
{
	arm_pm_idle = s3c24xx_default_idle;

	/* initialise the io descriptors we need for initialisation */
	iotable_init(mach_desc, size);
	iotable_init(s3c_iodesc, ARRAY_SIZE(s3c_iodesc));

	if (cpu_architecture() >= CPU_ARCH_ARMv5) {
		samsung_cpu_id = s3c24xx_read_idcode_v5();
	} else {
		samsung_cpu_id = s3c24xx_read_idcode_v4();
	}

	s3c_init_cpu(samsung_cpu_id, cpu_ids, ARRAY_SIZE(cpu_ids));

	samsung_pwm_set_platdata(&s3c24xx_pwm_variant);
}

void __init samsung_set_timer_source(unsigned int event, unsigned int source)
{
	s3c24xx_pwm_variant.output_mask = BIT(SAMSUNG_PWM_NUM) - 1;
	s3c24xx_pwm_variant.output_mask &= ~(BIT(event) | BIT(source));
}

void __init samsung_timer_init(void)
{
	unsigned int timer_irqs[SAMSUNG_PWM_NUM] = {
		IRQ_TIMER0, IRQ_TIMER1, IRQ_TIMER2, IRQ_TIMER3, IRQ_TIMER4,
	};

	samsung_pwm_clocksource_init(S3C_VA_TIMER,
					timer_irqs, &s3c24xx_pwm_variant);
}

/* Serial port registrations */

#define S3C2410_PA_UART0      (S3C24XX_PA_UART)
#define S3C2410_PA_UART1      (S3C24XX_PA_UART + 0x4000 )
#define S3C2410_PA_UART2      (S3C24XX_PA_UART + 0x8000 )
#define S3C2443_PA_UART3      (S3C24XX_PA_UART + 0xC000 )

static struct resource s3c2410_uart0_resource[] = {
	[0] = DEFINE_RES_MEM(S3C2410_PA_UART0, SZ_16K),
	[1] = DEFINE_RES_NAMED(IRQ_S3CUART_RX0, \
			IRQ_S3CUART_ERR0 - IRQ_S3CUART_RX0 + 1, \
			NULL, IORESOURCE_IRQ)
};

static struct resource s3c2410_uart1_resource[] = {
	[0] = DEFINE_RES_MEM(S3C2410_PA_UART1, SZ_16K),
	[1] = DEFINE_RES_NAMED(IRQ_S3CUART_RX1, \
			IRQ_S3CUART_ERR1 - IRQ_S3CUART_RX1 + 1, \
			NULL, IORESOURCE_IRQ)
};

static struct resource s3c2410_uart2_resource[] = {
	[0] = DEFINE_RES_MEM(S3C2410_PA_UART2, SZ_16K),
	[1] = DEFINE_RES_NAMED(IRQ_S3CUART_RX2, \
			IRQ_S3CUART_ERR2 - IRQ_S3CUART_RX2 + 1, \
			NULL, IORESOURCE_IRQ)
};

static struct resource s3c2410_uart3_resource[] = {
	[0] = DEFINE_RES_MEM(S3C2443_PA_UART3, SZ_16K),
	[1] = DEFINE_RES_NAMED(IRQ_S3CUART_RX3, \
			IRQ_S3CUART_ERR3 - IRQ_S3CUART_RX3 + 1, \
			NULL, IORESOURCE_IRQ)
};

struct s3c24xx_uart_resources s3c2410_uart_resources[] __initdata = {
	[0] = {
		.resources	= s3c2410_uart0_resource,
		.nr_resources	= ARRAY_SIZE(s3c2410_uart0_resource),
	},
	[1] = {
		.resources	= s3c2410_uart1_resource,
		.nr_resources	= ARRAY_SIZE(s3c2410_uart1_resource),
	},
	[2] = {
		.resources	= s3c2410_uart2_resource,
		.nr_resources	= ARRAY_SIZE(s3c2410_uart2_resource),
	},
	[3] = {
		.resources	= s3c2410_uart3_resource,
		.nr_resources	= ARRAY_SIZE(s3c2410_uart3_resource),
	},
};

#define s3c24xx_device_dma_mask (*((u64[]) { DMA_BIT_MASK(32) }))

#if defined(CONFIG_CPU_S3C2410) || defined(CONFIG_CPU_S3C2412) || \
	defined(CONFIG_CPU_S3C2440) || defined(CONFIG_CPU_S3C2442)
static struct resource s3c2410_dma_resource[] = {
	[0] = DEFINE_RES_MEM(S3C24XX_PA_DMA, S3C24XX_SZ_DMA),
	[1] = DEFINE_RES_IRQ(IRQ_DMA0),
	[2] = DEFINE_RES_IRQ(IRQ_DMA1),
	[3] = DEFINE_RES_IRQ(IRQ_DMA2),
	[4] = DEFINE_RES_IRQ(IRQ_DMA3),
};
#endif

#if defined(CONFIG_CPU_S3C2410) || defined(CONFIG_CPU_S3C2442)
static struct s3c24xx_dma_channel s3c2410_dma_channels[DMACH_MAX] = {
	[DMACH_XD0] = { S3C24XX_DMA_AHB, true, S3C24XX_DMA_CHANREQ(0, 0), },
	[DMACH_XD1] = { S3C24XX_DMA_AHB, true, S3C24XX_DMA_CHANREQ(0, 1), },
	[DMACH_SDI] = { S3C24XX_DMA_APB, false, S3C24XX_DMA_CHANREQ(2, 0) |
						S3C24XX_DMA_CHANREQ(2, 2) |
						S3C24XX_DMA_CHANREQ(1, 3),
	},
	[DMACH_SPI0] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(3, 1), },
	[DMACH_SPI1] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(2, 3), },
	[DMACH_UART0] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(1, 0), },
	[DMACH_UART1] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(1, 1), },
	[DMACH_UART2] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(0, 3), },
	[DMACH_TIMER] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(3, 0) |
						 S3C24XX_DMA_CHANREQ(3, 2) |
						 S3C24XX_DMA_CHANREQ(3, 3),
	},
	[DMACH_I2S_IN] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(2, 1) |
						  S3C24XX_DMA_CHANREQ(1, 2),
	},
	[DMACH_I2S_OUT] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(0, 2), },
	[DMACH_USB_EP1] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(4, 0), },
	[DMACH_USB_EP2] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(4, 1), },
	[DMACH_USB_EP3] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(4, 2), },
	[DMACH_USB_EP4] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(4, 3), },
};

static const struct dma_slave_map s3c2410_dma_slave_map[] = {
	{ "s3c2410-sdi", "rx-tx", (void *)DMACH_SDI },
	{ "s3c2410-spi.0", "rx", (void *)DMACH_SPI0_RX },
	{ "s3c2410-spi.0", "tx", (void *)DMACH_SPI0_TX },
	{ "s3c2410-spi.1", "rx", (void *)DMACH_SPI1_RX },
	{ "s3c2410-spi.1", "tx", (void *)DMACH_SPI1_TX },
	/*
	 * The DMA request source[1] (DMACH_UARTx_SRC2) are
	 * not used in the UART driver.
	 */
	{ "s3c2410-uart.0", "rx", (void *)DMACH_UART0 },
	{ "s3c2410-uart.0", "tx", (void *)DMACH_UART0 },
	{ "s3c2410-uart.1", "rx", (void *)DMACH_UART1 },
	{ "s3c2410-uart.1", "tx", (void *)DMACH_UART1 },
	{ "s3c2410-uart.2", "rx", (void *)DMACH_UART2 },
	{ "s3c2410-uart.2", "tx", (void *)DMACH_UART2 },
	{ "s3c24xx-iis", "rx", (void *)DMACH_I2S_IN },
	{ "s3c24xx-iis", "tx", (void *)DMACH_I2S_OUT },
	{ "s3c-hsudc", "rx0", (void *)DMACH_USB_EP1 },
	{ "s3c-hsudc", "tx0", (void *)DMACH_USB_EP1 },
	{ "s3c-hsudc", "rx1", (void *)DMACH_USB_EP2 },
	{ "s3c-hsudc", "tx1", (void *)DMACH_USB_EP2 },
	{ "s3c-hsudc", "rx2", (void *)DMACH_USB_EP3 },
	{ "s3c-hsudc", "tx2", (void *)DMACH_USB_EP3 },
	{ "s3c-hsudc", "rx3", (void *)DMACH_USB_EP4 },
	{ "s3c-hsudc", "tx3", (void *)DMACH_USB_EP4 }
};

static struct s3c24xx_dma_platdata s3c2410_dma_platdata = {
	.num_phy_channels = 4,
	.channels = s3c2410_dma_channels,
	.num_channels = DMACH_MAX,
	.slave_map = s3c2410_dma_slave_map,
	.slavecnt = ARRAY_SIZE(s3c2410_dma_slave_map),
};

struct platform_device s3c2410_device_dma = {
	.name		= "s3c2410-dma",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s3c2410_dma_resource),
	.resource	= s3c2410_dma_resource,
	.dev	= {
		.dma_mask = &s3c24xx_device_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &s3c2410_dma_platdata,
	},
};
#endif

#ifdef CONFIG_CPU_S3C2412
static struct s3c24xx_dma_channel s3c2412_dma_channels[DMACH_MAX] = {
	[DMACH_XD0] = { S3C24XX_DMA_AHB, true, 17 },
	[DMACH_XD1] = { S3C24XX_DMA_AHB, true, 18 },
	[DMACH_SDI] = { S3C24XX_DMA_APB, false, 10 },
	[DMACH_SPI0_RX] = { S3C24XX_DMA_APB, true, 1 },
	[DMACH_SPI0_TX] = { S3C24XX_DMA_APB, true, 0 },
	[DMACH_SPI1_RX] = { S3C24XX_DMA_APB, true, 3 },
	[DMACH_SPI1_TX] = { S3C24XX_DMA_APB, true, 2 },
	[DMACH_UART0] = { S3C24XX_DMA_APB, true, 19 },
	[DMACH_UART1] = { S3C24XX_DMA_APB, true, 21 },
	[DMACH_UART2] = { S3C24XX_DMA_APB, true, 23 },
	[DMACH_UART0_SRC2] = { S3C24XX_DMA_APB, true, 20 },
	[DMACH_UART1_SRC2] = { S3C24XX_DMA_APB, true, 22 },
	[DMACH_UART2_SRC2] = { S3C24XX_DMA_APB, true, 24 },
	[DMACH_TIMER] = { S3C24XX_DMA_APB, true, 9 },
	[DMACH_I2S_IN] = { S3C24XX_DMA_APB, true, 5 },
	[DMACH_I2S_OUT] = { S3C24XX_DMA_APB, true, 4 },
	[DMACH_USB_EP1] = { S3C24XX_DMA_APB, true, 13 },
	[DMACH_USB_EP2] = { S3C24XX_DMA_APB, true, 14 },
	[DMACH_USB_EP3] = { S3C24XX_DMA_APB, true, 15 },
	[DMACH_USB_EP4] = { S3C24XX_DMA_APB, true, 16 },
};

static const struct dma_slave_map s3c2412_dma_slave_map[] = {
	{ "s3c2412-sdi", "rx-tx", (void *)DMACH_SDI },
	{ "s3c2412-spi.0", "rx", (void *)DMACH_SPI0_RX },
	{ "s3c2412-spi.0", "tx", (void *)DMACH_SPI0_TX },
	{ "s3c2412-spi.1", "rx", (void *)DMACH_SPI1_RX },
	{ "s3c2412-spi.1", "tx", (void *)DMACH_SPI1_TX },
	{ "s3c2440-uart.0", "rx", (void *)DMACH_UART0 },
	{ "s3c2440-uart.0", "tx", (void *)DMACH_UART0 },
	{ "s3c2440-uart.1", "rx", (void *)DMACH_UART1 },
	{ "s3c2440-uart.1", "tx", (void *)DMACH_UART1 },
	{ "s3c2440-uart.2", "rx", (void *)DMACH_UART2 },
	{ "s3c2440-uart.2", "tx", (void *)DMACH_UART2 },
	{ "s3c2412-iis", "rx", (void *)DMACH_I2S_IN },
	{ "s3c2412-iis", "tx", (void *)DMACH_I2S_OUT },
	{ "s3c-hsudc", "rx0", (void *)DMACH_USB_EP1 },
	{ "s3c-hsudc", "tx0", (void *)DMACH_USB_EP1 },
	{ "s3c-hsudc", "rx1", (void *)DMACH_USB_EP2 },
	{ "s3c-hsudc", "tx1", (void *)DMACH_USB_EP2 },
	{ "s3c-hsudc", "rx2", (void *)DMACH_USB_EP3 },
	{ "s3c-hsudc", "tx2", (void *)DMACH_USB_EP3 },
	{ "s3c-hsudc", "rx3", (void *)DMACH_USB_EP4 },
	{ "s3c-hsudc", "tx3", (void *)DMACH_USB_EP4 }
};

static struct s3c24xx_dma_platdata s3c2412_dma_platdata = {
	.num_phy_channels = 4,
	.channels = s3c2412_dma_channels,
	.num_channels = DMACH_MAX,
	.slave_map = s3c2412_dma_slave_map,
	.slavecnt = ARRAY_SIZE(s3c2412_dma_slave_map),
};

struct platform_device s3c2412_device_dma = {
	.name		= "s3c2412-dma",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s3c2410_dma_resource),
	.resource	= s3c2410_dma_resource,
	.dev	= {
		.dma_mask = &s3c24xx_device_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &s3c2412_dma_platdata,
	},
};
#endif

#if defined(CONFIG_CPU_S3C2440)
static struct s3c24xx_dma_channel s3c2440_dma_channels[DMACH_MAX] = {
	[DMACH_XD0] = { S3C24XX_DMA_AHB, true, S3C24XX_DMA_CHANREQ(0, 0), },
	[DMACH_XD1] = { S3C24XX_DMA_AHB, true, S3C24XX_DMA_CHANREQ(0, 1), },
	[DMACH_SDI] = { S3C24XX_DMA_APB, false, S3C24XX_DMA_CHANREQ(2, 0) |
						S3C24XX_DMA_CHANREQ(6, 1) |
						S3C24XX_DMA_CHANREQ(2, 2) |
						S3C24XX_DMA_CHANREQ(1, 3),
	},
	[DMACH_SPI0] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(3, 1), },
	[DMACH_SPI1] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(2, 3), },
	[DMACH_UART0] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(1, 0), },
	[DMACH_UART1] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(1, 1), },
	[DMACH_UART2] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(0, 3), },
	[DMACH_TIMER] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(3, 0) |
						 S3C24XX_DMA_CHANREQ(3, 2) |
						 S3C24XX_DMA_CHANREQ(3, 3),
	},
	[DMACH_I2S_IN] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(2, 1) |
						  S3C24XX_DMA_CHANREQ(1, 2),
	},
	[DMACH_I2S_OUT] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(5, 0) |
						   S3C24XX_DMA_CHANREQ(0, 2),
	},
	[DMACH_PCM_IN] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(6, 0) |
						  S3C24XX_DMA_CHANREQ(5, 2),
	},
	[DMACH_PCM_OUT] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(5, 1) |
						  S3C24XX_DMA_CHANREQ(6, 3),
	},
	[DMACH_MIC_IN] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(6, 2) |
						  S3C24XX_DMA_CHANREQ(5, 3),
	},
	[DMACH_USB_EP1] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(4, 0), },
	[DMACH_USB_EP2] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(4, 1), },
	[DMACH_USB_EP3] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(4, 2), },
	[DMACH_USB_EP4] = { S3C24XX_DMA_APB, true, S3C24XX_DMA_CHANREQ(4, 3), },
};

static const struct dma_slave_map s3c2440_dma_slave_map[] = {
	/* TODO: DMACH_XD0 */
	/* TODO: DMACH_XD1 */
	{ "s3c2440-sdi", "rx-tx", (void *)DMACH_SDI },
	{ "s3c2410-spi.0", "rx", (void *)DMACH_SPI0 },
	{ "s3c2410-spi.0", "tx", (void *)DMACH_SPI0 },
	{ "s3c2410-spi.1", "rx", (void *)DMACH_SPI1 },
	{ "s3c2410-spi.1", "tx", (void *)DMACH_SPI1 },
	{ "s3c2440-uart.0", "rx", (void *)DMACH_UART0 },
	{ "s3c2440-uart.0", "tx", (void *)DMACH_UART0 },
	{ "s3c2440-uart.1", "rx", (void *)DMACH_UART1 },
	{ "s3c2440-uart.1", "tx", (void *)DMACH_UART1 },
	{ "s3c2440-uart.2", "rx", (void *)DMACH_UART2 },
	{ "s3c2440-uart.2", "tx", (void *)DMACH_UART2 },
	{ "s3c2440-uart.3", "rx", (void *)DMACH_UART3 },
	{ "s3c2440-uart.3", "tx", (void *)DMACH_UART3 },
	/* TODO: DMACH_TIMER */
	{ "s3c24xx-iis", "rx", (void *)DMACH_I2S_IN },
	{ "s3c24xx-iis", "tx", (void *)DMACH_I2S_OUT },
	{ "samsung-ac97", "rx", (void *)DMACH_PCM_IN },
	{ "samsung-ac97", "tx", (void *)DMACH_PCM_OUT },
	{ "samsung-ac97", "rx", (void *)DMACH_MIC_IN },
	{ "s3c-hsudc", "rx0", (void *)DMACH_USB_EP1 },
	{ "s3c-hsudc", "rx1", (void *)DMACH_USB_EP2 },
	{ "s3c-hsudc", "rx2", (void *)DMACH_USB_EP3 },
	{ "s3c-hsudc", "rx3", (void *)DMACH_USB_EP4 },
	{ "s3c-hsudc", "tx0", (void *)DMACH_USB_EP1 },
	{ "s3c-hsudc", "tx1", (void *)DMACH_USB_EP2 },
	{ "s3c-hsudc", "tx2", (void *)DMACH_USB_EP3 },
	{ "s3c-hsudc", "tx3", (void *)DMACH_USB_EP4 }
};

static struct s3c24xx_dma_platdata s3c2440_dma_platdata = {
	.num_phy_channels = 4,
	.channels = s3c2440_dma_channels,
	.num_channels = DMACH_MAX,
	.slave_map = s3c2440_dma_slave_map,
	.slavecnt = ARRAY_SIZE(s3c2440_dma_slave_map),
};

struct platform_device s3c2440_device_dma = {
	.name		= "s3c2410-dma",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s3c2410_dma_resource),
	.resource	= s3c2410_dma_resource,
	.dev	= {
		.dma_mask = &s3c24xx_device_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &s3c2440_dma_platdata,
	},
};
#endif

#if defined(CONFIG_CPU_S3C2443) || defined(CONFIG_CPU_S3C2416)
static struct resource s3c2443_dma_resource[] = {
	[0] = DEFINE_RES_MEM(S3C24XX_PA_DMA, S3C24XX_SZ_DMA),
	[1] = DEFINE_RES_IRQ(IRQ_S3C2443_DMA0),
	[2] = DEFINE_RES_IRQ(IRQ_S3C2443_DMA1),
	[3] = DEFINE_RES_IRQ(IRQ_S3C2443_DMA2),
	[4] = DEFINE_RES_IRQ(IRQ_S3C2443_DMA3),
	[5] = DEFINE_RES_IRQ(IRQ_S3C2443_DMA4),
	[6] = DEFINE_RES_IRQ(IRQ_S3C2443_DMA5),
};

static struct s3c24xx_dma_channel s3c2443_dma_channels[DMACH_MAX] = {
	[DMACH_XD0] = { S3C24XX_DMA_AHB, true, 17 },
	[DMACH_XD1] = { S3C24XX_DMA_AHB, true, 18 },
	[DMACH_SDI] = { S3C24XX_DMA_APB, false, 10 },
	[DMACH_SPI0_RX] = { S3C24XX_DMA_APB, true, 1 },
	[DMACH_SPI0_TX] = { S3C24XX_DMA_APB, true, 0 },
	[DMACH_SPI1_RX] = { S3C24XX_DMA_APB, true, 3 },
	[DMACH_SPI1_TX] = { S3C24XX_DMA_APB, true, 2 },
	[DMACH_UART0] = { S3C24XX_DMA_APB, true, 19 },
	[DMACH_UART1] = { S3C24XX_DMA_APB, true, 21 },
	[DMACH_UART2] = { S3C24XX_DMA_APB, true, 23 },
	[DMACH_UART3] = { S3C24XX_DMA_APB, true, 25 },
	[DMACH_UART0_SRC2] = { S3C24XX_DMA_APB, true, 20 },
	[DMACH_UART1_SRC2] = { S3C24XX_DMA_APB, true, 22 },
	[DMACH_UART2_SRC2] = { S3C24XX_DMA_APB, true, 24 },
	[DMACH_UART3_SRC2] = { S3C24XX_DMA_APB, true, 26 },
	[DMACH_TIMER] = { S3C24XX_DMA_APB, true, 9 },
	[DMACH_I2S_IN] = { S3C24XX_DMA_APB, true, 5 },
	[DMACH_I2S_OUT] = { S3C24XX_DMA_APB, true, 4 },
	[DMACH_PCM_IN] = { S3C24XX_DMA_APB, true, 28 },
	[DMACH_PCM_OUT] = { S3C24XX_DMA_APB, true, 27 },
	[DMACH_MIC_IN] = { S3C24XX_DMA_APB, true, 29 },
};

static const struct dma_slave_map s3c2443_dma_slave_map[] = {
	{ "s3c2440-sdi", "rx-tx", (void *)DMACH_SDI },
	{ "s3c2443-spi.0", "rx", (void *)DMACH_SPI0_RX },
	{ "s3c2443-spi.0", "tx", (void *)DMACH_SPI0_TX },
	{ "s3c2443-spi.1", "rx", (void *)DMACH_SPI1_RX },
	{ "s3c2443-spi.1", "tx", (void *)DMACH_SPI1_TX },
	{ "s3c2440-uart.0", "rx", (void *)DMACH_UART0 },
	{ "s3c2440-uart.0", "tx", (void *)DMACH_UART0 },
	{ "s3c2440-uart.1", "rx", (void *)DMACH_UART1 },
	{ "s3c2440-uart.1", "tx", (void *)DMACH_UART1 },
	{ "s3c2440-uart.2", "rx", (void *)DMACH_UART2 },
	{ "s3c2440-uart.2", "tx", (void *)DMACH_UART2 },
	{ "s3c2440-uart.3", "rx", (void *)DMACH_UART3 },
	{ "s3c2440-uart.3", "tx", (void *)DMACH_UART3 },
	{ "s3c24xx-iis", "rx", (void *)DMACH_I2S_IN },
	{ "s3c24xx-iis", "tx", (void *)DMACH_I2S_OUT },
};

static struct s3c24xx_dma_platdata s3c2443_dma_platdata = {
	.num_phy_channels = 6,
	.channels = s3c2443_dma_channels,
	.num_channels = DMACH_MAX,
	.slave_map = s3c2443_dma_slave_map,
	.slavecnt = ARRAY_SIZE(s3c2443_dma_slave_map),
};

struct platform_device s3c2443_device_dma = {
	.name		= "s3c2443-dma",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s3c2443_dma_resource),
	.resource	= s3c2443_dma_resource,
	.dev	= {
		.dma_mask = &s3c24xx_device_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &s3c2443_dma_platdata,
	},
};
#endif

#if defined(CONFIG_COMMON_CLK) && defined(CONFIG_CPU_S3C2410)
void __init s3c2410_init_clocks(int xtal)
{
	s3c2410_common_clk_init(NULL, xtal, 0, S3C24XX_VA_CLKPWR);
}
#endif

#ifdef CONFIG_CPU_S3C2412
void __init s3c2412_init_clocks(int xtal)
{
	s3c2412_common_clk_init(NULL, xtal, 0, S3C24XX_VA_CLKPWR);
}
#endif

#ifdef CONFIG_CPU_S3C2416
void __init s3c2416_init_clocks(int xtal)
{
	s3c2443_common_clk_init(NULL, xtal, 0, S3C24XX_VA_CLKPWR);
}
#endif

#if defined(CONFIG_COMMON_CLK) && defined(CONFIG_CPU_S3C2440)
void __init s3c2440_init_clocks(int xtal)
{
	s3c2410_common_clk_init(NULL, xtal, 1, S3C24XX_VA_CLKPWR);
}
#endif

#if defined(CONFIG_COMMON_CLK) && defined(CONFIG_CPU_S3C2442)
void __init s3c2442_init_clocks(int xtal)
{
	s3c2410_common_clk_init(NULL, xtal, 2, S3C24XX_VA_CLKPWR);
}
#endif

#ifdef CONFIG_CPU_S3C2443
void __init s3c2443_init_clocks(int xtal)
{
	s3c2443_common_clk_init(NULL, xtal, 1, S3C24XX_VA_CLKPWR);
}
#endif

#if defined(CONFIG_CPU_S3C2410) || defined(CONFIG_CPU_S3C2440) || \
	defined(CONFIG_CPU_S3C2442)
static struct resource s3c2410_dclk_resource[] = {
	[0] = DEFINE_RES_MEM(0x56000084, 0x4),
};

struct platform_device s3c2410_device_dclk = {
	.name		= "s3c2410-dclk",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s3c2410_dclk_resource),
	.resource	= s3c2410_dclk_resource,
};
#endif
