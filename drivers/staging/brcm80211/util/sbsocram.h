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

#ifndef	_SBSOCRAM_H
#define	_SBSOCRAM_H

#ifndef _LANGUAGE_ASSEMBLY

/* cpp contortions to concatenate w/arg prescan */
#ifndef PAD
#define	_PADLINE(line)	pad ## line
#define	_XSTR(line)	_PADLINE(line)
#define	PAD		_XSTR(__LINE__)
#endif				/* PAD */

/* Memcsocram core registers */
typedef volatile struct sbsocramregs {
	u32 coreinfo;
	u32 bwalloc;
	u32 extracoreinfo;
	u32 biststat;
	u32 bankidx;
	u32 standbyctrl;

	u32 errlogstatus;	/* rev 6 */
	u32 errlogaddr;	/* rev 6 */
	/* used for patching rev 3 & 5 */
	u32 cambankidx;
	u32 cambankstandbyctrl;
	u32 cambankpatchctrl;
	u32 cambankpatchtblbaseaddr;
	u32 cambankcmdreg;
	u32 cambankdatareg;
	u32 cambankmaskreg;
	u32 PAD[1];
	u32 bankinfo;	/* corev 8 */
	u32 PAD[15];
	u32 extmemconfig;
	u32 extmemparitycsr;
	u32 extmemparityerrdata;
	u32 extmemparityerrcnt;
	u32 extmemwrctrlandsize;
	u32 PAD[84];
	u32 workaround;
	u32 pwrctl;		/* corerev >= 2 */
} sbsocramregs_t;

#endif				/* _LANGUAGE_ASSEMBLY */

/* Register offsets */
#define	SR_COREINFO		0x00
#define	SR_BWALLOC		0x04
#define	SR_BISTSTAT		0x0c
#define	SR_BANKINDEX		0x10
#define	SR_BANKSTBYCTL		0x14
#define SR_PWRCTL		0x1e8

/* Coreinfo register */
#define	SRCI_PT_MASK		0x00070000	/* corerev >= 6; port type[18:16] */
#define	SRCI_PT_SHIFT		16
/* port types : SRCI_PT_<processorPT>_<backplanePT> */
#define SRCI_PT_OCP_OCP		0
#define SRCI_PT_AXI_OCP		1
#define SRCI_PT_ARM7AHB_OCP	2
#define SRCI_PT_CM3AHB_OCP	3
#define SRCI_PT_AXI_AXI		4
#define SRCI_PT_AHB_AXI		5
/* corerev >= 3 */
#define SRCI_LSS_MASK		0x00f00000
#define SRCI_LSS_SHIFT		20
#define SRCI_LRS_MASK		0x0f000000
#define SRCI_LRS_SHIFT		24

/* In corerev 0, the memory size is 2 to the power of the
 * base plus 16 plus to the contents of the memsize field plus 1.
 */
#define	SRCI_MS0_MASK		0xf
#define SR_MS0_BASE		16

/*
 * In corerev 1 the bank size is 2 ^ the bank size field plus 14,
 * the memory size is number of banks times bank size.
 * The same applies to rom size.
 */
#define	SRCI_ROMNB_MASK		0xf000
#define	SRCI_ROMNB_SHIFT	12
#define	SRCI_ROMBSZ_MASK	0xf00
#define	SRCI_ROMBSZ_SHIFT	8
#define	SRCI_SRNB_MASK		0xf0
#define	SRCI_SRNB_SHIFT		4
#define	SRCI_SRBSZ_MASK		0xf
#define	SRCI_SRBSZ_SHIFT	0

#define SR_BSZ_BASE		14

/* Standby control register */
#define	SRSC_SBYOVR_MASK	0x80000000
#define	SRSC_SBYOVR_SHIFT	31
#define	SRSC_SBYOVRVAL_MASK	0x60000000
#define	SRSC_SBYOVRVAL_SHIFT	29
#define	SRSC_SBYEN_MASK		0x01000000	/* rev >= 3 */
#define	SRSC_SBYEN_SHIFT	24

/* Power control register */
#define SRPC_PMU_STBYDIS_MASK	0x00000010	/* rev >= 3 */
#define SRPC_PMU_STBYDIS_SHIFT	4
#define SRPC_STBYOVRVAL_MASK	0x00000008
#define SRPC_STBYOVRVAL_SHIFT	3
#define SRPC_STBYOVR_MASK	0x00000007
#define SRPC_STBYOVR_SHIFT	0

/* Extra core capability register */
#define SRECC_NUM_BANKS_MASK   0x000000F0
#define SRECC_NUM_BANKS_SHIFT  4
#define SRECC_BANKSIZE_MASK    0x0000000F
#define SRECC_BANKSIZE_SHIFT   0

#define SRECC_BANKSIZE(value)	 (1 << (value))

/* CAM bank patch control */
#define SRCBPC_PATCHENABLE 0x80000000

#define SRP_ADDRESS   0x0001FFFC
#define SRP_VALID     0x8000

/* CAM bank command reg */
#define SRCMD_WRITE  0x00020000
#define SRCMD_READ   0x00010000
#define SRCMD_DONE   0x80000000

#define SRCMD_DONE_DLY	1000

/* bankidx and bankinfo reg defines corerev >= 8 */
#define SOCRAM_BANKINFO_SZMASK		0x3f
#define SOCRAM_BANKIDX_ROM_MASK		0x100

#define SOCRAM_BANKIDX_MEMTYPE_SHIFT	8
/* socram bankinfo memtype */
#define SOCRAM_MEMTYPE_RAM		0
#define SOCRAM_MEMTYPE_R0M		1
#define SOCRAM_MEMTYPE_DEVRAM		2

#define	SOCRAM_BANKINFO_REG		0x40
#define	SOCRAM_BANKIDX_REG		0x10
#define	SOCRAM_BANKINFO_STDBY_MASK	0x400
#define	SOCRAM_BANKINFO_STDBY_TIMER	0x800

/* bankinfo rev >= 10 */
#define SOCRAM_BANKINFO_DEVRAMSEL_SHIFT	13
#define SOCRAM_BANKINFO_DEVRAMSEL_MASK	0x2000
#define SOCRAM_BANKINFO_DEVRAMPRO_SHIFT	14
#define SOCRAM_BANKINFO_DEVRAMPRO_MASK	0x4000

/* extracoreinfo register */
#define SOCRAM_DEVRAMBANK_MASK		0xF000
#define SOCRAM_DEVRAMBANK_SHIFT		12

/* bank info to calculate bank size */
#define	SOCRAM_BANKINFO_SZBASE		8192
#define SOCRAM_BANKSIZE_SHIFT		13	/* SOCRAM_BANKINFO_SZBASE */

#endif				/* _SBSOCRAM_H */
