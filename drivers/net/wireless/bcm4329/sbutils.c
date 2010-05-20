/*
 * Misc utility routines for accessing chip-specific features
 * of the SiliconBackplane-based Broadcom chips.
 *
 * Copyright (C) 1999-2009, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: sbutils.c,v 1.662.4.10.2.7.4.1 2009/09/25 00:32:01 Exp $
 */

#include <typedefs.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <siutils.h>
#include <bcmdevs.h>
#include <hndsoc.h>
#include <sbchipc.h>
#include <pcicfg.h>
#include <sbpcmcia.h>

#include "siutils_priv.h"

/* local prototypes */
static uint _sb_coreidx(si_info_t *sii, uint32 sba);
static uint _sb_scan(si_info_t *sii, uint32 sba, void *regs, uint bus, uint32 sbba,
                     uint ncores);
static uint32 _sb_coresba(si_info_t *sii);
static void *_sb_setcoreidx(si_info_t *sii, uint coreidx);

#define	SET_SBREG(sii, r, mask, val)	\
		W_SBREG((sii), (r), ((R_SBREG((sii), (r)) & ~(mask)) | (val)))
#define	REGS2SB(va)	(sbconfig_t*) ((int8*)(va) + SBCONFIGOFF)

/* sonicsrev */
#define	SONICS_2_2	(SBIDL_RV_2_2 >> SBIDL_RV_SHIFT)
#define	SONICS_2_3	(SBIDL_RV_2_3 >> SBIDL_RV_SHIFT)

#define	R_SBREG(sii, sbr)	sb_read_sbreg((sii), (sbr))
#define	W_SBREG(sii, sbr, v)	sb_write_sbreg((sii), (sbr), (v))
#define	AND_SBREG(sii, sbr, v)	W_SBREG((sii), (sbr), (R_SBREG((sii), (sbr)) & (v)))
#define	OR_SBREG(sii, sbr, v)	W_SBREG((sii), (sbr), (R_SBREG((sii), (sbr)) | (v)))

static uint32
sb_read_sbreg(si_info_t *sii, volatile uint32 *sbr)
{
	uint8 tmp;
	uint32 val, intr_val = 0;


	/*
	 * compact flash only has 11 bits address, while we needs 12 bits address.
	 * MEM_SEG will be OR'd with other 11 bits address in hardware,
	 * so we program MEM_SEG with 12th bit when necessary(access sb regsiters).
	 * For normal PCMCIA bus(CFTable_regwinsz > 2k), do nothing special
	 */
	if (PCMCIA(sii)) {
		INTR_OFF(sii, intr_val);
		tmp = 1;
		OSL_PCMCIA_WRITE_ATTR(sii->osh, MEM_SEG, &tmp, 1);
		sbr = (volatile uint32 *)((uintptr)sbr & ~(1 << 11)); /* mask out bit 11 */
	}

	val = R_REG(sii->osh, sbr);

	if (PCMCIA(sii)) {
		tmp = 0;
		OSL_PCMCIA_WRITE_ATTR(sii->osh, MEM_SEG, &tmp, 1);
		INTR_RESTORE(sii, intr_val);
	}

	return (val);
}

static void
sb_write_sbreg(si_info_t *sii, volatile uint32 *sbr, uint32 v)
{
	uint8 tmp;
	volatile uint32 dummy;
	uint32 intr_val = 0;


	/*
	 * compact flash only has 11 bits address, while we needs 12 bits address.
	 * MEM_SEG will be OR'd with other 11 bits address in hardware,
	 * so we program MEM_SEG with 12th bit when necessary(access sb regsiters).
	 * For normal PCMCIA bus(CFTable_regwinsz > 2k), do nothing special
	 */
	if (PCMCIA(sii)) {
		INTR_OFF(sii, intr_val);
		tmp = 1;
		OSL_PCMCIA_WRITE_ATTR(sii->osh, MEM_SEG, &tmp, 1);
		sbr = (volatile uint32 *)((uintptr)sbr & ~(1 << 11)); /* mask out bit 11 */
	}

	if (BUSTYPE(sii->pub.bustype) == PCMCIA_BUS) {
#ifdef IL_BIGENDIAN
		dummy = R_REG(sii->osh, sbr);
		W_REG(sii->osh, ((volatile uint16 *)sbr + 1), (uint16)((v >> 16) & 0xffff));
		dummy = R_REG(sii->osh, sbr);
		W_REG(sii->osh, (volatile uint16 *)sbr, (uint16)(v & 0xffff));
#else
		dummy = R_REG(sii->osh, sbr);
		W_REG(sii->osh, (volatile uint16 *)sbr, (uint16)(v & 0xffff));
		dummy = R_REG(sii->osh, sbr);
		W_REG(sii->osh, ((volatile uint16 *)sbr + 1), (uint16)((v >> 16) & 0xffff));
#endif	/* IL_BIGENDIAN */
	} else
		W_REG(sii->osh, sbr, v);

	if (PCMCIA(sii)) {
		tmp = 0;
		OSL_PCMCIA_WRITE_ATTR(sii->osh, MEM_SEG, &tmp, 1);
		INTR_RESTORE(sii, intr_val);
	}
}

uint
sb_coreid(si_t *sih)
{
	si_info_t *sii;
	sbconfig_t *sb;

	sii = SI_INFO(sih);
	sb = REGS2SB(sii->curmap);

	return ((R_SBREG(sii, &sb->sbidhigh) & SBIDH_CC_MASK) >> SBIDH_CC_SHIFT);
}

uint
sb_flag(si_t *sih)
{
	si_info_t *sii;
	sbconfig_t *sb;

	sii = SI_INFO(sih);
	sb = REGS2SB(sii->curmap);

	return R_SBREG(sii, &sb->sbtpsflag) & SBTPS_NUM0_MASK;
}

void
sb_setint(si_t *sih, int siflag)
{
	si_info_t *sii;
	sbconfig_t *sb;
	uint32 vec;

	sii = SI_INFO(sih);
	sb = REGS2SB(sii->curmap);

	if (siflag == -1)
		vec = 0;
	else
		vec = 1 << siflag;
	W_SBREG(sii, &sb->sbintvec, vec);
}

/* return core index of the core with address 'sba' */
static uint
_sb_coreidx(si_info_t *sii, uint32 sba)
{
	uint i;

	for (i = 0; i < sii->numcores; i ++)
		if (sba == sii->common_info->coresba[i])
			return i;
	return BADIDX;
}

/* return core address of the current core */
static uint32
_sb_coresba(si_info_t *sii)
{
	uint32 sbaddr;


	switch (BUSTYPE(sii->pub.bustype)) {
	case SI_BUS: {
		sbconfig_t *sb = REGS2SB(sii->curmap);
		sbaddr = sb_base(R_SBREG(sii, &sb->sbadmatch0));
		break;
	}

	case PCI_BUS:
		sbaddr = OSL_PCI_READ_CONFIG(sii->osh, PCI_BAR0_WIN, sizeof(uint32));
		break;

	case PCMCIA_BUS: {
		uint8 tmp = 0;
		OSL_PCMCIA_READ_ATTR(sii->osh, PCMCIA_ADDR0, &tmp, 1);
		sbaddr  = (uint32)tmp << 12;
		OSL_PCMCIA_READ_ATTR(sii->osh, PCMCIA_ADDR1, &tmp, 1);
		sbaddr |= (uint32)tmp << 16;
		OSL_PCMCIA_READ_ATTR(sii->osh, PCMCIA_ADDR2, &tmp, 1);
		sbaddr |= (uint32)tmp << 24;
		break;
	}

	case SPI_BUS:
	case SDIO_BUS:
		sbaddr = (uint32)(uintptr)sii->curmap;
		break;


	default:
		sbaddr = BADCOREADDR;
		break;
	}

	return sbaddr;
}

uint
sb_corevendor(si_t *sih)
{
	si_info_t *sii;
	sbconfig_t *sb;

	sii = SI_INFO(sih);
	sb = REGS2SB(sii->curmap);

	return ((R_SBREG(sii, &sb->sbidhigh) & SBIDH_VC_MASK) >> SBIDH_VC_SHIFT);
}

uint
sb_corerev(si_t *sih)
{
	si_info_t *sii;
	sbconfig_t *sb;
	uint sbidh;

	sii = SI_INFO(sih);
	sb = REGS2SB(sii->curmap);
	sbidh = R_SBREG(sii, &sb->sbidhigh);

	return (SBCOREREV(sbidh));
}

/* set core-specific control flags */
void
sb_core_cflags_wo(si_t *sih, uint32 mask, uint32 val)
{
	si_info_t *sii;
	sbconfig_t *sb;
	uint32 w;

	sii = SI_INFO(sih);
	sb = REGS2SB(sii->curmap);

	ASSERT((val & ~mask) == 0);

	/* mask and set */
	w = (R_SBREG(sii, &sb->sbtmstatelow) & ~(mask << SBTML_SICF_SHIFT)) |
	        (val << SBTML_SICF_SHIFT);
	W_SBREG(sii, &sb->sbtmstatelow, w);
}

/* set/clear core-specific control flags */
uint32
sb_core_cflags(si_t *sih, uint32 mask, uint32 val)
{
	si_info_t *sii;
	sbconfig_t *sb;
	uint32 w;

	sii = SI_INFO(sih);
	sb = REGS2SB(sii->curmap);

	ASSERT((val & ~mask) == 0);

	/* mask and set */
	if (mask || val) {
		w = (R_SBREG(sii, &sb->sbtmstatelow) & ~(mask << SBTML_SICF_SHIFT)) |
		        (val << SBTML_SICF_SHIFT);
		W_SBREG(sii, &sb->sbtmstatelow, w);
	}

	/* return the new value
	 * for write operation, the following readback ensures the completion of write opration.
	 */
	return (R_SBREG(sii, &sb->sbtmstatelow) >> SBTML_SICF_SHIFT);
}

/* set/clear core-specific status flags */
uint32
sb_core_sflags(si_t *sih, uint32 mask, uint32 val)
{
	si_info_t *sii;
	sbconfig_t *sb;
	uint32 w;

	sii = SI_INFO(sih);
	sb = REGS2SB(sii->curmap);

	ASSERT((val & ~mask) == 0);
	ASSERT((mask & ~SISF_CORE_BITS) == 0);

	/* mask and set */
	if (mask || val) {
		w = (R_SBREG(sii, &sb->sbtmstatehigh) & ~(mask << SBTMH_SISF_SHIFT)) |
		        (val << SBTMH_SISF_SHIFT);
		W_SBREG(sii, &sb->sbtmstatehigh, w);
	}

	/* return the new value */
	return (R_SBREG(sii, &sb->sbtmstatehigh) >> SBTMH_SISF_SHIFT);
}

bool
sb_iscoreup(si_t *sih)
{
	si_info_t *sii;
	sbconfig_t *sb;

	sii = SI_INFO(sih);
	sb = REGS2SB(sii->curmap);

	return ((R_SBREG(sii, &sb->sbtmstatelow) &
	         (SBTML_RESET | SBTML_REJ_MASK | (SICF_CLOCK_EN << SBTML_SICF_SHIFT))) ==
	        (SICF_CLOCK_EN << SBTML_SICF_SHIFT));
}

/*
 * Switch to 'coreidx', issue a single arbitrary 32bit register mask&set operation,
 * switch back to the original core, and return the new value.
 *
 * When using the silicon backplane, no fidleing with interrupts or core switches are needed.
 *
 * Also, when using pci/pcie, we can optimize away the core switching for pci registers
 * and (on newer pci cores) chipcommon registers.
 */
uint
sb_corereg(si_t *sih, uint coreidx, uint regoff, uint mask, uint val)
{
	uint origidx = 0;
	uint32 *r = NULL;
	uint w;
	uint intr_val = 0;
	bool fast = FALSE;
	si_info_t *sii;

	sii = SI_INFO(sih);

	ASSERT(GOODIDX(coreidx));
	ASSERT(regoff < SI_CORE_SIZE);
	ASSERT((val & ~mask) == 0);

	if (coreidx >= SI_MAXCORES)
		return 0;

	if (BUSTYPE(sii->pub.bustype) == SI_BUS) {
		/* If internal bus, we can always get at everything */
		fast = TRUE;
		/* map if does not exist */
		if (!sii->common_info->regs[coreidx]) {
			sii->common_info->regs[coreidx] =
			    REG_MAP(sii->common_info->coresba[coreidx], SI_CORE_SIZE);
			ASSERT(GOODREGS(sii->common_info->regs[coreidx]));
		}
		r = (uint32 *)((uchar *)sii->common_info->regs[coreidx] + regoff);
	} else if (BUSTYPE(sii->pub.bustype) == PCI_BUS) {
		/* If pci/pcie, we can get at pci/pcie regs and on newer cores to chipc */

		if ((sii->common_info->coreid[coreidx] == CC_CORE_ID) && SI_FAST(sii)) {
			/* Chipc registers are mapped at 12KB */

			fast = TRUE;
			r = (uint32 *)((char *)sii->curmap + PCI_16KB0_CCREGS_OFFSET + regoff);
		} else if (sii->pub.buscoreidx == coreidx) {
			/* pci registers are at either in the last 2KB of an 8KB window
			 * or, in pcie and pci rev 13 at 8KB
			 */
			fast = TRUE;
			if (SI_FAST(sii))
				r = (uint32 *)((char *)sii->curmap +
				               PCI_16KB0_PCIREGS_OFFSET + regoff);
			else
				r = (uint32 *)((char *)sii->curmap +
				               ((regoff >= SBCONFIGOFF) ?
				                PCI_BAR0_PCISBR_OFFSET : PCI_BAR0_PCIREGS_OFFSET) +
				               regoff);
		}
	}

	if (!fast) {
		INTR_OFF(sii, intr_val);

		/* save current core index */
		origidx = si_coreidx(&sii->pub);

		/* switch core */
		r = (uint32*) ((uchar*)sb_setcoreidx(&sii->pub, coreidx) + regoff);
	}
	ASSERT(r != NULL);

	/* mask and set */
	if (mask || val) {
		if (regoff >= SBCONFIGOFF) {
			w = (R_SBREG(sii, r) & ~mask) | val;
			W_SBREG(sii, r, w);
		} else {
			w = (R_REG(sii->osh, r) & ~mask) | val;
			W_REG(sii->osh, r, w);
		}
	}

	/* readback */
	if (regoff >= SBCONFIGOFF)
		w = R_SBREG(sii, r);
	else {
		if ((CHIPID(sii->pub.chip) == BCM5354_CHIP_ID) &&
		    (coreidx == SI_CC_IDX) &&
		    (regoff == OFFSETOF(chipcregs_t, watchdog))) {
			w = val;
		} else
			w = R_REG(sii->osh, r);
	}

	if (!fast) {
		/* restore core index */
		if (origidx != coreidx)
			sb_setcoreidx(&sii->pub, origidx);

		INTR_RESTORE(sii, intr_val);
	}

	return (w);
}

/* Scan the enumeration space to find all cores starting from the given
 * bus 'sbba'. Append coreid and other info to the lists in 'si'. 'sba'
 * is the default core address at chip POR time and 'regs' is the virtual
 * address that the default core is mapped at. 'ncores' is the number of
 * cores expected on bus 'sbba'. It returns the total number of cores
 * starting from bus 'sbba', inclusive.
 */
#define SB_MAXBUSES	2
static uint
_sb_scan(si_info_t *sii, uint32 sba, void *regs, uint bus, uint32 sbba, uint numcores)
{
	uint next;
	uint ncc = 0;
	uint i;

	if (bus >= SB_MAXBUSES) {
		SI_ERROR(("_sb_scan: bus 0x%08x at level %d is too deep to scan\n", sbba, bus));
		return 0;
	}
	SI_MSG(("_sb_scan: scan bus 0x%08x assume %u cores\n", sbba, numcores));

	/* Scan all cores on the bus starting from core 0.
	 * Core addresses must be contiguous on each bus.
	 */
	for (i = 0, next = sii->numcores; i < numcores && next < SB_BUS_MAXCORES; i++, next++) {
		sii->common_info->coresba[next] = sbba + (i * SI_CORE_SIZE);

		/* keep and reuse the initial register mapping */
		if ((BUSTYPE(sii->pub.bustype) == SI_BUS) &&
			(sii->common_info->coresba[next] == sba)) {
			SI_MSG(("_sb_scan: reuse mapped regs %p for core %u\n", regs, next));
			sii->common_info->regs[next] = regs;
		}

		/* change core to 'next' and read its coreid */
		sii->curmap = _sb_setcoreidx(sii, next);
		sii->curidx = next;

		sii->common_info->coreid[next] = sb_coreid(&sii->pub);

		/* core specific processing... */
		/* chipc provides # cores */
		if (sii->common_info->coreid[next] == CC_CORE_ID) {
			chipcregs_t *cc = (chipcregs_t *)sii->curmap;
			uint32 ccrev = sb_corerev(&sii->pub);

			/* determine numcores - this is the total # cores in the chip */
			if (((ccrev == 4) || (ccrev >= 6)))
				numcores = (R_REG(sii->osh, &cc->chipid) & CID_CC_MASK) >>
				        CID_CC_SHIFT;
			else {
				/* Older chips */
				uint chip = sii->pub.chip;

				if (chip == BCM4306_CHIP_ID)	/* < 4306c0 */
					numcores = 6;
				else if (chip == BCM4704_CHIP_ID)
					numcores = 9;
				else if (chip == BCM5365_CHIP_ID)
					numcores = 7;
				else {
					SI_ERROR(("sb_chip2numcores: unsupported chip 0x%x\n",
					          chip));
					ASSERT(0);
					numcores = 1;
				}
			}
			SI_MSG(("_sb_scan: there are %u cores in the chip %s\n", numcores,
				sii->pub.issim ? "QT" : ""));
		}
		/* scan bridged SB(s) and add results to the end of the list */
		else if (sii->common_info->coreid[next] == OCP_CORE_ID) {
			sbconfig_t *sb = REGS2SB(sii->curmap);
			uint32 nsbba = R_SBREG(sii, &sb->sbadmatch1);
			uint nsbcc;

			sii->numcores = next + 1;

			if ((nsbba & 0xfff00000) != SI_ENUM_BASE)
				continue;
			nsbba &= 0xfffff000;
			if (_sb_coreidx(sii, nsbba) != BADIDX)
				continue;

			nsbcc = (R_SBREG(sii, &sb->sbtmstatehigh) & 0x000f0000) >> 16;
			nsbcc = _sb_scan(sii, sba, regs, bus + 1, nsbba, nsbcc);
			if (sbba == SI_ENUM_BASE)
				numcores -= nsbcc;
			ncc += nsbcc;
		}
	}

	SI_MSG(("_sb_scan: found %u cores on bus 0x%08x\n", i, sbba));

	sii->numcores = i + ncc;
	return sii->numcores;
}

/* scan the sb enumerated space to identify all cores */
void
sb_scan(si_t *sih, void *regs, uint devid)
{
	si_info_t *sii;
	uint32 origsba;

	sii = SI_INFO(sih);

	/* Save the current core info and validate it later till we know
	 * for sure what is good and what is bad.
	 */
	origsba = _sb_coresba(sii);

	/* scan all SB(s) starting from SI_ENUM_BASE */
	sii->numcores = _sb_scan(sii, origsba, regs, 0, SI_ENUM_BASE, 1);
}

/*
 * This function changes logical "focus" to the indicated core;
 * must be called with interrupts off.
 * Moreover, callers should keep interrupts off during switching out of and back to d11 core
 */
void *
sb_setcoreidx(si_t *sih, uint coreidx)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	if (coreidx >= sii->numcores)
		return (NULL);

	/*
	 * If the user has provided an interrupt mask enabled function,
	 * then assert interrupts are disabled before switching the core.
	 */
	ASSERT((sii->intrsenabled_fn == NULL) || !(*(sii)->intrsenabled_fn)((sii)->intr_arg));

	sii->curmap = _sb_setcoreidx(sii, coreidx);
	sii->curidx = coreidx;

	return (sii->curmap);
}

/* This function changes the logical "focus" to the indicated core.
 * Return the current core's virtual address.
 */
static void *
_sb_setcoreidx(si_info_t *sii, uint coreidx)
{
	uint32 sbaddr = sii->common_info->coresba[coreidx];
	void *regs;

	switch (BUSTYPE(sii->pub.bustype)) {
	case SI_BUS:
		/* map new one */
		if (!sii->common_info->regs[coreidx]) {
			sii->common_info->regs[coreidx] = REG_MAP(sbaddr, SI_CORE_SIZE);
			ASSERT(GOODREGS(sii->common_info->regs[coreidx]));
		}
		regs = sii->common_info->regs[coreidx];
		break;

	case PCI_BUS:
		/* point bar0 window */
		OSL_PCI_WRITE_CONFIG(sii->osh, PCI_BAR0_WIN, 4, sbaddr);
		regs = sii->curmap;
		break;

	case PCMCIA_BUS: {
		uint8 tmp = (sbaddr >> 12) & 0x0f;
		OSL_PCMCIA_WRITE_ATTR(sii->osh, PCMCIA_ADDR0, &tmp, 1);
		tmp = (sbaddr >> 16) & 0xff;
		OSL_PCMCIA_WRITE_ATTR(sii->osh, PCMCIA_ADDR1, &tmp, 1);
		tmp = (sbaddr >> 24) & 0xff;
		OSL_PCMCIA_WRITE_ATTR(sii->osh, PCMCIA_ADDR2, &tmp, 1);
		regs = sii->curmap;
		break;
	}
	case SPI_BUS:
	case SDIO_BUS:
		/* map new one */
		if (!sii->common_info->regs[coreidx]) {
			sii->common_info->regs[coreidx] = (void *)(uintptr)sbaddr;
			ASSERT(GOODREGS(sii->common_info->regs[coreidx]));
		}
		regs = sii->common_info->regs[coreidx];
		break;


	default:
		ASSERT(0);
		regs = NULL;
		break;
	}

	return regs;
}

/* Return the address of sbadmatch0/1/2/3 register */
static volatile uint32 *
sb_admatch(si_info_t *sii, uint asidx)
{
	sbconfig_t *sb;
	volatile uint32 *addrm;

	sb = REGS2SB(sii->curmap);

	switch (asidx) {
	case 0:
		addrm =  &sb->sbadmatch0;
		break;

	case 1:
		addrm =  &sb->sbadmatch1;
		break;

	case 2:
		addrm =  &sb->sbadmatch2;
		break;

	case 3:
		addrm =  &sb->sbadmatch3;
		break;

	default:
		SI_ERROR(("%s: Address space index (%d) out of range\n", __FUNCTION__, asidx));
		return 0;
	}

	return (addrm);
}

/* Return the number of address spaces in current core */
int
sb_numaddrspaces(si_t *sih)
{
	si_info_t *sii;
	sbconfig_t *sb;

	sii = SI_INFO(sih);
	sb = REGS2SB(sii->curmap);

	/* + 1 because of enumeration space */
	return ((R_SBREG(sii, &sb->sbidlow) & SBIDL_AR_MASK) >> SBIDL_AR_SHIFT) + 1;
}

/* Return the address of the nth address space in the current core */
uint32
sb_addrspace(si_t *sih, uint asidx)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	return (sb_base(R_SBREG(sii, sb_admatch(sii, asidx))));
}

/* Return the size of the nth address space in the current core */
uint32
sb_addrspacesize(si_t *sih, uint asidx)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	return (sb_size(R_SBREG(sii, sb_admatch(sii, asidx))));
}


/* do buffered registers update */
void
sb_commit(si_t *sih)
{
	si_info_t *sii;
	uint origidx;
	uint intr_val = 0;

	sii = SI_INFO(sih);

	origidx = sii->curidx;
	ASSERT(GOODIDX(origidx));

	INTR_OFF(sii, intr_val);

	/* switch over to chipcommon core if there is one, else use pci */
	if (sii->pub.ccrev != NOREV) {
		chipcregs_t *ccregs = (chipcregs_t *)si_setcore(sih, CC_CORE_ID, 0);

		/* do the buffer registers update */
		W_REG(sii->osh, &ccregs->broadcastaddress, SB_COMMIT);
		W_REG(sii->osh, &ccregs->broadcastdata, 0x0);
	} else
		ASSERT(0);

	/* restore core index */
	sb_setcoreidx(sih, origidx);
	INTR_RESTORE(sii, intr_val);
}

void
sb_core_disable(si_t *sih, uint32 bits)
{
	si_info_t *sii;
	volatile uint32 dummy;
	sbconfig_t *sb;

	sii = SI_INFO(sih);

	ASSERT(GOODREGS(sii->curmap));
	sb = REGS2SB(sii->curmap);

	/* if core is already in reset, just return */
	if (R_SBREG(sii, &sb->sbtmstatelow) & SBTML_RESET)
		return;

	/* if clocks are not enabled, put into reset and return */
	if ((R_SBREG(sii, &sb->sbtmstatelow) & (SICF_CLOCK_EN << SBTML_SICF_SHIFT)) == 0)
		goto disable;

	/* set target reject and spin until busy is clear (preserve core-specific bits) */
	OR_SBREG(sii, &sb->sbtmstatelow, SBTML_REJ);
	dummy = R_SBREG(sii, &sb->sbtmstatelow);
	OSL_DELAY(1);
	SPINWAIT((R_SBREG(sii, &sb->sbtmstatehigh) & SBTMH_BUSY), 100000);
	if (R_SBREG(sii, &sb->sbtmstatehigh) & SBTMH_BUSY)
		SI_ERROR(("%s: target state still busy\n", __FUNCTION__));

	if (R_SBREG(sii, &sb->sbidlow) & SBIDL_INIT) {
		OR_SBREG(sii, &sb->sbimstate, SBIM_RJ);
		dummy = R_SBREG(sii, &sb->sbimstate);
		OSL_DELAY(1);
		SPINWAIT((R_SBREG(sii, &sb->sbimstate) & SBIM_BY), 100000);
	}

	/* set reset and reject while enabling the clocks */
	W_SBREG(sii, &sb->sbtmstatelow,
	        (((bits | SICF_FGC | SICF_CLOCK_EN) << SBTML_SICF_SHIFT) |
	         SBTML_REJ | SBTML_RESET));
	dummy = R_SBREG(sii, &sb->sbtmstatelow);
	OSL_DELAY(10);

	/* don't forget to clear the initiator reject bit */
	if (R_SBREG(sii, &sb->sbidlow) & SBIDL_INIT)
		AND_SBREG(sii, &sb->sbimstate, ~SBIM_RJ);

disable:
	/* leave reset and reject asserted */
	W_SBREG(sii, &sb->sbtmstatelow, ((bits << SBTML_SICF_SHIFT) | SBTML_REJ | SBTML_RESET));
	OSL_DELAY(1);
}

/* reset and re-enable a core
 * inputs:
 * bits - core specific bits that are set during and after reset sequence
 * resetbits - core specific bits that are set only during reset sequence
 */
void
sb_core_reset(si_t *sih, uint32 bits, uint32 resetbits)
{
	si_info_t *sii;
	sbconfig_t *sb;
	volatile uint32 dummy;

	sii = SI_INFO(sih);
	ASSERT(GOODREGS(sii->curmap));
	sb = REGS2SB(sii->curmap);

	/*
	 * Must do the disable sequence first to work for arbitrary current core state.
	 */
	sb_core_disable(sih, (bits | resetbits));

	/*
	 * Now do the initialization sequence.
	 */

	/* set reset while enabling the clock and forcing them on throughout the core */
	W_SBREG(sii, &sb->sbtmstatelow,
	        (((bits | resetbits | SICF_FGC | SICF_CLOCK_EN) << SBTML_SICF_SHIFT) |
	         SBTML_RESET));
	dummy = R_SBREG(sii, &sb->sbtmstatelow);
	OSL_DELAY(1);

	if (R_SBREG(sii, &sb->sbtmstatehigh) & SBTMH_SERR) {
		W_SBREG(sii, &sb->sbtmstatehigh, 0);
	}
	if ((dummy = R_SBREG(sii, &sb->sbimstate)) & (SBIM_IBE | SBIM_TO)) {
		AND_SBREG(sii, &sb->sbimstate, ~(SBIM_IBE | SBIM_TO));
	}

	/* clear reset and allow it to propagate throughout the core */
	W_SBREG(sii, &sb->sbtmstatelow,
	        ((bits | resetbits | SICF_FGC | SICF_CLOCK_EN) << SBTML_SICF_SHIFT));
	dummy = R_SBREG(sii, &sb->sbtmstatelow);
	OSL_DELAY(1);

	/* leave clock enabled */
	W_SBREG(sii, &sb->sbtmstatelow, ((bits | SICF_CLOCK_EN) << SBTML_SICF_SHIFT));
	dummy = R_SBREG(sii, &sb->sbtmstatelow);
	OSL_DELAY(1);
}

void
sb_core_tofixup(si_t *sih)
{
	si_info_t *sii;
	sbconfig_t *sb;

	sii = SI_INFO(sih);

	if ((BUSTYPE(sii->pub.bustype) != PCI_BUS) || PCIE(sii) ||
	    (PCI(sii) && (sii->pub.buscorerev >= 5)))
		return;

	ASSERT(GOODREGS(sii->curmap));
	sb = REGS2SB(sii->curmap);

	if (BUSTYPE(sii->pub.bustype) == SI_BUS) {
		SET_SBREG(sii, &sb->sbimconfiglow,
		          SBIMCL_RTO_MASK | SBIMCL_STO_MASK,
		          (0x5 << SBIMCL_RTO_SHIFT) | 0x3);
	} else {
		if (sb_coreid(sih) == PCI_CORE_ID) {
			SET_SBREG(sii, &sb->sbimconfiglow,
			          SBIMCL_RTO_MASK | SBIMCL_STO_MASK,
			          (0x3 << SBIMCL_RTO_SHIFT) | 0x2);
		} else {
			SET_SBREG(sii, &sb->sbimconfiglow, (SBIMCL_RTO_MASK | SBIMCL_STO_MASK), 0);
		}
	}

	sb_commit(sih);
}

/*
 * Set the initiator timeout for the "master core".
 * The master core is defined to be the core in control
 * of the chip and so it issues accesses to non-memory
 * locations (Because of dma *any* core can access memeory).
 *
 * The routine uses the bus to decide who is the master:
 *	SI_BUS => mips
 *	JTAG_BUS => chipc
 *	PCI_BUS => pci or pcie
 *	PCMCIA_BUS => pcmcia
 *	SDIO_BUS => pcmcia
 *
 * This routine exists so callers can disable initiator
 * timeouts so accesses to very slow devices like otp
 * won't cause an abort. The routine allows arbitrary
 * settings of the service and request timeouts, though.
 *
 * Returns the timeout state before changing it or -1
 * on error.
 */

#define	TO_MASK	(SBIMCL_RTO_MASK | SBIMCL_STO_MASK)

uint32
sb_set_initiator_to(si_t *sih, uint32 to, uint idx)
{
	si_info_t *sii;
	uint origidx;
	uint intr_val = 0;
	uint32 tmp, ret = 0xffffffff;
	sbconfig_t *sb;

	sii = SI_INFO(sih);

	if ((to & ~TO_MASK) != 0)
		return ret;

	/* Figure out the master core */
	if (idx == BADIDX) {
		switch (BUSTYPE(sii->pub.bustype)) {
		case PCI_BUS:
			idx = sii->pub.buscoreidx;
			break;
		case JTAG_BUS:
			idx = SI_CC_IDX;
			break;
		case PCMCIA_BUS:
		case SDIO_BUS:
			idx = si_findcoreidx(sih, PCMCIA_CORE_ID, 0);
			break;
		case SI_BUS:
			idx = si_findcoreidx(sih, MIPS33_CORE_ID, 0);
			break;
		default:
			ASSERT(0);
		}
		if (idx == BADIDX)
			return ret;
	}

	INTR_OFF(sii, intr_val);
	origidx = si_coreidx(sih);

	sb = REGS2SB(sb_setcoreidx(sih, idx));

	tmp = R_SBREG(sii, &sb->sbimconfiglow);
	ret = tmp & TO_MASK;
	W_SBREG(sii, &sb->sbimconfiglow, (tmp & ~TO_MASK) | to);

	sb_commit(sih);
	sb_setcoreidx(sih, origidx);
	INTR_RESTORE(sii, intr_val);
	return ret;
}

uint32
sb_base(uint32 admatch)
{
	uint32 base;
	uint type;

	type = admatch & SBAM_TYPE_MASK;
	ASSERT(type < 3);

	base = 0;

	if (type == 0) {
		base = admatch & SBAM_BASE0_MASK;
	} else if (type == 1) {
		ASSERT(!(admatch & SBAM_ADNEG));	/* neg not supported */
		base = admatch & SBAM_BASE1_MASK;
	} else if (type == 2) {
		ASSERT(!(admatch & SBAM_ADNEG));	/* neg not supported */
		base = admatch & SBAM_BASE2_MASK;
	}

	return (base);
}

uint32
sb_size(uint32 admatch)
{
	uint32 size;
	uint type;

	type = admatch & SBAM_TYPE_MASK;
	ASSERT(type < 3);

	size = 0;

	if (type == 0) {
		size = 1 << (((admatch & SBAM_ADINT0_MASK) >> SBAM_ADINT0_SHIFT) + 1);
	} else if (type == 1) {
		ASSERT(!(admatch & SBAM_ADNEG));	/* neg not supported */
		size = 1 << (((admatch & SBAM_ADINT1_MASK) >> SBAM_ADINT1_SHIFT) + 1);
	} else if (type == 2) {
		ASSERT(!(admatch & SBAM_ADNEG));	/* neg not supported */
		size = 1 << (((admatch & SBAM_ADINT2_MASK) >> SBAM_ADINT2_SHIFT) + 1);
	}

	return (size);
}
