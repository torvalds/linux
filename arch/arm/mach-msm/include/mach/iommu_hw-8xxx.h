/* Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef __ARCH_ARM_MACH_MSM_IOMMU_HW_8XXX_H
#define __ARCH_ARM_MACH_MSM_IOMMU_HW_8XXX_H

#define CTX_SHIFT 12

#define GET_GLOBAL_REG(reg, base) (readl((base) + (reg)))
#define GET_CTX_REG(reg, base, ctx) \
				(readl((base) + (reg) + ((ctx) << CTX_SHIFT)))

#define SET_GLOBAL_REG(reg, base, val)	writel((val), ((base) + (reg)))

#define SET_CTX_REG(reg, base, ctx, val) \
			writel((val), ((base) + (reg) + ((ctx) << CTX_SHIFT)))

/* Wrappers for numbered registers */
#define SET_GLOBAL_REG_N(b, n, r, v) SET_GLOBAL_REG(b, ((r) + (n << 2)), (v))
#define GET_GLOBAL_REG_N(b, n, r)    GET_GLOBAL_REG(b, ((r) + (n << 2)))

/* Field wrappers */
#define GET_GLOBAL_FIELD(b, r, F)    GET_FIELD(((b) + (r)), F##_MASK, F##_SHIFT)
#define GET_CONTEXT_FIELD(b, c, r, F)	\
	GET_FIELD(((b) + (r) + ((c) << CTX_SHIFT)), F##_MASK, F##_SHIFT)

#define SET_GLOBAL_FIELD(b, r, F, v) \
	SET_FIELD(((b) + (r)), F##_MASK, F##_SHIFT, (v))
#define SET_CONTEXT_FIELD(b, c, r, F, v)	\
	SET_FIELD(((b) + (r) + ((c) << CTX_SHIFT)), F##_MASK, F##_SHIFT, (v))

#define GET_FIELD(addr, mask, shift)  ((readl(addr) >> (shift)) & (mask))

#define SET_FIELD(addr, mask, shift, v) \
do { \
	int t = readl(addr); \
	writel((t & ~((mask) << (shift))) + (((v) & (mask)) << (shift)), addr);\
} while (0)


#define NUM_FL_PTE	4096
#define NUM_SL_PTE	256
#define NUM_TEX_CLASS	8

/* First-level page table bits */
#define FL_BASE_MASK		0xFFFFFC00
#define FL_TYPE_TABLE		(1 << 0)
#define FL_TYPE_SECT		(2 << 0)
#define FL_SUPERSECTION		(1 << 18)
#define FL_AP_WRITE		(1 << 10)
#define FL_AP_READ		(1 << 11)
#define FL_SHARED		(1 << 16)
#define FL_BUFFERABLE		(1 << 2)
#define FL_CACHEABLE		(1 << 3)
#define FL_TEX0			(1 << 12)
#define FL_OFFSET(va)		(((va) & 0xFFF00000) >> 20)
#define FL_NG			(1 << 17)

/* Second-level page table bits */
#define SL_BASE_MASK_LARGE	0xFFFF0000
#define SL_BASE_MASK_SMALL	0xFFFFF000
#define SL_TYPE_LARGE		(1 << 0)
#define SL_TYPE_SMALL		(2 << 0)
#define SL_AP0			(1 << 4)
#define SL_AP1			(2 << 4)
#define SL_SHARED		(1 << 10)
#define SL_BUFFERABLE		(1 << 2)
#define SL_CACHEABLE		(1 << 3)
#define SL_TEX0			(1 << 6)
#define SL_OFFSET(va)		(((va) & 0xFF000) >> 12)
#define SL_NG			(1 << 11)

/* Memory type and cache policy attributes */
#define MT_SO			0
#define MT_DEV			1
#define MT_NORMAL		2
#define CP_NONCACHED		0
#define CP_WB_WA		1
#define CP_WT			2
#define CP_WB_NWA		3

/* Global register setters / getters */
#define SET_M2VCBR_N(b, N, v)	 SET_GLOBAL_REG_N(M2VCBR_N, N, (b), (v))
#define SET_CBACR_N(b, N, v)	 SET_GLOBAL_REG_N(CBACR_N, N, (b), (v))
#define SET_TLBRSW(b, v)	 SET_GLOBAL_REG(TLBRSW, (b), (v))
#define SET_TLBTR0(b, v)	 SET_GLOBAL_REG(TLBTR0, (b), (v))
#define SET_TLBTR1(b, v)	 SET_GLOBAL_REG(TLBTR1, (b), (v))
#define SET_TLBTR2(b, v)	 SET_GLOBAL_REG(TLBTR2, (b), (v))
#define SET_TESTBUSCR(b, v)	 SET_GLOBAL_REG(TESTBUSCR, (b), (v))
#define SET_GLOBAL_TLBIALL(b, v) SET_GLOBAL_REG(GLOBAL_TLBIALL, (b), (v))
#define SET_TLBIVMID(b, v)	 SET_GLOBAL_REG(TLBIVMID, (b), (v))
#define SET_CR(b, v)		 SET_GLOBAL_REG(CR, (b), (v))
#define SET_EAR(b, v)		 SET_GLOBAL_REG(EAR, (b), (v))
#define SET_ESR(b, v)		 SET_GLOBAL_REG(ESR, (b), (v))
#define SET_ESRRESTORE(b, v)	 SET_GLOBAL_REG(ESRRESTORE, (b), (v))
#define SET_ESYNR0(b, v)	 SET_GLOBAL_REG(ESYNR0, (b), (v))
#define SET_ESYNR1(b, v)	 SET_GLOBAL_REG(ESYNR1, (b), (v))
#define SET_RPU_ACR(b, v)	 SET_GLOBAL_REG(RPU_ACR, (b), (v))

#define GET_M2VCBR_N(b, N)	 GET_GLOBAL_REG_N(M2VCBR_N, N, (b))
#define GET_CBACR_N(b, N)	 GET_GLOBAL_REG_N(CBACR_N, N, (b))
#define GET_TLBTR0(b)		 GET_GLOBAL_REG(TLBTR0, (b))
#define GET_TLBTR1(b)		 GET_GLOBAL_REG(TLBTR1, (b))
#define GET_TLBTR2(b)		 GET_GLOBAL_REG(TLBTR2, (b))
#define GET_TESTBUSCR(b)	 GET_GLOBAL_REG(TESTBUSCR, (b))
#define GET_GLOBAL_TLBIALL(b)	 GET_GLOBAL_REG(GLOBAL_TLBIALL, (b))
#define GET_TLBIVMID(b)		 GET_GLOBAL_REG(TLBIVMID, (b))
#define GET_CR(b)		 GET_GLOBAL_REG(CR, (b))
#define GET_EAR(b)		 GET_GLOBAL_REG(EAR, (b))
#define GET_ESR(b)		 GET_GLOBAL_REG(ESR, (b))
#define GET_ESRRESTORE(b)	 GET_GLOBAL_REG(ESRRESTORE, (b))
#define GET_ESYNR0(b)		 GET_GLOBAL_REG(ESYNR0, (b))
#define GET_ESYNR1(b)		 GET_GLOBAL_REG(ESYNR1, (b))
#define GET_REV(b)		 GET_GLOBAL_REG(REV, (b))
#define GET_IDR(b)		 GET_GLOBAL_REG(IDR, (b))
#define GET_RPU_ACR(b)		 GET_GLOBAL_REG(RPU_ACR, (b))


/* Context register setters/getters */
#define SET_SCTLR(b, c, v)	 SET_CTX_REG(SCTLR, (b), (c), (v))
#define SET_ACTLR(b, c, v)	 SET_CTX_REG(ACTLR, (b), (c), (v))
#define SET_CONTEXTIDR(b, c, v)	 SET_CTX_REG(CONTEXTIDR, (b), (c), (v))
#define SET_TTBR0(b, c, v)	 SET_CTX_REG(TTBR0, (b), (c), (v))
#define SET_TTBR1(b, c, v)	 SET_CTX_REG(TTBR1, (b), (c), (v))
#define SET_TTBCR(b, c, v)	 SET_CTX_REG(TTBCR, (b), (c), (v))
#define SET_PAR(b, c, v)	 SET_CTX_REG(PAR, (b), (c), (v))
#define SET_FSR(b, c, v)	 SET_CTX_REG(FSR, (b), (c), (v))
#define SET_FSRRESTORE(b, c, v)	 SET_CTX_REG(FSRRESTORE, (b), (c), (v))
#define SET_FAR(b, c, v)	 SET_CTX_REG(FAR, (b), (c), (v))
#define SET_FSYNR0(b, c, v)	 SET_CTX_REG(FSYNR0, (b), (c), (v))
#define SET_FSYNR1(b, c, v)	 SET_CTX_REG(FSYNR1, (b), (c), (v))
#define SET_PRRR(b, c, v)	 SET_CTX_REG(PRRR, (b), (c), (v))
#define SET_NMRR(b, c, v)	 SET_CTX_REG(NMRR, (b), (c), (v))
#define SET_TLBLKCR(b, c, v)	 SET_CTX_REG(TLBLCKR, (b), (c), (v))
#define SET_V2PSR(b, c, v)	 SET_CTX_REG(V2PSR, (b), (c), (v))
#define SET_TLBFLPTER(b, c, v)	 SET_CTX_REG(TLBFLPTER, (b), (c), (v))
#define SET_TLBSLPTER(b, c, v)	 SET_CTX_REG(TLBSLPTER, (b), (c), (v))
#define SET_BFBCR(b, c, v)	 SET_CTX_REG(BFBCR, (b), (c), (v))
#define SET_CTX_TLBIALL(b, c, v) SET_CTX_REG(CTX_TLBIALL, (b), (c), (v))
#define SET_TLBIASID(b, c, v)	 SET_CTX_REG(TLBIASID, (b), (c), (v))
#define SET_TLBIVA(b, c, v)	 SET_CTX_REG(TLBIVA, (b), (c), (v))
#define SET_TLBIVAA(b, c, v)	 SET_CTX_REG(TLBIVAA, (b), (c), (v))
#define SET_V2PPR(b, c, v)	 SET_CTX_REG(V2PPR, (b), (c), (v))
#define SET_V2PPW(b, c, v)	 SET_CTX_REG(V2PPW, (b), (c), (v))
#define SET_V2PUR(b, c, v)	 SET_CTX_REG(V2PUR, (b), (c), (v))
#define SET_V2PUW(b, c, v)	 SET_CTX_REG(V2PUW, (b), (c), (v))
#define SET_RESUME(b, c, v)	 SET_CTX_REG(RESUME, (b), (c), (v))

#define GET_SCTLR(b, c)		 GET_CTX_REG(SCTLR, (b), (c))
#define GET_ACTLR(b, c)		 GET_CTX_REG(ACTLR, (b), (c))
#define GET_CONTEXTIDR(b, c)	 GET_CTX_REG(CONTEXTIDR, (b), (c))
#define GET_TTBR0(b, c)		 GET_CTX_REG(TTBR0, (b), (c))
#define GET_TTBR1(b, c)		 GET_CTX_REG(TTBR1, (b), (c))
#define GET_TTBCR(b, c)		 GET_CTX_REG(TTBCR, (b), (c))
#define GET_PAR(b, c)		 GET_CTX_REG(PAR, (b), (c))
#define GET_FSR(b, c)		 GET_CTX_REG(FSR, (b), (c))
#define GET_FSRRESTORE(b, c)	 GET_CTX_REG(FSRRESTORE, (b), (c))
#define GET_FAR(b, c)		 GET_CTX_REG(FAR, (b), (c))
#define GET_FSYNR0(b, c)	 GET_CTX_REG(FSYNR0, (b), (c))
#define GET_FSYNR1(b, c)	 GET_CTX_REG(FSYNR1, (b), (c))
#define GET_PRRR(b, c)		 GET_CTX_REG(PRRR, (b), (c))
#define GET_NMRR(b, c)		 GET_CTX_REG(NMRR, (b), (c))
#define GET_TLBLCKR(b, c)	 GET_CTX_REG(TLBLCKR, (b), (c))
#define GET_V2PSR(b, c)		 GET_CTX_REG(V2PSR, (b), (c))
#define GET_TLBFLPTER(b, c)	 GET_CTX_REG(TLBFLPTER, (b), (c))
#define GET_TLBSLPTER(b, c)	 GET_CTX_REG(TLBSLPTER, (b), (c))
#define GET_BFBCR(b, c)		 GET_CTX_REG(BFBCR, (b), (c))
#define GET_CTX_TLBIALL(b, c)	 GET_CTX_REG(CTX_TLBIALL, (b), (c))
#define GET_TLBIASID(b, c)	 GET_CTX_REG(TLBIASID, (b), (c))
#define GET_TLBIVA(b, c)	 GET_CTX_REG(TLBIVA, (b), (c))
#define GET_TLBIVAA(b, c)	 GET_CTX_REG(TLBIVAA, (b), (c))
#define GET_V2PPR(b, c)		 GET_CTX_REG(V2PPR, (b), (c))
#define GET_V2PPW(b, c)		 GET_CTX_REG(V2PPW, (b), (c))
#define GET_V2PUR(b, c)		 GET_CTX_REG(V2PUR, (b), (c))
#define GET_V2PUW(b, c)		 GET_CTX_REG(V2PUW, (b), (c))
#define GET_RESUME(b, c)	 GET_CTX_REG(RESUME, (b), (c))


/* Global field setters / getters */
/* Global Field Setters: */
/* CBACR_N */
#define SET_RWVMID(b, n, v)   SET_GLOBAL_FIELD(b, (n<<2)|(CBACR_N), RWVMID, v)
#define SET_RWE(b, n, v)      SET_GLOBAL_FIELD(b, (n<<2)|(CBACR_N), RWE, v)
#define SET_RWGE(b, n, v)     SET_GLOBAL_FIELD(b, (n<<2)|(CBACR_N), RWGE, v)
#define SET_CBVMID(b, n, v)   SET_GLOBAL_FIELD(b, (n<<2)|(CBACR_N), CBVMID, v)
#define SET_IRPTNDX(b, n, v)  SET_GLOBAL_FIELD(b, (n<<2)|(CBACR_N), IRPTNDX, v)


/* M2VCBR_N */
#define SET_VMID(b, n, v)     SET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), VMID, v)
#define SET_CBNDX(b, n, v)    SET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), CBNDX, v)
#define SET_BYPASSD(b, n, v)  SET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), BYPASSD, v)
#define SET_BPRCOSH(b, n, v)  SET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), BPRCOSH, v)
#define SET_BPRCISH(b, n, v)  SET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), BPRCISH, v)
#define SET_BPRCNSH(b, n, v)  SET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), BPRCNSH, v)
#define SET_BPSHCFG(b, n, v)  SET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), BPSHCFG, v)
#define SET_NSCFG(b, n, v)    SET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), NSCFG, v)
#define SET_BPMTCFG(b, n, v)  SET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), BPMTCFG, v)
#define SET_BPMEMTYPE(b, n, v) \
	SET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), BPMEMTYPE, v)


/* CR */
#define SET_RPUE(b, v)		 SET_GLOBAL_FIELD(b, CR, RPUE, v)
#define SET_RPUERE(b, v)	 SET_GLOBAL_FIELD(b, CR, RPUERE, v)
#define SET_RPUEIE(b, v)	 SET_GLOBAL_FIELD(b, CR, RPUEIE, v)
#define SET_DCDEE(b, v)		 SET_GLOBAL_FIELD(b, CR, DCDEE, v)
#define SET_CLIENTPD(b, v)       SET_GLOBAL_FIELD(b, CR, CLIENTPD, v)
#define SET_STALLD(b, v)	 SET_GLOBAL_FIELD(b, CR, STALLD, v)
#define SET_TLBLKCRWE(b, v)      SET_GLOBAL_FIELD(b, CR, TLBLKCRWE, v)
#define SET_CR_TLBIALLCFG(b, v)  SET_GLOBAL_FIELD(b, CR, CR_TLBIALLCFG, v)
#define SET_TLBIVMIDCFG(b, v)    SET_GLOBAL_FIELD(b, CR, TLBIVMIDCFG, v)
#define SET_CR_HUME(b, v)        SET_GLOBAL_FIELD(b, CR, CR_HUME, v)


/* ESR */
#define SET_CFG(b, v)		 SET_GLOBAL_FIELD(b, ESR, CFG, v)
#define SET_BYPASS(b, v)	 SET_GLOBAL_FIELD(b, ESR, BYPASS, v)
#define SET_ESR_MULTI(b, v)      SET_GLOBAL_FIELD(b, ESR, ESR_MULTI, v)


/* ESYNR0 */
#define SET_ESYNR0_AMID(b, v)    SET_GLOBAL_FIELD(b, ESYNR0, ESYNR0_AMID, v)
#define SET_ESYNR0_APID(b, v)    SET_GLOBAL_FIELD(b, ESYNR0, ESYNR0_APID, v)
#define SET_ESYNR0_ABID(b, v)    SET_GLOBAL_FIELD(b, ESYNR0, ESYNR0_ABID, v)
#define SET_ESYNR0_AVMID(b, v)   SET_GLOBAL_FIELD(b, ESYNR0, ESYNR0_AVMID, v)
#define SET_ESYNR0_ATID(b, v)    SET_GLOBAL_FIELD(b, ESYNR0, ESYNR0_ATID, v)


/* ESYNR1 */
#define SET_ESYNR1_AMEMTYPE(b, v) \
			SET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_AMEMTYPE, v)
#define SET_ESYNR1_ASHARED(b, v)  SET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_ASHARED, v)
#define SET_ESYNR1_AINNERSHARED(b, v) \
			SET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_AINNERSHARED, v)
#define SET_ESYNR1_APRIV(b, v)   SET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_APRIV, v)
#define SET_ESYNR1_APROTNS(b, v) SET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_APROTNS, v)
#define SET_ESYNR1_AINST(b, v)   SET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_AINST, v)
#define SET_ESYNR1_AWRITE(b, v)  SET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_AWRITE, v)
#define SET_ESYNR1_ABURST(b, v)  SET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_ABURST, v)
#define SET_ESYNR1_ALEN(b, v)    SET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_ALEN, v)
#define SET_ESYNR1_ASIZE(b, v)   SET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_ASIZE, v)
#define SET_ESYNR1_ALOCK(b, v)   SET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_ALOCK, v)
#define SET_ESYNR1_AOOO(b, v)    SET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_AOOO, v)
#define SET_ESYNR1_AFULL(b, v)   SET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_AFULL, v)
#define SET_ESYNR1_AC(b, v)      SET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_AC, v)
#define SET_ESYNR1_DCD(b, v)     SET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_DCD, v)


/* TESTBUSCR */
#define SET_TBE(b, v)		 SET_GLOBAL_FIELD(b, TESTBUSCR, TBE, v)
#define SET_SPDMBE(b, v)	 SET_GLOBAL_FIELD(b, TESTBUSCR, SPDMBE, v)
#define SET_WGSEL(b, v)		 SET_GLOBAL_FIELD(b, TESTBUSCR, WGSEL, v)
#define SET_TBLSEL(b, v)	 SET_GLOBAL_FIELD(b, TESTBUSCR, TBLSEL, v)
#define SET_TBHSEL(b, v)	 SET_GLOBAL_FIELD(b, TESTBUSCR, TBHSEL, v)
#define SET_SPDM0SEL(b, v)       SET_GLOBAL_FIELD(b, TESTBUSCR, SPDM0SEL, v)
#define SET_SPDM1SEL(b, v)       SET_GLOBAL_FIELD(b, TESTBUSCR, SPDM1SEL, v)
#define SET_SPDM2SEL(b, v)       SET_GLOBAL_FIELD(b, TESTBUSCR, SPDM2SEL, v)
#define SET_SPDM3SEL(b, v)       SET_GLOBAL_FIELD(b, TESTBUSCR, SPDM3SEL, v)


/* TLBIVMID */
#define SET_TLBIVMID_VMID(b, v)  SET_GLOBAL_FIELD(b, TLBIVMID, TLBIVMID_VMID, v)


/* TLBRSW */
#define SET_TLBRSW_INDEX(b, v)   SET_GLOBAL_FIELD(b, TLBRSW, TLBRSW_INDEX, v)
#define SET_TLBBFBS(b, v)	 SET_GLOBAL_FIELD(b, TLBRSW, TLBBFBS, v)


/* TLBTR0 */
#define SET_PR(b, v)		 SET_GLOBAL_FIELD(b, TLBTR0, PR, v)
#define SET_PW(b, v)		 SET_GLOBAL_FIELD(b, TLBTR0, PW, v)
#define SET_UR(b, v)		 SET_GLOBAL_FIELD(b, TLBTR0, UR, v)
#define SET_UW(b, v)		 SET_GLOBAL_FIELD(b, TLBTR0, UW, v)
#define SET_XN(b, v)		 SET_GLOBAL_FIELD(b, TLBTR0, XN, v)
#define SET_NSDESC(b, v)	 SET_GLOBAL_FIELD(b, TLBTR0, NSDESC, v)
#define SET_ISH(b, v)		 SET_GLOBAL_FIELD(b, TLBTR0, ISH, v)
#define SET_SH(b, v)		 SET_GLOBAL_FIELD(b, TLBTR0, SH, v)
#define SET_MT(b, v)		 SET_GLOBAL_FIELD(b, TLBTR0, MT, v)
#define SET_DPSIZR(b, v)	 SET_GLOBAL_FIELD(b, TLBTR0, DPSIZR, v)
#define SET_DPSIZC(b, v)	 SET_GLOBAL_FIELD(b, TLBTR0, DPSIZC, v)


/* TLBTR1 */
#define SET_TLBTR1_VMID(b, v)    SET_GLOBAL_FIELD(b, TLBTR1, TLBTR1_VMID, v)
#define SET_TLBTR1_PA(b, v)      SET_GLOBAL_FIELD(b, TLBTR1, TLBTR1_PA, v)


/* TLBTR2 */
#define SET_TLBTR2_ASID(b, v)    SET_GLOBAL_FIELD(b, TLBTR2, TLBTR2_ASID, v)
#define SET_TLBTR2_V(b, v)       SET_GLOBAL_FIELD(b, TLBTR2, TLBTR2_V, v)
#define SET_TLBTR2_NSTID(b, v)   SET_GLOBAL_FIELD(b, TLBTR2, TLBTR2_NSTID, v)
#define SET_TLBTR2_NV(b, v)      SET_GLOBAL_FIELD(b, TLBTR2, TLBTR2_NV, v)
#define SET_TLBTR2_VA(b, v)      SET_GLOBAL_FIELD(b, TLBTR2, TLBTR2_VA, v)


/* Global Field Getters */
/* CBACR_N */
#define GET_RWVMID(b, n)	 GET_GLOBAL_FIELD(b, (n<<2)|(CBACR_N), RWVMID)
#define GET_RWE(b, n)		 GET_GLOBAL_FIELD(b, (n<<2)|(CBACR_N), RWE)
#define GET_RWGE(b, n)		 GET_GLOBAL_FIELD(b, (n<<2)|(CBACR_N), RWGE)
#define GET_CBVMID(b, n)	 GET_GLOBAL_FIELD(b, (n<<2)|(CBACR_N), CBVMID)
#define GET_IRPTNDX(b, n)	 GET_GLOBAL_FIELD(b, (n<<2)|(CBACR_N), IRPTNDX)


/* M2VCBR_N */
#define GET_VMID(b, n)       GET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), VMID)
#define GET_CBNDX(b, n)      GET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), CBNDX)
#define GET_BYPASSD(b, n)    GET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), BYPASSD)
#define GET_BPRCOSH(b, n)    GET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), BPRCOSH)
#define GET_BPRCISH(b, n)    GET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), BPRCISH)
#define GET_BPRCNSH(b, n)    GET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), BPRCNSH)
#define GET_BPSHCFG(b, n)    GET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), BPSHCFG)
#define GET_NSCFG(b, n)      GET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), NSCFG)
#define GET_BPMTCFG(b, n)    GET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), BPMTCFG)
#define GET_BPMEMTYPE(b, n)  GET_GLOBAL_FIELD(b, (n<<2)|(M2VCBR_N), BPMEMTYPE)


/* CR */
#define GET_RPUE(b)		 GET_GLOBAL_FIELD(b, CR, RPUE)
#define GET_RPUERE(b)		 GET_GLOBAL_FIELD(b, CR, RPUERE)
#define GET_RPUEIE(b)		 GET_GLOBAL_FIELD(b, CR, RPUEIE)
#define GET_DCDEE(b)		 GET_GLOBAL_FIELD(b, CR, DCDEE)
#define GET_CLIENTPD(b)		 GET_GLOBAL_FIELD(b, CR, CLIENTPD)
#define GET_STALLD(b)		 GET_GLOBAL_FIELD(b, CR, STALLD)
#define GET_TLBLKCRWE(b)	 GET_GLOBAL_FIELD(b, CR, TLBLKCRWE)
#define GET_CR_TLBIALLCFG(b)	 GET_GLOBAL_FIELD(b, CR, CR_TLBIALLCFG)
#define GET_TLBIVMIDCFG(b)	 GET_GLOBAL_FIELD(b, CR, TLBIVMIDCFG)
#define GET_CR_HUME(b)		 GET_GLOBAL_FIELD(b, CR, CR_HUME)


/* ESR */
#define GET_CFG(b)		 GET_GLOBAL_FIELD(b, ESR, CFG)
#define GET_BYPASS(b)		 GET_GLOBAL_FIELD(b, ESR, BYPASS)
#define GET_ESR_MULTI(b)	 GET_GLOBAL_FIELD(b, ESR, ESR_MULTI)


/* ESYNR0 */
#define GET_ESYNR0_AMID(b)	 GET_GLOBAL_FIELD(b, ESYNR0, ESYNR0_AMID)
#define GET_ESYNR0_APID(b)	 GET_GLOBAL_FIELD(b, ESYNR0, ESYNR0_APID)
#define GET_ESYNR0_ABID(b)	 GET_GLOBAL_FIELD(b, ESYNR0, ESYNR0_ABID)
#define GET_ESYNR0_AVMID(b)	 GET_GLOBAL_FIELD(b, ESYNR0, ESYNR0_AVMID)
#define GET_ESYNR0_ATID(b)	 GET_GLOBAL_FIELD(b, ESYNR0, ESYNR0_ATID)


/* ESYNR1 */
#define GET_ESYNR1_AMEMTYPE(b)   GET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_AMEMTYPE)
#define GET_ESYNR1_ASHARED(b)    GET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_ASHARED)
#define GET_ESYNR1_AINNERSHARED(b) \
			GET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_AINNERSHARED)
#define GET_ESYNR1_APRIV(b)      GET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_APRIV)
#define GET_ESYNR1_APROTNS(b)	 GET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_APROTNS)
#define GET_ESYNR1_AINST(b)	 GET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_AINST)
#define GET_ESYNR1_AWRITE(b)	 GET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_AWRITE)
#define GET_ESYNR1_ABURST(b)	 GET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_ABURST)
#define GET_ESYNR1_ALEN(b)	 GET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_ALEN)
#define GET_ESYNR1_ASIZE(b)	 GET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_ASIZE)
#define GET_ESYNR1_ALOCK(b)	 GET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_ALOCK)
#define GET_ESYNR1_AOOO(b)	 GET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_AOOO)
#define GET_ESYNR1_AFULL(b)	 GET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_AFULL)
#define GET_ESYNR1_AC(b)	 GET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_AC)
#define GET_ESYNR1_DCD(b)	 GET_GLOBAL_FIELD(b, ESYNR1, ESYNR1_DCD)


/* IDR */
#define GET_NM2VCBMT(b)		 GET_GLOBAL_FIELD(b, IDR, NM2VCBMT)
#define GET_HTW(b)		 GET_GLOBAL_FIELD(b, IDR, HTW)
#define GET_HUM(b)		 GET_GLOBAL_FIELD(b, IDR, HUM)
#define GET_TLBSIZE(b)		 GET_GLOBAL_FIELD(b, IDR, TLBSIZE)
#define GET_NCB(b)		 GET_GLOBAL_FIELD(b, IDR, NCB)
#define GET_NIRPT(b)		 GET_GLOBAL_FIELD(b, IDR, NIRPT)


/* REV */
#define GET_MAJOR(b)		 GET_GLOBAL_FIELD(b, REV, MAJOR)
#define GET_MINOR(b)		 GET_GLOBAL_FIELD(b, REV, MINOR)


/* TESTBUSCR */
#define GET_TBE(b)		 GET_GLOBAL_FIELD(b, TESTBUSCR, TBE)
#define GET_SPDMBE(b)		 GET_GLOBAL_FIELD(b, TESTBUSCR, SPDMBE)
#define GET_WGSEL(b)		 GET_GLOBAL_FIELD(b, TESTBUSCR, WGSEL)
#define GET_TBLSEL(b)		 GET_GLOBAL_FIELD(b, TESTBUSCR, TBLSEL)
#define GET_TBHSEL(b)		 GET_GLOBAL_FIELD(b, TESTBUSCR, TBHSEL)
#define GET_SPDM0SEL(b)		 GET_GLOBAL_FIELD(b, TESTBUSCR, SPDM0SEL)
#define GET_SPDM1SEL(b)		 GET_GLOBAL_FIELD(b, TESTBUSCR, SPDM1SEL)
#define GET_SPDM2SEL(b)		 GET_GLOBAL_FIELD(b, TESTBUSCR, SPDM2SEL)
#define GET_SPDM3SEL(b)		 GET_GLOBAL_FIELD(b, TESTBUSCR, SPDM3SEL)


/* TLBIVMID */
#define GET_TLBIVMID_VMID(b)	 GET_GLOBAL_FIELD(b, TLBIVMID, TLBIVMID_VMID)


/* TLBTR0 */
#define GET_PR(b)		 GET_GLOBAL_FIELD(b, TLBTR0, PR)
#define GET_PW(b)		 GET_GLOBAL_FIELD(b, TLBTR0, PW)
#define GET_UR(b)		 GET_GLOBAL_FIELD(b, TLBTR0, UR)
#define GET_UW(b)		 GET_GLOBAL_FIELD(b, TLBTR0, UW)
#define GET_XN(b)		 GET_GLOBAL_FIELD(b, TLBTR0, XN)
#define GET_NSDESC(b)		 GET_GLOBAL_FIELD(b, TLBTR0, NSDESC)
#define GET_ISH(b)		 GET_GLOBAL_FIELD(b, TLBTR0, ISH)
#define GET_SH(b)		 GET_GLOBAL_FIELD(b, TLBTR0, SH)
#define GET_MT(b)		 GET_GLOBAL_FIELD(b, TLBTR0, MT)
#define GET_DPSIZR(b)		 GET_GLOBAL_FIELD(b, TLBTR0, DPSIZR)
#define GET_DPSIZC(b)		 GET_GLOBAL_FIELD(b, TLBTR0, DPSIZC)


/* TLBTR1 */
#define GET_TLBTR1_VMID(b)	 GET_GLOBAL_FIELD(b, TLBTR1, TLBTR1_VMID)
#define GET_TLBTR1_PA(b)	 GET_GLOBAL_FIELD(b, TLBTR1, TLBTR1_PA)


/* TLBTR2 */
#define GET_TLBTR2_ASID(b)	 GET_GLOBAL_FIELD(b, TLBTR2, TLBTR2_ASID)
#define GET_TLBTR2_V(b)		 GET_GLOBAL_FIELD(b, TLBTR2, TLBTR2_V)
#define GET_TLBTR2_NSTID(b)	 GET_GLOBAL_FIELD(b, TLBTR2, TLBTR2_NSTID)
#define GET_TLBTR2_NV(b)	 GET_GLOBAL_FIELD(b, TLBTR2, TLBTR2_NV)
#define GET_TLBTR2_VA(b)	 GET_GLOBAL_FIELD(b, TLBTR2, TLBTR2_VA)


/* Context Register setters / getters */
/* Context Register setters */
/* ACTLR */
#define SET_CFERE(b, c, v)	 SET_CONTEXT_FIELD(b, c, ACTLR, CFERE, v)
#define SET_CFEIE(b, c, v)	 SET_CONTEXT_FIELD(b, c, ACTLR, CFEIE, v)
#define SET_PTSHCFG(b, c, v)	 SET_CONTEXT_FIELD(b, c, ACTLR, PTSHCFG, v)
#define SET_RCOSH(b, c, v)	 SET_CONTEXT_FIELD(b, c, ACTLR, RCOSH, v)
#define SET_RCISH(b, c, v)	 SET_CONTEXT_FIELD(b, c, ACTLR, RCISH, v)
#define SET_RCNSH(b, c, v)	 SET_CONTEXT_FIELD(b, c, ACTLR, RCNSH, v)
#define SET_PRIVCFG(b, c, v)	 SET_CONTEXT_FIELD(b, c, ACTLR, PRIVCFG, v)
#define SET_DNA(b, c, v)	 SET_CONTEXT_FIELD(b, c, ACTLR, DNA, v)
#define SET_DNLV2PA(b, c, v)	 SET_CONTEXT_FIELD(b, c, ACTLR, DNLV2PA, v)
#define SET_TLBMCFG(b, c, v)	 SET_CONTEXT_FIELD(b, c, ACTLR, TLBMCFG, v)
#define SET_CFCFG(b, c, v)	 SET_CONTEXT_FIELD(b, c, ACTLR, CFCFG, v)
#define SET_TIPCF(b, c, v)	 SET_CONTEXT_FIELD(b, c, ACTLR, TIPCF, v)
#define SET_V2PCFG(b, c, v)	 SET_CONTEXT_FIELD(b, c, ACTLR, V2PCFG, v)
#define SET_HUME(b, c, v)	 SET_CONTEXT_FIELD(b, c, ACTLR, HUME, v)
#define SET_PTMTCFG(b, c, v)	 SET_CONTEXT_FIELD(b, c, ACTLR, PTMTCFG, v)
#define SET_PTMEMTYPE(b, c, v)	 SET_CONTEXT_FIELD(b, c, ACTLR, PTMEMTYPE, v)


/* BFBCR */
#define SET_BFBDFE(b, c, v)	 SET_CONTEXT_FIELD(b, c, BFBCR, BFBDFE, v)
#define SET_BFBSFE(b, c, v)	 SET_CONTEXT_FIELD(b, c, BFBCR, BFBSFE, v)
#define SET_SFVS(b, c, v)	 SET_CONTEXT_FIELD(b, c, BFBCR, SFVS, v)
#define SET_FLVIC(b, c, v)	 SET_CONTEXT_FIELD(b, c, BFBCR, FLVIC, v)
#define SET_SLVIC(b, c, v)	 SET_CONTEXT_FIELD(b, c, BFBCR, SLVIC, v)


/* CONTEXTIDR */
#define SET_CONTEXTIDR_ASID(b, c, v)   \
		SET_CONTEXT_FIELD(b, c, CONTEXTIDR, CONTEXTIDR_ASID, v)
#define SET_CONTEXTIDR_PROCID(b, c, v) \
		SET_CONTEXT_FIELD(b, c, CONTEXTIDR, PROCID, v)


/* FSR */
#define SET_TF(b, c, v)		 SET_CONTEXT_FIELD(b, c, FSR, TF, v)
#define SET_AFF(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSR, AFF, v)
#define SET_APF(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSR, APF, v)
#define SET_TLBMF(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSR, TLBMF, v)
#define SET_HTWDEEF(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSR, HTWDEEF, v)
#define SET_HTWSEEF(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSR, HTWSEEF, v)
#define SET_MHF(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSR, MHF, v)
#define SET_SL(b, c, v)		 SET_CONTEXT_FIELD(b, c, FSR, SL, v)
#define SET_SS(b, c, v)		 SET_CONTEXT_FIELD(b, c, FSR, SS, v)
#define SET_MULTI(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSR, MULTI, v)


/* FSYNR0 */
#define SET_AMID(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSYNR0, AMID, v)
#define SET_APID(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSYNR0, APID, v)
#define SET_ABID(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSYNR0, ABID, v)
#define SET_ATID(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSYNR0, ATID, v)


/* FSYNR1 */
#define SET_AMEMTYPE(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSYNR1, AMEMTYPE, v)
#define SET_ASHARED(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSYNR1, ASHARED, v)
#define SET_AINNERSHARED(b, c, v)  \
				SET_CONTEXT_FIELD(b, c, FSYNR1, AINNERSHARED, v)
#define SET_APRIV(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSYNR1, APRIV, v)
#define SET_APROTNS(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSYNR1, APROTNS, v)
#define SET_AINST(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSYNR1, AINST, v)
#define SET_AWRITE(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSYNR1, AWRITE, v)
#define SET_ABURST(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSYNR1, ABURST, v)
#define SET_ALEN(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSYNR1, ALEN, v)
#define SET_FSYNR1_ASIZE(b, c, v) \
				SET_CONTEXT_FIELD(b, c, FSYNR1, FSYNR1_ASIZE, v)
#define SET_ALOCK(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSYNR1, ALOCK, v)
#define SET_AFULL(b, c, v)	 SET_CONTEXT_FIELD(b, c, FSYNR1, AFULL, v)


/* NMRR */
#define SET_ICPC0(b, c, v)	 SET_CONTEXT_FIELD(b, c, NMRR, ICPC0, v)
#define SET_ICPC1(b, c, v)	 SET_CONTEXT_FIELD(b, c, NMRR, ICPC1, v)
#define SET_ICPC2(b, c, v)	 SET_CONTEXT_FIELD(b, c, NMRR, ICPC2, v)
#define SET_ICPC3(b, c, v)	 SET_CONTEXT_FIELD(b, c, NMRR, ICPC3, v)
#define SET_ICPC4(b, c, v)	 SET_CONTEXT_FIELD(b, c, NMRR, ICPC4, v)
#define SET_ICPC5(b, c, v)	 SET_CONTEXT_FIELD(b, c, NMRR, ICPC5, v)
#define SET_ICPC6(b, c, v)	 SET_CONTEXT_FIELD(b, c, NMRR, ICPC6, v)
#define SET_ICPC7(b, c, v)	 SET_CONTEXT_FIELD(b, c, NMRR, ICPC7, v)
#define SET_OCPC0(b, c, v)	 SET_CONTEXT_FIELD(b, c, NMRR, OCPC0, v)
#define SET_OCPC1(b, c, v)	 SET_CONTEXT_FIELD(b, c, NMRR, OCPC1, v)
#define SET_OCPC2(b, c, v)	 SET_CONTEXT_FIELD(b, c, NMRR, OCPC2, v)
#define SET_OCPC3(b, c, v)	 SET_CONTEXT_FIELD(b, c, NMRR, OCPC3, v)
#define SET_OCPC4(b, c, v)	 SET_CONTEXT_FIELD(b, c, NMRR, OCPC4, v)
#define SET_OCPC5(b, c, v)	 SET_CONTEXT_FIELD(b, c, NMRR, OCPC5, v)
#define SET_OCPC6(b, c, v)	 SET_CONTEXT_FIELD(b, c, NMRR, OCPC6, v)
#define SET_OCPC7(b, c, v)	 SET_CONTEXT_FIELD(b, c, NMRR, OCPC7, v)


/* PAR */
#define SET_FAULT(b, c, v)	 SET_CONTEXT_FIELD(b, c, PAR, FAULT, v)

#define SET_FAULT_TF(b, c, v)	 SET_CONTEXT_FIELD(b, c, PAR, FAULT_TF, v)
#define SET_FAULT_AFF(b, c, v)	 SET_CONTEXT_FIELD(b, c, PAR, FAULT_AFF, v)
#define SET_FAULT_APF(b, c, v)	 SET_CONTEXT_FIELD(b, c, PAR, FAULT_APF, v)
#define SET_FAULT_TLBMF(b, c, v) SET_CONTEXT_FIELD(b, c, PAR, FAULT_TLBMF, v)
#define SET_FAULT_HTWDEEF(b, c, v) \
				SET_CONTEXT_FIELD(b, c, PAR, FAULT_HTWDEEF, v)
#define SET_FAULT_HTWSEEF(b, c, v) \
				SET_CONTEXT_FIELD(b, c, PAR, FAULT_HTWSEEF, v)
#define SET_FAULT_MHF(b, c, v)	 SET_CONTEXT_FIELD(b, c, PAR, FAULT_MHF, v)
#define SET_FAULT_SL(b, c, v)	 SET_CONTEXT_FIELD(b, c, PAR, FAULT_SL, v)
#define SET_FAULT_SS(b, c, v)	 SET_CONTEXT_FIELD(b, c, PAR, FAULT_SS, v)

#define SET_NOFAULT_SS(b, c, v)	 SET_CONTEXT_FIELD(b, c, PAR, NOFAULT_SS, v)
#define SET_NOFAULT_MT(b, c, v)	 SET_CONTEXT_FIELD(b, c, PAR, NOFAULT_MT, v)
#define SET_NOFAULT_SH(b, c, v)	 SET_CONTEXT_FIELD(b, c, PAR, NOFAULT_SH, v)
#define SET_NOFAULT_NS(b, c, v)	 SET_CONTEXT_FIELD(b, c, PAR, NOFAULT_NS, v)
#define SET_NOFAULT_NOS(b, c, v) SET_CONTEXT_FIELD(b, c, PAR, NOFAULT_NOS, v)
#define SET_NPFAULT_PA(b, c, v)	 SET_CONTEXT_FIELD(b, c, PAR, NPFAULT_PA, v)


/* PRRR */
#define SET_MTC0(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, MTC0, v)
#define SET_MTC1(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, MTC1, v)
#define SET_MTC2(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, MTC2, v)
#define SET_MTC3(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, MTC3, v)
#define SET_MTC4(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, MTC4, v)
#define SET_MTC5(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, MTC5, v)
#define SET_MTC6(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, MTC6, v)
#define SET_MTC7(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, MTC7, v)
#define SET_SHDSH0(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, SHDSH0, v)
#define SET_SHDSH1(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, SHDSH1, v)
#define SET_SHNMSH0(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, SHNMSH0, v)
#define SET_SHNMSH1(b, c, v)     SET_CONTEXT_FIELD(b, c, PRRR, SHNMSH1, v)
#define SET_NOS0(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, NOS0, v)
#define SET_NOS1(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, NOS1, v)
#define SET_NOS2(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, NOS2, v)
#define SET_NOS3(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, NOS3, v)
#define SET_NOS4(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, NOS4, v)
#define SET_NOS5(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, NOS5, v)
#define SET_NOS6(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, NOS6, v)
#define SET_NOS7(b, c, v)	 SET_CONTEXT_FIELD(b, c, PRRR, NOS7, v)


/* RESUME */
#define SET_TNR(b, c, v)	 SET_CONTEXT_FIELD(b, c, RESUME, TNR, v)


/* SCTLR */
#define SET_M(b, c, v)		 SET_CONTEXT_FIELD(b, c, SCTLR, M, v)
#define SET_TRE(b, c, v)	 SET_CONTEXT_FIELD(b, c, SCTLR, TRE, v)
#define SET_AFE(b, c, v)	 SET_CONTEXT_FIELD(b, c, SCTLR, AFE, v)
#define SET_HAF(b, c, v)	 SET_CONTEXT_FIELD(b, c, SCTLR, HAF, v)
#define SET_BE(b, c, v)		 SET_CONTEXT_FIELD(b, c, SCTLR, BE, v)
#define SET_AFFD(b, c, v)	 SET_CONTEXT_FIELD(b, c, SCTLR, AFFD, v)


/* TLBLKCR */
#define SET_LKE(b, c, v)	   SET_CONTEXT_FIELD(b, c, TLBLKCR, LKE, v)
#define SET_TLBLKCR_TLBIALLCFG(b, c, v) \
			SET_CONTEXT_FIELD(b, c, TLBLKCR, TLBLCKR_TLBIALLCFG, v)
#define SET_TLBIASIDCFG(b, c, v) \
			SET_CONTEXT_FIELD(b, c, TLBLKCR, TLBIASIDCFG, v)
#define SET_TLBIVAACFG(b, c, v)	SET_CONTEXT_FIELD(b, c, TLBLKCR, TLBIVAACFG, v)
#define SET_FLOOR(b, c, v)	SET_CONTEXT_FIELD(b, c, TLBLKCR, FLOOR, v)
#define SET_VICTIM(b, c, v)	SET_CONTEXT_FIELD(b, c, TLBLKCR, VICTIM, v)


/* TTBCR */
#define SET_N(b, c, v)	         SET_CONTEXT_FIELD(b, c, TTBCR, N, v)
#define SET_PD0(b, c, v)	 SET_CONTEXT_FIELD(b, c, TTBCR, PD0, v)
#define SET_PD1(b, c, v)	 SET_CONTEXT_FIELD(b, c, TTBCR, PD1, v)


/* TTBR0 */
#define SET_TTBR0_IRGNH(b, c, v) SET_CONTEXT_FIELD(b, c, TTBR0, TTBR0_IRGNH, v)
#define SET_TTBR0_SH(b, c, v)	 SET_CONTEXT_FIELD(b, c, TTBR0, TTBR0_SH, v)
#define SET_TTBR0_ORGN(b, c, v)	 SET_CONTEXT_FIELD(b, c, TTBR0, TTBR0_ORGN, v)
#define SET_TTBR0_NOS(b, c, v)	 SET_CONTEXT_FIELD(b, c, TTBR0, TTBR0_NOS, v)
#define SET_TTBR0_IRGNL(b, c, v) SET_CONTEXT_FIELD(b, c, TTBR0, TTBR0_IRGNL, v)
#define SET_TTBR0_PA(b, c, v)	 SET_CONTEXT_FIELD(b, c, TTBR0, TTBR0_PA, v)


/* TTBR1 */
#define SET_TTBR1_IRGNH(b, c, v) SET_CONTEXT_FIELD(b, c, TTBR1, TTBR1_IRGNH, v)
#define SET_TTBR1_SH(b, c, v)	 SET_CONTEXT_FIELD(b, c, TTBR1, TTBR1_SH, v)
#define SET_TTBR1_ORGN(b, c, v)	 SET_CONTEXT_FIELD(b, c, TTBR1, TTBR1_ORGN, v)
#define SET_TTBR1_NOS(b, c, v)	 SET_CONTEXT_FIELD(b, c, TTBR1, TTBR1_NOS, v)
#define SET_TTBR1_IRGNL(b, c, v) SET_CONTEXT_FIELD(b, c, TTBR1, TTBR1_IRGNL, v)
#define SET_TTBR1_PA(b, c, v)	 SET_CONTEXT_FIELD(b, c, TTBR1, TTBR1_PA, v)


/* V2PSR */
#define SET_HIT(b, c, v)	 SET_CONTEXT_FIELD(b, c, V2PSR, HIT, v)
#define SET_INDEX(b, c, v)	 SET_CONTEXT_FIELD(b, c, V2PSR, INDEX, v)


/* Context Register getters */
/* ACTLR */
#define GET_CFERE(b, c)	        GET_CONTEXT_FIELD(b, c, ACTLR, CFERE)
#define GET_CFEIE(b, c)	        GET_CONTEXT_FIELD(b, c, ACTLR, CFEIE)
#define GET_PTSHCFG(b, c)       GET_CONTEXT_FIELD(b, c, ACTLR, PTSHCFG)
#define GET_RCOSH(b, c)	        GET_CONTEXT_FIELD(b, c, ACTLR, RCOSH)
#define GET_RCISH(b, c)	        GET_CONTEXT_FIELD(b, c, ACTLR, RCISH)
#define GET_RCNSH(b, c)	        GET_CONTEXT_FIELD(b, c, ACTLR, RCNSH)
#define GET_PRIVCFG(b, c)       GET_CONTEXT_FIELD(b, c, ACTLR, PRIVCFG)
#define GET_DNA(b, c)	        GET_CONTEXT_FIELD(b, c, ACTLR, DNA)
#define GET_DNLV2PA(b, c)       GET_CONTEXT_FIELD(b, c, ACTLR, DNLV2PA)
#define GET_TLBMCFG(b, c)       GET_CONTEXT_FIELD(b, c, ACTLR, TLBMCFG)
#define GET_CFCFG(b, c)	        GET_CONTEXT_FIELD(b, c, ACTLR, CFCFG)
#define GET_TIPCF(b, c)	        GET_CONTEXT_FIELD(b, c, ACTLR, TIPCF)
#define GET_V2PCFG(b, c)        GET_CONTEXT_FIELD(b, c, ACTLR, V2PCFG)
#define GET_HUME(b, c)	        GET_CONTEXT_FIELD(b, c, ACTLR, HUME)
#define GET_PTMTCFG(b, c)       GET_CONTEXT_FIELD(b, c, ACTLR, PTMTCFG)
#define GET_PTMEMTYPE(b, c)     GET_CONTEXT_FIELD(b, c, ACTLR, PTMEMTYPE)

/* BFBCR */
#define GET_BFBDFE(b, c)	GET_CONTEXT_FIELD(b, c, BFBCR, BFBDFE)
#define GET_BFBSFE(b, c)	GET_CONTEXT_FIELD(b, c, BFBCR, BFBSFE)
#define GET_SFVS(b, c)		GET_CONTEXT_FIELD(b, c, BFBCR, SFVS)
#define GET_FLVIC(b, c)		GET_CONTEXT_FIELD(b, c, BFBCR, FLVIC)
#define GET_SLVIC(b, c)		GET_CONTEXT_FIELD(b, c, BFBCR, SLVIC)


/* CONTEXTIDR */
#define GET_CONTEXTIDR_ASID(b, c) \
			GET_CONTEXT_FIELD(b, c, CONTEXTIDR, CONTEXTIDR_ASID)
#define GET_CONTEXTIDR_PROCID(b, c) GET_CONTEXT_FIELD(b, c, CONTEXTIDR, PROCID)


/* FSR */
#define GET_TF(b, c)		GET_CONTEXT_FIELD(b, c, FSR, TF)
#define GET_AFF(b, c)		GET_CONTEXT_FIELD(b, c, FSR, AFF)
#define GET_APF(b, c)		GET_CONTEXT_FIELD(b, c, FSR, APF)
#define GET_TLBMF(b, c)		GET_CONTEXT_FIELD(b, c, FSR, TLBMF)
#define GET_HTWDEEF(b, c)	GET_CONTEXT_FIELD(b, c, FSR, HTWDEEF)
#define GET_HTWSEEF(b, c)	GET_CONTEXT_FIELD(b, c, FSR, HTWSEEF)
#define GET_MHF(b, c)		GET_CONTEXT_FIELD(b, c, FSR, MHF)
#define GET_SL(b, c)		GET_CONTEXT_FIELD(b, c, FSR, SL)
#define GET_SS(b, c)		GET_CONTEXT_FIELD(b, c, FSR, SS)
#define GET_MULTI(b, c)		GET_CONTEXT_FIELD(b, c, FSR, MULTI)


/* FSYNR0 */
#define GET_AMID(b, c)		GET_CONTEXT_FIELD(b, c, FSYNR0, AMID)
#define GET_APID(b, c)		GET_CONTEXT_FIELD(b, c, FSYNR0, APID)
#define GET_ABID(b, c)		GET_CONTEXT_FIELD(b, c, FSYNR0, ABID)
#define GET_ATID(b, c)		GET_CONTEXT_FIELD(b, c, FSYNR0, ATID)


/* FSYNR1 */
#define GET_AMEMTYPE(b, c)	GET_CONTEXT_FIELD(b, c, FSYNR1, AMEMTYPE)
#define GET_ASHARED(b, c)	GET_CONTEXT_FIELD(b, c, FSYNR1, ASHARED)
#define GET_AINNERSHARED(b, c)  GET_CONTEXT_FIELD(b, c, FSYNR1, AINNERSHARED)
#define GET_APRIV(b, c)		GET_CONTEXT_FIELD(b, c, FSYNR1, APRIV)
#define GET_APROTNS(b, c)	GET_CONTEXT_FIELD(b, c, FSYNR1, APROTNS)
#define GET_AINST(b, c)		GET_CONTEXT_FIELD(b, c, FSYNR1, AINST)
#define GET_AWRITE(b, c)	GET_CONTEXT_FIELD(b, c, FSYNR1, AWRITE)
#define GET_ABURST(b, c)	GET_CONTEXT_FIELD(b, c, FSYNR1, ABURST)
#define GET_ALEN(b, c)		GET_CONTEXT_FIELD(b, c, FSYNR1, ALEN)
#define GET_FSYNR1_ASIZE(b, c)	GET_CONTEXT_FIELD(b, c, FSYNR1, FSYNR1_ASIZE)
#define GET_ALOCK(b, c)		GET_CONTEXT_FIELD(b, c, FSYNR1, ALOCK)
#define GET_AFULL(b, c)		GET_CONTEXT_FIELD(b, c, FSYNR1, AFULL)


/* NMRR */
#define GET_ICPC0(b, c)		GET_CONTEXT_FIELD(b, c, NMRR, ICPC0)
#define GET_ICPC1(b, c)		GET_CONTEXT_FIELD(b, c, NMRR, ICPC1)
#define GET_ICPC2(b, c)		GET_CONTEXT_FIELD(b, c, NMRR, ICPC2)
#define GET_ICPC3(b, c)		GET_CONTEXT_FIELD(b, c, NMRR, ICPC3)
#define GET_ICPC4(b, c)		GET_CONTEXT_FIELD(b, c, NMRR, ICPC4)
#define GET_ICPC5(b, c)		GET_CONTEXT_FIELD(b, c, NMRR, ICPC5)
#define GET_ICPC6(b, c)		GET_CONTEXT_FIELD(b, c, NMRR, ICPC6)
#define GET_ICPC7(b, c)		GET_CONTEXT_FIELD(b, c, NMRR, ICPC7)
#define GET_OCPC0(b, c)		GET_CONTEXT_FIELD(b, c, NMRR, OCPC0)
#define GET_OCPC1(b, c)		GET_CONTEXT_FIELD(b, c, NMRR, OCPC1)
#define GET_OCPC2(b, c)		GET_CONTEXT_FIELD(b, c, NMRR, OCPC2)
#define GET_OCPC3(b, c)		GET_CONTEXT_FIELD(b, c, NMRR, OCPC3)
#define GET_OCPC4(b, c)		GET_CONTEXT_FIELD(b, c, NMRR, OCPC4)
#define GET_OCPC5(b, c)		GET_CONTEXT_FIELD(b, c, NMRR, OCPC5)
#define GET_OCPC6(b, c)		GET_CONTEXT_FIELD(b, c, NMRR, OCPC6)
#define GET_OCPC7(b, c)		GET_CONTEXT_FIELD(b, c, NMRR, OCPC7)
#define NMRR_ICP(nmrr, n)	(((nmrr) & (3 << ((n) * 2))) >> ((n) * 2))
#define NMRR_OCP(nmrr, n)	(((nmrr) & (3 << ((n) * 2 + 16))) >> \
								((n) * 2 + 16))

/* PAR */
#define GET_FAULT(b, c)		GET_CONTEXT_FIELD(b, c, PAR, FAULT)

#define GET_FAULT_TF(b, c)	GET_CONTEXT_FIELD(b, c, PAR, FAULT_TF)
#define GET_FAULT_AFF(b, c)	GET_CONTEXT_FIELD(b, c, PAR, FAULT_AFF)
#define GET_FAULT_APF(b, c)	GET_CONTEXT_FIELD(b, c, PAR, FAULT_APF)
#define GET_FAULT_TLBMF(b, c)   GET_CONTEXT_FIELD(b, c, PAR, FAULT_TLBMF)
#define GET_FAULT_HTWDEEF(b, c) GET_CONTEXT_FIELD(b, c, PAR, FAULT_HTWDEEF)
#define GET_FAULT_HTWSEEF(b, c) GET_CONTEXT_FIELD(b, c, PAR, FAULT_HTWSEEF)
#define GET_FAULT_MHF(b, c)	GET_CONTEXT_FIELD(b, c, PAR, FAULT_MHF)
#define GET_FAULT_SL(b, c)	GET_CONTEXT_FIELD(b, c, PAR, FAULT_SL)
#define GET_FAULT_SS(b, c)	GET_CONTEXT_FIELD(b, c, PAR, FAULT_SS)

#define GET_NOFAULT_SS(b, c)	GET_CONTEXT_FIELD(b, c, PAR, PAR_NOFAULT_SS)
#define GET_NOFAULT_MT(b, c)	GET_CONTEXT_FIELD(b, c, PAR, PAR_NOFAULT_MT)
#define GET_NOFAULT_SH(b, c)	GET_CONTEXT_FIELD(b, c, PAR, PAR_NOFAULT_SH)
#define GET_NOFAULT_NS(b, c)	GET_CONTEXT_FIELD(b, c, PAR, PAR_NOFAULT_NS)
#define GET_NOFAULT_NOS(b, c)   GET_CONTEXT_FIELD(b, c, PAR, PAR_NOFAULT_NOS)
#define GET_NPFAULT_PA(b, c)	GET_CONTEXT_FIELD(b, c, PAR, PAR_NPFAULT_PA)


/* PRRR */
#define GET_MTC0(b, c)		GET_CONTEXT_FIELD(b, c, PRRR, MTC0)
#define GET_MTC1(b, c)		GET_CONTEXT_FIELD(b, c, PRRR, MTC1)
#define GET_MTC2(b, c)		GET_CONTEXT_FIELD(b, c, PRRR, MTC2)
#define GET_MTC3(b, c)		GET_CONTEXT_FIELD(b, c, PRRR, MTC3)
#define GET_MTC4(b, c)		GET_CONTEXT_FIELD(b, c, PRRR, MTC4)
#define GET_MTC5(b, c)		GET_CONTEXT_FIELD(b, c, PRRR, MTC5)
#define GET_MTC6(b, c)		GET_CONTEXT_FIELD(b, c, PRRR, MTC6)
#define GET_MTC7(b, c)		GET_CONTEXT_FIELD(b, c, PRRR, MTC7)
#define GET_SHDSH0(b, c)	GET_CONTEXT_FIELD(b, c, PRRR, SHDSH0)
#define GET_SHDSH1(b, c)	GET_CONTEXT_FIELD(b, c, PRRR, SHDSH1)
#define GET_SHNMSH0(b, c)	GET_CONTEXT_FIELD(b, c, PRRR, SHNMSH0)
#define GET_SHNMSH1(b, c)	GET_CONTEXT_FIELD(b, c, PRRR, SHNMSH1)
#define GET_NOS0(b, c)		GET_CONTEXT_FIELD(b, c, PRRR, NOS0)
#define GET_NOS1(b, c)		GET_CONTEXT_FIELD(b, c, PRRR, NOS1)
#define GET_NOS2(b, c)		GET_CONTEXT_FIELD(b, c, PRRR, NOS2)
#define GET_NOS3(b, c)		GET_CONTEXT_FIELD(b, c, PRRR, NOS3)
#define GET_NOS4(b, c)		GET_CONTEXT_FIELD(b, c, PRRR, NOS4)
#define GET_NOS5(b, c)		GET_CONTEXT_FIELD(b, c, PRRR, NOS5)
#define GET_NOS6(b, c)		GET_CONTEXT_FIELD(b, c, PRRR, NOS6)
#define GET_NOS7(b, c)		GET_CONTEXT_FIELD(b, c, PRRR, NOS7)
#define PRRR_NOS(prrr, n)	 ((prrr) & (1 << ((n) + 24)) ? 1 : 0)
#define PRRR_MT(prrr, n)	 ((((prrr) & (3 << ((n) * 2))) >> ((n) * 2)))


/* RESUME */
#define GET_TNR(b, c)		GET_CONTEXT_FIELD(b, c, RESUME, TNR)


/* SCTLR */
#define GET_M(b, c)		GET_CONTEXT_FIELD(b, c, SCTLR, M)
#define GET_TRE(b, c)		GET_CONTEXT_FIELD(b, c, SCTLR, TRE)
#define GET_AFE(b, c)		GET_CONTEXT_FIELD(b, c, SCTLR, AFE)
#define GET_HAF(b, c)		GET_CONTEXT_FIELD(b, c, SCTLR, HAF)
#define GET_BE(b, c)		GET_CONTEXT_FIELD(b, c, SCTLR, BE)
#define GET_AFFD(b, c)		GET_CONTEXT_FIELD(b, c, SCTLR, AFFD)


/* TLBLKCR */
#define GET_LKE(b, c)		GET_CONTEXT_FIELD(b, c, TLBLKCR, LKE)
#define GET_TLBLCKR_TLBIALLCFG(b, c) \
			GET_CONTEXT_FIELD(b, c, TLBLKCR, TLBLCKR_TLBIALLCFG)
#define GET_TLBIASIDCFG(b, c)   GET_CONTEXT_FIELD(b, c, TLBLKCR, TLBIASIDCFG)
#define GET_TLBIVAACFG(b, c)	GET_CONTEXT_FIELD(b, c, TLBLKCR, TLBIVAACFG)
#define GET_FLOOR(b, c)		GET_CONTEXT_FIELD(b, c, TLBLKCR, FLOOR)
#define GET_VICTIM(b, c)	GET_CONTEXT_FIELD(b, c, TLBLKCR, VICTIM)


/* TTBCR */
#define GET_N(b, c)		GET_CONTEXT_FIELD(b, c, TTBCR, N)
#define GET_PD0(b, c)		GET_CONTEXT_FIELD(b, c, TTBCR, PD0)
#define GET_PD1(b, c)		GET_CONTEXT_FIELD(b, c, TTBCR, PD1)


/* TTBR0 */
#define GET_TTBR0_IRGNH(b, c)	GET_CONTEXT_FIELD(b, c, TTBR0, TTBR0_IRGNH)
#define GET_TTBR0_SH(b, c)	GET_CONTEXT_FIELD(b, c, TTBR0, TTBR0_SH)
#define GET_TTBR0_ORGN(b, c)	GET_CONTEXT_FIELD(b, c, TTBR0, TTBR0_ORGN)
#define GET_TTBR0_NOS(b, c)	GET_CONTEXT_FIELD(b, c, TTBR0, TTBR0_NOS)
#define GET_TTBR0_IRGNL(b, c)	GET_CONTEXT_FIELD(b, c, TTBR0, TTBR0_IRGNL)
#define GET_TTBR0_PA(b, c)	GET_CONTEXT_FIELD(b, c, TTBR0, TTBR0_PA)


/* TTBR1 */
#define GET_TTBR1_IRGNH(b, c)	GET_CONTEXT_FIELD(b, c, TTBR1, TTBR1_IRGNH)
#define GET_TTBR1_SH(b, c)	GET_CONTEXT_FIELD(b, c, TTBR1, TTBR1_SH)
#define GET_TTBR1_ORGN(b, c)	GET_CONTEXT_FIELD(b, c, TTBR1, TTBR1_ORGN)
#define GET_TTBR1_NOS(b, c)	GET_CONTEXT_FIELD(b, c, TTBR1, TTBR1_NOS)
#define GET_TTBR1_IRGNL(b, c)	GET_CONTEXT_FIELD(b, c, TTBR1, TTBR1_IRGNL)
#define GET_TTBR1_PA(b, c)	GET_CONTEXT_FIELD(b, c, TTBR1, TTBR1_PA)


/* V2PSR */
#define GET_HIT(b, c)		GET_CONTEXT_FIELD(b, c, V2PSR, HIT)
#define GET_INDEX(b, c)		GET_CONTEXT_FIELD(b, c, V2PSR, INDEX)


/* Global Registers */
#define M2VCBR_N	(0xFF000)
#define CBACR_N		(0xFF800)
#define TLBRSW		(0xFFE00)
#define TLBTR0		(0xFFE80)
#define TLBTR1		(0xFFE84)
#define TLBTR2		(0xFFE88)
#define TESTBUSCR	(0xFFE8C)
#define GLOBAL_TLBIALL	(0xFFF00)
#define TLBIVMID	(0xFFF04)
#define CR		(0xFFF80)
#define EAR		(0xFFF84)
#define ESR		(0xFFF88)
#define ESRRESTORE	(0xFFF8C)
#define ESYNR0		(0xFFF90)
#define ESYNR1		(0xFFF94)
#define REV		(0xFFFF4)
#define IDR		(0xFFFF8)
#define RPU_ACR		(0xFFFFC)


/* Context Bank Registers */
#define SCTLR		(0x000)
#define ACTLR		(0x004)
#define CONTEXTIDR	(0x008)
#define TTBR0		(0x010)
#define TTBR1		(0x014)
#define TTBCR		(0x018)
#define PAR		(0x01C)
#define FSR		(0x020)
#define FSRRESTORE	(0x024)
#define FAR		(0x028)
#define FSYNR0		(0x02C)
#define FSYNR1		(0x030)
#define PRRR		(0x034)
#define NMRR		(0x038)
#define TLBLCKR		(0x03C)
#define V2PSR		(0x040)
#define TLBFLPTER	(0x044)
#define TLBSLPTER	(0x048)
#define BFBCR		(0x04C)
#define CTX_TLBIALL	(0x800)
#define TLBIASID	(0x804)
#define TLBIVA		(0x808)
#define TLBIVAA		(0x80C)
#define V2PPR		(0x810)
#define V2PPW		(0x814)
#define V2PUR		(0x818)
#define V2PUW		(0x81C)
#define RESUME		(0x820)


/* Global Register Fields */
/* CBACRn */
#define RWVMID        (RWVMID_MASK       << RWVMID_SHIFT)
#define RWE           (RWE_MASK          << RWE_SHIFT)
#define RWGE          (RWGE_MASK         << RWGE_SHIFT)
#define CBVMID        (CBVMID_MASK       << CBVMID_SHIFT)
#define IRPTNDX       (IRPTNDX_MASK      << IRPTNDX_SHIFT)


/* CR */
#define RPUE          (RPUE_MASK          << RPUE_SHIFT)
#define RPUERE        (RPUERE_MASK        << RPUERE_SHIFT)
#define RPUEIE        (RPUEIE_MASK        << RPUEIE_SHIFT)
#define DCDEE         (DCDEE_MASK         << DCDEE_SHIFT)
#define CLIENTPD      (CLIENTPD_MASK      << CLIENTPD_SHIFT)
#define STALLD        (STALLD_MASK        << STALLD_SHIFT)
#define TLBLKCRWE     (TLBLKCRWE_MASK     << TLBLKCRWE_SHIFT)
#define CR_TLBIALLCFG (CR_TLBIALLCFG_MASK << CR_TLBIALLCFG_SHIFT)
#define TLBIVMIDCFG   (TLBIVMIDCFG_MASK   << TLBIVMIDCFG_SHIFT)
#define CR_HUME       (CR_HUME_MASK       << CR_HUME_SHIFT)


/* ESR */
#define CFG           (CFG_MASK          << CFG_SHIFT)
#define BYPASS        (BYPASS_MASK       << BYPASS_SHIFT)
#define ESR_MULTI     (ESR_MULTI_MASK    << ESR_MULTI_SHIFT)


/* ESYNR0 */
#define ESYNR0_AMID   (ESYNR0_AMID_MASK  << ESYNR0_AMID_SHIFT)
#define ESYNR0_APID   (ESYNR0_APID_MASK  << ESYNR0_APID_SHIFT)
#define ESYNR0_ABID   (ESYNR0_ABID_MASK  << ESYNR0_ABID_SHIFT)
#define ESYNR0_AVMID  (ESYNR0_AVMID_MASK << ESYNR0_AVMID_SHIFT)
#define ESYNR0_ATID   (ESYNR0_ATID_MASK  << ESYNR0_ATID_SHIFT)


/* ESYNR1 */
#define ESYNR1_AMEMTYPE      (ESYNR1_AMEMTYPE_MASK    << ESYNR1_AMEMTYPE_SHIFT)
#define ESYNR1_ASHARED       (ESYNR1_ASHARED_MASK     << ESYNR1_ASHARED_SHIFT)
#define ESYNR1_AINNERSHARED  (ESYNR1_AINNERSHARED_MASK<< \
						ESYNR1_AINNERSHARED_SHIFT)
#define ESYNR1_APRIV         (ESYNR1_APRIV_MASK       << ESYNR1_APRIV_SHIFT)
#define ESYNR1_APROTNS       (ESYNR1_APROTNS_MASK     << ESYNR1_APROTNS_SHIFT)
#define ESYNR1_AINST         (ESYNR1_AINST_MASK       << ESYNR1_AINST_SHIFT)
#define ESYNR1_AWRITE        (ESYNR1_AWRITE_MASK      << ESYNR1_AWRITE_SHIFT)
#define ESYNR1_ABURST        (ESYNR1_ABURST_MASK      << ESYNR1_ABURST_SHIFT)
#define ESYNR1_ALEN          (ESYNR1_ALEN_MASK        << ESYNR1_ALEN_SHIFT)
#define ESYNR1_ASIZE         (ESYNR1_ASIZE_MASK       << ESYNR1_ASIZE_SHIFT)
#define ESYNR1_ALOCK         (ESYNR1_ALOCK_MASK       << ESYNR1_ALOCK_SHIFT)
#define ESYNR1_AOOO          (ESYNR1_AOOO_MASK        << ESYNR1_AOOO_SHIFT)
#define ESYNR1_AFULL         (ESYNR1_AFULL_MASK       << ESYNR1_AFULL_SHIFT)
#define ESYNR1_AC            (ESYNR1_AC_MASK          << ESYNR1_AC_SHIFT)
#define ESYNR1_DCD           (ESYNR1_DCD_MASK         << ESYNR1_DCD_SHIFT)


/* IDR */
#define NM2VCBMT      (NM2VCBMT_MASK     << NM2VCBMT_SHIFT)
#define HTW           (HTW_MASK          << HTW_SHIFT)
#define HUM           (HUM_MASK          << HUM_SHIFT)
#define TLBSIZE       (TLBSIZE_MASK      << TLBSIZE_SHIFT)
#define NCB           (NCB_MASK          << NCB_SHIFT)
#define NIRPT         (NIRPT_MASK        << NIRPT_SHIFT)


/* M2VCBRn */
#define VMID          (VMID_MASK         << VMID_SHIFT)
#define CBNDX         (CBNDX_MASK        << CBNDX_SHIFT)
#define BYPASSD       (BYPASSD_MASK      << BYPASSD_SHIFT)
#define BPRCOSH       (BPRCOSH_MASK      << BPRCOSH_SHIFT)
#define BPRCISH       (BPRCISH_MASK      << BPRCISH_SHIFT)
#define BPRCNSH       (BPRCNSH_MASK      << BPRCNSH_SHIFT)
#define BPSHCFG       (BPSHCFG_MASK      << BPSHCFG_SHIFT)
#define NSCFG         (NSCFG_MASK        << NSCFG_SHIFT)
#define BPMTCFG       (BPMTCFG_MASK      << BPMTCFG_SHIFT)
#define BPMEMTYPE     (BPMEMTYPE_MASK    << BPMEMTYPE_SHIFT)


/* REV */
#define IDR_MINOR     (MINOR_MASK        << MINOR_SHIFT)
#define IDR_MAJOR     (MAJOR_MASK        << MAJOR_SHIFT)


/* TESTBUSCR */
#define TBE           (TBE_MASK          << TBE_SHIFT)
#define SPDMBE        (SPDMBE_MASK       << SPDMBE_SHIFT)
#define WGSEL         (WGSEL_MASK        << WGSEL_SHIFT)
#define TBLSEL        (TBLSEL_MASK       << TBLSEL_SHIFT)
#define TBHSEL        (TBHSEL_MASK       << TBHSEL_SHIFT)
#define SPDM0SEL      (SPDM0SEL_MASK     << SPDM0SEL_SHIFT)
#define SPDM1SEL      (SPDM1SEL_MASK     << SPDM1SEL_SHIFT)
#define SPDM2SEL      (SPDM2SEL_MASK     << SPDM2SEL_SHIFT)
#define SPDM3SEL      (SPDM3SEL_MASK     << SPDM3SEL_SHIFT)


/* TLBIVMID */
#define TLBIVMID_VMID (TLBIVMID_VMID_MASK << TLBIVMID_VMID_SHIFT)


/* TLBRSW */
#define TLBRSW_INDEX  (TLBRSW_INDEX_MASK << TLBRSW_INDEX_SHIFT)
#define TLBBFBS       (TLBBFBS_MASK      << TLBBFBS_SHIFT)


/* TLBTR0 */
#define PR            (PR_MASK           << PR_SHIFT)
#define PW            (PW_MASK           << PW_SHIFT)
#define UR            (UR_MASK           << UR_SHIFT)
#define UW            (UW_MASK           << UW_SHIFT)
#define XN            (XN_MASK           << XN_SHIFT)
#define NSDESC        (NSDESC_MASK       << NSDESC_SHIFT)
#define ISH           (ISH_MASK          << ISH_SHIFT)
#define SH            (SH_MASK           << SH_SHIFT)
#define MT            (MT_MASK           << MT_SHIFT)
#define DPSIZR        (DPSIZR_MASK       << DPSIZR_SHIFT)
#define DPSIZC        (DPSIZC_MASK       << DPSIZC_SHIFT)


/* TLBTR1 */
#define TLBTR1_VMID   (TLBTR1_VMID_MASK  << TLBTR1_VMID_SHIFT)
#define TLBTR1_PA     (TLBTR1_PA_MASK    << TLBTR1_PA_SHIFT)


/* TLBTR2 */
#define TLBTR2_ASID   (TLBTR2_ASID_MASK  << TLBTR2_ASID_SHIFT)
#define TLBTR2_V      (TLBTR2_V_MASK     << TLBTR2_V_SHIFT)
#define TLBTR2_NSTID  (TLBTR2_NSTID_MASK << TLBTR2_NSTID_SHIFT)
#define TLBTR2_NV     (TLBTR2_NV_MASK    << TLBTR2_NV_SHIFT)
#define TLBTR2_VA     (TLBTR2_VA_MASK    << TLBTR2_VA_SHIFT)


/* Context Register Fields */
/* ACTLR */
#define CFERE              (CFERE_MASK              << CFERE_SHIFT)
#define CFEIE              (CFEIE_MASK              << CFEIE_SHIFT)
#define PTSHCFG            (PTSHCFG_MASK            << PTSHCFG_SHIFT)
#define RCOSH              (RCOSH_MASK              << RCOSH_SHIFT)
#define RCISH              (RCISH_MASK              << RCISH_SHIFT)
#define RCNSH              (RCNSH_MASK              << RCNSH_SHIFT)
#define PRIVCFG            (PRIVCFG_MASK            << PRIVCFG_SHIFT)
#define DNA                (DNA_MASK                << DNA_SHIFT)
#define DNLV2PA            (DNLV2PA_MASK            << DNLV2PA_SHIFT)
#define TLBMCFG            (TLBMCFG_MASK            << TLBMCFG_SHIFT)
#define CFCFG              (CFCFG_MASK              << CFCFG_SHIFT)
#define TIPCF              (TIPCF_MASK              << TIPCF_SHIFT)
#define V2PCFG             (V2PCFG_MASK             << V2PCFG_SHIFT)
#define HUME               (HUME_MASK               << HUME_SHIFT)
#define PTMTCFG            (PTMTCFG_MASK            << PTMTCFG_SHIFT)
#define PTMEMTYPE          (PTMEMTYPE_MASK          << PTMEMTYPE_SHIFT)


/* BFBCR */
#define BFBDFE             (BFBDFE_MASK             << BFBDFE_SHIFT)
#define BFBSFE             (BFBSFE_MASK             << BFBSFE_SHIFT)
#define SFVS               (SFVS_MASK               << SFVS_SHIFT)
#define FLVIC              (FLVIC_MASK              << FLVIC_SHIFT)
#define SLVIC              (SLVIC_MASK              << SLVIC_SHIFT)


/* CONTEXTIDR */
#define CONTEXTIDR_ASID    (CONTEXTIDR_ASID_MASK    << CONTEXTIDR_ASID_SHIFT)
#define PROCID             (PROCID_MASK             << PROCID_SHIFT)


/* FSR */
#define TF                 (TF_MASK                 << TF_SHIFT)
#define AFF                (AFF_MASK                << AFF_SHIFT)
#define APF                (APF_MASK                << APF_SHIFT)
#define TLBMF              (TLBMF_MASK              << TLBMF_SHIFT)
#define HTWDEEF            (HTWDEEF_MASK            << HTWDEEF_SHIFT)
#define HTWSEEF            (HTWSEEF_MASK            << HTWSEEF_SHIFT)
#define MHF                (MHF_MASK                << MHF_SHIFT)
#define SL                 (SL_MASK                 << SL_SHIFT)
#define SS                 (SS_MASK                 << SS_SHIFT)
#define MULTI              (MULTI_MASK              << MULTI_SHIFT)


/* FSYNR0 */
#define AMID               (AMID_MASK               << AMID_SHIFT)
#define APID               (APID_MASK               << APID_SHIFT)
#define ABID               (ABID_MASK               << ABID_SHIFT)
#define ATID               (ATID_MASK               << ATID_SHIFT)


/* FSYNR1 */
#define AMEMTYPE           (AMEMTYPE_MASK           << AMEMTYPE_SHIFT)
#define ASHARED            (ASHARED_MASK            << ASHARED_SHIFT)
#define AINNERSHARED       (AINNERSHARED_MASK       << AINNERSHARED_SHIFT)
#define APRIV              (APRIV_MASK              << APRIV_SHIFT)
#define APROTNS            (APROTNS_MASK            << APROTNS_SHIFT)
#define AINST              (AINST_MASK              << AINST_SHIFT)
#define AWRITE             (AWRITE_MASK             << AWRITE_SHIFT)
#define ABURST             (ABURST_MASK             << ABURST_SHIFT)
#define ALEN               (ALEN_MASK               << ALEN_SHIFT)
#define FSYNR1_ASIZE       (FSYNR1_ASIZE_MASK       << FSYNR1_ASIZE_SHIFT)
#define ALOCK              (ALOCK_MASK              << ALOCK_SHIFT)
#define AFULL              (AFULL_MASK              << AFULL_SHIFT)


/* NMRR */
#define ICPC0              (ICPC0_MASK              << ICPC0_SHIFT)
#define ICPC1              (ICPC1_MASK              << ICPC1_SHIFT)
#define ICPC2              (ICPC2_MASK              << ICPC2_SHIFT)
#define ICPC3              (ICPC3_MASK              << ICPC3_SHIFT)
#define ICPC4              (ICPC4_MASK              << ICPC4_SHIFT)
#define ICPC5              (ICPC5_MASK              << ICPC5_SHIFT)
#define ICPC6              (ICPC6_MASK              << ICPC6_SHIFT)
#define ICPC7              (ICPC7_MASK              << ICPC7_SHIFT)
#define OCPC0              (OCPC0_MASK              << OCPC0_SHIFT)
#define OCPC1              (OCPC1_MASK              << OCPC1_SHIFT)
#define OCPC2              (OCPC2_MASK              << OCPC2_SHIFT)
#define OCPC3              (OCPC3_MASK              << OCPC3_SHIFT)
#define OCPC4              (OCPC4_MASK              << OCPC4_SHIFT)
#define OCPC5              (OCPC5_MASK              << OCPC5_SHIFT)
#define OCPC6              (OCPC6_MASK              << OCPC6_SHIFT)
#define OCPC7              (OCPC7_MASK              << OCPC7_SHIFT)


/* PAR */
#define FAULT              (FAULT_MASK              << FAULT_SHIFT)
/* If a fault is present, these are the
same as the fault fields in the FAR */
#define FAULT_TF           (FAULT_TF_MASK           << FAULT_TF_SHIFT)
#define FAULT_AFF          (FAULT_AFF_MASK          << FAULT_AFF_SHIFT)
#define FAULT_APF          (FAULT_APF_MASK          << FAULT_APF_SHIFT)
#define FAULT_TLBMF        (FAULT_TLBMF_MASK        << FAULT_TLBMF_SHIFT)
#define FAULT_HTWDEEF      (FAULT_HTWDEEF_MASK      << FAULT_HTWDEEF_SHIFT)
#define FAULT_HTWSEEF      (FAULT_HTWSEEF_MASK      << FAULT_HTWSEEF_SHIFT)
#define FAULT_MHF          (FAULT_MHF_MASK          << FAULT_MHF_SHIFT)
#define FAULT_SL           (FAULT_SL_MASK           << FAULT_SL_SHIFT)
#define FAULT_SS           (FAULT_SS_MASK           << FAULT_SS_SHIFT)

/* If NO fault is present, the following fields are in effect */
/* (FAULT remains as before) */
#define PAR_NOFAULT_SS     (PAR_NOFAULT_SS_MASK     << PAR_NOFAULT_SS_SHIFT)
#define PAR_NOFAULT_MT     (PAR_NOFAULT_MT_MASK     << PAR_NOFAULT_MT_SHIFT)
#define PAR_NOFAULT_SH     (PAR_NOFAULT_SH_MASK     << PAR_NOFAULT_SH_SHIFT)
#define PAR_NOFAULT_NS     (PAR_NOFAULT_NS_MASK     << PAR_NOFAULT_NS_SHIFT)
#define PAR_NOFAULT_NOS    (PAR_NOFAULT_NOS_MASK    << PAR_NOFAULT_NOS_SHIFT)
#define PAR_NPFAULT_PA     (PAR_NPFAULT_PA_MASK     << PAR_NPFAULT_PA_SHIFT)


/* PRRR */
#define MTC0               (MTC0_MASK               << MTC0_SHIFT)
#define MTC1               (MTC1_MASK               << MTC1_SHIFT)
#define MTC2               (MTC2_MASK               << MTC2_SHIFT)
#define MTC3               (MTC3_MASK               << MTC3_SHIFT)
#define MTC4               (MTC4_MASK               << MTC4_SHIFT)
#define MTC5               (MTC5_MASK               << MTC5_SHIFT)
#define MTC6               (MTC6_MASK               << MTC6_SHIFT)
#define MTC7               (MTC7_MASK               << MTC7_SHIFT)
#define SHDSH0             (SHDSH0_MASK             << SHDSH0_SHIFT)
#define SHDSH1             (SHDSH1_MASK             << SHDSH1_SHIFT)
#define SHNMSH0            (SHNMSH0_MASK            << SHNMSH0_SHIFT)
#define SHNMSH1            (SHNMSH1_MASK            << SHNMSH1_SHIFT)
#define NOS0               (NOS0_MASK               << NOS0_SHIFT)
#define NOS1               (NOS1_MASK               << NOS1_SHIFT)
#define NOS2               (NOS2_MASK               << NOS2_SHIFT)
#define NOS3               (NOS3_MASK               << NOS3_SHIFT)
#define NOS4               (NOS4_MASK               << NOS4_SHIFT)
#define NOS5               (NOS5_MASK               << NOS5_SHIFT)
#define NOS6               (NOS6_MASK               << NOS6_SHIFT)
#define NOS7               (NOS7_MASK               << NOS7_SHIFT)


/* RESUME */
#define TNR                (TNR_MASK                << TNR_SHIFT)


/* SCTLR */
#define M                  (M_MASK                  << M_SHIFT)
#define TRE                (TRE_MASK                << TRE_SHIFT)
#define AFE                (AFE_MASK                << AFE_SHIFT)
#define HAF                (HAF_MASK                << HAF_SHIFT)
#define BE                 (BE_MASK                 << BE_SHIFT)
#define AFFD               (AFFD_MASK               << AFFD_SHIFT)


/* TLBIASID */
#define TLBIASID_ASID      (TLBIASID_ASID_MASK      << TLBIASID_ASID_SHIFT)


/* TLBIVA */
#define TLBIVA_ASID        (TLBIVA_ASID_MASK        << TLBIVA_ASID_SHIFT)
#define TLBIVA_VA          (TLBIVA_VA_MASK          << TLBIVA_VA_SHIFT)


/* TLBIVAA */
#define TLBIVAA_VA         (TLBIVAA_VA_MASK         << TLBIVAA_VA_SHIFT)


/* TLBLCKR */
#define LKE                (LKE_MASK                << LKE_SHIFT)
#define TLBLCKR_TLBIALLCFG (TLBLCKR_TLBIALLCFG_MASK<<TLBLCKR_TLBIALLCFG_SHIFT)
#define TLBIASIDCFG        (TLBIASIDCFG_MASK        << TLBIASIDCFG_SHIFT)
#define TLBIVAACFG         (TLBIVAACFG_MASK         << TLBIVAACFG_SHIFT)
#define FLOOR              (FLOOR_MASK              << FLOOR_SHIFT)
#define VICTIM             (VICTIM_MASK             << VICTIM_SHIFT)


/* TTBCR */
#define N                  (N_MASK                  << N_SHIFT)
#define PD0                (PD0_MASK                << PD0_SHIFT)
#define PD1                (PD1_MASK                << PD1_SHIFT)


/* TTBR0 */
#define TTBR0_IRGNH        (TTBR0_IRGNH_MASK        << TTBR0_IRGNH_SHIFT)
#define TTBR0_SH           (TTBR0_SH_MASK           << TTBR0_SH_SHIFT)
#define TTBR0_ORGN         (TTBR0_ORGN_MASK         << TTBR0_ORGN_SHIFT)
#define TTBR0_NOS          (TTBR0_NOS_MASK          << TTBR0_NOS_SHIFT)
#define TTBR0_IRGNL        (TTBR0_IRGNL_MASK        << TTBR0_IRGNL_SHIFT)
#define TTBR0_PA           (TTBR0_PA_MASK           << TTBR0_PA_SHIFT)


/* TTBR1 */
#define TTBR1_IRGNH        (TTBR1_IRGNH_MASK        << TTBR1_IRGNH_SHIFT)
#define TTBR1_SH           (TTBR1_SH_MASK           << TTBR1_SH_SHIFT)
#define TTBR1_ORGN         (TTBR1_ORGN_MASK         << TTBR1_ORGN_SHIFT)
#define TTBR1_NOS          (TTBR1_NOS_MASK          << TTBR1_NOS_SHIFT)
#define TTBR1_IRGNL        (TTBR1_IRGNL_MASK        << TTBR1_IRGNL_SHIFT)
#define TTBR1_PA           (TTBR1_PA_MASK           << TTBR1_PA_SHIFT)


/* V2PSR */
#define HIT                (HIT_MASK                << HIT_SHIFT)
#define INDEX              (INDEX_MASK              << INDEX_SHIFT)


/* V2Pxx */
#define V2Pxx_INDEX        (V2Pxx_INDEX_MASK        << V2Pxx_INDEX_SHIFT)
#define V2Pxx_VA           (V2Pxx_VA_MASK           << V2Pxx_VA_SHIFT)


/* Global Register Masks */
/* CBACRn */
#define RWVMID_MASK               0x1F
#define RWE_MASK                  0x01
#define RWGE_MASK                 0x01
#define CBVMID_MASK               0x1F
#define IRPTNDX_MASK              0xFF


/* CR */
#define RPUE_MASK                 0x01
#define RPUERE_MASK               0x01
#define RPUEIE_MASK               0x01
#define DCDEE_MASK                0x01
#define CLIENTPD_MASK             0x01
#define STALLD_MASK               0x01
#define TLBLKCRWE_MASK            0x01
#define CR_TLBIALLCFG_MASK        0x01
#define TLBIVMIDCFG_MASK          0x01
#define CR_HUME_MASK              0x01


/* ESR */
#define CFG_MASK                  0x01
#define BYPASS_MASK               0x01
#define ESR_MULTI_MASK            0x01


/* ESYNR0 */
#define ESYNR0_AMID_MASK          0xFF
#define ESYNR0_APID_MASK          0x1F
#define ESYNR0_ABID_MASK          0x07
#define ESYNR0_AVMID_MASK         0x1F
#define ESYNR0_ATID_MASK          0xFF


/* ESYNR1 */
#define ESYNR1_AMEMTYPE_MASK             0x07
#define ESYNR1_ASHARED_MASK              0x01
#define ESYNR1_AINNERSHARED_MASK         0x01
#define ESYNR1_APRIV_MASK                0x01
#define ESYNR1_APROTNS_MASK              0x01
#define ESYNR1_AINST_MASK                0x01
#define ESYNR1_AWRITE_MASK               0x01
#define ESYNR1_ABURST_MASK               0x01
#define ESYNR1_ALEN_MASK                 0x0F
#define ESYNR1_ASIZE_MASK                0x01
#define ESYNR1_ALOCK_MASK                0x03
#define ESYNR1_AOOO_MASK                 0x01
#define ESYNR1_AFULL_MASK                0x01
#define ESYNR1_AC_MASK                   0x01
#define ESYNR1_DCD_MASK                  0x01


/* IDR */
#define NM2VCBMT_MASK             0x1FF
#define HTW_MASK                  0x01
#define HUM_MASK                  0x01
#define TLBSIZE_MASK              0x0F
#define NCB_MASK                  0xFF
#define NIRPT_MASK                0xFF


/* M2VCBRn */
#define VMID_MASK                 0x1F
#define CBNDX_MASK                0xFF
#define BYPASSD_MASK              0x01
#define BPRCOSH_MASK              0x01
#define BPRCISH_MASK              0x01
#define BPRCNSH_MASK              0x01
#define BPSHCFG_MASK              0x03
#define NSCFG_MASK                0x03
#define BPMTCFG_MASK              0x01
#define BPMEMTYPE_MASK            0x07


/* REV */
#define MINOR_MASK                0x0F
#define MAJOR_MASK                0x0F


/* TESTBUSCR */
#define TBE_MASK                  0x01
#define SPDMBE_MASK               0x01
#define WGSEL_MASK                0x03
#define TBLSEL_MASK               0x03
#define TBHSEL_MASK               0x03
#define SPDM0SEL_MASK             0x0F
#define SPDM1SEL_MASK             0x0F
#define SPDM2SEL_MASK             0x0F
#define SPDM3SEL_MASK             0x0F


/* TLBIMID */
#define TLBIVMID_VMID_MASK        0x1F


/* TLBRSW */
#define TLBRSW_INDEX_MASK         0xFF
#define TLBBFBS_MASK              0x03


/* TLBTR0 */
#define PR_MASK                   0x01
#define PW_MASK                   0x01
#define UR_MASK                   0x01
#define UW_MASK                   0x01
#define XN_MASK                   0x01
#define NSDESC_MASK               0x01
#define ISH_MASK                  0x01
#define SH_MASK                   0x01
#define MT_MASK                   0x07
#define DPSIZR_MASK               0x07
#define DPSIZC_MASK               0x07


/* TLBTR1 */
#define TLBTR1_VMID_MASK          0x1F
#define TLBTR1_PA_MASK            0x000FFFFF


/* TLBTR2 */
#define TLBTR2_ASID_MASK          0xFF
#define TLBTR2_V_MASK             0x01
#define TLBTR2_NSTID_MASK         0x01
#define TLBTR2_NV_MASK            0x01
#define TLBTR2_VA_MASK            0x000FFFFF


/* Global Register Shifts */
/* CBACRn */
#define RWVMID_SHIFT             0
#define RWE_SHIFT                8
#define RWGE_SHIFT               9
#define CBVMID_SHIFT             16
#define IRPTNDX_SHIFT            24


/* CR */
#define RPUE_SHIFT               0
#define RPUERE_SHIFT             1
#define RPUEIE_SHIFT             2
#define DCDEE_SHIFT              3
#define CLIENTPD_SHIFT           4
#define STALLD_SHIFT             5
#define TLBLKCRWE_SHIFT          6
#define CR_TLBIALLCFG_SHIFT      7
#define TLBIVMIDCFG_SHIFT        8
#define CR_HUME_SHIFT            9


/* ESR */
#define CFG_SHIFT                0
#define BYPASS_SHIFT             1
#define ESR_MULTI_SHIFT          31


/* ESYNR0 */
#define ESYNR0_AMID_SHIFT        0
#define ESYNR0_APID_SHIFT        8
#define ESYNR0_ABID_SHIFT        13
#define ESYNR0_AVMID_SHIFT       16
#define ESYNR0_ATID_SHIFT        24


/* ESYNR1 */
#define ESYNR1_AMEMTYPE_SHIFT           0
#define ESYNR1_ASHARED_SHIFT            3
#define ESYNR1_AINNERSHARED_SHIFT       4
#define ESYNR1_APRIV_SHIFT              5
#define ESYNR1_APROTNS_SHIFT            6
#define ESYNR1_AINST_SHIFT              7
#define ESYNR1_AWRITE_SHIFT             8
#define ESYNR1_ABURST_SHIFT             10
#define ESYNR1_ALEN_SHIFT               12
#define ESYNR1_ASIZE_SHIFT              16
#define ESYNR1_ALOCK_SHIFT              20
#define ESYNR1_AOOO_SHIFT               22
#define ESYNR1_AFULL_SHIFT              24
#define ESYNR1_AC_SHIFT                 30
#define ESYNR1_DCD_SHIFT                31


/* IDR */
#define NM2VCBMT_SHIFT           0
#define HTW_SHIFT                9
#define HUM_SHIFT                10
#define TLBSIZE_SHIFT            12
#define NCB_SHIFT                16
#define NIRPT_SHIFT              24


/* M2VCBRn */
#define VMID_SHIFT               0
#define CBNDX_SHIFT              8
#define BYPASSD_SHIFT            16
#define BPRCOSH_SHIFT            17
#define BPRCISH_SHIFT            18
#define BPRCNSH_SHIFT            19
#define BPSHCFG_SHIFT            20
#define NSCFG_SHIFT              22
#define BPMTCFG_SHIFT            24
#define BPMEMTYPE_SHIFT          25


/* REV */
#define MINOR_SHIFT              0
#define MAJOR_SHIFT              4


/* TESTBUSCR */
#define TBE_SHIFT                0
#define SPDMBE_SHIFT             1
#define WGSEL_SHIFT              8
#define TBLSEL_SHIFT             12
#define TBHSEL_SHIFT             14
#define SPDM0SEL_SHIFT           16
#define SPDM1SEL_SHIFT           20
#define SPDM2SEL_SHIFT           24
#define SPDM3SEL_SHIFT           28


/* TLBIMID */
#define TLBIVMID_VMID_SHIFT      0


/* TLBRSW */
#define TLBRSW_INDEX_SHIFT       0
#define TLBBFBS_SHIFT            8


/* TLBTR0 */
#define PR_SHIFT                 0
#define PW_SHIFT                 1
#define UR_SHIFT                 2
#define UW_SHIFT                 3
#define XN_SHIFT                 4
#define NSDESC_SHIFT             6
#define ISH_SHIFT                7
#define SH_SHIFT                 8
#define MT_SHIFT                 9
#define DPSIZR_SHIFT             16
#define DPSIZC_SHIFT             20


/* TLBTR1 */
#define TLBTR1_VMID_SHIFT        0
#define TLBTR1_PA_SHIFT          12


/* TLBTR2 */
#define TLBTR2_ASID_SHIFT        0
#define TLBTR2_V_SHIFT           8
#define TLBTR2_NSTID_SHIFT       9
#define TLBTR2_NV_SHIFT          10
#define TLBTR2_VA_SHIFT          12


/* Context Register Masks */
/* ACTLR */
#define CFERE_MASK                       0x01
#define CFEIE_MASK                       0x01
#define PTSHCFG_MASK                     0x03
#define RCOSH_MASK                       0x01
#define RCISH_MASK                       0x01
#define RCNSH_MASK                       0x01
#define PRIVCFG_MASK                     0x03
#define DNA_MASK                         0x01
#define DNLV2PA_MASK                     0x01
#define TLBMCFG_MASK                     0x03
#define CFCFG_MASK                       0x01
#define TIPCF_MASK                       0x01
#define V2PCFG_MASK                      0x03
#define HUME_MASK                        0x01
#define PTMTCFG_MASK                     0x01
#define PTMEMTYPE_MASK                   0x07


/* BFBCR */
#define BFBDFE_MASK                      0x01
#define BFBSFE_MASK                      0x01
#define SFVS_MASK                        0x01
#define FLVIC_MASK                       0x0F
#define SLVIC_MASK                       0x0F


/* CONTEXTIDR */
#define CONTEXTIDR_ASID_MASK             0xFF
#define PROCID_MASK                      0x00FFFFFF


/* FSR */
#define TF_MASK                          0x01
#define AFF_MASK                         0x01
#define APF_MASK                         0x01
#define TLBMF_MASK                       0x01
#define HTWDEEF_MASK                     0x01
#define HTWSEEF_MASK                     0x01
#define MHF_MASK                         0x01
#define SL_MASK                          0x01
#define SS_MASK                          0x01
#define MULTI_MASK                       0x01


/* FSYNR0 */
#define AMID_MASK                        0xFF
#define APID_MASK                        0x1F
#define ABID_MASK                        0x07
#define ATID_MASK                        0xFF


/* FSYNR1 */
#define AMEMTYPE_MASK                    0x07
#define ASHARED_MASK                     0x01
#define AINNERSHARED_MASK                0x01
#define APRIV_MASK                       0x01
#define APROTNS_MASK                     0x01
#define AINST_MASK                       0x01
#define AWRITE_MASK                      0x01
#define ABURST_MASK                      0x01
#define ALEN_MASK                        0x0F
#define FSYNR1_ASIZE_MASK                0x07
#define ALOCK_MASK                       0x03
#define AFULL_MASK                       0x01


/* NMRR */
#define ICPC0_MASK                       0x03
#define ICPC1_MASK                       0x03
#define ICPC2_MASK                       0x03
#define ICPC3_MASK                       0x03
#define ICPC4_MASK                       0x03
#define ICPC5_MASK                       0x03
#define ICPC6_MASK                       0x03
#define ICPC7_MASK                       0x03
#define OCPC0_MASK                       0x03
#define OCPC1_MASK                       0x03
#define OCPC2_MASK                       0x03
#define OCPC3_MASK                       0x03
#define OCPC4_MASK                       0x03
#define OCPC5_MASK                       0x03
#define OCPC6_MASK                       0x03
#define OCPC7_MASK                       0x03


/* PAR */
#define FAULT_MASK                       0x01
/* If a fault is present, these are the
same as the fault fields in the FAR */
#define FAULT_TF_MASK                    0x01
#define FAULT_AFF_MASK                   0x01
#define FAULT_APF_MASK                   0x01
#define FAULT_TLBMF_MASK                 0x01
#define FAULT_HTWDEEF_MASK               0x01
#define FAULT_HTWSEEF_MASK               0x01
#define FAULT_MHF_MASK                   0x01
#define FAULT_SL_MASK                    0x01
#define FAULT_SS_MASK                    0x01

/* If NO fault is present, the following
 * fields are in effect
 * (FAULT remains as before) */
#define PAR_NOFAULT_SS_MASK              0x01
#define PAR_NOFAULT_MT_MASK              0x07
#define PAR_NOFAULT_SH_MASK              0x01
#define PAR_NOFAULT_NS_MASK              0x01
#define PAR_NOFAULT_NOS_MASK             0x01
#define PAR_NPFAULT_PA_MASK              0x000FFFFF


/* PRRR */
#define MTC0_MASK                        0x03
#define MTC1_MASK                        0x03
#define MTC2_MASK                        0x03
#define MTC3_MASK                        0x03
#define MTC4_MASK                        0x03
#define MTC5_MASK                        0x03
#define MTC6_MASK                        0x03
#define MTC7_MASK                        0x03
#define SHDSH0_MASK                      0x01
#define SHDSH1_MASK                      0x01
#define SHNMSH0_MASK                     0x01
#define SHNMSH1_MASK                     0x01
#define NOS0_MASK                        0x01
#define NOS1_MASK                        0x01
#define NOS2_MASK                        0x01
#define NOS3_MASK                        0x01
#define NOS4_MASK                        0x01
#define NOS5_MASK                        0x01
#define NOS6_MASK                        0x01
#define NOS7_MASK                        0x01


/* RESUME */
#define TNR_MASK                         0x01


/* SCTLR */
#define M_MASK                           0x01
#define TRE_MASK                         0x01
#define AFE_MASK                         0x01
#define HAF_MASK                         0x01
#define BE_MASK                          0x01
#define AFFD_MASK                        0x01


/* TLBIASID */
#define TLBIASID_ASID_MASK               0xFF


/* TLBIVA */
#define TLBIVA_ASID_MASK                 0xFF
#define TLBIVA_VA_MASK                   0x000FFFFF


/* TLBIVAA */
#define TLBIVAA_VA_MASK                  0x000FFFFF


/* TLBLCKR */
#define LKE_MASK                         0x01
#define TLBLCKR_TLBIALLCFG_MASK          0x01
#define TLBIASIDCFG_MASK                 0x01
#define TLBIVAACFG_MASK                  0x01
#define FLOOR_MASK                       0xFF
#define VICTIM_MASK                      0xFF


/* TTBCR */
#define N_MASK                           0x07
#define PD0_MASK                         0x01
#define PD1_MASK                         0x01


/* TTBR0 */
#define TTBR0_IRGNH_MASK                 0x01
#define TTBR0_SH_MASK                    0x01
#define TTBR0_ORGN_MASK                  0x03
#define TTBR0_NOS_MASK                   0x01
#define TTBR0_IRGNL_MASK                 0x01
#define TTBR0_PA_MASK                    0x0003FFFF


/* TTBR1 */
#define TTBR1_IRGNH_MASK                 0x01
#define TTBR1_SH_MASK                    0x01
#define TTBR1_ORGN_MASK                  0x03
#define TTBR1_NOS_MASK                   0x01
#define TTBR1_IRGNL_MASK                 0x01
#define TTBR1_PA_MASK                    0x0003FFFF


/* V2PSR */
#define HIT_MASK                         0x01
#define INDEX_MASK                       0xFF


/* V2Pxx */
#define V2Pxx_INDEX_MASK                 0xFF
#define V2Pxx_VA_MASK                    0x000FFFFF


/* Context Register Shifts */
/* ACTLR */
#define CFERE_SHIFT                    0
#define CFEIE_SHIFT                    1
#define PTSHCFG_SHIFT                  2
#define RCOSH_SHIFT                    4
#define RCISH_SHIFT                    5
#define RCNSH_SHIFT                    6
#define PRIVCFG_SHIFT                  8
#define DNA_SHIFT                      10
#define DNLV2PA_SHIFT                  11
#define TLBMCFG_SHIFT                  12
#define CFCFG_SHIFT                    14
#define TIPCF_SHIFT                    15
#define V2PCFG_SHIFT                   16
#define HUME_SHIFT                     18
#define PTMTCFG_SHIFT                  20
#define PTMEMTYPE_SHIFT                21


/* BFBCR */
#define BFBDFE_SHIFT                   0
#define BFBSFE_SHIFT                   1
#define SFVS_SHIFT                     2
#define FLVIC_SHIFT                    4
#define SLVIC_SHIFT                    8


/* CONTEXTIDR */
#define CONTEXTIDR_ASID_SHIFT          0
#define PROCID_SHIFT                   8


/* FSR */
#define TF_SHIFT                       1
#define AFF_SHIFT                      2
#define APF_SHIFT                      3
#define TLBMF_SHIFT                    4
#define HTWDEEF_SHIFT                  5
#define HTWSEEF_SHIFT                  6
#define MHF_SHIFT                      7
#define SL_SHIFT                       16
#define SS_SHIFT                       30
#define MULTI_SHIFT                    31


/* FSYNR0 */
#define AMID_SHIFT                     0
#define APID_SHIFT                     8
#define ABID_SHIFT                     13
#define ATID_SHIFT                     24


/* FSYNR1 */
#define AMEMTYPE_SHIFT                 0
#define ASHARED_SHIFT                  3
#define AINNERSHARED_SHIFT             4
#define APRIV_SHIFT                    5
#define APROTNS_SHIFT                  6
#define AINST_SHIFT                    7
#define AWRITE_SHIFT                   8
#define ABURST_SHIFT                   10
#define ALEN_SHIFT                     12
#define FSYNR1_ASIZE_SHIFT             16
#define ALOCK_SHIFT                    20
#define AFULL_SHIFT                    24


/* NMRR */
#define ICPC0_SHIFT                    0
#define ICPC1_SHIFT                    2
#define ICPC2_SHIFT                    4
#define ICPC3_SHIFT                    6
#define ICPC4_SHIFT                    8
#define ICPC5_SHIFT                    10
#define ICPC6_SHIFT                    12
#define ICPC7_SHIFT                    14
#define OCPC0_SHIFT                    16
#define OCPC1_SHIFT                    18
#define OCPC2_SHIFT                    20
#define OCPC3_SHIFT                    22
#define OCPC4_SHIFT                    24
#define OCPC5_SHIFT                    26
#define OCPC6_SHIFT                    28
#define OCPC7_SHIFT                    30


/* PAR */
#define FAULT_SHIFT                    0
/* If a fault is present, these are the
same as the fault fields in the FAR */
#define FAULT_TF_SHIFT                 1
#define FAULT_AFF_SHIFT                2
#define FAULT_APF_SHIFT                3
#define FAULT_TLBMF_SHIFT              4
#define FAULT_HTWDEEF_SHIFT            5
#define FAULT_HTWSEEF_SHIFT            6
#define FAULT_MHF_SHIFT                7
#define FAULT_SL_SHIFT                 16
#define FAULT_SS_SHIFT                 30

/* If NO fault is present, the following
 * fields are in effect
 * (FAULT remains as before) */
#define PAR_NOFAULT_SS_SHIFT           1
#define PAR_NOFAULT_MT_SHIFT           4
#define PAR_NOFAULT_SH_SHIFT           7
#define PAR_NOFAULT_NS_SHIFT           9
#define PAR_NOFAULT_NOS_SHIFT          10
#define PAR_NPFAULT_PA_SHIFT           12


/* PRRR */
#define MTC0_SHIFT                     0
#define MTC1_SHIFT                     2
#define MTC2_SHIFT                     4
#define MTC3_SHIFT                     6
#define MTC4_SHIFT                     8
#define MTC5_SHIFT                     10
#define MTC6_SHIFT                     12
#define MTC7_SHIFT                     14
#define SHDSH0_SHIFT                   16
#define SHDSH1_SHIFT                   17
#define SHNMSH0_SHIFT                  18
#define SHNMSH1_SHIFT                  19
#define NOS0_SHIFT                     24
#define NOS1_SHIFT                     25
#define NOS2_SHIFT                     26
#define NOS3_SHIFT                     27
#define NOS4_SHIFT                     28
#define NOS5_SHIFT                     29
#define NOS6_SHIFT                     30
#define NOS7_SHIFT                     31


/* RESUME */
#define TNR_SHIFT                      0


/* SCTLR */
#define M_SHIFT                        0
#define TRE_SHIFT                      1
#define AFE_SHIFT                      2
#define HAF_SHIFT                      3
#define BE_SHIFT                       4
#define AFFD_SHIFT                     5


/* TLBIASID */
#define TLBIASID_ASID_SHIFT            0


/* TLBIVA */
#define TLBIVA_ASID_SHIFT              0
#define TLBIVA_VA_SHIFT                12


/* TLBIVAA */
#define TLBIVAA_VA_SHIFT               12


/* TLBLCKR */
#define LKE_SHIFT                      0
#define TLBLCKR_TLBIALLCFG_SHIFT       1
#define TLBIASIDCFG_SHIFT              2
#define TLBIVAACFG_SHIFT               3
#define FLOOR_SHIFT                    8
#define VICTIM_SHIFT                   8


/* TTBCR */
#define N_SHIFT                        3
#define PD0_SHIFT                      4
#define PD1_SHIFT                      5


/* TTBR0 */
#define TTBR0_IRGNH_SHIFT              0
#define TTBR0_SH_SHIFT                 1
#define TTBR0_ORGN_SHIFT               3
#define TTBR0_NOS_SHIFT                5
#define TTBR0_IRGNL_SHIFT              6
#define TTBR0_PA_SHIFT                 14


/* TTBR1 */
#define TTBR1_IRGNH_SHIFT              0
#define TTBR1_SH_SHIFT                 1
#define TTBR1_ORGN_SHIFT               3
#define TTBR1_NOS_SHIFT                5
#define TTBR1_IRGNL_SHIFT              6
#define TTBR1_PA_SHIFT                 14


/* V2PSR */
#define HIT_SHIFT                      0
#define INDEX_SHIFT                    8


/* V2Pxx */
#define V2Pxx_INDEX_SHIFT              0
#define V2Pxx_VA_SHIFT                 12

#endif
