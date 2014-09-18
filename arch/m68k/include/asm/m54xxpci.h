/****************************************************************************/

/*
 *	m54xxpci.h -- ColdFire 547x and 548x PCI bus support
 *
 *	(C) Copyright 2011,  Greg Ungerer <gerg@uclinux.org>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/****************************************************************************/
#ifndef	M54XXPCI_H
#define	M54XXPCI_H
/****************************************************************************/

/*
 *	The core set of PCI support registers are mapped into the MBAR region.
 */
#define	PCIIDR		(CONFIG_MBAR + 0xb00)	/* PCI device/vendor ID */
#define	PCISCR		(CONFIG_MBAR + 0xb04)	/* PCI status/command */
#define	PCICCRIR	(CONFIG_MBAR + 0xb08)	/* PCI class/revision */
#define	PCICR1		(CONFIG_MBAR + 0xb0c)	/* PCI configuration 1 */
#define	PCIBAR0		(CONFIG_MBAR + 0xb10)	/* PCI base address 0 */
#define	PCIBAR1		(CONFIG_MBAR + 0xb14)	/* PCI base address 1 */
#define	PCICCPR		(CONFIG_MBAR + 0xb28)	/* PCI cardbus CIS pointer */
#define	PCISID		(CONFIG_MBAR + 0xb2c)	/* PCI subsystem IDs */
#define	PCIERBAR	(CONFIG_MBAR + 0xb30)	/* PCI expansion ROM */
#define	PCICPR		(CONFIG_MBAR + 0xb34)	/* PCI capabilities pointer */
#define	PCICR2		(CONFIG_MBAR + 0xb3c)	/* PCI configuration 2 */

#define	PCIGSCR		(CONFIG_MBAR + 0xb60)	/* Global status/control */
#define	PCITBATR0	(CONFIG_MBAR + 0xb64)	/* Target base translation 0 */
#define	PCITBATR1	(CONFIG_MBAR + 0xb68)	/* Target base translation 1 */
#define	PCITCR		(CONFIG_MBAR + 0xb6c)	/* Target control */
#define	PCIIW0BTAR	(CONFIG_MBAR + 0xb70)	/* Initiator window 0 */
#define	PCIIW1BTAR	(CONFIG_MBAR + 0xb74)	/* Initiator window 1 */
#define	PCIIW2BTAR	(CONFIG_MBAR + 0xb78)	/* Initiator window 2 */
#define	PCIIWCR		(CONFIG_MBAR + 0xb80)	/* Initiator window config */
#define	PCIICR		(CONFIG_MBAR + 0xb84)	/* Initiator control */
#define	PCIISR		(CONFIG_MBAR + 0xb88)	/* Initiator status */
#define	PCICAR		(CONFIG_MBAR + 0xbf8)	/* Configuration address */

#define	PCITPSR		(CONFIG_MBAR + 0x8400)	/* TX packet size */
#define	PCITSAR		(CONFIG_MBAR + 0x8404)	/* TX start address */
#define	PCITTCR		(CONFIG_MBAR + 0x8408)	/* TX transaction control */
#define	PCITER		(CONFIG_MBAR + 0x840c)	/* TX enables */
#define	PCITNAR		(CONFIG_MBAR + 0x8410)	/* TX next address */
#define	PCITLWR		(CONFIG_MBAR + 0x8414)	/* TX last word */
#define	PCITDCR		(CONFIG_MBAR + 0x8418)	/* TX done counts */
#define	PCITSR		(CONFIG_MBAR + 0x841c)	/* TX status */
#define	PCITFDR		(CONFIG_MBAR + 0x8440)	/* TX FIFO data */
#define	PCITFSR		(CONFIG_MBAR + 0x8444)	/* TX FIFO status */
#define	PCITFCR		(CONFIG_MBAR + 0x8448)	/* TX FIFO control */
#define	PCITFAR		(CONFIG_MBAR + 0x844c)	/* TX FIFO alarm */
#define	PCITFRPR	(CONFIG_MBAR + 0x8450)	/* TX FIFO read pointer */
#define	PCITFWPR	(CONFIG_MBAR + 0x8454)	/* TX FIFO write pointer */

#define	PCIRPSR		(CONFIG_MBAR + 0x8480)	/* RX packet size */
#define	PCIRSAR		(CONFIG_MBAR + 0x8484)	/* RX start address */
#define	PCIRTCR		(CONFIG_MBAR + 0x8488)	/* RX transaction control */
#define	PCIRER		(CONFIG_MBAR + 0x848c)	/* RX enables */
#define	PCIRNAR		(CONFIG_MBAR + 0x8490)	/* RX next address */
#define	PCIRDCR		(CONFIG_MBAR + 0x8498)	/* RX done counts */
#define	PCIRSR		(CONFIG_MBAR + 0x849c)	/* RX status */
#define	PCIRFDR		(CONFIG_MBAR + 0x84c0)	/* RX FIFO data */
#define	PCIRFSR		(CONFIG_MBAR + 0x84c4)	/* RX FIFO status */
#define	PCIRFCR		(CONFIG_MBAR + 0x84c8)	/* RX FIFO control */
#define	PCIRFAR		(CONFIG_MBAR + 0x84cc)	/* RX FIFO alarm */
#define	PCIRFRPR	(CONFIG_MBAR + 0x84d0)	/* RX FIFO read pointer */
#define	PCIRFWPR	(CONFIG_MBAR + 0x84d4)	/* RX FIFO write pointer */

#define	PACR		(CONFIG_MBAR + 0xc00)	/* PCI arbiter control */
#define	PASR		(COFNIG_MBAR + 0xc04)	/* PCI arbiter status */

/*
 *	Definitions for the Global status and control register.
 */
#define	PCIGSCR_PE	0x20000000		/* Parity error detected */
#define	PCIGSCR_SE	0x10000000		/* System error detected */
#define	PCIGSCR_XCLKBIN	0x07000000		/* XLB2CLKIN mask */
#define	PCIGSCR_PEE	0x00002000		/* Parity error intr enable */
#define	PCIGSCR_SEE	0x00001000		/* System error intr enable */
#define	PCIGSCR_RESET	0x00000001		/* Reset bit */

/*
 *	Bit definitions for the PCICAR configuration address register.
 */
#define	PCICAR_E	0x80000000		/* Enable config space */
#define	PCICAR_BUSN	16			/* Move bus bits */
#define	PCICAR_DEVFNN	8			/* Move devfn bits */
#define	PCICAR_DWORDN	0			/* Move dword bits */

/*
 *	The initiator windows hold the memory and IO mapping information.
 *	This macro creates the register values from the desired addresses.
 */
#define	WXBTAR(hostaddr, pciaddr, size)	\
			(((hostaddr) & 0xff000000) | \
			((((size) - 1) & 0xff000000) >> 8) | \
			(((pciaddr) & 0xff000000) >> 16))

#define	PCIIWCR_W0_MEM	0x00000000		/* Window 0 is memory */
#define	PCIIWCR_W0_IO	0x08000000		/* Window 0 is IO */
#define	PCIIWCR_W0_MRD	0x00000000		/* Window 0 memory read */
#define	PCIIWCR_W0_MRDL	0x02000000		/* Window 0 memory read line */
#define	PCIIWCR_W0_MRDM	0x04000000		/* Window 0 memory read mult */
#define	PCIIWCR_W0_E	0x01000000		/* Window 0 enable */

#define	PCIIWCR_W1_MEM	0x00000000		/* Window 0 is memory */
#define	PCIIWCR_W1_IO	0x00080000		/* Window 0 is IO */
#define	PCIIWCR_W1_MRD	0x00000000		/* Window 0 memory read */
#define	PCIIWCR_W1_MRDL	0x00020000		/* Window 0 memory read line */
#define	PCIIWCR_W1_MRDM	0x00040000		/* Window 0 memory read mult */
#define	PCIIWCR_W1_E	0x00010000		/* Window 0 enable */

/*
 *	Bit definitions for the PCIBATR registers.
 */
#define	PCITBATR0_E	0x00000001		/* Enable window 0 */
#define	PCITBATR1_E	0x00000001		/* Enable window 1 */

/*
 *	PCI arbiter support definitions and macros.
 */
#define	PACR_INTMPRI	0x00000001
#define	PACR_EXTMPRI(x)	(((x) & 0x1f) << 1)
#define	PACR_INTMINTE	0x00010000
#define	PACR_EXTMINTE(x) (((x) & 0x1f) << 17)
#define	PACR_PKMD	0x40000000
#define	PACR_DS		0x80000000

#define	PCICR1_CL(x)	((x) & 0xf)		/* Cacheline size field */
#define	PCICR1_LT(x)	(((x) & 0xff) << 8)	/* Latency timer field */

/****************************************************************************/
#endif	/* M54XXPCI_H */
