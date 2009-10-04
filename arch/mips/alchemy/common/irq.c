/*
 * Copyright 2001, 2007-2008 MontaVista Software Inc.
 * Author: MontaVista Software, Inc. <source@mvista.com>
 *
 * Copyright (C) 2007 Ralf Baechle (ralf@linux-mips.org)
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED	  ``AS	IS'' AND   ANY	EXPRESS OR IMPLIED
 *  WARRANTIES,	  INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO	EVENT  SHALL   THE AUTHOR  BE	 LIABLE FOR ANY	  DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED	  TO, PROCUREMENT OF  SUBSTITUTE GOODS	OR SERVICES; LOSS OF
 *  USE, DATA,	OR PROFITS; OR	BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN	 CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <asm/mach-au1x00/au1000.h>
#ifdef CONFIG_MIPS_PB1000
#include <asm/mach-pb1x00/pb1000.h>
#endif

static int au1x_ic_settype(unsigned int irq, unsigned int flow_type);

/* per-processor fixed function irqs */
struct au1xxx_irqmap {
	int im_irq;
	int im_type;
	int im_request;
} au1xxx_ic0_map[] __initdata = {
#if defined(CONFIG_SOC_AU1000)
	{ AU1000_UART0_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_UART1_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_UART2_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_UART3_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_SSI0_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_SSI1_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+1, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+2, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+3, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+4, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+5, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+6, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+7, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_TOY_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_TOY_MATCH0_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_TOY_MATCH1_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_TOY_MATCH2_INT, IRQ_TYPE_EDGE_RISING, 1 },
	{ AU1000_RTC_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_RTC_MATCH0_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_RTC_MATCH1_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_RTC_MATCH2_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_IRDA_TX_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_IRDA_RX_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_USB_DEV_REQ_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_USB_DEV_SUS_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_USB_HOST_INT, IRQ_TYPE_LEVEL_LOW, 0 },
	{ AU1000_ACSYNC_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_MAC0_DMA_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_MAC1_DMA_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_AC97C_INT, IRQ_TYPE_EDGE_RISING, 0 },

#elif defined(CONFIG_SOC_AU1500)

	{ AU1500_UART0_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_PCI_INTA, IRQ_TYPE_LEVEL_LOW, 0 },
	{ AU1000_PCI_INTB, IRQ_TYPE_LEVEL_LOW, 0 },
	{ AU1500_UART3_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_PCI_INTC, IRQ_TYPE_LEVEL_LOW, 0 },
	{ AU1000_PCI_INTD, IRQ_TYPE_LEVEL_LOW, 0 },
	{ AU1000_DMA_INT_BASE, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+1, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+2, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+3, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+4, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+5, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+6, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+7, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_TOY_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_TOY_MATCH0_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_TOY_MATCH1_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_TOY_MATCH2_INT, IRQ_TYPE_EDGE_RISING, 1 },
	{ AU1000_RTC_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_RTC_MATCH0_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_RTC_MATCH1_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_RTC_MATCH2_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_USB_DEV_REQ_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_USB_DEV_SUS_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_USB_HOST_INT, IRQ_TYPE_LEVEL_LOW, 0 },
	{ AU1000_ACSYNC_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1500_MAC0_DMA_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1500_MAC1_DMA_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_AC97C_INT, IRQ_TYPE_EDGE_RISING, 0 },

#elif defined(CONFIG_SOC_AU1100)

	{ AU1100_UART0_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1100_UART1_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1100_SD_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1100_UART3_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_SSI0_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_SSI1_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+1, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+2, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+3, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+4, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+5, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+6, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_DMA_INT_BASE+7, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_TOY_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_TOY_MATCH0_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_TOY_MATCH1_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_TOY_MATCH2_INT, IRQ_TYPE_EDGE_RISING, 1 },
	{ AU1000_RTC_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_RTC_MATCH0_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_RTC_MATCH1_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_RTC_MATCH2_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_IRDA_TX_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_IRDA_RX_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_USB_DEV_REQ_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_USB_DEV_SUS_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_USB_HOST_INT, IRQ_TYPE_LEVEL_LOW, 0 },
	{ AU1000_ACSYNC_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1100_MAC0_DMA_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1100_LCD_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_AC97C_INT, IRQ_TYPE_EDGE_RISING, 0 },

#elif defined(CONFIG_SOC_AU1550)

	{ AU1550_UART0_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1550_PCI_INTA, IRQ_TYPE_LEVEL_LOW, 0 },
	{ AU1550_PCI_INTB, IRQ_TYPE_LEVEL_LOW, 0 },
	{ AU1550_DDMA_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1550_CRYPTO_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1550_PCI_INTC, IRQ_TYPE_LEVEL_LOW, 0 },
	{ AU1550_PCI_INTD, IRQ_TYPE_LEVEL_LOW, 0 },
	{ AU1550_PCI_RST_INT, IRQ_TYPE_LEVEL_LOW, 0 },
	{ AU1550_UART1_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1550_UART3_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1550_PSC0_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1550_PSC1_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1550_PSC2_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1550_PSC3_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_TOY_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_TOY_MATCH0_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_TOY_MATCH1_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_TOY_MATCH2_INT, IRQ_TYPE_EDGE_RISING, 1 },
	{ AU1000_RTC_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_RTC_MATCH0_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_RTC_MATCH1_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_RTC_MATCH2_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1550_NAND_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1550_USB_DEV_REQ_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1550_USB_DEV_SUS_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1550_USB_HOST_INT, IRQ_TYPE_LEVEL_LOW, 0 },
	{ AU1550_MAC0_DMA_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1550_MAC1_DMA_INT, IRQ_TYPE_LEVEL_HIGH, 0 },

#elif defined(CONFIG_SOC_AU1200)

	{ AU1200_UART0_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1200_SWT_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1200_SD_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1200_DDMA_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1200_MAE_BE_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1200_UART1_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1200_MAE_FE_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1200_PSC0_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1200_PSC1_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1200_AES_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1200_CAMERA_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1000_TOY_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_TOY_MATCH0_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_TOY_MATCH1_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_TOY_MATCH2_INT, IRQ_TYPE_EDGE_RISING, 1 },
	{ AU1000_RTC_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_RTC_MATCH0_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_RTC_MATCH1_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1000_RTC_MATCH2_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1200_NAND_INT, IRQ_TYPE_EDGE_RISING, 0 },
	{ AU1200_USB_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1200_LCD_INT, IRQ_TYPE_LEVEL_HIGH, 0 },
	{ AU1200_MAE_BOTH_INT, IRQ_TYPE_LEVEL_HIGH, 0 },

#else
#error "Error: Unknown Alchemy SOC"
#endif
};


#ifdef CONFIG_PM

/*
 * Save/restore the interrupt controller state.
 * Called from the save/restore core registers as part of the
 * au_sleep function in power.c.....maybe I should just pm_register()
 * them instead?
 */
static unsigned int	sleep_intctl_config0[2];
static unsigned int	sleep_intctl_config1[2];
static unsigned int	sleep_intctl_config2[2];
static unsigned int	sleep_intctl_src[2];
static unsigned int	sleep_intctl_assign[2];
static unsigned int	sleep_intctl_wake[2];
static unsigned int	sleep_intctl_mask[2];

void save_au1xxx_intctl(void)
{
	sleep_intctl_config0[0] = au_readl(IC0_CFG0RD);
	sleep_intctl_config1[0] = au_readl(IC0_CFG1RD);
	sleep_intctl_config2[0] = au_readl(IC0_CFG2RD);
	sleep_intctl_src[0] = au_readl(IC0_SRCRD);
	sleep_intctl_assign[0] = au_readl(IC0_ASSIGNRD);
	sleep_intctl_wake[0] = au_readl(IC0_WAKERD);
	sleep_intctl_mask[0] = au_readl(IC0_MASKRD);

	sleep_intctl_config0[1] = au_readl(IC1_CFG0RD);
	sleep_intctl_config1[1] = au_readl(IC1_CFG1RD);
	sleep_intctl_config2[1] = au_readl(IC1_CFG2RD);
	sleep_intctl_src[1] = au_readl(IC1_SRCRD);
	sleep_intctl_assign[1] = au_readl(IC1_ASSIGNRD);
	sleep_intctl_wake[1] = au_readl(IC1_WAKERD);
	sleep_intctl_mask[1] = au_readl(IC1_MASKRD);
}

/*
 * For most restore operations, we clear the entire register and
 * then set the bits we found during the save.
 */
void restore_au1xxx_intctl(void)
{
	au_writel(0xffffffff, IC0_MASKCLR); au_sync();

	au_writel(0xffffffff, IC0_CFG0CLR); au_sync();
	au_writel(sleep_intctl_config0[0], IC0_CFG0SET); au_sync();
	au_writel(0xffffffff, IC0_CFG1CLR); au_sync();
	au_writel(sleep_intctl_config1[0], IC0_CFG1SET); au_sync();
	au_writel(0xffffffff, IC0_CFG2CLR); au_sync();
	au_writel(sleep_intctl_config2[0], IC0_CFG2SET); au_sync();
	au_writel(0xffffffff, IC0_SRCCLR); au_sync();
	au_writel(sleep_intctl_src[0], IC0_SRCSET); au_sync();
	au_writel(0xffffffff, IC0_ASSIGNCLR); au_sync();
	au_writel(sleep_intctl_assign[0], IC0_ASSIGNSET); au_sync();
	au_writel(0xffffffff, IC0_WAKECLR); au_sync();
	au_writel(sleep_intctl_wake[0], IC0_WAKESET); au_sync();
	au_writel(0xffffffff, IC0_RISINGCLR); au_sync();
	au_writel(0xffffffff, IC0_FALLINGCLR); au_sync();
	au_writel(0x00000000, IC0_TESTBIT); au_sync();

	au_writel(0xffffffff, IC1_MASKCLR); au_sync();

	au_writel(0xffffffff, IC1_CFG0CLR); au_sync();
	au_writel(sleep_intctl_config0[1], IC1_CFG0SET); au_sync();
	au_writel(0xffffffff, IC1_CFG1CLR); au_sync();
	au_writel(sleep_intctl_config1[1], IC1_CFG1SET); au_sync();
	au_writel(0xffffffff, IC1_CFG2CLR); au_sync();
	au_writel(sleep_intctl_config2[1], IC1_CFG2SET); au_sync();
	au_writel(0xffffffff, IC1_SRCCLR); au_sync();
	au_writel(sleep_intctl_src[1], IC1_SRCSET); au_sync();
	au_writel(0xffffffff, IC1_ASSIGNCLR); au_sync();
	au_writel(sleep_intctl_assign[1], IC1_ASSIGNSET); au_sync();
	au_writel(0xffffffff, IC1_WAKECLR); au_sync();
	au_writel(sleep_intctl_wake[1], IC1_WAKESET); au_sync();
	au_writel(0xffffffff, IC1_RISINGCLR); au_sync();
	au_writel(0xffffffff, IC1_FALLINGCLR); au_sync();
	au_writel(0x00000000, IC1_TESTBIT); au_sync();

	au_writel(sleep_intctl_mask[1], IC1_MASKSET); au_sync();

	au_writel(sleep_intctl_mask[0], IC0_MASKSET); au_sync();
}
#endif /* CONFIG_PM */


static void au1x_ic0_unmask(unsigned int irq_nr)
{
	unsigned int bit = irq_nr - AU1000_INTC0_INT_BASE;
	au_writel(1 << bit, IC0_MASKSET);
	au_writel(1 << bit, IC0_WAKESET);
	au_sync();
}

static void au1x_ic1_unmask(unsigned int irq_nr)
{
	unsigned int bit = irq_nr - AU1000_INTC1_INT_BASE;
	au_writel(1 << bit, IC1_MASKSET);
	au_writel(1 << bit, IC1_WAKESET);

/* very hacky. does the pb1000 cpld auto-disable this int?
 * nowhere in the current kernel sources is it disabled.	--mlau
 */
#if defined(CONFIG_MIPS_PB1000)
	if (irq_nr == AU1000_GPIO_15)
		au_writel(0x4000, PB1000_MDR); /* enable int */
#endif
	au_sync();
}

static void au1x_ic0_mask(unsigned int irq_nr)
{
	unsigned int bit = irq_nr - AU1000_INTC0_INT_BASE;
	au_writel(1 << bit, IC0_MASKCLR);
	au_writel(1 << bit, IC0_WAKECLR);
	au_sync();
}

static void au1x_ic1_mask(unsigned int irq_nr)
{
	unsigned int bit = irq_nr - AU1000_INTC1_INT_BASE;
	au_writel(1 << bit, IC1_MASKCLR);
	au_writel(1 << bit, IC1_WAKECLR);
	au_sync();
}

static void au1x_ic0_ack(unsigned int irq_nr)
{
	unsigned int bit = irq_nr - AU1000_INTC0_INT_BASE;

	/*
	 * This may assume that we don't get interrupts from
	 * both edges at once, or if we do, that we don't care.
	 */
	au_writel(1 << bit, IC0_FALLINGCLR);
	au_writel(1 << bit, IC0_RISINGCLR);
	au_sync();
}

static void au1x_ic1_ack(unsigned int irq_nr)
{
	unsigned int bit = irq_nr - AU1000_INTC1_INT_BASE;

	/*
	 * This may assume that we don't get interrupts from
	 * both edges at once, or if we do, that we don't care.
	 */
	au_writel(1 << bit, IC1_FALLINGCLR);
	au_writel(1 << bit, IC1_RISINGCLR);
	au_sync();
}

static void au1x_ic0_maskack(unsigned int irq_nr)
{
	unsigned int bit = irq_nr - AU1000_INTC0_INT_BASE;

	au_writel(1 << bit, IC0_WAKECLR);
	au_writel(1 << bit, IC0_MASKCLR);
	au_writel(1 << bit, IC0_RISINGCLR);
	au_writel(1 << bit, IC0_FALLINGCLR);
	au_sync();
}

static void au1x_ic1_maskack(unsigned int irq_nr)
{
	unsigned int bit = irq_nr - AU1000_INTC1_INT_BASE;

	au_writel(1 << bit, IC1_WAKECLR);
	au_writel(1 << bit, IC1_MASKCLR);
	au_writel(1 << bit, IC1_RISINGCLR);
	au_writel(1 << bit, IC1_FALLINGCLR);
	au_sync();
}

static int au1x_ic1_setwake(unsigned int irq, unsigned int on)
{
	unsigned int bit = irq - AU1000_INTC1_INT_BASE;
	unsigned long wakemsk, flags;

	/* only GPIO 0-7 can act as wakeup source: */
	if ((irq < AU1000_GPIO_0) || (irq > AU1000_GPIO_7))
		return -EINVAL;

	local_irq_save(flags);
	wakemsk = au_readl(SYS_WAKEMSK);
	if (on)
		wakemsk |= 1 << bit;
	else
		wakemsk &= ~(1 << bit);
	au_writel(wakemsk, SYS_WAKEMSK);
	au_sync();
	local_irq_restore(flags);

	return 0;
}

/*
 * irq_chips for both ICs; this way the mask handlers can be
 * as short as possible.
 */
static struct irq_chip au1x_ic0_chip = {
	.name		= "Alchemy-IC0",
	.ack		= au1x_ic0_ack,
	.mask		= au1x_ic0_mask,
	.mask_ack	= au1x_ic0_maskack,
	.unmask		= au1x_ic0_unmask,
	.set_type	= au1x_ic_settype,
};

static struct irq_chip au1x_ic1_chip = {
	.name		= "Alchemy-IC1",
	.ack		= au1x_ic1_ack,
	.mask		= au1x_ic1_mask,
	.mask_ack	= au1x_ic1_maskack,
	.unmask		= au1x_ic1_unmask,
	.set_type	= au1x_ic_settype,
	.set_wake	= au1x_ic1_setwake,
};

static int au1x_ic_settype(unsigned int irq, unsigned int flow_type)
{
	struct irq_chip *chip;
	unsigned long icr[6];
	unsigned int bit, ic;
	int ret;

	if (irq >= AU1000_INTC1_INT_BASE) {
		bit = irq - AU1000_INTC1_INT_BASE;
		chip = &au1x_ic1_chip;
		ic = 1;
	} else {
		bit = irq - AU1000_INTC0_INT_BASE;
		chip = &au1x_ic0_chip;
		ic = 0;
	}

	if (bit > 31)
		return -EINVAL;

	icr[0] = ic ? IC1_CFG0SET : IC0_CFG0SET;
	icr[1] = ic ? IC1_CFG1SET : IC0_CFG1SET;
	icr[2] = ic ? IC1_CFG2SET : IC0_CFG2SET;
	icr[3] = ic ? IC1_CFG0CLR : IC0_CFG0CLR;
	icr[4] = ic ? IC1_CFG1CLR : IC0_CFG1CLR;
	icr[5] = ic ? IC1_CFG2CLR : IC0_CFG2CLR;

	ret = 0;

	switch (flow_type) {	/* cfgregs 2:1:0 */
	case IRQ_TYPE_EDGE_RISING:	/* 0:0:1 */
		au_writel(1 << bit, icr[5]);
		au_writel(1 << bit, icr[4]);
		au_writel(1 << bit, icr[0]);
		set_irq_chip_and_handler_name(irq, chip,
				handle_edge_irq, "riseedge");
		break;
	case IRQ_TYPE_EDGE_FALLING:	/* 0:1:0 */
		au_writel(1 << bit, icr[5]);
		au_writel(1 << bit, icr[1]);
		au_writel(1 << bit, icr[3]);
		set_irq_chip_and_handler_name(irq, chip,
				handle_edge_irq, "falledge");
		break;
	case IRQ_TYPE_EDGE_BOTH:	/* 0:1:1 */
		au_writel(1 << bit, icr[5]);
		au_writel(1 << bit, icr[1]);
		au_writel(1 << bit, icr[0]);
		set_irq_chip_and_handler_name(irq, chip,
				handle_edge_irq, "bothedge");
		break;
	case IRQ_TYPE_LEVEL_HIGH:	/* 1:0:1 */
		au_writel(1 << bit, icr[2]);
		au_writel(1 << bit, icr[4]);
		au_writel(1 << bit, icr[0]);
		set_irq_chip_and_handler_name(irq, chip,
				handle_level_irq, "hilevel");
		break;
	case IRQ_TYPE_LEVEL_LOW:	/* 1:1:0 */
		au_writel(1 << bit, icr[2]);
		au_writel(1 << bit, icr[1]);
		au_writel(1 << bit, icr[3]);
		set_irq_chip_and_handler_name(irq, chip,
				handle_level_irq, "lowlevel");
		break;
	case IRQ_TYPE_NONE:		/* 0:0:0 */
		au_writel(1 << bit, icr[5]);
		au_writel(1 << bit, icr[4]);
		au_writel(1 << bit, icr[3]);
		/* set at least chip so we can call set_irq_type() on it */
		set_irq_chip(irq, chip);
		break;
	default:
		ret = -EINVAL;
	}
	au_sync();

	return ret;
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_status() & read_c0_cause();
	unsigned long s, off, bit;

	if (pending & CAUSEF_IP7) {
		do_IRQ(MIPS_CPU_IRQ_BASE + 7);
		return;
	} else if (pending & CAUSEF_IP2) {
		s = IC0_REQ0INT;
		off = AU1000_INTC0_INT_BASE;
	} else if (pending & CAUSEF_IP3) {
		s = IC0_REQ1INT;
		off = AU1000_INTC0_INT_BASE;
	} else if (pending & CAUSEF_IP4) {
		s = IC1_REQ0INT;
		off = AU1000_INTC1_INT_BASE;
	} else if (pending & CAUSEF_IP5) {
		s = IC1_REQ1INT;
		off = AU1000_INTC1_INT_BASE;
	} else
		goto spurious;

	bit = 0;
	s = au_readl(s);
	if (unlikely(!s)) {
spurious:
		spurious_interrupt();
		return;
	}
#ifdef AU1000_USB_DEV_REQ_INT
	/*
	 * Because of the tight timing of SETUP token to reply
	 * transactions, the USB devices-side packet complete
	 * interrupt needs the highest priority.
	 */
	bit = 1 << (AU1000_USB_DEV_REQ_INT - AU1000_INTC0_INT_BASE);
	if ((pending & CAUSEF_IP2) && (s & bit)) {
		do_IRQ(AU1000_USB_DEV_REQ_INT);
		return;
	}
#endif
	do_IRQ(__ffs(s) + off);
}

/* setup edge/level and assign request 0/1 */
static void __init setup_irqmap(struct au1xxx_irqmap *map, int count)
{
	unsigned int bit, irq_nr;

	while (count--) {
		irq_nr = map[count].im_irq;

		if (((irq_nr < AU1000_INTC0_INT_BASE) ||
		     (irq_nr >= AU1000_INTC0_INT_BASE + 32)) &&
		    ((irq_nr < AU1000_INTC1_INT_BASE) ||
		     (irq_nr >= AU1000_INTC1_INT_BASE + 32)))
			continue;

		if (irq_nr >= AU1000_INTC1_INT_BASE) {
			bit = irq_nr - AU1000_INTC1_INT_BASE;
			if (map[count].im_request)
				au_writel(1 << bit, IC1_ASSIGNCLR);
		} else {
			bit = irq_nr - AU1000_INTC0_INT_BASE;
			if (map[count].im_request)
				au_writel(1 << bit, IC0_ASSIGNCLR);
		}

		au1x_ic_settype(irq_nr, map[count].im_type);
	}
}

void __init arch_init_irq(void)
{
	int i;

	/*
	 * Initialize interrupt controllers to a safe state.
	 */
	au_writel(0xffffffff, IC0_CFG0CLR);
	au_writel(0xffffffff, IC0_CFG1CLR);
	au_writel(0xffffffff, IC0_CFG2CLR);
	au_writel(0xffffffff, IC0_MASKCLR);
	au_writel(0xffffffff, IC0_ASSIGNSET);
	au_writel(0xffffffff, IC0_WAKECLR);
	au_writel(0xffffffff, IC0_SRCSET);
	au_writel(0xffffffff, IC0_FALLINGCLR);
	au_writel(0xffffffff, IC0_RISINGCLR);
	au_writel(0x00000000, IC0_TESTBIT);

	au_writel(0xffffffff, IC1_CFG0CLR);
	au_writel(0xffffffff, IC1_CFG1CLR);
	au_writel(0xffffffff, IC1_CFG2CLR);
	au_writel(0xffffffff, IC1_MASKCLR);
	au_writel(0xffffffff, IC1_ASSIGNSET);
	au_writel(0xffffffff, IC1_WAKECLR);
	au_writel(0xffffffff, IC1_SRCSET);
	au_writel(0xffffffff, IC1_FALLINGCLR);
	au_writel(0xffffffff, IC1_RISINGCLR);
	au_writel(0x00000000, IC1_TESTBIT);

	mips_cpu_irq_init();

	/* register all 64 possible IC0+IC1 irq sources as type "none".
	 * Use set_irq_type() to set edge/level behaviour at runtime.
	 */
	for (i = AU1000_INTC0_INT_BASE;
	     (i < AU1000_INTC0_INT_BASE + 32); i++)
		au1x_ic_settype(i, IRQ_TYPE_NONE);

	for (i = AU1000_INTC1_INT_BASE;
	     (i < AU1000_INTC1_INT_BASE + 32); i++)
		au1x_ic_settype(i, IRQ_TYPE_NONE);

	/*
	 * Initialize IC0, which is fixed per processor.
	 */
	setup_irqmap(au1xxx_ic0_map, ARRAY_SIZE(au1xxx_ic0_map));

	set_c0_status(IE_IRQ0 | IE_IRQ1 | IE_IRQ2 | IE_IRQ3);
}
