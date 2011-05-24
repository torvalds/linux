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

#ifndef	_HNDSOC_H
#define	_HNDSOC_H

/* Include the soci specific files */
#include <sbconfig.h>
#include <aidmp.h>

/*
 * SOC Interconnect Address Map.
 * All regions may not exist on all chips.
 */
#define SI_SDRAM_BASE		0x00000000	/* Physical SDRAM */
#define SI_PCI_MEM		0x08000000	/* Host Mode sb2pcitranslation0 (64 MB) */
#define SI_PCI_MEM_SZ		(64 * 1024 * 1024)
#define SI_PCI_CFG		0x0c000000	/* Host Mode sb2pcitranslation1 (64 MB) */
#define	SI_SDRAM_SWAPPED	0x10000000	/* Byteswapped Physical SDRAM */
#define SI_SDRAM_R2		0x80000000	/* Region 2 for sdram (512 MB) */

#ifdef SI_ENUM_BASE_VARIABLE
#define SI_ENUM_BASE		(sii->pub.si_enum_base)
#else
#define SI_ENUM_BASE    	0x18000000	/* Enumeration space base */
#endif				/* SI_ENUM_BASE_VARIABLE */

#define SI_WRAP_BASE    	0x18100000	/* Wrapper space base */
#define SI_CORE_SIZE    	0x1000	/* each core gets 4Kbytes for registers */
#define	SI_MAXCORES		16	/* Max cores (this is arbitrary, for software
					 * convenience and could be changed if we
					 * make any larger chips
					 */

#define	SI_FASTRAM		0x19000000	/* On-chip RAM on chips that also have DDR */
#define	SI_FASTRAM_SWAPPED	0x19800000

#define	SI_FLASH2		0x1c000000	/* Flash Region 2 (region 1 shadowed here) */
#define	SI_FLASH2_SZ		0x02000000	/* Size of Flash Region 2 */
#define	SI_ARMCM3_ROM		0x1e000000	/* ARM Cortex-M3 ROM */
#define	SI_FLASH1		0x1fc00000	/* MIPS Flash Region 1 */
#define	SI_FLASH1_SZ		0x00400000	/* MIPS Size of Flash Region 1 */
#define	SI_ARM7S_ROM		0x20000000	/* ARM7TDMI-S ROM */
#define	SI_ARMCM3_SRAM2		0x60000000	/* ARM Cortex-M3 SRAM Region 2 */
#define	SI_ARM7S_SRAM2		0x80000000	/* ARM7TDMI-S SRAM Region 2 */
#define	SI_ARM_FLASH1		0xffff0000	/* ARM Flash Region 1 */
#define	SI_ARM_FLASH1_SZ	0x00010000	/* ARM Size of Flash Region 1 */

#define SI_PCI_DMA		0x40000000	/* Client Mode sb2pcitranslation2 (1 GB) */
#define SI_PCI_DMA2		0x80000000	/* Client Mode sb2pcitranslation2 (1 GB) */
#define SI_PCI_DMA_SZ		0x40000000	/* Client Mode sb2pcitranslation2 size in bytes */
#define SI_PCIE_DMA_L32		0x00000000	/* PCIE Client Mode sb2pcitranslation2
						 * (2 ZettaBytes), low 32 bits
						 */
#define SI_PCIE_DMA_H32		0x80000000	/* PCIE Client Mode sb2pcitranslation2
						 * (2 ZettaBytes), high 32 bits
						 */

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
#define	DEF_AI_COMP		0xfff	/* Default component, in ai chips it maps all
					 * unused address ranges
					 */

/* There are TWO constants on all HND chips: SI_ENUM_BASE above,
 * and chipcommon being the first core:
 */
#define	SI_CC_IDX		0

/* SOC Interconnect types (aka chip types) */
#define	SOCI_AI			1

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

/* A register that is common to all cores to
 * communicate w/PMU regarding clock control.
 */
#define SI_CLK_CTL_ST		0x1e0	/* clock control and status */

/* clk_ctl_st register */
#define	CCS_FORCEALP		0x00000001	/* force ALP request */
#define	CCS_FORCEHT		0x00000002	/* force HT request */
#define	CCS_FORCEILP		0x00000004	/* force ILP request */
#define	CCS_ALPAREQ		0x00000008	/* ALP Avail Request */
#define	CCS_HTAREQ		0x00000010	/* HT Avail Request */
#define	CCS_FORCEHWREQOFF	0x00000020	/* Force HW Clock Request Off */
#define CCS_ERSRC_REQ_MASK	0x00000700	/* external resource requests */
#define CCS_ERSRC_REQ_SHIFT	8
#define	CCS_ALPAVAIL		0x00010000	/* ALP is available */
#define	CCS_HTAVAIL		0x00020000	/* HT is available */
#define CCS_BP_ON_APL		0x00040000	/* RO: Backplane is running on ALP clock */
#define CCS_BP_ON_HT		0x00080000	/* RO: Backplane is running on HT clock */
#define CCS_ERSRC_STS_MASK	0x07000000	/* external resource status */
#define CCS_ERSRC_STS_SHIFT	24

#define	CCS0_HTAVAIL		0x00010000	/* HT avail in chipc and pcmcia on 4328a0 */
#define	CCS0_ALPAVAIL		0x00020000	/* ALP avail in chipc and pcmcia on 4328a0 */

/* Not really related to SOC Interconnect, but a couple of software
 * conventions for the use the flash space:
 */

/* Minimum amount of flash we support */
#define FLASH_MIN		0x00020000	/* Minimum flash size */

/* A boot/binary may have an embedded block that describes its size  */
#define	BISZ_OFFSET		0x3e0	/* At this offset into the binary */
#define	BISZ_MAGIC		0x4249535a	/* Marked with this value: 'BISZ' */
#define	BISZ_MAGIC_IDX		0	/* Word 0: magic */
#define	BISZ_TXTST_IDX		1	/*      1: text start */
#define	BISZ_TXTEND_IDX		2	/*      2: text end */
#define	BISZ_DATAST_IDX		3	/*      3: data start */
#define	BISZ_DATAEND_IDX	4	/*      4: data end */
#define	BISZ_BSSST_IDX		5	/*      5: bss start */
#define	BISZ_BSSEND_IDX		6	/*      6: bss end */
#define BISZ_SIZE		7	/* descriptor size in 32-bit integers */

#endif				/* _HNDSOC_H */
