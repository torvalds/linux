/*
 * linux/arch/sh/boards/renesas/rts7751r2d/irq.c
 *
 * Copyright (C) 2007  Magnus Damm
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Renesas Technology Sales RTS7751R2D Support, R2D-PLUS and R2D-1.
 *
 * Modified for RTS7751R2D by
 * Atom Create Engineering Co., Ltd. 2002.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <asm/voyagergx.h>
#include <asm/rts7751r2d.h>

#define R2D_NR_IRL 13

enum {
	UNUSED = 0,

	/* board specific interrupt sources (R2D-1 and R2D-PLUS) */
	EXT,              /* EXT_INT0-3 */
	RTC_T, RTC_A,     /* Real Time Clock */
	AX88796,          /* Ethernet controller (R2D-1 board) */
	KEY,              /* Key input (R2D-PLUS board) */
	SDCARD,           /* SD Card */
	CF_CD, CF_IDE,    /* CF Card Detect + CF IDE */
	SM501,            /* SM501 aka Voyager */
	PCI_INTD_RTL8139, /* Ethernet controller */
	PCI_INTC_PCI1520, /* Cardbus/PCMCIA bridge */
	PCI_INTB_RTL8139, /* Ethernet controller with HUB (R2D-PLUS board) */
	PCI_INTB_SLOT,    /* PCI Slot 3.3v (R2D-1 board) */
	PCI_INTA_SLOT,    /* PCI Slot 3.3v */
	TP,               /* Touch Panel */
};

#ifdef CONFIG_RTS7751R2D_1

/* Vectors for R2D-1 */
static struct intc_vect vectors_r2d_1[] __initdata = {
	INTC_IRQ(EXT, IRQ_EXT),
	INTC_IRQ(RTC_T, IRQ_RTC_T), INTC_IRQ(RTC_A, IRQ_RTC_A),
	INTC_IRQ(AX88796, IRQ_AX88796), INTC_IRQ(SDCARD, IRQ_SDCARD),
	INTC_IRQ(CF_CD, IRQ_CF_CD), INTC_IRQ(CF_IDE, IRQ_CF_IDE), /* ng */
	INTC_IRQ(SM501, IRQ_VOYAGER),
	INTC_IRQ(PCI_INTD_RTL8139, IRQ_PCI_INTD),
	INTC_IRQ(PCI_INTC_PCI1520, IRQ_PCI_INTC),
	INTC_IRQ(PCI_INTB_SLOT, IRQ_PCI_INTB),
	INTC_IRQ(PCI_INTA_SLOT, IRQ_PCI_INTA),
	INTC_IRQ(TP, IRQ_TP),
};

/* IRLMSK mask register layout for R2D-1 */
static struct intc_mask_reg mask_registers_r2d_1[] __initdata = {
	{ 0xa4000000, 0, 16, /* IRLMSK */
	  { TP, PCI_INTA_SLOT, PCI_INTB_SLOT,
	    PCI_INTC_PCI1520, PCI_INTD_RTL8139,
	    SM501, CF_IDE, CF_CD, SDCARD, AX88796,
	    RTC_A, RTC_T, 0, 0, 0, EXT } },
};

/* IRLn to IRQ table for R2D-1 */
static unsigned char irl2irq_r2d_1[R2D_NR_IRL] __initdata = {
	IRQ_PCI_INTD, IRQ_CF_IDE, IRQ_CF_CD, IRQ_PCI_INTC,
	IRQ_VOYAGER, IRQ_AX88796, IRQ_RTC_A, IRQ_RTC_T,
	IRQ_SDCARD, IRQ_PCI_INTA, IRQ_PCI_INTB, IRQ_EXT,
	IRQ_TP,
};

static DECLARE_INTC_DESC(intc_desc_r2d_1, "r2d-1", vectors_r2d_1,
			 NULL, NULL, mask_registers_r2d_1, NULL, NULL);

#endif /* CONFIG_RTS7751R2D_1 */

#ifdef CONFIG_RTS7751R2D_PLUS

/* Vectors for R2D-PLUS */
static struct intc_vect vectors_r2d_plus[] __initdata = {
	INTC_IRQ(EXT, IRQ_EXT),
	INTC_IRQ(RTC_T, IRQ_RTC_T), INTC_IRQ(RTC_A, IRQ_RTC_A),
	INTC_IRQ(KEY, IRQ_KEY), INTC_IRQ(SDCARD, IRQ_SDCARD),
	INTC_IRQ(CF_CD, IRQ_CF_CD), INTC_IRQ(CF_IDE, IRQ_CF_IDE),
	INTC_IRQ(SM501, IRQ_VOYAGER),
	INTC_IRQ(PCI_INTD_RTL8139, IRQ_PCI_INTD),
	INTC_IRQ(PCI_INTC_PCI1520, IRQ_PCI_INTC),
	INTC_IRQ(PCI_INTB_RTL8139, IRQ_PCI_INTB),
	INTC_IRQ(PCI_INTA_SLOT, IRQ_PCI_INTA),
	INTC_IRQ(TP, IRQ_TP),
};

/* IRLMSK mask register layout for R2D-PLUS */
static struct intc_mask_reg mask_registers_r2d_plus[] __initdata = {
	{ 0xa4000000, 0, 16, /* IRLMSK */
	  { TP, PCI_INTA_SLOT, PCI_INTB_RTL8139,
	    PCI_INTC_PCI1520, PCI_INTD_RTL8139,
	    SM501, CF_IDE, CF_CD, SDCARD, KEY,
	    RTC_A, RTC_T, 0, 0, 0, EXT } },
};

/* IRLn to IRQ table for R2D-PLUS */
static unsigned char irl2irq_r2d_plus[R2D_NR_IRL] __initdata = {
	IRQ_PCI_INTD, IRQ_CF_IDE, IRQ_CF_CD, IRQ_PCI_INTC,
	IRQ_VOYAGER, IRQ_KEY, IRQ_RTC_A, IRQ_RTC_T,
	IRQ_SDCARD, IRQ_PCI_INTA, IRQ_PCI_INTB, IRQ_EXT,
	IRQ_TP,
};

static DECLARE_INTC_DESC(intc_desc_r2d_plus, "r2d-plus", vectors_r2d_plus,
			 NULL, NULL, mask_registers_r2d_plus, NULL, NULL);

#endif /* CONFIG_RTS7751R2D_PLUS */

static unsigned char irl2irq[R2D_NR_IRL];

int rts7751r2d_irq_demux(int irq)
{
	if (irq >= R2D_NR_IRL || !irl2irq[irq])
		return irq;

	return irl2irq[irq];
}

/*
 * Initialize IRQ setting
 */
void __init init_rts7751r2d_IRQ(void)
{
	struct intc_desc *d;

	switch (ctrl_inw(PA_VERREG) & 0xf0) {
#ifdef CONFIG_RTS7751R2D_PLUS
	case 0x10:
		printk(KERN_INFO "Using R2D-PLUS interrupt controller.\n");
		d = &intc_desc_r2d_plus;
		memcpy(irl2irq, irl2irq_r2d_plus, R2D_NR_IRL);
		break;
#endif
#ifdef CONFIG_RTS7751R2D_1
	case 0x00: /* according to manual */
	case 0x30: /* in reality */
		printk(KERN_INFO "Using R2D-1 interrupt controller.\n");
		d = &intc_desc_r2d_1;
		memcpy(irl2irq, irl2irq_r2d_1, R2D_NR_IRL);
		break;
#endif
	default:
		printk(KERN_INFO "Unknown R2D interrupt controller 0x%04x\n",
		       ctrl_inw(PA_VERREG));
		return;
	}

	register_intc_controller(d);
#ifdef CONFIG_MFD_SM501
	setup_voyagergx_irq();
#endif
}
