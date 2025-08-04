/* SPDX-License-Identifier: GPL-2.0 */
/*
 * R-Car RPC Interface Registers Definitions
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#ifndef __RENESAS_RPC_IF_REGS_H__
#define __RENESAS_RPC_IF_REGS_H__

#include <linux/bits.h>

#define RPCIF_CMNCR		0x0000	/* R/W */
#define RPCIF_CMNCR_MD		BIT(31)
#define RPCIF_CMNCR_MOIIO3(val)	(((val) & 0x3) << 22)
#define RPCIF_CMNCR_MOIIO2(val)	(((val) & 0x3) << 20)
#define RPCIF_CMNCR_MOIIO1(val)	(((val) & 0x3) << 18)
#define RPCIF_CMNCR_MOIIO0(val)	(((val) & 0x3) << 16)
#define RPCIF_CMNCR_MOIIO(val)	(RPCIF_CMNCR_MOIIO0(val) | RPCIF_CMNCR_MOIIO1(val) | \
				 RPCIF_CMNCR_MOIIO2(val) | RPCIF_CMNCR_MOIIO3(val))
#define RPCIF_CMNCR_IO3FV(val)	(((val) & 0x3) << 14) /* documented for RZ/G2L */
#define RPCIF_CMNCR_IO2FV(val)	(((val) & 0x3) << 12) /* documented for RZ/G2L */
#define RPCIF_CMNCR_IO0FV(val)	(((val) & 0x3) << 8)
#define RPCIF_CMNCR_IOFV(val)	(RPCIF_CMNCR_IO0FV(val) | RPCIF_CMNCR_IO2FV(val) | \
				 RPCIF_CMNCR_IO3FV(val))
#define RPCIF_CMNCR_BSZ(val)	(((val) & 0x3) << 0)

#define RPCIF_SSLDR		0x0004	/* R/W */
#define RPCIF_SSLDR_SPNDL(d)	(((d) & 0x7) << 16)
#define RPCIF_SSLDR_SLNDL(d)	(((d) & 0x7) << 8)
#define RPCIF_SSLDR_SCKDL(d)	(((d) & 0x7) << 0)

#define RPCIF_DRCR		0x000C	/* R/W */
#define RPCIF_DRCR_SSLN		BIT(24)
#define RPCIF_DRCR_RBURST(v)	((((v) - 1) & 0x1F) << 16)
#define RPCIF_DRCR_RCF		BIT(9)
#define RPCIF_DRCR_RBE		BIT(8)
#define RPCIF_DRCR_SSLE		BIT(0)

#define RPCIF_DRCMR		0x0010	/* R/W */
#define RPCIF_DRCMR_CMD(c)	(((c) & 0xFF) << 16)
#define RPCIF_DRCMR_OCMD(c)	(((c) & 0xFF) << 0)

#define RPCIF_DREAR		0x0014	/* R/W */
#define RPCIF_DREAR_EAV(c)	(((c) & 0xF) << 16)
#define RPCIF_DREAR_EAC(c)	(((c) & 0x7) << 0)

#define RPCIF_DROPR		0x0018	/* R/W */

#define RPCIF_DRENR		0x001C	/* R/W */
#define RPCIF_DRENR_CDB(o)	(((u32)((o) & 0x3)) << 30)
#define RPCIF_DRENR_OCDB(o)	(((o) & 0x3) << 28)
#define RPCIF_DRENR_ADB(o)	(((o) & 0x3) << 24)
#define RPCIF_DRENR_OPDB(o)	(((o) & 0x3) << 20)
#define RPCIF_DRENR_DRDB(o)	(((o) & 0x3) << 16)
#define RPCIF_DRENR_DME		BIT(15)
#define RPCIF_DRENR_CDE		BIT(14)
#define RPCIF_DRENR_OCDE	BIT(12)
#define RPCIF_DRENR_ADE(v)	(((v) & 0xF) << 8)
#define RPCIF_DRENR_OPDE(v)	(((v) & 0xF) << 4)

#define RPCIF_SMCR		0x0020	/* R/W */
#define RPCIF_SMCR_SSLKP	BIT(8)
#define RPCIF_SMCR_SPIRE	BIT(2)
#define RPCIF_SMCR_SPIWE	BIT(1)
#define RPCIF_SMCR_SPIE		BIT(0)

#define RPCIF_SMCMR		0x0024	/* R/W */
#define RPCIF_SMCMR_CMD(c)	(((c) & 0xFF) << 16)
#define RPCIF_SMCMR_OCMD(c)	(((c) & 0xFF) << 0)

#define RPCIF_SMADR		0x0028	/* R/W */

#define RPCIF_SMOPR		0x002C	/* R/W */
#define RPCIF_SMOPR_OPD3(o)	(((o) & 0xFF) << 24)
#define RPCIF_SMOPR_OPD2(o)	(((o) & 0xFF) << 16)
#define RPCIF_SMOPR_OPD1(o)	(((o) & 0xFF) << 8)
#define RPCIF_SMOPR_OPD0(o)	(((o) & 0xFF) << 0)

#define RPCIF_SMENR		0x0030	/* R/W */
#define RPCIF_SMENR_CDB(o)	(((o) & 0x3) << 30)
#define RPCIF_SMENR_OCDB(o)	(((o) & 0x3) << 28)
#define RPCIF_SMENR_ADB(o)	(((o) & 0x3) << 24)
#define RPCIF_SMENR_OPDB(o)	(((o) & 0x3) << 20)
#define RPCIF_SMENR_SPIDB(o)	(((o) & 0x3) << 16)
#define RPCIF_SMENR_DME		BIT(15)
#define RPCIF_SMENR_CDE		BIT(14)
#define RPCIF_SMENR_OCDE	BIT(12)
#define RPCIF_SMENR_ADE(v)	(((v) & 0xF) << 8)
#define RPCIF_SMENR_OPDE(v)	(((v) & 0xF) << 4)
#define RPCIF_SMENR_SPIDE(v)	(((v) & 0xF) << 0)

#define RPCIF_SMRDR0		0x0038	/* R */
#define RPCIF_SMRDR1		0x003C	/* R */
#define RPCIF_SMWDR0		0x0040	/* W */
#define RPCIF_SMWDR1		0x0044	/* W */

#define RPCIF_CMNSR		0x0048	/* R */
#define RPCIF_CMNSR_SSLF	BIT(1)
#define RPCIF_CMNSR_TEND	BIT(0)

#define RPCIF_DRDMCR		0x0058	/* R/W */
#define RPCIF_DMDMCR_DMCYC(v)	((((v) - 1) & 0x1F) << 0)

#define RPCIF_DRDRENR		0x005C	/* R/W */
#define RPCIF_DRDRENR_HYPE(v)	(((v) & 0x7) << 12)
#define RPCIF_DRDRENR_ADDRE	BIT(8)
#define RPCIF_DRDRENR_OPDRE	BIT(4)
#define RPCIF_DRDRENR_DRDRE	BIT(0)

#define RPCIF_SMDMCR		0x0060	/* R/W */
#define RPCIF_SMDMCR_DMCYC(v)	((((v) - 1) & 0x1F) << 0)

#define RPCIF_SMDRENR		0x0064	/* R/W */
#define RPCIF_SMDRENR_HYPE(v)	(((v) & 0x7) << 12)
#define RPCIF_SMDRENR_ADDRE	BIT(8)
#define RPCIF_SMDRENR_OPDRE	BIT(4)
#define RPCIF_SMDRENR_SPIDRE	BIT(0)

#define RPCIF_PHYADD		0x0070	/* R/W available on R-Car E3/D3/V3M and RZ/G2{E,L} */
#define RPCIF_PHYWR		0x0074	/* R/W available on R-Car E3/D3/V3M and RZ/G2{E,L} */

#define RPCIF_PHYCNT		0x007C	/* R/W */
#define RPCIF_PHYCNT_CAL	BIT(31)
#define RPCIF_PHYCNT_OCTA(v)	(((v) & 0x3) << 22)
#define RPCIF_PHYCNT_EXDS	BIT(21)
#define RPCIF_PHYCNT_OCT	BIT(20)
#define RPCIF_PHYCNT_DDRCAL	BIT(19)
#define RPCIF_PHYCNT_HS		BIT(18)
#define RPCIF_PHYCNT_CKSEL(v)	(((v) & 0x3) << 16) /* valid only for RZ/G2L */
#define RPCIF_PHYCNT_STRTIM(v)	(((v) & 0x7) << 15 | ((v) & 0x8) << 24) /* valid for R-Car and RZ/G2{E,H,M,N} */

#define RPCIF_PHYCNT_WBUF2	BIT(4)
#define RPCIF_PHYCNT_WBUF	BIT(2)
#define RPCIF_PHYCNT_PHYMEM(v)	(((v) & 0x3) << 0)
#define RPCIF_PHYCNT_PHYMEM_MASK GENMASK(1, 0)

#define RPCIF_PHYOFFSET1	0x0080	/* R/W */
#define RPCIF_PHYOFFSET1_DDRTMG(v) (((v) & 0x3) << 28)

#define RPCIF_PHYOFFSET2	0x0084	/* R/W */
#define RPCIF_PHYOFFSET2_OCTTMG(v) (((v) & 0x7) << 8)

#define RPCIF_PHYINT		0x0088	/* R/W */
#define RPCIF_PHYINT_WPVAL	BIT(1)

#endif /* __RENESAS_RPC_IF_REGS_H__ */
