/*
 *  include/asm-ppc/hydra.h -- Mac I/O `Hydra' definitions
 *
 *  Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is based on the following documentation:
 *
 *	Macintosh Technology in the Common Hardware Reference Platform
 *	Apple Computer, Inc.
 *
 *	© Copyright 1995 Apple Computer, Inc. All rights reserved.
 *
 *  It's available online from http://www.cpu.lu/~mlan/ftp/MacTech.pdf
 *  You can obtain paper copies of this book from computer bookstores or by
 *  writing Morgan Kaufmann Publishers, Inc., 340 Pine Street, Sixth Floor, San
 *  Francisco, CA 94104. Reference ISBN 1-55860-393-X.
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#ifndef _ASMPPC_HYDRA_H
#define _ASMPPC_HYDRA_H

#ifdef __KERNEL__

struct Hydra {
    /* DBDMA Controller Register Space */
    char Pad1[0x30];
    u_int CachePD;
    u_int IDs;
    u_int Feature_Control;
    char Pad2[0x7fc4];
    /* DBDMA Channel Register Space */
    char SCSI_DMA[0x100];
    char Pad3[0x300];
    char SCCA_Tx_DMA[0x100];
    char SCCA_Rx_DMA[0x100];
    char SCCB_Tx_DMA[0x100];
    char SCCB_Rx_DMA[0x100];
    char Pad4[0x7800];
    /* Device Register Space */
    char SCSI[0x1000];
    char ADB[0x1000];
    char SCC_Legacy[0x1000];
    char SCC[0x1000];
    char Pad9[0x2000];
    char VIA[0x2000];
    char Pad10[0x28000];
    char OpenPIC[0x40000];
};

extern volatile struct Hydra __iomem *Hydra;


    /*
     *  Feature Control Register
     */

#define HYDRA_FC_SCC_CELL_EN	0x00000001	/* Enable SCC Clock */
#define HYDRA_FC_SCSI_CELL_EN	0x00000002	/* Enable SCSI Clock */
#define HYDRA_FC_SCCA_ENABLE	0x00000004	/* Enable SCC A Lines */
#define HYDRA_FC_SCCB_ENABLE	0x00000008	/* Enable SCC B Lines */
#define HYDRA_FC_ARB_BYPASS	0x00000010	/* Bypass Internal Arbiter */
#define HYDRA_FC_RESET_SCC	0x00000020	/* Reset SCC */
#define HYDRA_FC_MPIC_ENABLE	0x00000040	/* Enable OpenPIC */
#define HYDRA_FC_SLOW_SCC_PCLK	0x00000080	/* 1=15.6672, 0=25 MHz */
#define HYDRA_FC_MPIC_IS_MASTER	0x00000100	/* OpenPIC Master Mode */


    /*
     *  OpenPIC Interrupt Sources
     */

#define HYDRA_INT_SIO		0
#define HYDRA_INT_SCSI_DMA	1
#define HYDRA_INT_SCCA_TX_DMA	2
#define HYDRA_INT_SCCA_RX_DMA	3
#define HYDRA_INT_SCCB_TX_DMA	4
#define HYDRA_INT_SCCB_RX_DMA	5
#define HYDRA_INT_SCSI		6
#define HYDRA_INT_SCCA		7
#define HYDRA_INT_SCCB		8
#define HYDRA_INT_VIA		9
#define HYDRA_INT_ADB		10
#define HYDRA_INT_ADB_NMI	11
#define HYDRA_INT_EXT1		12	/* PCI IRQW */
#define HYDRA_INT_EXT2		13	/* PCI IRQX */
#define HYDRA_INT_EXT3		14	/* PCI IRQY */
#define HYDRA_INT_EXT4		15	/* PCI IRQZ */
#define HYDRA_INT_EXT5		16	/* IDE Primary/Secondary */
#define HYDRA_INT_EXT6		17	/* IDE Secondary */
#define HYDRA_INT_EXT7		18	/* Power Off Request */
#define HYDRA_INT_SPARE		19

extern int hydra_init(void);

#endif /* __KERNEL__ */

#endif /* _ASMPPC_HYDRA_H */
