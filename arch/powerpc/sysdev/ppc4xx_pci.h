/*
 * PCI / PCI-X / PCI-Express support for 4xx parts
 *
 * Copyright 2007 Ben. Herrenschmidt <benh@kernel.crashing.org>, IBM Corp.
 *
 * Bits and pieces extracted from arch/ppc support by
 *
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * Copyright 2002-2005 MontaVista Software Inc.
 */
#ifndef __PPC4XX_PCI_H__
#define __PPC4XX_PCI_H__

/*
 * 4xx PCI-X bridge register definitions
 */
#define PCIX0_VENDID		0x000
#define PCIX0_DEVID		0x002
#define PCIX0_COMMAND		0x004
#define PCIX0_STATUS		0x006
#define PCIX0_REVID		0x008
#define PCIX0_CLS		0x009
#define PCIX0_CACHELS		0x00c
#define PCIX0_LATTIM		0x00d
#define PCIX0_HDTYPE		0x00e
#define PCIX0_BIST		0x00f
#define PCIX0_BAR0L		0x010
#define PCIX0_BAR0H		0x014
#define PCIX0_BAR1		0x018
#define PCIX0_BAR2L		0x01c
#define PCIX0_BAR2H		0x020
#define PCIX0_BAR3		0x024
#define PCIX0_CISPTR		0x028
#define PCIX0_SBSYSVID		0x02c
#define PCIX0_SBSYSID		0x02e
#define PCIX0_EROMBA		0x030
#define PCIX0_CAP		0x034
#define PCIX0_RES0		0x035
#define PCIX0_RES1		0x036
#define PCIX0_RES2		0x038
#define PCIX0_INTLN		0x03c
#define PCIX0_INTPN		0x03d
#define PCIX0_MINGNT		0x03e
#define PCIX0_MAXLTNCY		0x03f
#define PCIX0_BRDGOPT1		0x040
#define PCIX0_BRDGOPT2		0x044
#define PCIX0_ERREN		0x050
#define PCIX0_ERRSTS		0x054
#define PCIX0_PLBBESR		0x058
#define PCIX0_PLBBEARL		0x05c
#define PCIX0_PLBBEARH		0x060
#define PCIX0_POM0LAL		0x068
#define PCIX0_POM0LAH		0x06c
#define PCIX0_POM0SA		0x070
#define PCIX0_POM0PCIAL		0x074
#define PCIX0_POM0PCIAH		0x078
#define PCIX0_POM1LAL		0x07c
#define PCIX0_POM1LAH		0x080
#define PCIX0_POM1SA		0x084
#define PCIX0_POM1PCIAL		0x088
#define PCIX0_POM1PCIAH		0x08c
#define PCIX0_POM2SA		0x090
#define PCIX0_PIM0SAL		0x098
#define PCIX0_PIM0SA		PCIX0_PIM0SAL
#define PCIX0_PIM0LAL		0x09c
#define PCIX0_PIM0LAH		0x0a0
#define PCIX0_PIM1SA		0x0a4
#define PCIX0_PIM1LAL		0x0a8
#define PCIX0_PIM1LAH		0x0ac
#define PCIX0_PIM2SAL		0x0b0
#define PCIX0_PIM2SA		PCIX0_PIM2SAL
#define PCIX0_PIM2LAL		0x0b4
#define PCIX0_PIM2LAH		0x0b8
#define PCIX0_OMCAPID		0x0c0
#define PCIX0_OMNIPTR		0x0c1
#define PCIX0_OMMC		0x0c2
#define PCIX0_OMMA		0x0c4
#define PCIX0_OMMUA		0x0c8
#define PCIX0_OMMDATA		0x0cc
#define PCIX0_OMMEOI		0x0ce
#define PCIX0_PMCAPID		0x0d0
#define PCIX0_PMNIPTR		0x0d1
#define PCIX0_PMC		0x0d2
#define PCIX0_PMCSR		0x0d4
#define PCIX0_PMCSRBSE		0x0d6
#define PCIX0_PMDATA		0x0d7
#define PCIX0_PMSCRR		0x0d8
#define PCIX0_CAPID		0x0dc
#define PCIX0_NIPTR		0x0dd
#define PCIX0_CMD		0x0de
#define PCIX0_STS		0x0e0
#define PCIX0_IDR		0x0e4
#define PCIX0_CID		0x0e8
#define PCIX0_RID		0x0ec
#define PCIX0_PIM0SAH		0x0f8
#define PCIX0_PIM2SAH		0x0fc
#define PCIX0_MSGIL		0x100
#define PCIX0_MSGIH		0x104
#define PCIX0_MSGOL		0x108
#define PCIX0_MSGOH		0x10c
#define PCIX0_IM		0x1f8



#endif /* __PPC4XX_PCI_H__ */
