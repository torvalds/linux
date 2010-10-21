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

#include <linux/types.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <siutils.h>
#include <bcmdevs.h>
#include <hndsoc.h>
#include <sbchipc.h>
#include <pci_core.h>
#include <pcicfg.h>
#include <sbpcmcia.h>
#include "siutils_priv.h"

/* local prototypes */
static uint _sb_coreidx(si_info_t *sii, u32 sba);
static uint _sb_scan(si_info_t *sii, u32 sba, void *regs, uint bus,
		     u32 sbba, uint ncores);
static u32 _sb_coresba(si_info_t *sii);
static void *_sb_setcoreidx(si_info_t *sii, uint coreidx);

#define	SET_SBREG(sii, r, mask, val)	\
		W_SBREG((sii), (r), ((R_SBREG((sii), (r)) & ~(mask)) | (val)))
#define	REGS2SB(va)	(sbconfig_t *) ((s8 *)(va) + SBCONFIGOFF)

/* sonicsrev */
#define	SONICS_2_2	(SBIDL_RV_2_2 >> SBIDL_RV_SHIFT)
#define	SONICS_2_3	(SBIDL_RV_2_3 >> SBIDL_RV_SHIFT)

#define	R_SBREG(sii, sbr)	sb_read_sbreg((sii), (sbr))
#define	W_SBREG(sii, sbr, v)	sb_write_sbreg((sii), (sbr), (v))
#define	AND_SBREG(sii, sbr, v)	\
	W_SBREG((sii), (sbr), (R_SBREG((sii), (sbr)) & (v)))
#define	OR_SBREG(sii, sbr, v)	\
	W_SBREG((sii), (sbr), (R_SBREG((sii), (sbr)) | (v)))

static u32 sb_read_sbreg(si_info_t *sii, volatile u32 *sbr)
{
	return R_REG(sii->osh, sbr);
}

static void sb_write_sbreg(si_info_t *sii, volatile u32 *sbr, u32 v)
{
	W_REG(sii->osh, sbr, v);
}

uint sb_coreid(si_t *sih)
{
	si_info_t *sii;
	sbconfig_t *sb;

	sii = SI_INFO(sih);
	sb = REGS2SB(sii->curmap);

	return (R_SBREG(sii, &sb->sbidhigh) & SBIDH_CC_MASK) >>
		SBIDH_CC_SHIFT;
}

/* return core index of the core with address 'sba' */
static uint _sb_coreidx(si_info_t *sii, u32 sba)
{
	uint i;

	for (i = 0; i < sii->numcores; i++)
		if (sba == sii->coresba[i])
			return i;
	return BADIDX;
}

/* return core address of the current core */
static u32 _sb_coresba(si_info_t *sii)
{
	u32 sbaddr = 0;

	switch (BUSTYPE(sii->pub.bustype)) {
	case SPI_BUS:
	case SDIO_BUS:
		sbaddr = (u32)(unsigned long)sii->curmap;
		break;
	default:
		ASSERT(0);
		break;
	}

	return sbaddr;
}

uint sb_corerev(si_t *sih)
{
	si_info_t *sii;
	sbconfig_t *sb;
	uint sbidh;

	sii = SI_INFO(sih);
	sb = REGS2SB(sii->curmap);
	sbidh = R_SBREG(sii, &sb->sbidhigh);

	return SBCOREREV(sbidh);
}

bool sb_iscoreup(si_t *sih)
{
	si_info_t *sii;
	sbconfig_t *sb;

	sii = SI_INFO(sih);
	sb = REGS2SB(sii->curmap);

	return (R_SBREG(sii, &sb->sbtmstatelow) &
		 (SBTML_RESET | SBTML_REJ_MASK |
		  (SICF_CLOCK_EN << SBTML_SICF_SHIFT))) ==
		(SICF_CLOCK_EN << SBTML_SICF_SHIFT);
}

/*
 * Switch to 'coreidx', issue a single arbitrary 32bit
 * register mask&set operation,
 * switch back to the original core, and return the new value.
 *
 * When using the silicon backplane, no fidleing with interrupts
 * or core switches are needed.
 *
 * Also, when using pci/pcie, we can optimize away the core switching
 * for pci registers
 * and (on newer pci cores) chipcommon registers.
 */
uint sb_corereg(si_t *sih, uint coreidx, uint regoff, uint mask, uint val)
{
	uint origidx = 0;
	u32 *r = NULL;
	uint w;
	uint intr_val = 0;
	bool fast = false;
	si_info_t *sii;

	sii = SI_INFO(sih);

	ASSERT(GOODIDX(coreidx));
	ASSERT(regoff < SI_CORE_SIZE);
	ASSERT((val & ~mask) == 0);

	if (coreidx >= SI_MAXCORES)
		return 0;

	if (!fast) {
		INTR_OFF(sii, intr_val);

		/* save current core index */
		origidx = si_coreidx(&sii->pub);

		/* switch core */
		r = (u32 *) ((unsigned char *) sb_setcoreidx(&sii->pub, coreidx) +
				regoff);
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
	else
		w = R_REG(sii->osh, r);

	if (!fast) {
		/* restore core index */
		if (origidx != coreidx)
			sb_setcoreidx(&sii->pub, origidx);

		INTR_RESTORE(sii, intr_val);
	}

	return w;
}

/* Scan the enumeration space to find all cores starting from the given
 * bus 'sbba'. Append coreid and other info to the lists in 'si'. 'sba'
 * is the default core address at chip POR time and 'regs' is the virtual
 * address that the default core is mapped at. 'ncores' is the number of
 * cores expected on bus 'sbba'. It returns the total number of cores
 * starting from bus 'sbba', inclusive.
 */
#define SB_MAXBUSES	2
static uint _sb_scan(si_info_t *sii, u32 sba, void *regs, uint bus, u32 sbba,
		     uint numcores)
{
	uint next;
	uint ncc = 0;
	uint i;

	if (bus >= SB_MAXBUSES) {
		SI_ERROR(("_sb_scan: bus 0x%08x at level %d is too deep to "
			"scan\n", sbba, bus));
		return 0;
	}
	SI_MSG(("_sb_scan: scan bus 0x%08x assume %u cores\n",
		sbba, numcores));

	/* Scan all cores on the bus starting from core 0.
	 * Core addresses must be contiguous on each bus.
	 */
	for (i = 0, next = sii->numcores;
	     i < numcores && next < SB_BUS_MAXCORES; i++, next++) {
		sii->coresba[next] = sbba + (i * SI_CORE_SIZE);

		/* change core to 'next' and read its coreid */
		sii->curmap = _sb_setcoreidx(sii, next);
		sii->curidx = next;

		sii->coreid[next] = sb_coreid(&sii->pub);

		/* core specific processing... */
		/* chipc provides # cores */
		if (sii->coreid[next] == CC_CORE_ID) {
			chipcregs_t *cc = (chipcregs_t *) sii->curmap;
			u32 ccrev = sb_corerev(&sii->pub);

			/* determine numcores - this is the
				 total # cores in the chip */
			if (((ccrev == 4) || (ccrev >= 6)))
				numcores =
				    (R_REG(sii->osh, &cc->chipid) & CID_CC_MASK)
				    >> CID_CC_SHIFT;
			else {
				/* Older chips */
				SI_ERROR(("sb_chip2numcores: unsupported chip "
					"0x%x\n", CHIPID(sii->pub.chip)));
				ASSERT(0);
				numcores = 1;
			}

			SI_VMSG(("_sb_scan: %u cores in the chip %s\n",
			numcores, sii->pub.issim ? "QT" : ""));
		}
		/* scan bridged SB(s) and add results to the end of the list */
		else if (sii->coreid[next] == OCP_CORE_ID) {
			sbconfig_t *sb = REGS2SB(sii->curmap);
			u32 nsbba = R_SBREG(sii, &sb->sbadmatch1);
			uint nsbcc;

			sii->numcores = next + 1;

			if ((nsbba & 0xfff00000) != SI_ENUM_BASE)
				continue;
			nsbba &= 0xfffff000;
			if (_sb_coreidx(sii, nsbba) != BADIDX)
				continue;

			nsbcc =
			    (R_SBREG(sii, &sb->sbtmstatehigh) & 0x000f0000) >>
			    16;
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
void sb_scan(si_t *sih, void *regs, uint devid)
{
	si_info_t *sii;
	u32 origsba;
	sbconfig_t *sb;

	sii = SI_INFO(sih);
	sb = REGS2SB(sii->curmap);

	sii->pub.socirev =
	    (R_SBREG(sii, &sb->sbidlow) & SBIDL_RV_MASK) >> SBIDL_RV_SHIFT;

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
 * Moreover, callers should keep interrupts off during switching out of
 * and back to d11 core
 */
void *sb_setcoreidx(si_t *sih, uint coreidx)
{
	si_info_t *sii;

	sii = SI_INFO(sih);

	if (coreidx >= sii->numcores)
		return NULL;

	/*
	 * If the user has provided an interrupt mask enabled function,
	 * then assert interrupts are disabled before switching the core.
	 */
	ASSERT((sii->intrsenabled_fn == NULL)
	       || !(*(sii)->intrsenabled_fn) ((sii)->intr_arg));

	sii->curmap = _sb_setcoreidx(sii, coreidx);
	sii->curidx = coreidx;

	return sii->curmap;
}

/* This function changes the logical "focus" to the indicated core.
 * Return the current core's virtual address.
 */
static void *_sb_setcoreidx(si_info_t *sii, uint coreidx)
{
	u32 sbaddr = sii->coresba[coreidx];
	void *regs;

	switch (BUSTYPE(sii->pub.bustype)) {
#ifdef BCMSDIO
	case SPI_BUS:
	case SDIO_BUS:
		/* map new one */
		if (!sii->regs[coreidx]) {
			sii->regs[coreidx] = (void *)sbaddr;
			ASSERT(GOODREGS(sii->regs[coreidx]));
		}
		regs = sii->regs[coreidx];
		break;
#endif				/* BCMSDIO */
	default:
		ASSERT(0);
		regs = NULL;
		break;
	}

	return regs;
}

/* traverse all cores to find and clear source of serror */
static void sb_serr_clear(si_info_t *sii)
{
	sbconfig_t *sb;
	uint origidx;
	uint i, intr_val = 0;
	void *corereg = NULL;

	INTR_OFF(sii, intr_val);
	origidx = si_coreidx(&sii->pub);

	for (i = 0; i < sii->numcores; i++) {
		corereg = sb_setcoreidx(&sii->pub, i);
		if (NULL != corereg) {
			sb = REGS2SB(corereg);
			if ((R_SBREG(sii, &sb->sbtmstatehigh)) & SBTMH_SERR) {
				AND_SBREG(sii, &sb->sbtmstatehigh, ~SBTMH_SERR);
				SI_ERROR(("sb_serr_clear: SError core 0x%x\n",
				sb_coreid(&sii->pub)));
			}
		}
	}

	sb_setcoreidx(&sii->pub, origidx);
	INTR_RESTORE(sii, intr_val);
}

/*
 * Check if any inband, outband or timeout errors has happened and clear them.
 * Must be called with chip clk on !
 */
bool sb_taclear(si_t *sih, bool details)
{
	si_info_t *sii;
	sbconfig_t *sb;
	uint origidx;
	uint intr_val = 0;
	bool rc = false;
	u32 inband = 0, serror = 0, timeout = 0;
	void *corereg = NULL;
	volatile u32 imstate, tmstate;

	sii = SI_INFO(sih);

	if ((BUSTYPE(sii->pub.bustype) == SDIO_BUS) ||
	    (BUSTYPE(sii->pub.bustype) == SPI_BUS)) {

		INTR_OFF(sii, intr_val);
		origidx = si_coreidx(sih);

		corereg = si_setcore(sih, PCMCIA_CORE_ID, 0);
		if (NULL == corereg)
			corereg = si_setcore(sih, SDIOD_CORE_ID, 0);
		if (NULL != corereg) {
			sb = REGS2SB(corereg);

			imstate = R_SBREG(sii, &sb->sbimstate);
			if ((imstate != 0xffffffff)
			    && (imstate & (SBIM_IBE | SBIM_TO))) {
				AND_SBREG(sii, &sb->sbimstate,
					  ~(SBIM_IBE | SBIM_TO));
				/* inband = imstate & SBIM_IBE; cmd error */
				timeout = imstate & SBIM_TO;
			}
			tmstate = R_SBREG(sii, &sb->sbtmstatehigh);
			if ((tmstate != 0xffffffff)
			    && (tmstate & SBTMH_INT_STATUS)) {
				sb_serr_clear(sii);
				serror = 1;
				OR_SBREG(sii, &sb->sbtmstatelow, SBTML_INT_ACK);
				AND_SBREG(sii, &sb->sbtmstatelow,
					  ~SBTML_INT_ACK);
			}
		}

		sb_setcoreidx(sih, origidx);
		INTR_RESTORE(sii, intr_val);
	}

	if (inband | timeout | serror) {
		rc = true;
		SI_ERROR(("sb_taclear: inband 0x%x, serror 0x%x, timeout "
			"0x%x!\n", inband, serror, timeout));
	}

	return rc;
}

void sb_core_disable(si_t *sih, u32 bits)
{
	si_info_t *sii;
	volatile u32 dummy;
	sbconfig_t *sb;

	sii = SI_INFO(sih);

	ASSERT(GOODREGS(sii->curmap));
	sb = REGS2SB(sii->curmap);

	/* if core is already in reset, just return */
	if (R_SBREG(sii, &sb->sbtmstatelow) & SBTML_RESET)
		return;

	/* if clocks are not enabled, put into reset and return */
	if ((R_SBREG(sii, &sb->sbtmstatelow) &
	     (SICF_CLOCK_EN << SBTML_SICF_SHIFT)) == 0)
		goto disable;

	/* set target reject and spin until busy is clear
	   (preserve core-specific bits) */
	OR_SBREG(sii, &sb->sbtmstatelow, SBTML_REJ);
	dummy = R_SBREG(sii, &sb->sbtmstatelow);
	udelay(1);
	SPINWAIT((R_SBREG(sii, &sb->sbtmstatehigh) & SBTMH_BUSY), 100000);
	if (R_SBREG(sii, &sb->sbtmstatehigh) & SBTMH_BUSY)
		SI_ERROR(("%s: target state still busy\n", __func__));

	if (R_SBREG(sii, &sb->sbidlow) & SBIDL_INIT) {
		OR_SBREG(sii, &sb->sbimstate, SBIM_RJ);
		dummy = R_SBREG(sii, &sb->sbimstate);
		udelay(1);
		SPINWAIT((R_SBREG(sii, &sb->sbimstate) & SBIM_BY), 100000);
	}

	/* set reset and reject while enabling the clocks */
	W_SBREG(sii, &sb->sbtmstatelow,
		(((bits | SICF_FGC | SICF_CLOCK_EN) << SBTML_SICF_SHIFT) |
		 SBTML_REJ | SBTML_RESET));
	dummy = R_SBREG(sii, &sb->sbtmstatelow);
	udelay(10);

	/* don't forget to clear the initiator reject bit */
	if (R_SBREG(sii, &sb->sbidlow) & SBIDL_INIT)
		AND_SBREG(sii, &sb->sbimstate, ~SBIM_RJ);

disable:
	/* leave reset and reject asserted */
	W_SBREG(sii, &sb->sbtmstatelow,
		((bits << SBTML_SICF_SHIFT) | SBTML_REJ | SBTML_RESET));
	udelay(1);
}

/* reset and re-enable a core
 * inputs:
 * bits - core specific bits that are set during and after reset sequence
 * resetbits - core specific bits that are set only during reset sequence
 */
void sb_core_reset(si_t *sih, u32 bits, u32 resetbits)
{
	si_info_t *sii;
	sbconfig_t *sb;
	volatile u32 dummy;

	sii = SI_INFO(sih);
	ASSERT(GOODREGS(sii->curmap));
	sb = REGS2SB(sii->curmap);

	/*
	 * Must do the disable sequence first to work for
	 * arbitrary current core state.
	 */
	sb_core_disable(sih, (bits | resetbits));

	/*
	 * Now do the initialization sequence.
	 */

	/* set reset while enabling the clock and
		 forcing them on throughout the core */
	W_SBREG(sii, &sb->sbtmstatelow,
		(((bits | resetbits | SICF_FGC | SICF_CLOCK_EN) <<
		  SBTML_SICF_SHIFT) | SBTML_RESET));
	dummy = R_SBREG(sii, &sb->sbtmstatelow);
	udelay(1);

	if (R_SBREG(sii, &sb->sbtmstatehigh) & SBTMH_SERR)
		W_SBREG(sii, &sb->sbtmstatehigh, 0);

	dummy = R_SBREG(sii, &sb->sbimstate);
	if (dummy & (SBIM_IBE | SBIM_TO))
		AND_SBREG(sii, &sb->sbimstate, ~(SBIM_IBE | SBIM_TO));

	/* clear reset and allow it to propagate throughout the core */
	W_SBREG(sii, &sb->sbtmstatelow,
		((bits | resetbits | SICF_FGC | SICF_CLOCK_EN) <<
		 SBTML_SICF_SHIFT));
	dummy = R_SBREG(sii, &sb->sbtmstatelow);
	udelay(1);

	/* leave clock enabled */
	W_SBREG(sii, &sb->sbtmstatelow,
		((bits | SICF_CLOCK_EN) << SBTML_SICF_SHIFT));
	dummy = R_SBREG(sii, &sb->sbtmstatelow);
	udelay(1);
}

u32 sb_base(u32 admatch)
{
	u32 base;
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

	return base;
}
