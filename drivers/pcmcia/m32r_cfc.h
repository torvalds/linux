/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2001 by Hiroyuki Kondo
 */

#if !defined(CONFIG_M32R_CFC_NUM)
#define M32R_MAX_PCC	2
#else
#define M32R_MAX_PCC	CONFIG_M32R_CFC_NUM
#endif

/*
 * M32R PC Card Controller
 */
#define M32R_PCC0_BASE        0x00ef7000
#define M32R_PCC1_BASE        0x00ef7020

/*
 * Register offsets
 */
#define PCCR            0x00
#define PCADR           0x04
#define PCMOD           0x08
#define PCIRC           0x0c
#define PCCSIGCR        0x10
#define PCATCR          0x14

/*
 * PCCR
 */
#define PCCR_PCEN       (1UL<<(31-31))

/*
 * PCIRC
 */
#define PCIRC_BWERR     (1UL<<(31-7))
#define PCIRC_CDIN1     (1UL<<(31-14))
#define PCIRC_CDIN2     (1UL<<(31-15))
#define PCIRC_BEIEN     (1UL<<(31-23))
#define PCIRC_CIIEN     (1UL<<(31-30))
#define PCIRC_COIEN     (1UL<<(31-31))

/*
 * PCCSIGCR
 */
#define PCCSIGCR_SEN    (1UL<<(31-3))
#define PCCSIGCR_VEN    (1UL<<(31-7))
#define PCCSIGCR_CRST   (1UL<<(31-15))
#define PCCSIGCR_COCR   (1UL<<(31-31))

/*
 *
 */
#define PCMOD_AS_ATTRIB	(1UL<<(31-19))
#define PCMOD_AS_IO	(1UL<<(31-18))

#define PCMOD_CBSZ	(1UL<<(31-23)) /* set for 8bit */

#define PCMOD_DBEX	(1UL<<(31-31)) /* set for excahnge */

/*
 * M32R PCC Map addr
 */

#define M32R_PCC0_MAPBASE        0x14000000
#define M32R_PCC1_MAPBASE        0x16000000

#define M32R_PCC_MAPMAX		 0x02000000

#define M32R_PCC_MAPSIZE	 0x00001000 /* XXX */
#define M32R_PCC_MAPMASK     	(~(M32R_PCC_MAPMAX-1))

#define CFC_IOPORT_BASE		0x1000

#if defined(CONFIG_PLAT_MAPPI3)
#define CFC_ATTR_MAPBASE	0x14014000
#define CFC_IO_MAPBASE_BYTE	0xb4012000
#define CFC_IO_MAPBASE_WORD	0xb4002000
#elif !defined(CONFIG_PLAT_USRV)
#define CFC_ATTR_MAPBASE        0x0c014000
#define CFC_IO_MAPBASE_BYTE     0xac012000
#define CFC_IO_MAPBASE_WORD     0xac002000
#else
#define CFC_ATTR_MAPBASE	0x04014000
#define CFC_IO_MAPBASE_BYTE	0xa4012000
#define CFC_IO_MAPBASE_WORD	0xa4002000
#endif	/* CONFIG_PLAT_USRV */

