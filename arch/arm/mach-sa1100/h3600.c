/*
 * Hardware definitions for Compaq iPAQ H3xxx Handheld Computers
 *
 * Copyright 2000,1 Compaq Computer Corporation.
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 *
 * COMPAQ COMPUTER CORPORATION MAKES NO WARRANTIES, EXPRESSED OR IMPLIED,
 * AS TO THE USEFULNESS OR CORRECTNESS OF THIS CODE OR ITS
 * FITNESS FOR ANY PARTICULAR PURPOSE.
 *
 * Author: Jamey Hicks.
 *
 * History:
 *
 * 2001-10-??	Andrew Christian   Added support for iPAQ H3800
 *				   and abstracted EGPIO interface.
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/serial_core.h>

#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/setup.h>

#include <asm/mach/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/irda.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include <asm/arch/h3600.h>

#if defined (CONFIG_SA1100_H3600) || defined (CONFIG_SA1100_H3100)
#include <asm/arch/h3600_gpio.h>
#endif

#ifdef CONFIG_SA1100_H3800
#include <asm/arch/h3600_asic.h>
#endif

#include "generic.h"

struct ipaq_model_ops ipaq_model_ops;
EXPORT_SYMBOL(ipaq_model_ops);

static struct mtd_partition h3xxx_partitions[] = {
	{
		.name		= "H3XXX boot firmware",
		.size		= 0x00040000,
		.offset		= 0,
		.mask_flags	= MTD_WRITEABLE,  /* force read-only */
	}, {
#ifdef CONFIG_MTD_2PARTS_IPAQ
		.name		= "H3XXX root jffs2",
		.size		= MTDPART_SIZ_FULL,
		.offset		= 0x00040000,
#else
		.name		= "H3XXX kernel",
		.size		= 0x00080000,
		.offset		= 0x00040000,
	}, {
		.name		= "H3XXX params",
		.size		= 0x00040000,
		.offset		= 0x000C0000,
	}, {
#ifdef CONFIG_JFFS2_FS
		.name		= "H3XXX root jffs2",
		.size		= MTDPART_SIZ_FULL,
		.offset		= 0x00100000,
#else
		.name		= "H3XXX initrd",
		.size		= 0x00100000,
		.offset		= 0x00100000,
	}, {
		.name		= "H3XXX root cramfs",
		.size		= 0x00300000,
		.offset		= 0x00200000,
	}, {
		.name		= "H3XXX usr cramfs",
		.size		= 0x00800000,
		.offset		= 0x00500000,
	}, {
		.name		= "H3XXX usr local",
		.size		= MTDPART_SIZ_FULL,
		.offset		= 0x00d00000,
#endif
#endif
	}
};

static void h3xxx_set_vpp(int vpp)
{
	assign_h3600_egpio(IPAQ_EGPIO_VPP_ON, vpp);
}

static struct flash_platform_data h3xxx_flash_data = {
	.map_name	= "cfi_probe",
	.set_vpp	= h3xxx_set_vpp,
	.parts		= h3xxx_partitions,
	.nr_parts	= ARRAY_SIZE(h3xxx_partitions),
};

static struct resource h3xxx_flash_resource = {
	.start		= SA1100_CS0_PHYS,
	.end		= SA1100_CS0_PHYS + SZ_32M - 1,
	.flags		= IORESOURCE_MEM,
};

/*
 * This turns the IRDA power on or off on the Compaq H3600
 */
static int h3600_irda_set_power(struct device *dev, unsigned int state)
{
	assign_h3600_egpio( IPAQ_EGPIO_IR_ON, state );

	return 0;
}

static void h3600_irda_set_speed(struct device *dev, unsigned int speed)
{
	if (speed < 4000000) {
		clr_h3600_egpio(IPAQ_EGPIO_IR_FSEL);
	} else {
		set_h3600_egpio(IPAQ_EGPIO_IR_FSEL);
	}
}

static struct irda_platform_data h3600_irda_data = {
	.set_power	= h3600_irda_set_power,
	.set_speed	= h3600_irda_set_speed,
};

static void h3xxx_mach_init(void)
{
	sa11x0_set_flash_data(&h3xxx_flash_data, &h3xxx_flash_resource, 1);
	sa11x0_set_irda_data(&h3600_irda_data);
}

/*
 * low-level UART features
 */

static void h3600_uart_set_mctrl(struct uart_port *port, u_int mctrl)
{
	if (port->mapbase == _Ser3UTCR0) {
		if (mctrl & TIOCM_RTS)
			GPCR = GPIO_H3600_COM_RTS;
		else
			GPSR = GPIO_H3600_COM_RTS;
	}
}

static u_int h3600_uart_get_mctrl(struct uart_port *port)
{
	u_int ret = TIOCM_CD | TIOCM_CTS | TIOCM_DSR;

	if (port->mapbase == _Ser3UTCR0) {
		int gplr = GPLR;
		/* DCD and CTS bits are inverted in GPLR by RS232 transceiver */
		if (gplr & GPIO_H3600_COM_DCD)
			ret &= ~TIOCM_CD;
		if (gplr & GPIO_H3600_COM_CTS)
			ret &= ~TIOCM_CTS;
	}

	return ret;
}

static void h3600_uart_pm(struct uart_port *port, u_int state, u_int oldstate)
{
	if (port->mapbase == _Ser2UTCR0) { /* TODO: REMOVE THIS */
		assign_h3600_egpio(IPAQ_EGPIO_IR_ON, !state);
	} else if (port->mapbase == _Ser3UTCR0) {
		assign_h3600_egpio(IPAQ_EGPIO_RS232_ON, !state);
	}
}

/*
 * Enable/Disable wake up events for this serial port.
 * Obviously, we only support this on the normal COM port.
 */
static int h3600_uart_set_wake(struct uart_port *port, u_int enable)
{
	int err = -EINVAL;

	if (port->mapbase == _Ser3UTCR0) {
		if (enable)
			PWER |= PWER_GPIO23 | PWER_GPIO25; /* DCD and CTS */
		else
			PWER &= ~(PWER_GPIO23 | PWER_GPIO25); /* DCD and CTS */
		err = 0;
	}
	return err;
}

static struct sa1100_port_fns h3600_port_fns __initdata = {
	.set_mctrl	= h3600_uart_set_mctrl,
	.get_mctrl	= h3600_uart_get_mctrl,
	.pm		= h3600_uart_pm,
	.set_wake	= h3600_uart_set_wake,
};

/*
 * helper for sa1100fb
 */
static void h3xxx_lcd_power(int enable)
{
	assign_h3600_egpio(IPAQ_EGPIO_LCD_POWER, enable);
}

static struct map_desc h3600_io_desc[] __initdata = {
  	{	/* static memory bank 2  CS#2 */
		.virtual	=  H3600_BANK_2_VIRT,
		.pfn		= __phys_to_pfn(SA1100_CS2_PHYS),
		.length		= 0x02800000,
		.type		= MT_DEVICE
	}, {	/* static memory bank 4  CS#4 */
		.virtual	=  H3600_BANK_4_VIRT,
		.pfn		= __phys_to_pfn(SA1100_CS4_PHYS),
		.length		= 0x00800000,
		.type		= MT_DEVICE
	}, {	/* EGPIO 0		CS#5 */
		.virtual	=  H3600_EGPIO_VIRT,
		.pfn		= __phys_to_pfn(H3600_EGPIO_PHYS),
		.length		= 0x01000000,
		.type		= MT_DEVICE
	}
};

/*
 * Common map_io initialization
 */

static void __init h3xxx_map_io(void)
{
	sa1100_map_io();
	iotable_init(h3600_io_desc, ARRAY_SIZE(h3600_io_desc));

	sa1100_register_uart_fns(&h3600_port_fns);
	sa1100_register_uart(0, 3); /* Common serial port */
//	sa1100_register_uart(1, 1); /* Microcontroller on 3100/3600 */

	/* Ensure those pins are outputs and driving low  */
	PPDR |= PPC_TXD4 | PPC_SCLK | PPC_SFRM;
	PPSR &= ~(PPC_TXD4 | PPC_SCLK | PPC_SFRM);

	/* Configure suspend conditions */
	PGSR = 0;
	PWER = PWER_GPIO0 | PWER_RTC;
	PCFR = PCFR_OPDE;
	PSDR = 0;

	sa1100fb_lcd_power = h3xxx_lcd_power;
}

static __inline__ void do_blank(int setp)
{
	if (ipaq_model_ops.blank_callback)
		ipaq_model_ops.blank_callback(1-setp);
}

/************************* H3100 *************************/

#ifdef CONFIG_SA1100_H3100

#define H3100_EGPIO	(*(volatile unsigned int *)H3600_EGPIO_VIRT)
static unsigned int h3100_egpio = 0;

static void h3100_control_egpio(enum ipaq_egpio_type x, int setp)
{
	unsigned int egpio = 0;
	long	     gpio = 0;
	unsigned long flags;

	switch (x) {
	case IPAQ_EGPIO_LCD_POWER:
		egpio |= EGPIO_H3600_LCD_ON;
		gpio  |= GPIO_H3100_LCD_3V_ON;
		do_blank(setp);
		break;
	case IPAQ_EGPIO_LCD_ENABLE:
		break;
	case IPAQ_EGPIO_CODEC_NRESET:
		egpio |= EGPIO_H3600_CODEC_NRESET;
		break;
	case IPAQ_EGPIO_AUDIO_ON:
		gpio |= GPIO_H3100_AUD_PWR_ON
			| GPIO_H3100_AUD_ON;
		break;
	case IPAQ_EGPIO_QMUTE:
		gpio |= GPIO_H3100_QMUTE;
		break;
	case IPAQ_EGPIO_OPT_NVRAM_ON:
		egpio |= EGPIO_H3600_OPT_NVRAM_ON;
		break;
	case IPAQ_EGPIO_OPT_ON:
		egpio |= EGPIO_H3600_OPT_ON;
		break;
	case IPAQ_EGPIO_CARD_RESET:
		egpio |= EGPIO_H3600_CARD_RESET;
		break;
	case IPAQ_EGPIO_OPT_RESET:
		egpio |= EGPIO_H3600_OPT_RESET;
		break;
	case IPAQ_EGPIO_IR_ON:
		gpio |= GPIO_H3100_IR_ON;
		break;
	case IPAQ_EGPIO_IR_FSEL:
		gpio |= GPIO_H3100_IR_FSEL;
		break;
	case IPAQ_EGPIO_RS232_ON:
		egpio |= EGPIO_H3600_RS232_ON;
		break;
	case IPAQ_EGPIO_VPP_ON:
		egpio |= EGPIO_H3600_VPP_ON;
		break;
	}

	if (egpio || gpio) {
		local_irq_save(flags);
		if (setp) {
			h3100_egpio |= egpio;
			GPSR = gpio;
		} else {
			h3100_egpio &= ~egpio;
			GPCR = gpio;
		}
		H3100_EGPIO = h3100_egpio;
		local_irq_restore(flags);
	}
}

static unsigned long h3100_read_egpio(void)
{
	return h3100_egpio;
}

static int h3100_pm_callback(int req)
{
	if (ipaq_model_ops.pm_callback_aux)
		return ipaq_model_ops.pm_callback_aux(req);
	return 0;
}

static struct ipaq_model_ops h3100_model_ops __initdata = {
	.generic_name	= "3100",
	.control	= h3100_control_egpio,
	.read		= h3100_read_egpio,
	.pm_callback	= h3100_pm_callback
};

#define H3100_DIRECT_EGPIO (GPIO_H3100_BT_ON	  \
			  | GPIO_H3100_GPIO3	  \
			  | GPIO_H3100_QMUTE	  \
			  | GPIO_H3100_LCD_3V_ON  \
			  | GPIO_H3100_AUD_ON	  \
			  | GPIO_H3100_AUD_PWR_ON \
			  | GPIO_H3100_IR_ON	  \
			  | GPIO_H3100_IR_FSEL)

static void __init h3100_map_io(void)
{
	h3xxx_map_io();

	/* Initialize h3100-specific values here */
	GPCR = 0x0fffffff;	 /* All outputs are set low by default */
	GPDR = GPIO_H3600_COM_RTS  | GPIO_H3600_L3_CLOCK |
	       GPIO_H3600_L3_MODE  | GPIO_H3600_L3_DATA  |
	       GPIO_H3600_CLK_SET1 | GPIO_H3600_CLK_SET0 |
	       H3100_DIRECT_EGPIO;

	/* Older bootldrs put GPIO2-9 in alternate mode on the
	   assumption that they are used for video */
	GAFR &= ~H3100_DIRECT_EGPIO;

	H3100_EGPIO = h3100_egpio;
	ipaq_model_ops = h3100_model_ops;
}

MACHINE_START(H3100, "Compaq iPAQ H3100")
	.phys_io	= 0x80000000,
	.io_pg_offst	= ((0xf8000000) >> 18) & 0xfffc,
	.boot_params	= 0xc0000100,
	.map_io		= h3100_map_io,
	.init_irq	= sa1100_init_irq,
	.timer		= &sa1100_timer,
	.init_machine	= h3xxx_mach_init,
MACHINE_END

#endif /* CONFIG_SA1100_H3100 */

/************************* H3600 *************************/

#ifdef CONFIG_SA1100_H3600

#define H3600_EGPIO	(*(volatile unsigned int *)H3600_EGPIO_VIRT)
static unsigned int h3600_egpio = EGPIO_H3600_RS232_ON;

static void h3600_control_egpio(enum ipaq_egpio_type x, int setp)
{
	unsigned int egpio = 0;
	unsigned long flags;

	switch (x) {
	case IPAQ_EGPIO_LCD_POWER:
		egpio |= EGPIO_H3600_LCD_ON |
			 EGPIO_H3600_LCD_PCI |
			 EGPIO_H3600_LCD_5V_ON |
			 EGPIO_H3600_LVDD_ON;
		do_blank(setp);
		break;
	case IPAQ_EGPIO_LCD_ENABLE:
		break;
	case IPAQ_EGPIO_CODEC_NRESET:
		egpio |= EGPIO_H3600_CODEC_NRESET;
		break;
	case IPAQ_EGPIO_AUDIO_ON:
		egpio |= EGPIO_H3600_AUD_AMP_ON |
			 EGPIO_H3600_AUD_PWR_ON;
		break;
	case IPAQ_EGPIO_QMUTE:
		egpio |= EGPIO_H3600_QMUTE;
		break;
	case IPAQ_EGPIO_OPT_NVRAM_ON:
		egpio |= EGPIO_H3600_OPT_NVRAM_ON;
		break;
	case IPAQ_EGPIO_OPT_ON:
		egpio |= EGPIO_H3600_OPT_ON;
		break;
	case IPAQ_EGPIO_CARD_RESET:
		egpio |= EGPIO_H3600_CARD_RESET;
		break;
	case IPAQ_EGPIO_OPT_RESET:
		egpio |= EGPIO_H3600_OPT_RESET;
		break;
	case IPAQ_EGPIO_IR_ON:
		egpio |= EGPIO_H3600_IR_ON;
		break;
	case IPAQ_EGPIO_IR_FSEL:
		egpio |= EGPIO_H3600_IR_FSEL;
		break;
	case IPAQ_EGPIO_RS232_ON:
		egpio |= EGPIO_H3600_RS232_ON;
		break;
	case IPAQ_EGPIO_VPP_ON:
		egpio |= EGPIO_H3600_VPP_ON;
		break;
	}

	if (egpio) {
		local_irq_save(flags);
		if (setp)
			h3600_egpio |= egpio;
		else
			h3600_egpio &= ~egpio;
		H3600_EGPIO = h3600_egpio;
		local_irq_restore(flags);
	}
}

static unsigned long h3600_read_egpio(void)
{
	return h3600_egpio;
}

static int h3600_pm_callback(int req)
{
	if (ipaq_model_ops.pm_callback_aux)
		return ipaq_model_ops.pm_callback_aux(req);
	return 0;
}

static struct ipaq_model_ops h3600_model_ops __initdata = {
	.generic_name	= "3600",
	.control	= h3600_control_egpio,
	.read		= h3600_read_egpio,
	.pm_callback	= h3600_pm_callback
};

static void __init h3600_map_io(void)
{
	h3xxx_map_io();

	/* Initialize h3600-specific values here */

	GPCR = 0x0fffffff;	 /* All outputs are set low by default */
	GPDR = GPIO_H3600_COM_RTS  | GPIO_H3600_L3_CLOCK |
	       GPIO_H3600_L3_MODE  | GPIO_H3600_L3_DATA  |
	       GPIO_H3600_CLK_SET1 | GPIO_H3600_CLK_SET0 |
	       GPIO_LDD15 | GPIO_LDD14 | GPIO_LDD13 | GPIO_LDD12 |
	       GPIO_LDD11 | GPIO_LDD10 | GPIO_LDD9  | GPIO_LDD8;

	H3600_EGPIO = h3600_egpio;	   /* Maintains across sleep? */
	ipaq_model_ops = h3600_model_ops;
}

MACHINE_START(H3600, "Compaq iPAQ H3600")
	.phys_io	= 0x80000000,
	.io_pg_offst	= ((0xf8000000) >> 18) & 0xfffc,
	.boot_params	= 0xc0000100,
	.map_io		= h3600_map_io,
	.init_irq	= sa1100_init_irq,
	.timer		= &sa1100_timer,
	.init_machine	= h3xxx_mach_init,
MACHINE_END

#endif /* CONFIG_SA1100_H3600 */

#ifdef CONFIG_SA1100_H3800

#define SET_ASIC1(x) \
   do {if (setp) { H3800_ASIC1_GPIO_OUT |= (x); } else { H3800_ASIC1_GPIO_OUT &= ~(x); }} while(0)

#define SET_ASIC2(x) \
   do {if (setp) { H3800_ASIC2_GPIOPIOD |= (x); } else { H3800_ASIC2_GPIOPIOD &= ~(x); }} while(0)

#define CLEAR_ASIC1(x) \
   do {if (setp) { H3800_ASIC1_GPIO_OUT &= ~(x); } else { H3800_ASIC1_GPIO_OUT |= (x); }} while(0)

#define CLEAR_ASIC2(x) \
   do {if (setp) { H3800_ASIC2_GPIOPIOD &= ~(x); } else { H3800_ASIC2_GPIOPIOD |= (x); }} while(0)


/*
  On screen enable, we get

     h3800_video_power_on(1)
     LCD controller starts
     h3800_video_lcd_enable(1)

  On screen disable, we get

     h3800_video_lcd_enable(0)
     LCD controller stops
     h3800_video_power_on(0)
*/


static void h3800_video_power_on(int setp)
{
	if (setp) {
		H3800_ASIC1_GPIO_OUT |= GPIO1_LCD_ON;
		msleep(30);
		H3800_ASIC1_GPIO_OUT |= GPIO1_VGL_ON;
		msleep(5);
		H3800_ASIC1_GPIO_OUT |= GPIO1_VGH_ON;
		msleep(50);
		H3800_ASIC1_GPIO_OUT |= GPIO1_LCD_5V_ON;
		msleep(5);
	} else {
		msleep(5);
		H3800_ASIC1_GPIO_OUT &= ~GPIO1_LCD_5V_ON;
		msleep(50);
		H3800_ASIC1_GPIO_OUT &= ~GPIO1_VGL_ON;
		msleep(5);
		H3800_ASIC1_GPIO_OUT &= ~GPIO1_VGH_ON;
		msleep(100);
		H3800_ASIC1_GPIO_OUT &= ~GPIO1_LCD_ON;
	}
}

static void h3800_video_lcd_enable(int setp)
{
	if (setp) {
		msleep(17);	// Wait one from before turning on
		H3800_ASIC1_GPIO_OUT |= GPIO1_LCD_PCI;
	} else {
		H3800_ASIC1_GPIO_OUT &= ~GPIO1_LCD_PCI;
		msleep(30);	// Wait before turning off
	}
}


static void h3800_control_egpio(enum ipaq_egpio_type x, int setp)
{
	switch (x) {
	case IPAQ_EGPIO_LCD_POWER:
		h3800_video_power_on(setp);
		break;
	case IPAQ_EGPIO_LCD_ENABLE:
		h3800_video_lcd_enable(setp);
		break;
	case IPAQ_EGPIO_CODEC_NRESET:
	case IPAQ_EGPIO_AUDIO_ON:
	case IPAQ_EGPIO_QMUTE:
		printk("%s: error - should not be called\n", __FUNCTION__);
		break;
	case IPAQ_EGPIO_OPT_NVRAM_ON:
		SET_ASIC2(GPIO2_OPT_ON_NVRAM);
		break;
	case IPAQ_EGPIO_OPT_ON:
		SET_ASIC2(GPIO2_OPT_ON);
		break;
	case IPAQ_EGPIO_CARD_RESET:
		SET_ASIC2(GPIO2_OPT_PCM_RESET);
		break;
	case IPAQ_EGPIO_OPT_RESET:
		SET_ASIC2(GPIO2_OPT_RESET);
		break;
	case IPAQ_EGPIO_IR_ON:
		CLEAR_ASIC1(GPIO1_IR_ON_N);
		break;
	case IPAQ_EGPIO_IR_FSEL:
		break;
	case IPAQ_EGPIO_RS232_ON:
		SET_ASIC1(GPIO1_RS232_ON);
		break;
	case IPAQ_EGPIO_VPP_ON:
		H3800_ASIC2_FlashWP_VPP_ON = setp;
		break;
	}
}

static unsigned long h3800_read_egpio(void)
{
	return H3800_ASIC1_GPIO_OUT | (H3800_ASIC2_GPIOPIOD << 16);
}

/* We need to fix ASIC2 GPIO over suspend/resume.  At the moment,
   it doesn't appear that ASIC1 GPIO has the same problem */

static int h3800_pm_callback(int req)
{
	static u16 asic1_data;
	static u16 asic2_data;
	int result = 0;

	printk("%s %d\n", __FUNCTION__, req);

	switch (req) {
	case PM_RESUME:
		MSC2 = (MSC2 & 0x0000ffff) | 0xE4510000;  /* Set MSC2 correctly */

		H3800_ASIC2_GPIOPIOD = asic2_data;
		H3800_ASIC2_GPIODIR = GPIO2_PEN_IRQ
			| GPIO2_SD_DETECT
			| GPIO2_EAR_IN_N
			| GPIO2_USB_DETECT_N
			| GPIO2_SD_CON_SLT;

		H3800_ASIC1_GPIO_OUT = asic1_data;

		if (ipaq_model_ops.pm_callback_aux)
			result = ipaq_model_ops.pm_callback_aux(req);
		break;

	case PM_SUSPEND:
		if (ipaq_model_ops.pm_callback_aux &&
		     ((result = ipaq_model_ops.pm_callback_aux(req)) != 0))
			return result;

		asic1_data = H3800_ASIC1_GPIO_OUT;
		asic2_data = H3800_ASIC2_GPIOPIOD;
		break;
	default:
		printk("%s: unrecognized PM callback\n", __FUNCTION__);
		break;
	}
	return result;
}

static struct ipaq_model_ops h3800_model_ops __initdata = {
	.generic_name	= "3800",
	.control	= h3800_control_egpio,
	.read		= h3800_read_egpio,
	.pm_callback	= h3800_pm_callback
};

#define MAX_ASIC_ISR_LOOPS    20

/* The order of these is important - see #include <asm/arch/irqs.h> */
static u32 kpio_irq_mask[] = {
	KPIO_KEY_ALL,
	KPIO_SPI_INT,
	KPIO_OWM_INT,
	KPIO_ADC_INT,
	KPIO_UART_0_INT,
	KPIO_UART_1_INT,
	KPIO_TIMER_0_INT,
	KPIO_TIMER_1_INT,
	KPIO_TIMER_2_INT
};

static u32 gpio_irq_mask[] = {
	GPIO2_PEN_IRQ,
	GPIO2_SD_DETECT,
	GPIO2_EAR_IN_N,
	GPIO2_USB_DETECT_N,
	GPIO2_SD_CON_SLT,
};

static void h3800_IRQ_demux(unsigned int irq, struct irq_desc *desc)
{
	int i;

	if (0) printk("%s: interrupt received\n", __FUNCTION__);

	desc->chip->ack(irq);

	for (i = 0; i < MAX_ASIC_ISR_LOOPS && (GPLR & GPIO_H3800_ASIC); i++) {
		u32 irq;
		int j;

		/* KPIO */
		irq = H3800_ASIC2_KPIINTFLAG;
		if (0) printk("%s KPIO 0x%08X\n", __FUNCTION__, irq);
		for (j = 0; j < H3800_KPIO_IRQ_COUNT; j++)
			if (irq & kpio_irq_mask[j])
				handle_edge_irq(H3800_KPIO_IRQ_COUNT + j, irq_desc + H3800_KPIO_IRQ_COUNT + j);

		/* GPIO2 */
		irq = H3800_ASIC2_GPIINTFLAG;
		if (0) printk("%s GPIO 0x%08X\n", __FUNCTION__, irq);
		for (j = 0; j < H3800_GPIO_IRQ_COUNT; j++)
			if (irq & gpio_irq_mask[j])
				handle_edge_irq(H3800_GPIO_IRQ_COUNT + j, irq_desc + H3800_GPIO_IRQ_COUNT + j);
	}

	if (i >= MAX_ASIC_ISR_LOOPS)
		printk("%s: interrupt processing overrun\n", __FUNCTION__);

	/* For level-based interrupts */
	desc->chip->unmask(irq);

}

static struct irqaction h3800_irq = {
	.name		= "h3800_asic",
	.handler	= h3800_IRQ_demux,
	.flags		= IRQF_DISABLED | IRQF_TIMER,
};

u32 kpio_int_shadow = 0;


/* mask_ack <- IRQ is first serviced.
       mask <- IRQ is disabled.
     unmask <- IRQ is enabled

     The INTCLR registers are poorly documented.  I believe that writing
     a "1" to the register clears the specific interrupt, but the documentation
     indicates writing a "0" clears the interrupt.  In any case, they shouldn't
     be read (that's the INTFLAG register)
 */

static void h3800_mask_ack_kpio_irq(unsigned int irq)
{
	u32 mask = kpio_irq_mask[irq - H3800_KPIO_IRQ_START];
	kpio_int_shadow &= ~mask;
	H3800_ASIC2_KPIINTSTAT = kpio_int_shadow;
	H3800_ASIC2_KPIINTCLR  = mask;
}

static void h3800_mask_kpio_irq(unsigned int irq)
{
	u32 mask = kpio_irq_mask[irq - H3800_KPIO_IRQ_START];
	kpio_int_shadow &= ~mask;
	H3800_ASIC2_KPIINTSTAT = kpio_int_shadow;
}

static void h3800_unmask_kpio_irq(unsigned int irq)
{
	u32 mask = kpio_irq_mask[irq - H3800_KPIO_IRQ_START];
	kpio_int_shadow |= mask;
	H3800_ASIC2_KPIINTSTAT = kpio_int_shadow;
}

static void h3800_mask_ack_gpio_irq(unsigned int irq)
{
	u32 mask = gpio_irq_mask[irq - H3800_GPIO_IRQ_START];
	H3800_ASIC2_GPIINTSTAT &= ~mask;
	H3800_ASIC2_GPIINTCLR	= mask;
}

static void h3800_mask_gpio_irq(unsigned int irq)
{
	u32 mask = gpio_irq_mask[irq - H3800_GPIO_IRQ_START];
	H3800_ASIC2_GPIINTSTAT &= ~mask;
	}

static void h3800_unmask_gpio_irq(unsigned int irq)
{
	u32 mask = gpio_irq_mask[irq - H3800_GPIO_IRQ_START];
	H3800_ASIC2_GPIINTSTAT |= mask;
}

static void __init h3800_init_irq(void)
{
	int i;

	/* Initialize standard IRQs */
	sa1100_init_irq();

	/* Disable all IRQs and set up clock */
	H3800_ASIC2_KPIINTSTAT	   =  0;     /* Disable all interrupts */
	H3800_ASIC2_GPIINTSTAT	   =  0;

	H3800_ASIC2_KPIINTCLR	   =  0;     /* Clear all KPIO interrupts */
	H3800_ASIC2_GPIINTCLR	   =  0;     /* Clear all GPIO interrupts */

//	H3800_ASIC2_KPIINTCLR	   =  0xffff;	  /* Clear all KPIO interrupts */
//	H3800_ASIC2_GPIINTCLR	   =  0xffff;	  /* Clear all GPIO interrupts */

	H3800_ASIC2_CLOCK_Enable       |= ASIC2_CLOCK_EX0;   /* 32 kHZ crystal on */
	H3800_ASIC2_INTR_ClockPrescale |= ASIC2_INTCPS_SET;
	H3800_ASIC2_INTR_ClockPrescale	= ASIC2_INTCPS_CPS(0x0e) | ASIC2_INTCPS_SET;
	H3800_ASIC2_INTR_TimerSet	= 1;

#if 0
	for (i = 0; i < H3800_KPIO_IRQ_COUNT; i++) {
		int irq = i + H3800_KPIO_IRQ_START;
		irq_desc[irq].valid    = 1;
		irq_desc[irq].probe_ok = 1;
		set_irq_chip(irq, &h3800_kpio_irqchip);
	}

	for (i = 0; i < H3800_GPIO_IRQ_COUNT; i++) {
		int irq = i + H3800_GPIO_IRQ_START;
		irq_desc[irq].valid    = 1;
		irq_desc[irq].probe_ok = 1;
		set_irq_chip(irq, &h3800_gpio_irqchip);
	}
#endif
	set_irq_type(IRQ_GPIO_H3800_ASIC, IRQT_RISING);
	set_irq_chained_handler(IRQ_GPIO_H3800_ASIC, h3800_IRQ_demux);
}


#define ASIC1_OUTPUTS	 0x7fff   /* First 15 bits are used */

static void __init h3800_map_io(void)
{
	h3xxx_map_io();

	/* Add wakeup on AC plug/unplug */
	PWER  |= PWER_GPIO12;

	/* Initialize h3800-specific values here */
	GPCR = 0x0fffffff;	 /* All outputs are set low by default */
	GAFR =	GPIO_H3800_CLK_OUT |
		GPIO_LDD15 | GPIO_LDD14 | GPIO_LDD13 | GPIO_LDD12 |
		GPIO_LDD11 | GPIO_LDD10 | GPIO_LDD9  | GPIO_LDD8;
	GPDR =	GPIO_H3800_CLK_OUT |
		GPIO_H3600_COM_RTS  | GPIO_H3600_L3_CLOCK |
		GPIO_H3600_L3_MODE  | GPIO_H3600_L3_DATA  |
		GPIO_LDD15 | GPIO_LDD14 | GPIO_LDD13 | GPIO_LDD12 |
		GPIO_LDD11 | GPIO_LDD10 | GPIO_LDD9  | GPIO_LDD8;
	TUCR =	TUCR_3_6864MHz;   /* Seems to be used only for the Bluetooth UART */

	/* Fix the memory bus */
	MSC2 = (MSC2 & 0x0000ffff) | 0xE4510000;

	/* Set up ASIC #1 */
	H3800_ASIC1_GPIO_DIR		= ASIC1_OUTPUTS;	    /* All outputs */
	H3800_ASIC1_GPIO_MASK		= ASIC1_OUTPUTS;	    /* No interrupts */
	H3800_ASIC1_GPIO_SLEEP_MASK	= ASIC1_OUTPUTS;
	H3800_ASIC1_GPIO_SLEEP_DIR	= ASIC1_OUTPUTS;
	H3800_ASIC1_GPIO_SLEEP_OUT	= GPIO1_EAR_ON_N;
	H3800_ASIC1_GPIO_BATT_FAULT_DIR = ASIC1_OUTPUTS;
	H3800_ASIC1_GPIO_BATT_FAULT_OUT = GPIO1_EAR_ON_N;

	H3800_ASIC1_GPIO_OUT = GPIO1_IR_ON_N
				      | GPIO1_RS232_ON
				      | GPIO1_EAR_ON_N;

	/* Set up ASIC #2 */
	H3800_ASIC2_GPIOPIOD	= GPIO2_IN_Y1_N | GPIO2_IN_X1_N;
	H3800_ASIC2_GPOBFSTAT	= GPIO2_IN_Y1_N | GPIO2_IN_X1_N;

	H3800_ASIC2_GPIODIR	= GPIO2_PEN_IRQ
				      | GPIO2_SD_DETECT
				      | GPIO2_EAR_IN_N
				      | GPIO2_USB_DETECT_N
				      | GPIO2_SD_CON_SLT;

	/* TODO : Set sleep states & battery fault states */

	/* Clear VPP Enable */
	H3800_ASIC2_FlashWP_VPP_ON = 0;
	ipaq_model_ops = h3800_model_ops;
}

MACHINE_START(H3800, "Compaq iPAQ H3800")
	.phys_io	= 0x80000000,
	.io_pg_offst	= ((0xf8000000) >> 18) & 0xfffc,
	.boot_params	= 0xc0000100,
	.map_io		= h3800_map_io,
	.init_irq	= h3800_init_irq,
	.timer		= &sa1100_timer,
	.init_machine	= h3xxx_mach_init,
MACHINE_END

#endif /* CONFIG_SA1100_H3800 */
