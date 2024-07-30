/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * This interface is used for compatibility with old U-boots *ONLY*.
 * Please do not imitate or extend this.
 */

/*
 * (C) Copyright 2000, 2001
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 */

#ifndef __PPCBOOT_H__
#define __PPCBOOT_H__

/*
 * Board information passed to kernel from PPCBoot
 *
 * include/asm-ppc/ppcboot.h
 */

#include "types.h"

typedef struct bd_info {
	unsigned long	bi_memstart;	/* start of DRAM memory */
	unsigned long	bi_memsize;	/* size	 of DRAM memory in bytes */
	unsigned long	bi_flashstart;	/* start of FLASH memory */
	unsigned long	bi_flashsize;	/* size	 of FLASH memory */
	unsigned long	bi_flashoffset; /* reserved area for startup monitor */
	unsigned long	bi_sramstart;	/* start of SRAM memory */
	unsigned long	bi_sramsize;	/* size	 of SRAM memory */
#if defined(TARGET_8xx) || defined(TARGET_CPM2) || defined(TARGET_85xx) ||\
	defined(TARGET_83xx) || defined(TARGET_86xx)
	unsigned long	bi_immr_base;	/* base of IMMR register */
#endif
#if defined(TARGET_PPC_MPC52xx)
	unsigned long   bi_mbar_base;   /* base of internal registers */
#endif
	unsigned long	bi_bootflags;	/* boot / reboot flag (for LynxOS) */
	unsigned long	bi_ip_addr;	/* IP Address */
	unsigned char	bi_enetaddr[6];	/* Ethernet address */
	unsigned short	bi_ethspeed;	/* Ethernet speed in Mbps */
	unsigned long	bi_intfreq;	/* Internal Freq, in MHz */
	unsigned long	bi_busfreq;	/* Bus Freq, in MHz */
#if defined(TARGET_CPM2)
	unsigned long	bi_cpmfreq;	/* CPM_CLK Freq, in MHz */
	unsigned long	bi_brgfreq;	/* BRG_CLK Freq, in MHz */
	unsigned long	bi_sccfreq;	/* SCC_CLK Freq, in MHz */
	unsigned long	bi_vco;		/* VCO Out from PLL, in MHz */
#endif
#if defined(TARGET_PPC_MPC52xx)
	unsigned long   bi_ipbfreq;     /* IPB Bus Freq, in MHz */
	unsigned long   bi_pcifreq;     /* PCI Bus Freq, in MHz */
#endif
	unsigned long	bi_baudrate;	/* Console Baudrate */
#if defined(TARGET_4xx)
	unsigned char	bi_s_version[4];	/* Version of this structure */
	unsigned char	bi_r_version[32];	/* Version of the ROM (IBM) */
	unsigned int	bi_procfreq;	/* CPU (Internal) Freq, in Hz */
	unsigned int	bi_plb_busfreq;	/* PLB Bus speed, in Hz */
	unsigned int	bi_pci_busfreq;	/* PCI Bus speed, in Hz */
	unsigned char	bi_pci_enetaddr[6];	/* PCI Ethernet MAC address */
#endif
#if defined(TARGET_HYMOD)
	hymod_conf_t	bi_hymod_conf;	/* hymod configuration information */
#endif
#if defined(TARGET_EVB64260) || defined(TARGET_44x) || \
	defined(TARGET_85xx) ||	defined(TARGET_83xx) || defined(TARGET_HAS_ETH1)
	/* second onboard ethernet port */
	unsigned char	bi_enet1addr[6];
#define HAVE_ENET1ADDR
#endif
#if defined(TARGET_EVB64260) || defined(TARGET_440GX) || \
    defined(TARGET_85xx) || defined(TARGET_HAS_ETH2)
	/* third onboard ethernet ports */
	unsigned char	bi_enet2addr[6];
#define HAVE_ENET2ADDR
#endif
#if defined(TARGET_440GX) || defined(TARGET_HAS_ETH3)
	/* fourth onboard ethernet ports */
	unsigned char	bi_enet3addr[6];
#define HAVE_ENET3ADDR
#endif
#if defined(TARGET_4xx)
	unsigned int	bi_opbfreq;		/* OB clock in Hz */
	int		bi_iic_fast[2];		/* Use fast i2c mode */
#endif
#if defined(TARGET_440GX)
	int		bi_phynum[4];		/* phy mapping */
	int		bi_phymode[4];		/* phy mode */
#endif
} bd_t;

#define bi_tbfreq	bi_intfreq

#endif	/* __PPCBOOT_H__ */
