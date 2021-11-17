/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
** asm/bootinfo-mac.h -- Macintosh-specific boot information definitions
*/

#ifndef _UAPI_ASM_M68K_BOOTINFO_MAC_H
#define _UAPI_ASM_M68K_BOOTINFO_MAC_H


    /*
     *  Macintosh-specific tags (all __be32)
     */

#define BI_MAC_MODEL		0x8000	/* Mac Gestalt ID (model type) */
#define BI_MAC_VADDR		0x8001	/* Mac video base address */
#define BI_MAC_VDEPTH		0x8002	/* Mac video depth */
#define BI_MAC_VROW		0x8003	/* Mac video rowbytes */
#define BI_MAC_VDIM		0x8004	/* Mac video dimensions */
#define BI_MAC_VLOGICAL		0x8005	/* Mac video logical base */
#define BI_MAC_SCCBASE		0x8006	/* Mac SCC base address */
#define BI_MAC_BTIME		0x8007	/* Mac boot time */
#define BI_MAC_GMTBIAS		0x8008	/* Mac GMT timezone offset */
#define BI_MAC_MEMSIZE		0x8009	/* Mac RAM size (sanity check) */
#define BI_MAC_CPUID		0x800a	/* Mac CPU type (sanity check) */
#define BI_MAC_ROMBASE		0x800b	/* Mac system ROM base address */


    /*
     *  Macintosh hardware profile data - unused, see macintosh.h for
     *  reasonable type values
     */

#define BI_MAC_VIA1BASE		0x8010	/* Mac VIA1 base address (always present) */
#define BI_MAC_VIA2BASE		0x8011	/* Mac VIA2 base address (type varies) */
#define BI_MAC_VIA2TYPE		0x8012	/* Mac VIA2 type (VIA, RBV, OSS) */
#define BI_MAC_ADBTYPE		0x8013	/* Mac ADB interface type */
#define BI_MAC_ASCBASE		0x8014	/* Mac Apple Sound Chip base address */
#define BI_MAC_SCSI5380		0x8015	/* Mac NCR 5380 SCSI (base address, multi) */
#define BI_MAC_SCSIDMA		0x8016	/* Mac SCSI DMA (base address) */
#define BI_MAC_SCSI5396		0x8017	/* Mac NCR 53C96 SCSI (base address, multi) */
#define BI_MAC_IDETYPE		0x8018	/* Mac IDE interface type */
#define BI_MAC_IDEBASE		0x8019	/* Mac IDE interface base address */
#define BI_MAC_NUBUS		0x801a	/* Mac Nubus type (none, regular, pseudo) */
#define BI_MAC_SLOTMASK		0x801b	/* Mac Nubus slots present */
#define BI_MAC_SCCTYPE		0x801c	/* Mac SCC serial type (normal, IOP) */
#define BI_MAC_ETHTYPE		0x801d	/* Mac builtin ethernet type (Sonic, MACE */
#define BI_MAC_ETHBASE		0x801e	/* Mac builtin ethernet base address */
#define BI_MAC_PMU		0x801f	/* Mac power management / poweroff hardware */
#define BI_MAC_IOP_SWIM		0x8020	/* Mac SWIM floppy IOP */
#define BI_MAC_IOP_ADB		0x8021	/* Mac ADB IOP */


    /*
     * Macintosh Gestalt numbers (BI_MAC_MODEL)
     */

#define MAC_MODEL_II		6
#define MAC_MODEL_IIX		7
#define MAC_MODEL_IICX		8
#define MAC_MODEL_SE30		9
#define MAC_MODEL_IICI		11
#define MAC_MODEL_IIFX		13	/* And well numbered it is too */
#define MAC_MODEL_IISI		18
#define MAC_MODEL_LC		19
#define MAC_MODEL_Q900		20
#define MAC_MODEL_PB170		21
#define MAC_MODEL_Q700		22
#define MAC_MODEL_CLII		23	/* aka: P200 */
#define MAC_MODEL_PB140		25
#define MAC_MODEL_Q950		26	/* aka: WGS95 */
#define MAC_MODEL_LCIII		27	/* aka: P450 */
#define MAC_MODEL_PB210		29
#define MAC_MODEL_C650		30
#define MAC_MODEL_PB230		32
#define MAC_MODEL_PB180		33
#define MAC_MODEL_PB160		34
#define MAC_MODEL_Q800		35	/* aka: WGS80 */
#define MAC_MODEL_Q650		36
#define MAC_MODEL_LCII		37	/* aka: P400/405/410/430 */
#define MAC_MODEL_PB250		38
#define MAC_MODEL_IIVI		44
#define MAC_MODEL_P600		45	/* aka: P600CD */
#define MAC_MODEL_IIVX		48
#define MAC_MODEL_CCL		49	/* aka: P250 */
#define MAC_MODEL_PB165C	50
#define MAC_MODEL_C610		52	/* aka: WGS60 */
#define MAC_MODEL_Q610		53
#define MAC_MODEL_PB145		54	/* aka: PB145B */
#define MAC_MODEL_P520		56	/* aka: LC520 */
#define MAC_MODEL_C660		60
#define MAC_MODEL_P460		62	/* aka: LCIII+, P466/P467 */
#define MAC_MODEL_PB180C	71
#define MAC_MODEL_PB520		72	/* aka: PB520C, PB540, PB540C, PB550C */
#define MAC_MODEL_PB270C	77
#define MAC_MODEL_Q840		78
#define MAC_MODEL_P550		80	/* aka: LC550, P560 */
#define MAC_MODEL_CCLII		83	/* aka: P275 */
#define MAC_MODEL_PB165		84
#define MAC_MODEL_PB190		85	/* aka: PB190CS */
#define MAC_MODEL_TV		88
#define MAC_MODEL_P475		89	/* aka: LC475, P476 */
#define MAC_MODEL_P475F		90	/* aka: P475 w/ FPU (no LC040) */
#define MAC_MODEL_P575		92	/* aka: LC575, P577/P578 */
#define MAC_MODEL_Q605		94
#define MAC_MODEL_Q605_ACC	95	/* Q605 accelerated to 33 MHz */
#define MAC_MODEL_Q630		98	/* aka: LC630, P630/631/635/636/637/638/640 */
#define MAC_MODEL_P588		99	/* aka: LC580, P580 */
#define MAC_MODEL_PB280		102
#define MAC_MODEL_PB280C	103
#define MAC_MODEL_PB150		115


    /*
     *  Latest Macintosh bootinfo version
     */

#define MAC_BOOTI_VERSION	MK_BI_VERSION(2, 0)


#endif /* _UAPI_ASM_M68K_BOOTINFO_MAC_H */
