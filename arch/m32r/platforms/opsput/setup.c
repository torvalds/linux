/*
 *  linux/arch/m32r/platforms/opsput/setup.c
 *
 *  Setup routines for Renesas OPSPUT Board
 *
 *  Copyright (c) 2002-2005
 * 	Hiroyuki Kondo, Hirokazu Takata,
 *      Hitoshi Yamamoto, Takeo Takahashi, Mamoru Sakugawa
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file "COPYING" in the main directory of this
 *  archive for more details.
 */

#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/system.h>
#include <asm/m32r.h>
#include <asm/io.h>

/*
 * OPSP Interrupt Control Unit (Level 1)
 */
#define irq2port(x) (M32R_ICU_CR1_PORTL + ((x - 1) * sizeof(unsigned long)))

icu_data_t icu_data[OPSPUT_NUM_CPU_IRQ];

static void disable_opsput_irq(unsigned int irq)
{
	unsigned long port, data;

	port = irq2port(irq);
	data = icu_data[irq].icucr|M32R_ICUCR_ILEVEL7;
	outl(data, port);
}

static void enable_opsput_irq(unsigned int irq)
{
	unsigned long port, data;

	port = irq2port(irq);
	data = icu_data[irq].icucr|M32R_ICUCR_IEN|M32R_ICUCR_ILEVEL6;
	outl(data, port);
}

static void mask_and_ack_opsput(unsigned int irq)
{
	disable_opsput_irq(irq);
}

static void end_opsput_irq(unsigned int irq)
{
	enable_opsput_irq(irq);
}

static unsigned int startup_opsput_irq(unsigned int irq)
{
	enable_opsput_irq(irq);
	return (0);
}

static void shutdown_opsput_irq(unsigned int irq)
{
	unsigned long port;

	port = irq2port(irq);
	outl(M32R_ICUCR_ILEVEL7, port);
}

static struct irq_chip opsput_irq_type =
{
	.typename = "OPSPUT-IRQ",
	.startup = startup_opsput_irq,
	.shutdown = shutdown_opsput_irq,
	.enable = enable_opsput_irq,
	.disable = disable_opsput_irq,
	.ack = mask_and_ack_opsput,
	.end = end_opsput_irq
};

/*
 * Interrupt Control Unit of PLD on OPSPUT (Level 2)
 */
#define irq2pldirq(x)		((x) - OPSPUT_PLD_IRQ_BASE)
#define pldirq2port(x)		(unsigned long)((int)PLD_ICUCR1 + \
				 (((x) - 1) * sizeof(unsigned short)))

typedef struct {
	unsigned short icucr;  /* ICU Control Register */
} pld_icu_data_t;

static pld_icu_data_t pld_icu_data[OPSPUT_NUM_PLD_IRQ];

static void disable_opsput_pld_irq(unsigned int irq)
{
	unsigned long port, data;
	unsigned int pldirq;

	pldirq = irq2pldirq(irq);
//	disable_opsput_irq(M32R_IRQ_INT1);
	port = pldirq2port(pldirq);
	data = pld_icu_data[pldirq].icucr|PLD_ICUCR_ILEVEL7;
	outw(data, port);
}

static void enable_opsput_pld_irq(unsigned int irq)
{
	unsigned long port, data;
	unsigned int pldirq;

	pldirq = irq2pldirq(irq);
//	enable_opsput_irq(M32R_IRQ_INT1);
	port = pldirq2port(pldirq);
	data = pld_icu_data[pldirq].icucr|PLD_ICUCR_IEN|PLD_ICUCR_ILEVEL6;
	outw(data, port);
}

static void mask_and_ack_opsput_pld(unsigned int irq)
{
	disable_opsput_pld_irq(irq);
//	mask_and_ack_opsput(M32R_IRQ_INT1);
}

static void end_opsput_pld_irq(unsigned int irq)
{
	enable_opsput_pld_irq(irq);
	end_opsput_irq(M32R_IRQ_INT1);
}

static unsigned int startup_opsput_pld_irq(unsigned int irq)
{
	enable_opsput_pld_irq(irq);
	return (0);
}

static void shutdown_opsput_pld_irq(unsigned int irq)
{
	unsigned long port;
	unsigned int pldirq;

	pldirq = irq2pldirq(irq);
//	shutdown_opsput_irq(M32R_IRQ_INT1);
	port = pldirq2port(pldirq);
	outw(PLD_ICUCR_ILEVEL7, port);
}

static struct irq_chip opsput_pld_irq_type =
{
	.typename = "OPSPUT-PLD-IRQ",
	.startup = startup_opsput_pld_irq,
	.shutdown = shutdown_opsput_pld_irq,
	.enable = enable_opsput_pld_irq,
	.disable = disable_opsput_pld_irq,
	.ack = mask_and_ack_opsput_pld,
	.end = end_opsput_pld_irq
};

/*
 * Interrupt Control Unit of PLD on OPSPUT-LAN (Level 2)
 */
#define irq2lanpldirq(x)	((x) - OPSPUT_LAN_PLD_IRQ_BASE)
#define lanpldirq2port(x)	(unsigned long)((int)OPSPUT_LAN_ICUCR1 + \
				 (((x) - 1) * sizeof(unsigned short)))

static pld_icu_data_t lanpld_icu_data[OPSPUT_NUM_LAN_PLD_IRQ];

static void disable_opsput_lanpld_irq(unsigned int irq)
{
	unsigned long port, data;
	unsigned int pldirq;

	pldirq = irq2lanpldirq(irq);
	port = lanpldirq2port(pldirq);
	data = lanpld_icu_data[pldirq].icucr|PLD_ICUCR_ILEVEL7;
	outw(data, port);
}

static void enable_opsput_lanpld_irq(unsigned int irq)
{
	unsigned long port, data;
	unsigned int pldirq;

	pldirq = irq2lanpldirq(irq);
	port = lanpldirq2port(pldirq);
	data = lanpld_icu_data[pldirq].icucr|PLD_ICUCR_IEN|PLD_ICUCR_ILEVEL6;
	outw(data, port);
}

static void mask_and_ack_opsput_lanpld(unsigned int irq)
{
	disable_opsput_lanpld_irq(irq);
}

static void end_opsput_lanpld_irq(unsigned int irq)
{
	enable_opsput_lanpld_irq(irq);
	end_opsput_irq(M32R_IRQ_INT0);
}

static unsigned int startup_opsput_lanpld_irq(unsigned int irq)
{
	enable_opsput_lanpld_irq(irq);
	return (0);
}

static void shutdown_opsput_lanpld_irq(unsigned int irq)
{
	unsigned long port;
	unsigned int pldirq;

	pldirq = irq2lanpldirq(irq);
	port = lanpldirq2port(pldirq);
	outw(PLD_ICUCR_ILEVEL7, port);
}

static struct irq_chip opsput_lanpld_irq_type =
{
	.typename = "OPSPUT-PLD-LAN-IRQ",
	.startup = startup_opsput_lanpld_irq,
	.shutdown = shutdown_opsput_lanpld_irq,
	.enable = enable_opsput_lanpld_irq,
	.disable = disable_opsput_lanpld_irq,
	.ack = mask_and_ack_opsput_lanpld,
	.end = end_opsput_lanpld_irq
};

/*
 * Interrupt Control Unit of PLD on OPSPUT-LCD (Level 2)
 */
#define irq2lcdpldirq(x)	((x) - OPSPUT_LCD_PLD_IRQ_BASE)
#define lcdpldirq2port(x)	(unsigned long)((int)OPSPUT_LCD_ICUCR1 + \
				 (((x) - 1) * sizeof(unsigned short)))

static pld_icu_data_t lcdpld_icu_data[OPSPUT_NUM_LCD_PLD_IRQ];

static void disable_opsput_lcdpld_irq(unsigned int irq)
{
	unsigned long port, data;
	unsigned int pldirq;

	pldirq = irq2lcdpldirq(irq);
	port = lcdpldirq2port(pldirq);
	data = lcdpld_icu_data[pldirq].icucr|PLD_ICUCR_ILEVEL7;
	outw(data, port);
}

static void enable_opsput_lcdpld_irq(unsigned int irq)
{
	unsigned long port, data;
	unsigned int pldirq;

	pldirq = irq2lcdpldirq(irq);
	port = lcdpldirq2port(pldirq);
	data = lcdpld_icu_data[pldirq].icucr|PLD_ICUCR_IEN|PLD_ICUCR_ILEVEL6;
	outw(data, port);
}

static void mask_and_ack_opsput_lcdpld(unsigned int irq)
{
	disable_opsput_lcdpld_irq(irq);
}

static void end_opsput_lcdpld_irq(unsigned int irq)
{
	enable_opsput_lcdpld_irq(irq);
	end_opsput_irq(M32R_IRQ_INT2);
}

static unsigned int startup_opsput_lcdpld_irq(unsigned int irq)
{
	enable_opsput_lcdpld_irq(irq);
	return (0);
}

static void shutdown_opsput_lcdpld_irq(unsigned int irq)
{
	unsigned long port;
	unsigned int pldirq;

	pldirq = irq2lcdpldirq(irq);
	port = lcdpldirq2port(pldirq);
	outw(PLD_ICUCR_ILEVEL7, port);
}

static struct irq_chip opsput_lcdpld_irq_type =
{
	"OPSPUT-PLD-LCD-IRQ",
	startup_opsput_lcdpld_irq,
	shutdown_opsput_lcdpld_irq,
	enable_opsput_lcdpld_irq,
	disable_opsput_lcdpld_irq,
	mask_and_ack_opsput_lcdpld,
	end_opsput_lcdpld_irq
};

void __init init_IRQ(void)
{
#if defined(CONFIG_SMC91X)
	/* INT#0: LAN controller on OPSPUT-LAN (SMC91C111)*/
	irq_desc[OPSPUT_LAN_IRQ_LAN].status = IRQ_DISABLED;
	irq_desc[OPSPUT_LAN_IRQ_LAN].chip = &opsput_lanpld_irq_type;
	irq_desc[OPSPUT_LAN_IRQ_LAN].action = 0;
	irq_desc[OPSPUT_LAN_IRQ_LAN].depth = 1;	/* disable nested irq */
	lanpld_icu_data[irq2lanpldirq(OPSPUT_LAN_IRQ_LAN)].icucr = PLD_ICUCR_IEN|PLD_ICUCR_ISMOD02;	/* "H" edge sense */
	disable_opsput_lanpld_irq(OPSPUT_LAN_IRQ_LAN);
#endif  /* CONFIG_SMC91X */

	/* MFT2 : system timer */
	irq_desc[M32R_IRQ_MFT2].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_MFT2].chip = &opsput_irq_type;
	irq_desc[M32R_IRQ_MFT2].action = 0;
	irq_desc[M32R_IRQ_MFT2].depth = 1;
	icu_data[M32R_IRQ_MFT2].icucr = M32R_ICUCR_IEN;
	disable_opsput_irq(M32R_IRQ_MFT2);

	/* SIO0 : receive */
	irq_desc[M32R_IRQ_SIO0_R].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO0_R].chip = &opsput_irq_type;
	irq_desc[M32R_IRQ_SIO0_R].action = 0;
	irq_desc[M32R_IRQ_SIO0_R].depth = 1;
	icu_data[M32R_IRQ_SIO0_R].icucr = 0;
	disable_opsput_irq(M32R_IRQ_SIO0_R);

	/* SIO0 : send */
	irq_desc[M32R_IRQ_SIO0_S].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO0_S].chip = &opsput_irq_type;
	irq_desc[M32R_IRQ_SIO0_S].action = 0;
	irq_desc[M32R_IRQ_SIO0_S].depth = 1;
	icu_data[M32R_IRQ_SIO0_S].icucr = 0;
	disable_opsput_irq(M32R_IRQ_SIO0_S);

	/* SIO1 : receive */
	irq_desc[M32R_IRQ_SIO1_R].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO1_R].chip = &opsput_irq_type;
	irq_desc[M32R_IRQ_SIO1_R].action = 0;
	irq_desc[M32R_IRQ_SIO1_R].depth = 1;
	icu_data[M32R_IRQ_SIO1_R].icucr = 0;
	disable_opsput_irq(M32R_IRQ_SIO1_R);

	/* SIO1 : send */
	irq_desc[M32R_IRQ_SIO1_S].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_SIO1_S].chip = &opsput_irq_type;
	irq_desc[M32R_IRQ_SIO1_S].action = 0;
	irq_desc[M32R_IRQ_SIO1_S].depth = 1;
	icu_data[M32R_IRQ_SIO1_S].icucr = 0;
	disable_opsput_irq(M32R_IRQ_SIO1_S);

	/* DMA1 : */
	irq_desc[M32R_IRQ_DMA1].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_DMA1].chip = &opsput_irq_type;
	irq_desc[M32R_IRQ_DMA1].action = 0;
	irq_desc[M32R_IRQ_DMA1].depth = 1;
	icu_data[M32R_IRQ_DMA1].icucr = 0;
	disable_opsput_irq(M32R_IRQ_DMA1);

#ifdef CONFIG_SERIAL_M32R_PLDSIO
	/* INT#1: SIO0 Receive on PLD */
	irq_desc[PLD_IRQ_SIO0_RCV].status = IRQ_DISABLED;
	irq_desc[PLD_IRQ_SIO0_RCV].chip = &opsput_pld_irq_type;
	irq_desc[PLD_IRQ_SIO0_RCV].action = 0;
	irq_desc[PLD_IRQ_SIO0_RCV].depth = 1;	/* disable nested irq */
	pld_icu_data[irq2pldirq(PLD_IRQ_SIO0_RCV)].icucr = PLD_ICUCR_IEN|PLD_ICUCR_ISMOD03;
	disable_opsput_pld_irq(PLD_IRQ_SIO0_RCV);

	/* INT#1: SIO0 Send on PLD */
	irq_desc[PLD_IRQ_SIO0_SND].status = IRQ_DISABLED;
	irq_desc[PLD_IRQ_SIO0_SND].chip = &opsput_pld_irq_type;
	irq_desc[PLD_IRQ_SIO0_SND].action = 0;
	irq_desc[PLD_IRQ_SIO0_SND].depth = 1;	/* disable nested irq */
	pld_icu_data[irq2pldirq(PLD_IRQ_SIO0_SND)].icucr = PLD_ICUCR_IEN|PLD_ICUCR_ISMOD03;
	disable_opsput_pld_irq(PLD_IRQ_SIO0_SND);
#endif  /* CONFIG_SERIAL_M32R_PLDSIO */

	/* INT#1: CFC IREQ on PLD */
	irq_desc[PLD_IRQ_CFIREQ].status = IRQ_DISABLED;
	irq_desc[PLD_IRQ_CFIREQ].chip = &opsput_pld_irq_type;
	irq_desc[PLD_IRQ_CFIREQ].action = 0;
	irq_desc[PLD_IRQ_CFIREQ].depth = 1;	/* disable nested irq */
	pld_icu_data[irq2pldirq(PLD_IRQ_CFIREQ)].icucr = PLD_ICUCR_IEN|PLD_ICUCR_ISMOD01;	/* 'L' level sense */
	disable_opsput_pld_irq(PLD_IRQ_CFIREQ);

	/* INT#1: CFC Insert on PLD */
	irq_desc[PLD_IRQ_CFC_INSERT].status = IRQ_DISABLED;
	irq_desc[PLD_IRQ_CFC_INSERT].chip = &opsput_pld_irq_type;
	irq_desc[PLD_IRQ_CFC_INSERT].action = 0;
	irq_desc[PLD_IRQ_CFC_INSERT].depth = 1;	/* disable nested irq */
	pld_icu_data[irq2pldirq(PLD_IRQ_CFC_INSERT)].icucr = PLD_ICUCR_IEN|PLD_ICUCR_ISMOD00;	/* 'L' edge sense */
	disable_opsput_pld_irq(PLD_IRQ_CFC_INSERT);

	/* INT#1: CFC Eject on PLD */
	irq_desc[PLD_IRQ_CFC_EJECT].status = IRQ_DISABLED;
	irq_desc[PLD_IRQ_CFC_EJECT].chip = &opsput_pld_irq_type;
	irq_desc[PLD_IRQ_CFC_EJECT].action = 0;
	irq_desc[PLD_IRQ_CFC_EJECT].depth = 1;	/* disable nested irq */
	pld_icu_data[irq2pldirq(PLD_IRQ_CFC_EJECT)].icucr = PLD_ICUCR_IEN|PLD_ICUCR_ISMOD02;	/* 'H' edge sense */
	disable_opsput_pld_irq(PLD_IRQ_CFC_EJECT);

	/*
	 * INT0# is used for LAN, DIO
	 * We enable it here.
	 */
	icu_data[M32R_IRQ_INT0].icucr = M32R_ICUCR_IEN|M32R_ICUCR_ISMOD11;
	enable_opsput_irq(M32R_IRQ_INT0);

	/*
	 * INT1# is used for UART, MMC, CF Controller in FPGA.
	 * We enable it here.
	 */
	icu_data[M32R_IRQ_INT1].icucr = M32R_ICUCR_IEN|M32R_ICUCR_ISMOD11;
	enable_opsput_irq(M32R_IRQ_INT1);

#if defined(CONFIG_USB)
	outw(USBCR_OTGS, USBCR); 	/* USBCR: non-OTG */

    irq_desc[OPSPUT_LCD_IRQ_USB_INT1].status = IRQ_DISABLED;
    irq_desc[OPSPUT_LCD_IRQ_USB_INT1].chip = &opsput_lcdpld_irq_type;
    irq_desc[OPSPUT_LCD_IRQ_USB_INT1].action = 0;
    irq_desc[OPSPUT_LCD_IRQ_USB_INT1].depth = 1;
    lcdpld_icu_data[irq2lcdpldirq(OPSPUT_LCD_IRQ_USB_INT1)].icucr = PLD_ICUCR_IEN|PLD_ICUCR_ISMOD01;	/* "L" level sense */
    disable_opsput_lcdpld_irq(OPSPUT_LCD_IRQ_USB_INT1);
#endif
	/*
	 * INT2# is used for BAT, USB, AUDIO
	 * We enable it here.
	 */
	icu_data[M32R_IRQ_INT2].icucr = M32R_ICUCR_IEN|M32R_ICUCR_ISMOD01;
	enable_opsput_irq(M32R_IRQ_INT2);

#if defined(CONFIG_VIDEO_M32R_AR)
	/*
	 * INT3# is used for AR
	 */
	irq_desc[M32R_IRQ_INT3].status = IRQ_DISABLED;
	irq_desc[M32R_IRQ_INT3].chip = &opsput_irq_type;
	irq_desc[M32R_IRQ_INT3].action = 0;
	irq_desc[M32R_IRQ_INT3].depth = 1;
	icu_data[M32R_IRQ_INT3].icucr = M32R_ICUCR_IEN|M32R_ICUCR_ISMOD10;
	disable_opsput_irq(M32R_IRQ_INT3);
#endif /* CONFIG_VIDEO_M32R_AR */
}

#if defined(CONFIG_SMC91X)

#define LAN_IOSTART     0x300
#define LAN_IOEND       0x320
static struct resource smc91x_resources[] = {
	[0] = {
		.start  = (LAN_IOSTART),
		.end    = (LAN_IOEND),
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = OPSPUT_LAN_IRQ_LAN,
		.end    = OPSPUT_LAN_IRQ_LAN,
		.flags  = IORESOURCE_IRQ,
	}
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources  = ARRAY_SIZE(smc91x_resources),
	.resource       = smc91x_resources,
};
#endif

#if defined(CONFIG_FB_S1D13XXX)

#include <video/s1d13xxxfb.h>
#include <asm/s1d13806.h>

static struct s1d13xxxfb_pdata s1d13xxxfb_data = {
	.initregs		= s1d13xxxfb_initregs,
	.initregssize		= ARRAY_SIZE(s1d13xxxfb_initregs),
	.platform_init_video	= NULL,
#ifdef CONFIG_PM
	.platform_suspend_video	= NULL,
	.platform_resume_video	= NULL,
#endif
};

static struct resource s1d13xxxfb_resources[] = {
	[0] = {
		.start  = 0x10600000UL,
		.end    = 0x1073FFFFUL,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = 0x10400000UL,
		.end    = 0x104001FFUL,
		.flags  = IORESOURCE_MEM,
	}
};

static struct platform_device s1d13xxxfb_device = {
	.name		= S1D_DEVICENAME,
	.id		= 0,
	.dev            = {
		.platform_data  = &s1d13xxxfb_data,
	},
	.num_resources  = ARRAY_SIZE(s1d13xxxfb_resources),
	.resource       = s1d13xxxfb_resources,
};
#endif

static int __init platform_init(void)
{
#if defined(CONFIG_SMC91X)
	platform_device_register(&smc91x_device);
#endif
#if defined(CONFIG_FB_S1D13XXX)
	platform_device_register(&s1d13xxxfb_device);
#endif
	return 0;
}
arch_initcall(platform_init);
