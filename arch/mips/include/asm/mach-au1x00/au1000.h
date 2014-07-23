/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Include file for Alchemy Semiconductor's Au1k CPU.
 *
 * Copyright 2000-2001, 2006-2008 MontaVista Software Inc.
 * Author: MontaVista Software, Inc. <source@mvista.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

 /*
  * some definitions add by takuzo@sm.sony.co.jp and sato@sm.sony.co.jp
  */

#ifndef _AU1000_H_
#define _AU1000_H_


#ifndef _LANGUAGE_ASSEMBLY

#include <linux/delay.h>
#include <linux/types.h>

#include <linux/io.h>
#include <linux/irq.h>

#include <asm/cpu.h>

/* cpu pipeline flush */
void static inline au_sync(void)
{
	__asm__ volatile ("sync");
}

void static inline au_sync_udelay(int us)
{
	__asm__ volatile ("sync");
	udelay(us);
}

void static inline au_sync_delay(int ms)
{
	__asm__ volatile ("sync");
	mdelay(ms);
}

void static inline au_writeb(u8 val, unsigned long reg)
{
	*(volatile u8 *)reg = val;
}

void static inline au_writew(u16 val, unsigned long reg)
{
	*(volatile u16 *)reg = val;
}

void static inline au_writel(u32 val, unsigned long reg)
{
	*(volatile u32 *)reg = val;
}

static inline u8 au_readb(unsigned long reg)
{
	return *(volatile u8 *)reg;
}

static inline u16 au_readw(unsigned long reg)
{
	return *(volatile u16 *)reg;
}

static inline u32 au_readl(unsigned long reg)
{
	return *(volatile u32 *)reg;
}

/* Early Au1000 have a write-only SYS_CPUPLL register. */
static inline int au1xxx_cpu_has_pll_wo(void)
{
	switch (read_c0_prid()) {
	case 0x00030100:	/* Au1000 DA */
	case 0x00030201:	/* Au1000 HA */
	case 0x00030202:	/* Au1000 HB */
		return 1;
	}
	return 0;
}

/* does CPU need CONFIG[OD] set to fix tons of errata? */
static inline int au1xxx_cpu_needs_config_od(void)
{
	/*
	 * c0_config.od (bit 19) was write only (and read as 0) on the
	 * early revisions of Alchemy SOCs.  It disables the bus trans-
	 * action overlapping and needs to be set to fix various errata.
	 */
	switch (read_c0_prid()) {
	case 0x00030100: /* Au1000 DA */
	case 0x00030201: /* Au1000 HA */
	case 0x00030202: /* Au1000 HB */
	case 0x01030200: /* Au1500 AB */
	/*
	 * Au1100/Au1200 errata actually keep silence about this bit,
	 * so we set it just in case for those revisions that require
	 * it to be set according to the (now gone) cpu_table.
	 */
	case 0x02030200: /* Au1100 AB */
	case 0x02030201: /* Au1100 BA */
	case 0x02030202: /* Au1100 BC */
	case 0x04030201: /* Au1200 AC */
		return 1;
	}
	return 0;
}

#define ALCHEMY_CPU_UNKNOWN	-1
#define ALCHEMY_CPU_AU1000	0
#define ALCHEMY_CPU_AU1500	1
#define ALCHEMY_CPU_AU1100	2
#define ALCHEMY_CPU_AU1550	3
#define ALCHEMY_CPU_AU1200	4
#define ALCHEMY_CPU_AU1300	5

static inline int alchemy_get_cputype(void)
{
	switch (read_c0_prid() & (PRID_OPT_MASK | PRID_COMP_MASK)) {
	case 0x00030000:
		return ALCHEMY_CPU_AU1000;
		break;
	case 0x01030000:
		return ALCHEMY_CPU_AU1500;
		break;
	case 0x02030000:
		return ALCHEMY_CPU_AU1100;
		break;
	case 0x03030000:
		return ALCHEMY_CPU_AU1550;
		break;
	case 0x04030000:
	case 0x05030000:
		return ALCHEMY_CPU_AU1200;
		break;
	case 0x800c0000:
		return ALCHEMY_CPU_AU1300;
		break;
	}

	return ALCHEMY_CPU_UNKNOWN;
}

/* return number of uarts on a given cputype */
static inline int alchemy_get_uarts(int type)
{
	switch (type) {
	case ALCHEMY_CPU_AU1000:
	case ALCHEMY_CPU_AU1300:
		return 4;
	case ALCHEMY_CPU_AU1500:
	case ALCHEMY_CPU_AU1200:
		return 2;
	case ALCHEMY_CPU_AU1100:
	case ALCHEMY_CPU_AU1550:
		return 3;
	}
	return 0;
}

/* enable an UART block if it isn't already */
static inline void alchemy_uart_enable(u32 uart_phys)
{
	void __iomem *addr = (void __iomem *)KSEG1ADDR(uart_phys);

	/* reset, enable clock, deassert reset */
	if ((__raw_readl(addr + 0x100) & 3) != 3) {
		__raw_writel(0, addr + 0x100);
		wmb();
		__raw_writel(1, addr + 0x100);
		wmb();
	}
	__raw_writel(3, addr + 0x100);
	wmb();
}

static inline void alchemy_uart_disable(u32 uart_phys)
{
	void __iomem *addr = (void __iomem *)KSEG1ADDR(uart_phys);
	__raw_writel(0, addr + 0x100);	/* UART_MOD_CNTRL */
	wmb();
}

static inline void alchemy_uart_putchar(u32 uart_phys, u8 c)
{
	void __iomem *base = (void __iomem *)KSEG1ADDR(uart_phys);
	int timeout, i;

	/* check LSR TX_EMPTY bit */
	timeout = 0xffffff;
	do {
		if (__raw_readl(base + 0x1c) & 0x20)
			break;
		/* slow down */
		for (i = 10000; i; i--)
			asm volatile ("nop");
	} while (--timeout);

	__raw_writel(c, base + 0x04);	/* tx */
	wmb();
}

/* return number of ethernet MACs on a given cputype */
static inline int alchemy_get_macs(int type)
{
	switch (type) {
	case ALCHEMY_CPU_AU1000:
	case ALCHEMY_CPU_AU1500:
	case ALCHEMY_CPU_AU1550:
		return 2;
	case ALCHEMY_CPU_AU1100:
		return 1;
	}
	return 0;
}

/* arch/mips/au1000/common/clocks.c */
extern void set_au1x00_speed(unsigned int new_freq);
extern unsigned int get_au1x00_speed(void);
extern void set_au1x00_uart_baud_base(unsigned long new_baud_base);
extern unsigned long get_au1x00_uart_baud_base(void);
extern unsigned long au1xxx_calc_clock(void);

/* PM: arch/mips/alchemy/common/sleeper.S, power.c, irq.c */
void alchemy_sleep_au1000(void);
void alchemy_sleep_au1550(void);
void alchemy_sleep_au1300(void);
void au_sleep(void);

/* USB: drivers/usb/host/alchemy-common.c */
enum alchemy_usb_block {
	ALCHEMY_USB_OHCI0,
	ALCHEMY_USB_UDC0,
	ALCHEMY_USB_EHCI0,
	ALCHEMY_USB_OTG0,
	ALCHEMY_USB_OHCI1,
};
int alchemy_usb_control(int block, int enable);

/* PCI controller platform data */
struct alchemy_pci_platdata {
	int (*board_map_irq)(const struct pci_dev *d, u8 slot, u8 pin);
	int (*board_pci_idsel)(unsigned int devsel, int assert);
	/* bits to set/clear in PCI_CONFIG register */
	unsigned long pci_cfg_set;
	unsigned long pci_cfg_clr;
};

/* Multifunction pins: Each of these pins can either be assigned to the
 * GPIO controller or a on-chip peripheral.
 * Call "au1300_pinfunc_to_dev()" or "au1300_pinfunc_to_gpio()" to
 * assign one of these to either the GPIO controller or the device.
 */
enum au1300_multifunc_pins {
	/* wake-from-str pins 0-3 */
	AU1300_PIN_WAKE0 = 0, AU1300_PIN_WAKE1, AU1300_PIN_WAKE2,
	AU1300_PIN_WAKE3,
	/* external clock sources for PSCs: 4-5 */
	AU1300_PIN_EXTCLK0, AU1300_PIN_EXTCLK1,
	/* 8bit MMC interface on SD0: 6-9 */
	AU1300_PIN_SD0DAT4, AU1300_PIN_SD0DAT5, AU1300_PIN_SD0DAT6,
	AU1300_PIN_SD0DAT7,
	/* aux clk input for freqgen 3: 10 */
	AU1300_PIN_FG3AUX,
	/* UART1 pins: 11-18 */
	AU1300_PIN_U1RI, AU1300_PIN_U1DCD, AU1300_PIN_U1DSR,
	AU1300_PIN_U1CTS, AU1300_PIN_U1RTS, AU1300_PIN_U1DTR,
	AU1300_PIN_U1RX, AU1300_PIN_U1TX,
	/* UART0 pins: 19-24 */
	AU1300_PIN_U0RI, AU1300_PIN_U0DCD, AU1300_PIN_U0DSR,
	AU1300_PIN_U0CTS, AU1300_PIN_U0RTS, AU1300_PIN_U0DTR,
	/* UART2: 25-26 */
	AU1300_PIN_U2RX, AU1300_PIN_U2TX,
	/* UART3: 27-28 */
	AU1300_PIN_U3RX, AU1300_PIN_U3TX,
	/* LCD controller PWMs, ext pixclock: 29-31 */
	AU1300_PIN_LCDPWM0, AU1300_PIN_LCDPWM1, AU1300_PIN_LCDCLKIN,
	/* SD1 interface: 32-37 */
	AU1300_PIN_SD1DAT0, AU1300_PIN_SD1DAT1, AU1300_PIN_SD1DAT2,
	AU1300_PIN_SD1DAT3, AU1300_PIN_SD1CMD, AU1300_PIN_SD1CLK,
	/* SD2 interface: 38-43 */
	AU1300_PIN_SD2DAT0, AU1300_PIN_SD2DAT1, AU1300_PIN_SD2DAT2,
	AU1300_PIN_SD2DAT3, AU1300_PIN_SD2CMD, AU1300_PIN_SD2CLK,
	/* PSC0/1 clocks: 44-45 */
	AU1300_PIN_PSC0CLK, AU1300_PIN_PSC1CLK,
	/* PSCs: 46-49/50-53/54-57/58-61 */
	AU1300_PIN_PSC0SYNC0, AU1300_PIN_PSC0SYNC1, AU1300_PIN_PSC0D0,
	AU1300_PIN_PSC0D1,
	AU1300_PIN_PSC1SYNC0, AU1300_PIN_PSC1SYNC1, AU1300_PIN_PSC1D0,
	AU1300_PIN_PSC1D1,
	AU1300_PIN_PSC2SYNC0, AU1300_PIN_PSC2SYNC1, AU1300_PIN_PSC2D0,
	AU1300_PIN_PSC2D1,
	AU1300_PIN_PSC3SYNC0, AU1300_PIN_PSC3SYNC1, AU1300_PIN_PSC3D0,
	AU1300_PIN_PSC3D1,
	/* PCMCIA interface: 62-70 */
	AU1300_PIN_PCE2, AU1300_PIN_PCE1, AU1300_PIN_PIOS16,
	AU1300_PIN_PIOR, AU1300_PIN_PWE, AU1300_PIN_PWAIT,
	AU1300_PIN_PREG, AU1300_PIN_POE, AU1300_PIN_PIOW,
	/* camera interface H/V sync inputs: 71-72 */
	AU1300_PIN_CIMLS, AU1300_PIN_CIMFS,
	/* PSC2/3 clocks: 73-74 */
	AU1300_PIN_PSC2CLK, AU1300_PIN_PSC3CLK,
};

/* GPIC (Au1300) pin management: arch/mips/alchemy/common/gpioint.c */
extern void au1300_pinfunc_to_gpio(enum au1300_multifunc_pins gpio);
extern void au1300_pinfunc_to_dev(enum au1300_multifunc_pins gpio);
extern void au1300_set_irq_priority(unsigned int irq, int p);
extern void au1300_set_dbdma_gpio(int dchan, unsigned int gpio);

/* Au1300 allows to disconnect certain blocks from internal power supply */
enum au1300_vss_block {
	AU1300_VSS_MPE = 0,
	AU1300_VSS_BSA,
	AU1300_VSS_GPE,
	AU1300_VSS_MGP,
};

extern void au1300_vss_block_control(int block, int enable);


/* SOC Interrupt numbers */
/* Au1000-style (IC0/1): 2 controllers with 32 sources each */
#define AU1000_INTC0_INT_BASE	(MIPS_CPU_IRQ_BASE + 8)
#define AU1000_INTC0_INT_LAST	(AU1000_INTC0_INT_BASE + 31)
#define AU1000_INTC1_INT_BASE	(AU1000_INTC0_INT_LAST + 1)
#define AU1000_INTC1_INT_LAST	(AU1000_INTC1_INT_BASE + 31)
#define AU1000_MAX_INTR		AU1000_INTC1_INT_LAST

/* Au1300-style (GPIC): 1 controller with up to 128 sources */
#define ALCHEMY_GPIC_INT_BASE	(MIPS_CPU_IRQ_BASE + 8)
#define ALCHEMY_GPIC_INT_NUM	128
#define ALCHEMY_GPIC_INT_LAST	(ALCHEMY_GPIC_INT_BASE + ALCHEMY_GPIC_INT_NUM - 1)

enum soc_au1000_ints {
	AU1000_FIRST_INT	= AU1000_INTC0_INT_BASE,
	AU1000_UART0_INT	= AU1000_FIRST_INT,
	AU1000_UART1_INT,
	AU1000_UART2_INT,
	AU1000_UART3_INT,
	AU1000_SSI0_INT,
	AU1000_SSI1_INT,
	AU1000_DMA_INT_BASE,

	AU1000_TOY_INT		= AU1000_FIRST_INT + 14,
	AU1000_TOY_MATCH0_INT,
	AU1000_TOY_MATCH1_INT,
	AU1000_TOY_MATCH2_INT,
	AU1000_RTC_INT,
	AU1000_RTC_MATCH0_INT,
	AU1000_RTC_MATCH1_INT,
	AU1000_RTC_MATCH2_INT,
	AU1000_IRDA_TX_INT,
	AU1000_IRDA_RX_INT,
	AU1000_USB_DEV_REQ_INT,
	AU1000_USB_DEV_SUS_INT,
	AU1000_USB_HOST_INT,
	AU1000_ACSYNC_INT,
	AU1000_MAC0_DMA_INT,
	AU1000_MAC1_DMA_INT,
	AU1000_I2S_UO_INT,
	AU1000_AC97C_INT,
	AU1000_GPIO0_INT,
	AU1000_GPIO1_INT,
	AU1000_GPIO2_INT,
	AU1000_GPIO3_INT,
	AU1000_GPIO4_INT,
	AU1000_GPIO5_INT,
	AU1000_GPIO6_INT,
	AU1000_GPIO7_INT,
	AU1000_GPIO8_INT,
	AU1000_GPIO9_INT,
	AU1000_GPIO10_INT,
	AU1000_GPIO11_INT,
	AU1000_GPIO12_INT,
	AU1000_GPIO13_INT,
	AU1000_GPIO14_INT,
	AU1000_GPIO15_INT,
	AU1000_GPIO16_INT,
	AU1000_GPIO17_INT,
	AU1000_GPIO18_INT,
	AU1000_GPIO19_INT,
	AU1000_GPIO20_INT,
	AU1000_GPIO21_INT,
	AU1000_GPIO22_INT,
	AU1000_GPIO23_INT,
	AU1000_GPIO24_INT,
	AU1000_GPIO25_INT,
	AU1000_GPIO26_INT,
	AU1000_GPIO27_INT,
	AU1000_GPIO28_INT,
	AU1000_GPIO29_INT,
	AU1000_GPIO30_INT,
	AU1000_GPIO31_INT,
};

enum soc_au1100_ints {
	AU1100_FIRST_INT	= AU1000_INTC0_INT_BASE,
	AU1100_UART0_INT	= AU1100_FIRST_INT,
	AU1100_UART1_INT,
	AU1100_SD_INT,
	AU1100_UART3_INT,
	AU1100_SSI0_INT,
	AU1100_SSI1_INT,
	AU1100_DMA_INT_BASE,

	AU1100_TOY_INT		= AU1100_FIRST_INT + 14,
	AU1100_TOY_MATCH0_INT,
	AU1100_TOY_MATCH1_INT,
	AU1100_TOY_MATCH2_INT,
	AU1100_RTC_INT,
	AU1100_RTC_MATCH0_INT,
	AU1100_RTC_MATCH1_INT,
	AU1100_RTC_MATCH2_INT,
	AU1100_IRDA_TX_INT,
	AU1100_IRDA_RX_INT,
	AU1100_USB_DEV_REQ_INT,
	AU1100_USB_DEV_SUS_INT,
	AU1100_USB_HOST_INT,
	AU1100_ACSYNC_INT,
	AU1100_MAC0_DMA_INT,
	AU1100_GPIO208_215_INT,
	AU1100_LCD_INT,
	AU1100_AC97C_INT,
	AU1100_GPIO0_INT,
	AU1100_GPIO1_INT,
	AU1100_GPIO2_INT,
	AU1100_GPIO3_INT,
	AU1100_GPIO4_INT,
	AU1100_GPIO5_INT,
	AU1100_GPIO6_INT,
	AU1100_GPIO7_INT,
	AU1100_GPIO8_INT,
	AU1100_GPIO9_INT,
	AU1100_GPIO10_INT,
	AU1100_GPIO11_INT,
	AU1100_GPIO12_INT,
	AU1100_GPIO13_INT,
	AU1100_GPIO14_INT,
	AU1100_GPIO15_INT,
	AU1100_GPIO16_INT,
	AU1100_GPIO17_INT,
	AU1100_GPIO18_INT,
	AU1100_GPIO19_INT,
	AU1100_GPIO20_INT,
	AU1100_GPIO21_INT,
	AU1100_GPIO22_INT,
	AU1100_GPIO23_INT,
	AU1100_GPIO24_INT,
	AU1100_GPIO25_INT,
	AU1100_GPIO26_INT,
	AU1100_GPIO27_INT,
	AU1100_GPIO28_INT,
	AU1100_GPIO29_INT,
	AU1100_GPIO30_INT,
	AU1100_GPIO31_INT,
};

enum soc_au1500_ints {
	AU1500_FIRST_INT	= AU1000_INTC0_INT_BASE,
	AU1500_UART0_INT	= AU1500_FIRST_INT,
	AU1500_PCI_INTA,
	AU1500_PCI_INTB,
	AU1500_UART3_INT,
	AU1500_PCI_INTC,
	AU1500_PCI_INTD,
	AU1500_DMA_INT_BASE,

	AU1500_TOY_INT		= AU1500_FIRST_INT + 14,
	AU1500_TOY_MATCH0_INT,
	AU1500_TOY_MATCH1_INT,
	AU1500_TOY_MATCH2_INT,
	AU1500_RTC_INT,
	AU1500_RTC_MATCH0_INT,
	AU1500_RTC_MATCH1_INT,
	AU1500_RTC_MATCH2_INT,
	AU1500_PCI_ERR_INT,
	AU1500_RESERVED_INT,
	AU1500_USB_DEV_REQ_INT,
	AU1500_USB_DEV_SUS_INT,
	AU1500_USB_HOST_INT,
	AU1500_ACSYNC_INT,
	AU1500_MAC0_DMA_INT,
	AU1500_MAC1_DMA_INT,
	AU1500_AC97C_INT	= AU1500_FIRST_INT + 31,
	AU1500_GPIO0_INT,
	AU1500_GPIO1_INT,
	AU1500_GPIO2_INT,
	AU1500_GPIO3_INT,
	AU1500_GPIO4_INT,
	AU1500_GPIO5_INT,
	AU1500_GPIO6_INT,
	AU1500_GPIO7_INT,
	AU1500_GPIO8_INT,
	AU1500_GPIO9_INT,
	AU1500_GPIO10_INT,
	AU1500_GPIO11_INT,
	AU1500_GPIO12_INT,
	AU1500_GPIO13_INT,
	AU1500_GPIO14_INT,
	AU1500_GPIO15_INT,
	AU1500_GPIO200_INT,
	AU1500_GPIO201_INT,
	AU1500_GPIO202_INT,
	AU1500_GPIO203_INT,
	AU1500_GPIO20_INT,
	AU1500_GPIO204_INT,
	AU1500_GPIO205_INT,
	AU1500_GPIO23_INT,
	AU1500_GPIO24_INT,
	AU1500_GPIO25_INT,
	AU1500_GPIO26_INT,
	AU1500_GPIO27_INT,
	AU1500_GPIO28_INT,
	AU1500_GPIO206_INT,
	AU1500_GPIO207_INT,
	AU1500_GPIO208_215_INT,
};

enum soc_au1550_ints {
	AU1550_FIRST_INT	= AU1000_INTC0_INT_BASE,
	AU1550_UART0_INT	= AU1550_FIRST_INT,
	AU1550_PCI_INTA,
	AU1550_PCI_INTB,
	AU1550_DDMA_INT,
	AU1550_CRYPTO_INT,
	AU1550_PCI_INTC,
	AU1550_PCI_INTD,
	AU1550_PCI_RST_INT,
	AU1550_UART1_INT,
	AU1550_UART3_INT,
	AU1550_PSC0_INT,
	AU1550_PSC1_INT,
	AU1550_PSC2_INT,
	AU1550_PSC3_INT,
	AU1550_TOY_INT,
	AU1550_TOY_MATCH0_INT,
	AU1550_TOY_MATCH1_INT,
	AU1550_TOY_MATCH2_INT,
	AU1550_RTC_INT,
	AU1550_RTC_MATCH0_INT,
	AU1550_RTC_MATCH1_INT,
	AU1550_RTC_MATCH2_INT,

	AU1550_NAND_INT		= AU1550_FIRST_INT + 23,
	AU1550_USB_DEV_REQ_INT,
	AU1550_USB_DEV_SUS_INT,
	AU1550_USB_HOST_INT,
	AU1550_MAC0_DMA_INT,
	AU1550_MAC1_DMA_INT,
	AU1550_GPIO0_INT	= AU1550_FIRST_INT + 32,
	AU1550_GPIO1_INT,
	AU1550_GPIO2_INT,
	AU1550_GPIO3_INT,
	AU1550_GPIO4_INT,
	AU1550_GPIO5_INT,
	AU1550_GPIO6_INT,
	AU1550_GPIO7_INT,
	AU1550_GPIO8_INT,
	AU1550_GPIO9_INT,
	AU1550_GPIO10_INT,
	AU1550_GPIO11_INT,
	AU1550_GPIO12_INT,
	AU1550_GPIO13_INT,
	AU1550_GPIO14_INT,
	AU1550_GPIO15_INT,
	AU1550_GPIO200_INT,
	AU1550_GPIO201_205_INT, /* Logical or of GPIO201:205 */
	AU1550_GPIO16_INT,
	AU1550_GPIO17_INT,
	AU1550_GPIO20_INT,
	AU1550_GPIO21_INT,
	AU1550_GPIO22_INT,
	AU1550_GPIO23_INT,
	AU1550_GPIO24_INT,
	AU1550_GPIO25_INT,
	AU1550_GPIO26_INT,
	AU1550_GPIO27_INT,
	AU1550_GPIO28_INT,
	AU1550_GPIO206_INT,
	AU1550_GPIO207_INT,
	AU1550_GPIO208_215_INT, /* Logical or of GPIO208:215 */
};

enum soc_au1200_ints {
	AU1200_FIRST_INT	= AU1000_INTC0_INT_BASE,
	AU1200_UART0_INT	= AU1200_FIRST_INT,
	AU1200_SWT_INT,
	AU1200_SD_INT,
	AU1200_DDMA_INT,
	AU1200_MAE_BE_INT,
	AU1200_GPIO200_INT,
	AU1200_GPIO201_INT,
	AU1200_GPIO202_INT,
	AU1200_UART1_INT,
	AU1200_MAE_FE_INT,
	AU1200_PSC0_INT,
	AU1200_PSC1_INT,
	AU1200_AES_INT,
	AU1200_CAMERA_INT,
	AU1200_TOY_INT,
	AU1200_TOY_MATCH0_INT,
	AU1200_TOY_MATCH1_INT,
	AU1200_TOY_MATCH2_INT,
	AU1200_RTC_INT,
	AU1200_RTC_MATCH0_INT,
	AU1200_RTC_MATCH1_INT,
	AU1200_RTC_MATCH2_INT,
	AU1200_GPIO203_INT,
	AU1200_NAND_INT,
	AU1200_GPIO204_INT,
	AU1200_GPIO205_INT,
	AU1200_GPIO206_INT,
	AU1200_GPIO207_INT,
	AU1200_GPIO208_215_INT, /* Logical OR of 208:215 */
	AU1200_USB_INT,
	AU1200_LCD_INT,
	AU1200_MAE_BOTH_INT,
	AU1200_GPIO0_INT,
	AU1200_GPIO1_INT,
	AU1200_GPIO2_INT,
	AU1200_GPIO3_INT,
	AU1200_GPIO4_INT,
	AU1200_GPIO5_INT,
	AU1200_GPIO6_INT,
	AU1200_GPIO7_INT,
	AU1200_GPIO8_INT,
	AU1200_GPIO9_INT,
	AU1200_GPIO10_INT,
	AU1200_GPIO11_INT,
	AU1200_GPIO12_INT,
	AU1200_GPIO13_INT,
	AU1200_GPIO14_INT,
	AU1200_GPIO15_INT,
	AU1200_GPIO16_INT,
	AU1200_GPIO17_INT,
	AU1200_GPIO18_INT,
	AU1200_GPIO19_INT,
	AU1200_GPIO20_INT,
	AU1200_GPIO21_INT,
	AU1200_GPIO22_INT,
	AU1200_GPIO23_INT,
	AU1200_GPIO24_INT,
	AU1200_GPIO25_INT,
	AU1200_GPIO26_INT,
	AU1200_GPIO27_INT,
	AU1200_GPIO28_INT,
	AU1200_GPIO29_INT,
	AU1200_GPIO30_INT,
	AU1200_GPIO31_INT,
};

#endif /* !defined (_LANGUAGE_ASSEMBLY) */

/* Au1300 peripheral interrupt numbers */
#define AU1300_FIRST_INT	(ALCHEMY_GPIC_INT_BASE)
#define AU1300_UART1_INT	(AU1300_FIRST_INT + 17)
#define AU1300_UART2_INT	(AU1300_FIRST_INT + 25)
#define AU1300_UART3_INT	(AU1300_FIRST_INT + 27)
#define AU1300_SD1_INT		(AU1300_FIRST_INT + 32)
#define AU1300_SD2_INT		(AU1300_FIRST_INT + 38)
#define AU1300_PSC0_INT		(AU1300_FIRST_INT + 48)
#define AU1300_PSC1_INT		(AU1300_FIRST_INT + 52)
#define AU1300_PSC2_INT		(AU1300_FIRST_INT + 56)
#define AU1300_PSC3_INT		(AU1300_FIRST_INT + 60)
#define AU1300_NAND_INT		(AU1300_FIRST_INT + 62)
#define AU1300_DDMA_INT		(AU1300_FIRST_INT + 75)
#define AU1300_MMU_INT		(AU1300_FIRST_INT + 76)
#define AU1300_MPU_INT		(AU1300_FIRST_INT + 77)
#define AU1300_GPU_INT		(AU1300_FIRST_INT + 78)
#define AU1300_UDMA_INT		(AU1300_FIRST_INT + 79)
#define AU1300_TOY_INT		(AU1300_FIRST_INT + 80)
#define AU1300_TOY_MATCH0_INT	(AU1300_FIRST_INT + 81)
#define AU1300_TOY_MATCH1_INT	(AU1300_FIRST_INT + 82)
#define AU1300_TOY_MATCH2_INT	(AU1300_FIRST_INT + 83)
#define AU1300_RTC_INT		(AU1300_FIRST_INT + 84)
#define AU1300_RTC_MATCH0_INT	(AU1300_FIRST_INT + 85)
#define AU1300_RTC_MATCH1_INT	(AU1300_FIRST_INT + 86)
#define AU1300_RTC_MATCH2_INT	(AU1300_FIRST_INT + 87)
#define AU1300_UART0_INT	(AU1300_FIRST_INT + 88)
#define AU1300_SD0_INT		(AU1300_FIRST_INT + 89)
#define AU1300_USB_INT		(AU1300_FIRST_INT + 90)
#define AU1300_LCD_INT		(AU1300_FIRST_INT + 91)
#define AU1300_BSA_INT		(AU1300_FIRST_INT + 92)
#define AU1300_MPE_INT		(AU1300_FIRST_INT + 93)
#define AU1300_ITE_INT		(AU1300_FIRST_INT + 94)
#define AU1300_AES_INT		(AU1300_FIRST_INT + 95)
#define AU1300_CIM_INT		(AU1300_FIRST_INT + 96)

/**********************************************************************/

/*
 * Physical base addresses for integrated peripherals
 * 0..au1000 1..au1500 2..au1100 3..au1550 4..au1200 5..au1300
 */

#define AU1000_AC97_PHYS_ADDR		0x10000000 /* 012 */
#define AU1300_ROM_PHYS_ADDR		0x10000000 /* 5 */
#define AU1300_OTP_PHYS_ADDR		0x10002000 /* 5 */
#define AU1300_VSS_PHYS_ADDR		0x10003000 /* 5 */
#define AU1300_UART0_PHYS_ADDR		0x10100000 /* 5 */
#define AU1300_UART1_PHYS_ADDR		0x10101000 /* 5 */
#define AU1300_UART2_PHYS_ADDR		0x10102000 /* 5 */
#define AU1300_UART3_PHYS_ADDR		0x10103000 /* 5 */
#define AU1000_USB_OHCI_PHYS_ADDR	0x10100000 /* 012 */
#define AU1000_USB_UDC_PHYS_ADDR	0x10200000 /* 0123 */
#define AU1300_GPIC_PHYS_ADDR		0x10200000 /* 5 */
#define AU1000_IRDA_PHYS_ADDR		0x10300000 /* 02 */
#define AU1200_AES_PHYS_ADDR		0x10300000 /* 45 */
#define AU1000_IC0_PHYS_ADDR		0x10400000 /* 01234 */
#define AU1300_GPU_PHYS_ADDR		0x10500000 /* 5 */
#define AU1000_MAC0_PHYS_ADDR		0x10500000 /* 023 */
#define AU1000_MAC1_PHYS_ADDR		0x10510000 /* 023 */
#define AU1000_MACEN_PHYS_ADDR		0x10520000 /* 023 */
#define AU1100_SD0_PHYS_ADDR		0x10600000 /* 245 */
#define AU1300_SD1_PHYS_ADDR		0x10601000 /* 5 */
#define AU1300_SD2_PHYS_ADDR		0x10602000 /* 5 */
#define AU1100_SD1_PHYS_ADDR		0x10680000 /* 24 */
#define AU1300_SYS_PHYS_ADDR		0x10900000 /* 5 */
#define AU1550_PSC2_PHYS_ADDR		0x10A00000 /* 3 */
#define AU1550_PSC3_PHYS_ADDR		0x10B00000 /* 3 */
#define AU1300_PSC0_PHYS_ADDR		0x10A00000 /* 5 */
#define AU1300_PSC1_PHYS_ADDR		0x10A01000 /* 5 */
#define AU1300_PSC2_PHYS_ADDR		0x10A02000 /* 5 */
#define AU1300_PSC3_PHYS_ADDR		0x10A03000 /* 5 */
#define AU1000_I2S_PHYS_ADDR		0x11000000 /* 02 */
#define AU1500_MAC0_PHYS_ADDR		0x11500000 /* 1 */
#define AU1500_MAC1_PHYS_ADDR		0x11510000 /* 1 */
#define AU1500_MACEN_PHYS_ADDR		0x11520000 /* 1 */
#define AU1000_UART0_PHYS_ADDR		0x11100000 /* 01234 */
#define AU1200_SWCNT_PHYS_ADDR		0x1110010C /* 4 */
#define AU1000_UART1_PHYS_ADDR		0x11200000 /* 0234 */
#define AU1000_UART2_PHYS_ADDR		0x11300000 /* 0 */
#define AU1000_UART3_PHYS_ADDR		0x11400000 /* 0123 */
#define AU1000_SSI0_PHYS_ADDR		0x11600000 /* 02 */
#define AU1000_SSI1_PHYS_ADDR		0x11680000 /* 02 */
#define AU1500_GPIO2_PHYS_ADDR		0x11700000 /* 1234 */
#define AU1000_IC1_PHYS_ADDR		0x11800000 /* 01234 */
#define AU1000_SYS_PHYS_ADDR		0x11900000 /* 012345 */
#define AU1550_PSC0_PHYS_ADDR		0x11A00000 /* 34 */
#define AU1550_PSC1_PHYS_ADDR		0x11B00000 /* 34 */
#define AU1000_MEM_PHYS_ADDR		0x14000000 /* 01234 */
#define AU1000_STATIC_MEM_PHYS_ADDR	0x14001000 /* 01234 */
#define AU1300_UDMA_PHYS_ADDR		0x14001800 /* 5 */
#define AU1000_DMA_PHYS_ADDR		0x14002000 /* 012 */
#define AU1550_DBDMA_PHYS_ADDR		0x14002000 /* 345 */
#define AU1550_DBDMA_CONF_PHYS_ADDR	0x14003000 /* 345 */
#define AU1000_MACDMA0_PHYS_ADDR	0x14004000 /* 0123 */
#define AU1000_MACDMA1_PHYS_ADDR	0x14004200 /* 0123 */
#define AU1200_CIM_PHYS_ADDR		0x14004000 /* 45 */
#define AU1500_PCI_PHYS_ADDR		0x14005000 /* 13 */
#define AU1550_PE_PHYS_ADDR		0x14008000 /* 3 */
#define AU1200_MAEBE_PHYS_ADDR		0x14010000 /* 4 */
#define AU1200_MAEFE_PHYS_ADDR		0x14012000 /* 4 */
#define AU1300_MAEITE_PHYS_ADDR		0x14010000 /* 5 */
#define AU1300_MAEMPE_PHYS_ADDR		0x14014000 /* 5 */
#define AU1550_USB_OHCI_PHYS_ADDR	0x14020000 /* 3 */
#define AU1200_USB_CTL_PHYS_ADDR	0x14020000 /* 4 */
#define AU1200_USB_OTG_PHYS_ADDR	0x14020020 /* 4 */
#define AU1200_USB_OHCI_PHYS_ADDR	0x14020100 /* 4 */
#define AU1200_USB_EHCI_PHYS_ADDR	0x14020200 /* 4 */
#define AU1200_USB_UDC_PHYS_ADDR	0x14022000 /* 4 */
#define AU1300_USB_EHCI_PHYS_ADDR	0x14020000 /* 5 */
#define AU1300_USB_OHCI0_PHYS_ADDR	0x14020400 /* 5 */
#define AU1300_USB_OHCI1_PHYS_ADDR	0x14020800 /* 5 */
#define AU1300_USB_CTL_PHYS_ADDR	0x14021000 /* 5 */
#define AU1300_USB_OTG_PHYS_ADDR	0x14022000 /* 5 */
#define AU1300_MAEBSA_PHYS_ADDR		0x14030000 /* 5 */
#define AU1100_LCD_PHYS_ADDR		0x15000000 /* 2 */
#define AU1200_LCD_PHYS_ADDR		0x15000000 /* 45 */
#define AU1500_PCI_MEM_PHYS_ADDR	0x400000000ULL /* 13 */
#define AU1500_PCI_IO_PHYS_ADDR		0x500000000ULL /* 13 */
#define AU1500_PCI_CONFIG0_PHYS_ADDR	0x600000000ULL /* 13 */
#define AU1500_PCI_CONFIG1_PHYS_ADDR	0x680000000ULL /* 13 */
#define AU1000_PCMCIA_IO_PHYS_ADDR	0xF00000000ULL /* 012345 */
#define AU1000_PCMCIA_ATTR_PHYS_ADDR	0xF40000000ULL /* 012345 */
#define AU1000_PCMCIA_MEM_PHYS_ADDR	0xF80000000ULL /* 012345 */

/**********************************************************************/


/*
 * Au1300 GPIO+INT controller (GPIC) register offsets and bits
 * Registers are 128bits (0x10 bytes), divided into 4 "banks".
 */
#define AU1300_GPIC_PINVAL	0x0000
#define AU1300_GPIC_PINVALCLR	0x0010
#define AU1300_GPIC_IPEND	0x0020
#define AU1300_GPIC_PRIENC	0x0030
#define AU1300_GPIC_IEN		0x0040	/* int_mask in manual */
#define AU1300_GPIC_IDIS	0x0050	/* int_maskclr in manual */
#define AU1300_GPIC_DMASEL	0x0060
#define AU1300_GPIC_DEVSEL	0x0080
#define AU1300_GPIC_DEVCLR	0x0090
#define AU1300_GPIC_RSTVAL	0x00a0
/* pin configuration space. one 32bit register for up to 128 IRQs */
#define AU1300_GPIC_PINCFG	0x1000

#define GPIC_GPIO_TO_BIT(gpio)	\
	(1 << ((gpio) & 0x1f))

#define GPIC_GPIO_BANKOFF(gpio) \
	(((gpio) >> 5) * 4)

/* Pin Control bits: who owns the pin, what does it do */
#define GPIC_CFG_PC_GPIN		0
#define GPIC_CFG_PC_DEV			1
#define GPIC_CFG_PC_GPOLOW		2
#define GPIC_CFG_PC_GPOHIGH		3
#define GPIC_CFG_PC_MASK		3

/* assign pin to MIPS IRQ line */
#define GPIC_CFG_IL_SET(x)	(((x) & 3) << 2)
#define GPIC_CFG_IL_MASK	(3 << 2)

/* pin interrupt type setup */
#define GPIC_CFG_IC_OFF		(0 << 4)
#define GPIC_CFG_IC_LEVEL_LOW	(1 << 4)
#define GPIC_CFG_IC_LEVEL_HIGH	(2 << 4)
#define GPIC_CFG_IC_EDGE_FALL	(5 << 4)
#define GPIC_CFG_IC_EDGE_RISE	(6 << 4)
#define GPIC_CFG_IC_EDGE_BOTH	(7 << 4)
#define GPIC_CFG_IC_MASK	(7 << 4)

/* allow interrupt to wake cpu from 'wait' */
#define GPIC_CFG_IDLEWAKE	(1 << 7)

/***********************************************************************/

/* Au1000 SDRAM memory controller register offsets */
#define AU1000_MEM_SDMODE0		0x0000
#define AU1000_MEM_SDMODE1		0x0004
#define AU1000_MEM_SDMODE2		0x0008
#define AU1000_MEM_SDADDR0		0x000C
#define AU1000_MEM_SDADDR1		0x0010
#define AU1000_MEM_SDADDR2		0x0014
#define AU1000_MEM_SDREFCFG		0x0018
#define AU1000_MEM_SDPRECMD		0x001C
#define AU1000_MEM_SDAUTOREF		0x0020
#define AU1000_MEM_SDWRMD0		0x0024
#define AU1000_MEM_SDWRMD1		0x0028
#define AU1000_MEM_SDWRMD2		0x002C
#define AU1000_MEM_SDSLEEP		0x0030
#define AU1000_MEM_SDSMCKE		0x0034

/* MEM_SDMODE register content definitions */
#define MEM_SDMODE_F		(1 << 22)
#define MEM_SDMODE_SR		(1 << 21)
#define MEM_SDMODE_BS		(1 << 20)
#define MEM_SDMODE_RS		(3 << 18)
#define MEM_SDMODE_CS		(7 << 15)
#define MEM_SDMODE_TRAS		(15 << 11)
#define MEM_SDMODE_TMRD		(3 << 9)
#define MEM_SDMODE_TWR		(3 << 7)
#define MEM_SDMODE_TRP		(3 << 5)
#define MEM_SDMODE_TRCD		(3 << 3)
#define MEM_SDMODE_TCL		(7 << 0)

#define MEM_SDMODE_BS_2Bank	(0 << 20)
#define MEM_SDMODE_BS_4Bank	(1 << 20)
#define MEM_SDMODE_RS_11Row	(0 << 18)
#define MEM_SDMODE_RS_12Row	(1 << 18)
#define MEM_SDMODE_RS_13Row	(2 << 18)
#define MEM_SDMODE_RS_N(N)	((N) << 18)
#define MEM_SDMODE_CS_7Col	(0 << 15)
#define MEM_SDMODE_CS_8Col	(1 << 15)
#define MEM_SDMODE_CS_9Col	(2 << 15)
#define MEM_SDMODE_CS_10Col	(3 << 15)
#define MEM_SDMODE_CS_11Col	(4 << 15)
#define MEM_SDMODE_CS_N(N)	((N) << 15)
#define MEM_SDMODE_TRAS_N(N)	((N) << 11)
#define MEM_SDMODE_TMRD_N(N)	((N) << 9)
#define MEM_SDMODE_TWR_N(N)	((N) << 7)
#define MEM_SDMODE_TRP_N(N)	((N) << 5)
#define MEM_SDMODE_TRCD_N(N)	((N) << 3)
#define MEM_SDMODE_TCL_N(N)	((N) << 0)

/* MEM_SDADDR register contents definitions */
#define MEM_SDADDR_E		(1 << 20)
#define MEM_SDADDR_CSBA		(0x03FF << 10)
#define MEM_SDADDR_CSMASK	(0x03FF << 0)
#define MEM_SDADDR_CSBA_N(N)	((N) & (0x03FF << 22) >> 12)
#define MEM_SDADDR_CSMASK_N(N)	((N)&(0x03FF << 22) >> 22)

/* MEM_SDREFCFG register content definitions */
#define MEM_SDREFCFG_TRC	(15 << 28)
#define MEM_SDREFCFG_TRPM	(3 << 26)
#define MEM_SDREFCFG_E		(1 << 25)
#define MEM_SDREFCFG_RE		(0x1ffffff << 0)
#define MEM_SDREFCFG_TRC_N(N)	((N) << MEM_SDREFCFG_TRC)
#define MEM_SDREFCFG_TRPM_N(N)	((N) << MEM_SDREFCFG_TRPM)
#define MEM_SDREFCFG_REF_N(N)	(N)

/* Au1550 SDRAM Register Offsets */
#define AU1550_MEM_SDMODE0		0x0800
#define AU1550_MEM_SDMODE1		0x0808
#define AU1550_MEM_SDMODE2		0x0810
#define AU1550_MEM_SDADDR0		0x0820
#define AU1550_MEM_SDADDR1		0x0828
#define AU1550_MEM_SDADDR2		0x0830
#define AU1550_MEM_SDCONFIGA		0x0840
#define AU1550_MEM_SDCONFIGB		0x0848
#define AU1550_MEM_SDSTAT		0x0850
#define AU1550_MEM_SDERRADDR		0x0858
#define AU1550_MEM_SDSTRIDE0		0x0860
#define AU1550_MEM_SDSTRIDE1		0x0868
#define AU1550_MEM_SDSTRIDE2		0x0870
#define AU1550_MEM_SDWRMD0		0x0880
#define AU1550_MEM_SDWRMD1		0x0888
#define AU1550_MEM_SDWRMD2		0x0890
#define AU1550_MEM_SDPRECMD		0x08C0
#define AU1550_MEM_SDAUTOREF		0x08C8
#define AU1550_MEM_SDSREF		0x08D0
#define AU1550_MEM_SDSLEEP		MEM_SDSREF

/* Static Bus Controller */
#define MEM_STCFG0		0xB4001000
#define MEM_STTIME0		0xB4001004
#define MEM_STADDR0		0xB4001008

#define MEM_STCFG1		0xB4001010
#define MEM_STTIME1		0xB4001014
#define MEM_STADDR1		0xB4001018

#define MEM_STCFG2		0xB4001020
#define MEM_STTIME2		0xB4001024
#define MEM_STADDR2		0xB4001028

#define MEM_STCFG3		0xB4001030
#define MEM_STTIME3		0xB4001034
#define MEM_STADDR3		0xB4001038

#define MEM_STNDCTL		0xB4001100
#define MEM_STSTAT		0xB4001104

#define MEM_STNAND_CMD		0x0
#define MEM_STNAND_ADDR		0x4
#define MEM_STNAND_DATA		0x20


/* Programmable Counters 0 and 1 */
#define SYS_BASE		0xB1900000
#define SYS_COUNTER_CNTRL	(SYS_BASE + 0x14)
#  define SYS_CNTRL_E1S		(1 << 23)
#  define SYS_CNTRL_T1S		(1 << 20)
#  define SYS_CNTRL_M21		(1 << 19)
#  define SYS_CNTRL_M11		(1 << 18)
#  define SYS_CNTRL_M01		(1 << 17)
#  define SYS_CNTRL_C1S		(1 << 16)
#  define SYS_CNTRL_BP		(1 << 14)
#  define SYS_CNTRL_EN1		(1 << 13)
#  define SYS_CNTRL_BT1		(1 << 12)
#  define SYS_CNTRL_EN0		(1 << 11)
#  define SYS_CNTRL_BT0		(1 << 10)
#  define SYS_CNTRL_E0		(1 << 8)
#  define SYS_CNTRL_E0S		(1 << 7)
#  define SYS_CNTRL_32S		(1 << 5)
#  define SYS_CNTRL_T0S		(1 << 4)
#  define SYS_CNTRL_M20		(1 << 3)
#  define SYS_CNTRL_M10		(1 << 2)
#  define SYS_CNTRL_M00		(1 << 1)
#  define SYS_CNTRL_C0S		(1 << 0)

/* Programmable Counter 0 Registers */
#define SYS_TOYTRIM		(SYS_BASE + 0)
#define SYS_TOYWRITE		(SYS_BASE + 4)
#define SYS_TOYMATCH0		(SYS_BASE + 8)
#define SYS_TOYMATCH1		(SYS_BASE + 0xC)
#define SYS_TOYMATCH2		(SYS_BASE + 0x10)
#define SYS_TOYREAD		(SYS_BASE + 0x40)

/* Programmable Counter 1 Registers */
#define SYS_RTCTRIM		(SYS_BASE + 0x44)
#define SYS_RTCWRITE		(SYS_BASE + 0x48)
#define SYS_RTCMATCH0		(SYS_BASE + 0x4C)
#define SYS_RTCMATCH1		(SYS_BASE + 0x50)
#define SYS_RTCMATCH2		(SYS_BASE + 0x54)
#define SYS_RTCREAD		(SYS_BASE + 0x58)

/* Ethernet Controllers  */

/* 4 byte offsets from AU1000_ETH_BASE */
#define MAC_CONTROL		0x0
#  define MAC_RX_ENABLE		(1 << 2)
#  define MAC_TX_ENABLE		(1 << 3)
#  define MAC_DEF_CHECK		(1 << 5)
#  define MAC_SET_BL(X)		(((X) & 0x3) << 6)
#  define MAC_AUTO_PAD		(1 << 8)
#  define MAC_DISABLE_RETRY	(1 << 10)
#  define MAC_DISABLE_BCAST	(1 << 11)
#  define MAC_LATE_COL		(1 << 12)
#  define MAC_HASH_MODE		(1 << 13)
#  define MAC_HASH_ONLY		(1 << 15)
#  define MAC_PASS_ALL		(1 << 16)
#  define MAC_INVERSE_FILTER	(1 << 17)
#  define MAC_PROMISCUOUS	(1 << 18)
#  define MAC_PASS_ALL_MULTI	(1 << 19)
#  define MAC_FULL_DUPLEX	(1 << 20)
#  define MAC_NORMAL_MODE	0
#  define MAC_INT_LOOPBACK	(1 << 21)
#  define MAC_EXT_LOOPBACK	(1 << 22)
#  define MAC_DISABLE_RX_OWN	(1 << 23)
#  define MAC_BIG_ENDIAN	(1 << 30)
#  define MAC_RX_ALL		(1 << 31)
#define MAC_ADDRESS_HIGH	0x4
#define MAC_ADDRESS_LOW		0x8
#define MAC_MCAST_HIGH		0xC
#define MAC_MCAST_LOW		0x10
#define MAC_MII_CNTRL		0x14
#  define MAC_MII_BUSY		(1 << 0)
#  define MAC_MII_READ		0
#  define MAC_MII_WRITE		(1 << 1)
#  define MAC_SET_MII_SELECT_REG(X) (((X) & 0x1f) << 6)
#  define MAC_SET_MII_SELECT_PHY(X) (((X) & 0x1f) << 11)
#define MAC_MII_DATA		0x18
#define MAC_FLOW_CNTRL		0x1C
#  define MAC_FLOW_CNTRL_BUSY	(1 << 0)
#  define MAC_FLOW_CNTRL_ENABLE (1 << 1)
#  define MAC_PASS_CONTROL	(1 << 2)
#  define MAC_SET_PAUSE(X)	(((X) & 0xffff) << 16)
#define MAC_VLAN1_TAG		0x20
#define MAC_VLAN2_TAG		0x24

/* Ethernet Controller Enable */

#  define MAC_EN_CLOCK_ENABLE	(1 << 0)
#  define MAC_EN_RESET0		(1 << 1)
#  define MAC_EN_TOSS		(0 << 2)
#  define MAC_EN_CACHEABLE	(1 << 3)
#  define MAC_EN_RESET1		(1 << 4)
#  define MAC_EN_RESET2		(1 << 5)
#  define MAC_DMA_RESET		(1 << 6)

/* Ethernet Controller DMA Channels */

#define MAC0_TX_DMA_ADDR	0xB4004000
#define MAC1_TX_DMA_ADDR	0xB4004200
/* offsets from MAC_TX_RING_ADDR address */
#define MAC_TX_BUFF0_STATUS	0x0
#  define TX_FRAME_ABORTED	(1 << 0)
#  define TX_JAB_TIMEOUT	(1 << 1)
#  define TX_NO_CARRIER		(1 << 2)
#  define TX_LOSS_CARRIER	(1 << 3)
#  define TX_EXC_DEF		(1 << 4)
#  define TX_LATE_COLL_ABORT	(1 << 5)
#  define TX_EXC_COLL		(1 << 6)
#  define TX_UNDERRUN		(1 << 7)
#  define TX_DEFERRED		(1 << 8)
#  define TX_LATE_COLL		(1 << 9)
#  define TX_COLL_CNT_MASK	(0xF << 10)
#  define TX_PKT_RETRY		(1 << 31)
#define MAC_TX_BUFF0_ADDR	0x4
#  define TX_DMA_ENABLE		(1 << 0)
#  define TX_T_DONE		(1 << 1)
#  define TX_GET_DMA_BUFFER(X)	(((X) >> 2) & 0x3)
#define MAC_TX_BUFF0_LEN	0x8
#define MAC_TX_BUFF1_STATUS	0x10
#define MAC_TX_BUFF1_ADDR	0x14
#define MAC_TX_BUFF1_LEN	0x18
#define MAC_TX_BUFF2_STATUS	0x20
#define MAC_TX_BUFF2_ADDR	0x24
#define MAC_TX_BUFF2_LEN	0x28
#define MAC_TX_BUFF3_STATUS	0x30
#define MAC_TX_BUFF3_ADDR	0x34
#define MAC_TX_BUFF3_LEN	0x38

#define MAC0_RX_DMA_ADDR	0xB4004100
#define MAC1_RX_DMA_ADDR	0xB4004300
/* offsets from MAC_RX_RING_ADDR */
#define MAC_RX_BUFF0_STATUS	0x0
#  define RX_FRAME_LEN_MASK	0x3fff
#  define RX_WDOG_TIMER		(1 << 14)
#  define RX_RUNT		(1 << 15)
#  define RX_OVERLEN		(1 << 16)
#  define RX_COLL		(1 << 17)
#  define RX_ETHER		(1 << 18)
#  define RX_MII_ERROR		(1 << 19)
#  define RX_DRIBBLING		(1 << 20)
#  define RX_CRC_ERROR		(1 << 21)
#  define RX_VLAN1		(1 << 22)
#  define RX_VLAN2		(1 << 23)
#  define RX_LEN_ERROR		(1 << 24)
#  define RX_CNTRL_FRAME	(1 << 25)
#  define RX_U_CNTRL_FRAME	(1 << 26)
#  define RX_MCAST_FRAME	(1 << 27)
#  define RX_BCAST_FRAME	(1 << 28)
#  define RX_FILTER_FAIL	(1 << 29)
#  define RX_PACKET_FILTER	(1 << 30)
#  define RX_MISSED_FRAME	(1 << 31)

#  define RX_ERROR (RX_WDOG_TIMER | RX_RUNT | RX_OVERLEN |  \
		    RX_COLL | RX_MII_ERROR | RX_CRC_ERROR | \
		    RX_LEN_ERROR | RX_U_CNTRL_FRAME | RX_MISSED_FRAME)
#define MAC_RX_BUFF0_ADDR	0x4
#  define RX_DMA_ENABLE		(1 << 0)
#  define RX_T_DONE		(1 << 1)
#  define RX_GET_DMA_BUFFER(X)	(((X) >> 2) & 0x3)
#  define RX_SET_BUFF_ADDR(X)	((X) & 0xffffffc0)
#define MAC_RX_BUFF1_STATUS	0x10
#define MAC_RX_BUFF1_ADDR	0x14
#define MAC_RX_BUFF2_STATUS	0x20
#define MAC_RX_BUFF2_ADDR	0x24
#define MAC_RX_BUFF3_STATUS	0x30
#define MAC_RX_BUFF3_ADDR	0x34


/*
 * The IrDA peripheral has an IRFIRSEL pin, but on the DB/PB boards it's not
 * used to select FIR/SIR mode on the transceiver but as a GPIO.  Instead a
 * CPLD has to be told about the mode.
 */
#define AU1000_IRDA_PHY_MODE_OFF	0
#define AU1000_IRDA_PHY_MODE_SIR	1
#define AU1000_IRDA_PHY_MODE_FIR	2

struct au1k_irda_platform_data {
	void(*set_phy_mode)(int mode);
};


/* GPIO */
#define SYS_PINFUNC		0xB190002C
#  define SYS_PF_USB		(1 << 15)	/* 2nd USB device/host */
#  define SYS_PF_U3		(1 << 14)	/* GPIO23/U3TXD */
#  define SYS_PF_U2		(1 << 13)	/* GPIO22/U2TXD */
#  define SYS_PF_U1		(1 << 12)	/* GPIO21/U1TXD */
#  define SYS_PF_SRC		(1 << 11)	/* GPIO6/SROMCKE */
#  define SYS_PF_CK5		(1 << 10)	/* GPIO3/CLK5 */
#  define SYS_PF_CK4		(1 << 9)	/* GPIO2/CLK4 */
#  define SYS_PF_IRF		(1 << 8)	/* GPIO15/IRFIRSEL */
#  define SYS_PF_UR3		(1 << 7)	/* GPIO[14:9]/UART3 */
#  define SYS_PF_I2D		(1 << 6)	/* GPIO8/I2SDI */
#  define SYS_PF_I2S		(1 << 5)	/* I2S/GPIO[29:31] */
#  define SYS_PF_NI2		(1 << 4)	/* NI2/GPIO[24:28] */
#  define SYS_PF_U0		(1 << 3)	/* U0TXD/GPIO20 */
#  define SYS_PF_RD		(1 << 2)	/* IRTXD/GPIO19 */
#  define SYS_PF_A97		(1 << 1)	/* AC97/SSL1 */
#  define SYS_PF_S0		(1 << 0)	/* SSI_0/GPIO[16:18] */

/* Au1100 only */
#  define SYS_PF_PC		(1 << 18)	/* PCMCIA/GPIO[207:204] */
#  define SYS_PF_LCD		(1 << 17)	/* extern lcd/GPIO[203:200] */
#  define SYS_PF_CS		(1 << 16)	/* EXTCLK0/32KHz to gpio2 */
#  define SYS_PF_EX0		(1 << 9)	/* GPIO2/clock */

/* Au1550 only.	 Redefines lots of pins */
#  define SYS_PF_PSC2_MASK	(7 << 17)
#  define SYS_PF_PSC2_AC97	0
#  define SYS_PF_PSC2_SPI	0
#  define SYS_PF_PSC2_I2S	(1 << 17)
#  define SYS_PF_PSC2_SMBUS	(3 << 17)
#  define SYS_PF_PSC2_GPIO	(7 << 17)
#  define SYS_PF_PSC3_MASK	(7 << 20)
#  define SYS_PF_PSC3_AC97	0
#  define SYS_PF_PSC3_SPI	0
#  define SYS_PF_PSC3_I2S	(1 << 20)
#  define SYS_PF_PSC3_SMBUS	(3 << 20)
#  define SYS_PF_PSC3_GPIO	(7 << 20)
#  define SYS_PF_PSC1_S1	(1 << 1)
#  define SYS_PF_MUST_BE_SET	((1 << 5) | (1 << 2))

/* Au1200 only */
#define SYS_PINFUNC_DMA		(1 << 31)
#define SYS_PINFUNC_S0A		(1 << 30)
#define SYS_PINFUNC_S1A		(1 << 29)
#define SYS_PINFUNC_LP0		(1 << 28)
#define SYS_PINFUNC_LP1		(1 << 27)
#define SYS_PINFUNC_LD16	(1 << 26)
#define SYS_PINFUNC_LD8		(1 << 25)
#define SYS_PINFUNC_LD1		(1 << 24)
#define SYS_PINFUNC_LD0		(1 << 23)
#define SYS_PINFUNC_P1A		(3 << 21)
#define SYS_PINFUNC_P1B		(1 << 20)
#define SYS_PINFUNC_FS3		(1 << 19)
#define SYS_PINFUNC_P0A		(3 << 17)
#define SYS_PINFUNC_CS		(1 << 16)
#define SYS_PINFUNC_CIM		(1 << 15)
#define SYS_PINFUNC_P1C		(1 << 14)
#define SYS_PINFUNC_U1T		(1 << 12)
#define SYS_PINFUNC_U1R		(1 << 11)
#define SYS_PINFUNC_EX1		(1 << 10)
#define SYS_PINFUNC_EX0		(1 << 9)
#define SYS_PINFUNC_U0R		(1 << 8)
#define SYS_PINFUNC_MC		(1 << 7)
#define SYS_PINFUNC_S0B		(1 << 6)
#define SYS_PINFUNC_S0C		(1 << 5)
#define SYS_PINFUNC_P0B		(1 << 4)
#define SYS_PINFUNC_U0T		(1 << 3)
#define SYS_PINFUNC_S1B		(1 << 2)

/* Power Management */
#define SYS_SCRATCH0		0xB1900018
#define SYS_SCRATCH1		0xB190001C
#define SYS_WAKEMSK		0xB1900034
#define SYS_ENDIAN		0xB1900038
#define SYS_POWERCTRL		0xB190003C
#define SYS_WAKESRC		0xB190005C
#define SYS_SLPPWR		0xB1900078
#define SYS_SLEEP		0xB190007C

#define SYS_WAKEMSK_D2		(1 << 9)
#define SYS_WAKEMSK_M2		(1 << 8)
#define SYS_WAKEMSK_GPIO(x)	(1 << (x))

/* Clock Controller */
#define SYS_FREQCTRL0		0xB1900020
#  define SYS_FC_FRDIV2_BIT	22
#  define SYS_FC_FRDIV2_MASK	(0xff << SYS_FC_FRDIV2_BIT)
#  define SYS_FC_FE2		(1 << 21)
#  define SYS_FC_FS2		(1 << 20)
#  define SYS_FC_FRDIV1_BIT	12
#  define SYS_FC_FRDIV1_MASK	(0xff << SYS_FC_FRDIV1_BIT)
#  define SYS_FC_FE1		(1 << 11)
#  define SYS_FC_FS1		(1 << 10)
#  define SYS_FC_FRDIV0_BIT	2
#  define SYS_FC_FRDIV0_MASK	(0xff << SYS_FC_FRDIV0_BIT)
#  define SYS_FC_FE0		(1 << 1)
#  define SYS_FC_FS0		(1 << 0)
#define SYS_FREQCTRL1		0xB1900024
#  define SYS_FC_FRDIV5_BIT	22
#  define SYS_FC_FRDIV5_MASK	(0xff << SYS_FC_FRDIV5_BIT)
#  define SYS_FC_FE5		(1 << 21)
#  define SYS_FC_FS5		(1 << 20)
#  define SYS_FC_FRDIV4_BIT	12
#  define SYS_FC_FRDIV4_MASK	(0xff << SYS_FC_FRDIV4_BIT)
#  define SYS_FC_FE4		(1 << 11)
#  define SYS_FC_FS4		(1 << 10)
#  define SYS_FC_FRDIV3_BIT	2
#  define SYS_FC_FRDIV3_MASK	(0xff << SYS_FC_FRDIV3_BIT)
#  define SYS_FC_FE3		(1 << 1)
#  define SYS_FC_FS3		(1 << 0)
#define SYS_CLKSRC		0xB1900028
#  define SYS_CS_ME1_BIT	27
#  define SYS_CS_ME1_MASK	(0x7 << SYS_CS_ME1_BIT)
#  define SYS_CS_DE1		(1 << 26)
#  define SYS_CS_CE1		(1 << 25)
#  define SYS_CS_ME0_BIT	22
#  define SYS_CS_ME0_MASK	(0x7 << SYS_CS_ME0_BIT)
#  define SYS_CS_DE0		(1 << 21)
#  define SYS_CS_CE0		(1 << 20)
#  define SYS_CS_MI2_BIT	17
#  define SYS_CS_MI2_MASK	(0x7 << SYS_CS_MI2_BIT)
#  define SYS_CS_DI2		(1 << 16)
#  define SYS_CS_CI2		(1 << 15)

#  define SYS_CS_ML_BIT		7
#  define SYS_CS_ML_MASK	(0x7 << SYS_CS_ML_BIT)
#  define SYS_CS_DL		(1 << 6)
#  define SYS_CS_CL		(1 << 5)

#  define SYS_CS_MUH_BIT	12
#  define SYS_CS_MUH_MASK	(0x7 << SYS_CS_MUH_BIT)
#  define SYS_CS_DUH		(1 << 11)
#  define SYS_CS_CUH		(1 << 10)
#  define SYS_CS_MUD_BIT	7
#  define SYS_CS_MUD_MASK	(0x7 << SYS_CS_MUD_BIT)
#  define SYS_CS_DUD		(1 << 6)
#  define SYS_CS_CUD		(1 << 5)

#  define SYS_CS_MIR_BIT	2
#  define SYS_CS_MIR_MASK	(0x7 << SYS_CS_MIR_BIT)
#  define SYS_CS_DIR		(1 << 1)
#  define SYS_CS_CIR		(1 << 0)

#  define SYS_CS_MUX_AUX	0x1
#  define SYS_CS_MUX_FQ0	0x2
#  define SYS_CS_MUX_FQ1	0x3
#  define SYS_CS_MUX_FQ2	0x4
#  define SYS_CS_MUX_FQ3	0x5
#  define SYS_CS_MUX_FQ4	0x6
#  define SYS_CS_MUX_FQ5	0x7
#define SYS_CPUPLL		0xB1900060
#define SYS_AUXPLL		0xB1900064


/* The PCI chip selects are outside the 32bit space, and since we can't
 * just program the 36bit addresses into BARs, we have to take a chunk
 * out of the 32bit space and reserve it for PCI.  When these addresses
 * are ioremap()ed, they'll be fixed up to the real 36bit address before
 * being passed to the real ioremap function.
 */
#define ALCHEMY_PCI_MEMWIN_START	(AU1500_PCI_MEM_PHYS_ADDR >> 4)
#define ALCHEMY_PCI_MEMWIN_END		(ALCHEMY_PCI_MEMWIN_START + 0x0FFFFFFF)

/* for PCI IO it's simpler because we get to do the ioremap ourselves and then
 * adjust the device's resources.
 */
#define ALCHEMY_PCI_IOWIN_START		0x00001000
#define ALCHEMY_PCI_IOWIN_END		0x0000FFFF

#ifdef CONFIG_PCI

#define IOPORT_RESOURCE_START	0x00001000	/* skip legacy probing */
#define IOPORT_RESOURCE_END	0xffffffff
#define IOMEM_RESOURCE_START	0x10000000
#define IOMEM_RESOURCE_END	0xfffffffffULL

#else

/* Don't allow any legacy ports probing */
#define IOPORT_RESOURCE_START	0x10000000
#define IOPORT_RESOURCE_END	0xffffffff
#define IOMEM_RESOURCE_START	0x10000000
#define IOMEM_RESOURCE_END	0xfffffffffULL

#endif

/* PCI controller block register offsets */
#define PCI_REG_CMEM		0x0000
#define PCI_REG_CONFIG		0x0004
#define PCI_REG_B2BMASK_CCH	0x0008
#define PCI_REG_B2BBASE0_VID	0x000C
#define PCI_REG_B2BBASE1_SID	0x0010
#define PCI_REG_MWMASK_DEV	0x0014
#define PCI_REG_MWBASE_REV_CCL	0x0018
#define PCI_REG_ERR_ADDR	0x001C
#define PCI_REG_SPEC_INTACK	0x0020
#define PCI_REG_ID		0x0100
#define PCI_REG_STATCMD		0x0104
#define PCI_REG_CLASSREV	0x0108
#define PCI_REG_PARAM		0x010C
#define PCI_REG_MBAR		0x0110
#define PCI_REG_TIMEOUT		0x0140

/* PCI controller block register bits */
#define PCI_CMEM_E		(1 << 28)	/* enable cacheable memory */
#define PCI_CMEM_CMBASE(x)	(((x) & 0x3fff) << 14)
#define PCI_CMEM_CMMASK(x)	((x) & 0x3fff)
#define PCI_CONFIG_ERD		(1 << 27) /* pci error during R/W */
#define PCI_CONFIG_ET		(1 << 26) /* error in target mode */
#define PCI_CONFIG_EF		(1 << 25) /* fatal error */
#define PCI_CONFIG_EP		(1 << 24) /* parity error */
#define PCI_CONFIG_EM		(1 << 23) /* multiple errors */
#define PCI_CONFIG_BM		(1 << 22) /* bad master error */
#define PCI_CONFIG_PD		(1 << 20) /* PCI Disable */
#define PCI_CONFIG_BME		(1 << 19) /* Byte Mask Enable for reads */
#define PCI_CONFIG_NC		(1 << 16) /* mark mem access non-coherent */
#define PCI_CONFIG_IA		(1 << 15) /* INTA# enabled (target mode) */
#define PCI_CONFIG_IP		(1 << 13) /* int on PCI_PERR# */
#define PCI_CONFIG_IS		(1 << 12) /* int on PCI_SERR# */
#define PCI_CONFIG_IMM		(1 << 11) /* int on master abort */
#define PCI_CONFIG_ITM		(1 << 10) /* int on target abort (as master) */
#define PCI_CONFIG_ITT		(1 << 9)  /* int on target abort (as target) */
#define PCI_CONFIG_IPB		(1 << 8)  /* int on PERR# in bus master acc */
#define PCI_CONFIG_SIC_NO	(0 << 6)  /* no byte mask changes */
#define PCI_CONFIG_SIC_BA_ADR	(1 << 6)  /* on byte/hw acc, invert adr bits */
#define PCI_CONFIG_SIC_HWA_DAT	(2 << 6)  /* on halfword acc, swap data */
#define PCI_CONFIG_SIC_ALL	(3 << 6)  /* swap data bytes on all accesses */
#define PCI_CONFIG_ST		(1 << 5)  /* swap data by target transactions */
#define PCI_CONFIG_SM		(1 << 4)  /* swap data from PCI ctl */
#define PCI_CONFIG_AEN		(1 << 3)  /* enable internal arbiter */
#define PCI_CONFIG_R2H		(1 << 2)  /* REQ2# to hi-prio arbiter */
#define PCI_CONFIG_R1H		(1 << 1)  /* REQ1# to hi-prio arbiter */
#define PCI_CONFIG_CH		(1 << 0)  /* PCI ctl to hi-prio arbiter */
#define PCI_B2BMASK_B2BMASK(x)	(((x) & 0xffff) << 16)
#define PCI_B2BMASK_CCH(x)	((x) & 0xffff) /* 16 upper bits of class code */
#define PCI_B2BBASE0_VID_B0(x)	(((x) & 0xffff) << 16)
#define PCI_B2BBASE0_VID_SV(x)	((x) & 0xffff)
#define PCI_B2BBASE1_SID_B1(x)	(((x) & 0xffff) << 16)
#define PCI_B2BBASE1_SID_SI(x)	((x) & 0xffff)
#define PCI_MWMASKDEV_MWMASK(x) (((x) & 0xffff) << 16)
#define PCI_MWMASKDEV_DEVID(x)	((x) & 0xffff)
#define PCI_MWBASEREVCCL_BASE(x) (((x) & 0xffff) << 16)
#define PCI_MWBASEREVCCL_REV(x)	 (((x) & 0xff) << 8)
#define PCI_MWBASEREVCCL_CCL(x)	 ((x) & 0xff)
#define PCI_ID_DID(x)		(((x) & 0xffff) << 16)
#define PCI_ID_VID(x)		((x) & 0xffff)
#define PCI_STATCMD_STATUS(x)	(((x) & 0xffff) << 16)
#define PCI_STATCMD_CMD(x)	((x) & 0xffff)
#define PCI_CLASSREV_CLASS(x)	(((x) & 0x00ffffff) << 8)
#define PCI_CLASSREV_REV(x)	((x) & 0xff)
#define PCI_PARAM_BIST(x)	(((x) & 0xff) << 24)
#define PCI_PARAM_HT(x)		(((x) & 0xff) << 16)
#define PCI_PARAM_LT(x)		(((x) & 0xff) << 8)
#define PCI_PARAM_CLS(x)	((x) & 0xff)
#define PCI_TIMEOUT_RETRIES(x)	(((x) & 0xff) << 8)	/* max retries */
#define PCI_TIMEOUT_TO(x)	((x) & 0xff)	/* target ready timeout */

#endif
