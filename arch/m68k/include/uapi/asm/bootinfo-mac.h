/*
** asm/bootinfo-mac.h -- Macintosh-specific boot information definitions
*/

#ifndef _UAPI_ASM_M68K_BOOTINFO_MAC_H
#define _UAPI_ASM_M68K_BOOTINFO_MAC_H


    /*
     *  Macintosh-specific tags (all u_long)
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
     *  Latest Macintosh bootinfo version
     */

#define MAC_BOOTI_VERSION	MK_BI_VERSION(2, 0)


#endif /* _UAPI_ASM_M68K_BOOTINFO_MAC_H */
