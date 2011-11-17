/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_BRCM_SOC_H
#define	_BRCM_SOC_H

#define SI_ENUM_BASE		0x18000000	/* Enumeration space base */

/* core codes */
#define	NODEV_CORE_ID		0x700	/* Invalid coreid */
#define	CC_CORE_ID		0x800	/* chipcommon core */
#define	ILINE20_CORE_ID		0x801	/* iline20 core */
#define	SRAM_CORE_ID		0x802	/* sram core */
#define	SDRAM_CORE_ID		0x803	/* sdram core */
#define	PCI_CORE_ID		0x804	/* pci core */
#define	MIPS_CORE_ID		0x805	/* mips core */
#define	ENET_CORE_ID		0x806	/* enet mac core */
#define	CODEC_CORE_ID		0x807	/* v90 codec core */
#define	USB_CORE_ID		0x808	/* usb 1.1 host/device core */
#define	ADSL_CORE_ID		0x809	/* ADSL core */
#define	ILINE100_CORE_ID	0x80a	/* iline100 core */
#define	IPSEC_CORE_ID		0x80b	/* ipsec core */
#define	UTOPIA_CORE_ID		0x80c	/* utopia core */
#define	PCMCIA_CORE_ID		0x80d	/* pcmcia core */
#define	SOCRAM_CORE_ID		0x80e	/* internal memory core */
#define	MEMC_CORE_ID		0x80f	/* memc sdram core */
#define	OFDM_CORE_ID		0x810	/* OFDM phy core */
#define	EXTIF_CORE_ID		0x811	/* external interface core */
#define	D11_CORE_ID		0x812	/* 802.11 MAC core */
#define	APHY_CORE_ID		0x813	/* 802.11a phy core */
#define	BPHY_CORE_ID		0x814	/* 802.11b phy core */
#define	GPHY_CORE_ID		0x815	/* 802.11g phy core */
#define	MIPS33_CORE_ID		0x816	/* mips3302 core */
#define	USB11H_CORE_ID		0x817	/* usb 1.1 host core */
#define	USB11D_CORE_ID		0x818	/* usb 1.1 device core */
#define	USB20H_CORE_ID		0x819	/* usb 2.0 host core */
#define	USB20D_CORE_ID		0x81a	/* usb 2.0 device core */
#define	SDIOH_CORE_ID		0x81b	/* sdio host core */
#define	ROBO_CORE_ID		0x81c	/* roboswitch core */
#define	ATA100_CORE_ID		0x81d	/* parallel ATA core */
#define	SATAXOR_CORE_ID		0x81e	/* serial ATA & XOR DMA core */
#define	GIGETH_CORE_ID		0x81f	/* gigabit ethernet core */
#define	PCIE_CORE_ID		0x820	/* pci express core */
#define	NPHY_CORE_ID		0x821	/* 802.11n 2x2 phy core */
#define	SRAMC_CORE_ID		0x822	/* SRAM controller core */
#define	MINIMAC_CORE_ID		0x823	/* MINI MAC/phy core */
#define	ARM11_CORE_ID		0x824	/* ARM 1176 core */
#define	ARM7S_CORE_ID		0x825	/* ARM7tdmi-s core */
#define	LPPHY_CORE_ID		0x826	/* 802.11a/b/g phy core */
#define	PMU_CORE_ID		0x827	/* PMU core */
#define	SSNPHY_CORE_ID		0x828	/* 802.11n single-stream phy core */
#define	SDIOD_CORE_ID		0x829	/* SDIO device core */
#define	ARMCM3_CORE_ID		0x82a	/* ARM Cortex M3 core */
#define	HTPHY_CORE_ID		0x82b	/* 802.11n 4x4 phy core */
#define	MIPS74K_CORE_ID		0x82c	/* mips 74k core */
#define	GMAC_CORE_ID		0x82d	/* Gigabit MAC core */
#define	DMEMC_CORE_ID		0x82e	/* DDR1/2 memory controller core */
#define	PCIERC_CORE_ID		0x82f	/* PCIE Root Complex core */
#define	OCP_CORE_ID		0x830	/* OCP2OCP bridge core */
#define	SC_CORE_ID		0x831	/* shared common core */
#define	AHB_CORE_ID		0x832	/* OCP2AHB bridge core */
#define	SPIH_CORE_ID		0x833	/* SPI host core */
#define	I2S_CORE_ID		0x834	/* I2S core */
#define	DMEMS_CORE_ID		0x835	/* SDR/DDR1 memory controller core */
#define	DEF_SHIM_COMP		0x837	/* SHIM component in ubus/6362 */
#define OOB_ROUTER_CORE_ID	0x367	/* OOB router core ID */
#define	DEF_AI_COMP		0xfff	/* Default component, in ai chips it
					 * maps all unused address ranges
					 */

/* Common core control flags */
#define	SICF_BIST_EN		0x8000
#define	SICF_PME_EN		0x4000
#define	SICF_CORE_BITS		0x3ffc
#define	SICF_FGC		0x0002
#define	SICF_CLOCK_EN		0x0001

/* Common core status flags */
#define	SISF_BIST_DONE		0x8000
#define	SISF_BIST_ERROR		0x4000
#define	SISF_GATED_CLK		0x2000
#define	SISF_DMA64		0x1000
#define	SISF_CORE_BITS		0x0fff

#endif				/* _BRCM_SOC_H */
