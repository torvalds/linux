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

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <bcmdefs.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <bcmutils.h>
#include <siutils.h>
#include <hndsoc.h>
#include <sbchipc.h>
#include <pcicfg.h>
#include <bcmdevs.h>

#define BCM47162_DMP() ((sih->chip == BCM47162_CHIP_ID) && \
		(sih->chiprev == 0) && \
		(sii->coreid[sii->curidx] == MIPS74K_CORE_ID))

/* EROM parsing */

static u32
get_erom_ent(si_t *sih, u32 **eromptr, u32 mask, u32 match)
{
	u32 ent;
	uint inv = 0, nom = 0;

	while (true) {
		ent = R_REG(*eromptr);
		(*eromptr)++;

		if (mask == 0)
			break;

		if ((ent & ER_VALID) == 0) {
			inv++;
			continue;
		}

		if (ent == (ER_END | ER_VALID))
			break;

		if ((ent & mask) == match)
			break;

		nom++;
	}

	SI_VMSG(("%s: Returning ent 0x%08x\n", __func__, ent));
	if (inv + nom) {
		SI_VMSG(("  after %d invalid and %d non-matching entries\n",
			 inv, nom));
	}
	return ent;
}

static u32
get_asd(si_t *sih, u32 **eromptr, uint sp, uint ad, uint st,
	u32 *addrl, u32 *addrh, u32 *sizel, u32 *sizeh)
{
	u32 asd, sz, szd;

	asd = get_erom_ent(sih, eromptr, ER_VALID, ER_VALID);
	if (((asd & ER_TAG1) != ER_ADD) ||
	    (((asd & AD_SP_MASK) >> AD_SP_SHIFT) != sp) ||
	    ((asd & AD_ST_MASK) != st)) {
		/* This is not what we want, "push" it back */
		(*eromptr)--;
		return 0;
	}
	*addrl = asd & AD_ADDR_MASK;
	if (asd & AD_AG32)
		*addrh = get_erom_ent(sih, eromptr, 0, 0);
	else
		*addrh = 0;
	*sizeh = 0;
	sz = asd & AD_SZ_MASK;
	if (sz == AD_SZ_SZD) {
		szd = get_erom_ent(sih, eromptr, 0, 0);
		*sizel = szd & SD_SZ_MASK;
		if (szd & SD_SG32)
			*sizeh = get_erom_ent(sih, eromptr, 0, 0);
	} else
		*sizel = AD_SZ_BASE << (sz >> AD_SZ_SHIFT);

	SI_VMSG(("  SP %d, ad %d: st = %d, 0x%08x_0x%08x @ 0x%08x_0x%08x\n",
		 sp, ad, st, *sizeh, *sizel, *addrh, *addrl));

	return asd;
}

static void ai_hwfixup(si_info_t *sii)
{
}

/* parse the enumeration rom to identify all cores */
void ai_scan(si_t *sih, void *regs, uint devid)
{
	si_info_t *sii = SI_INFO(sih);
	chipcregs_t *cc = (chipcregs_t *) regs;
	u32 erombase, *eromptr, *eromlim;

	erombase = R_REG(&cc->eromptr);

	switch (sih->bustype) {
	case SI_BUS:
		eromptr = (u32 *) REG_MAP(erombase, SI_CORE_SIZE);
		break;

	case PCI_BUS:
		/* Set wrappers address */
		sii->curwrap = (void *)((unsigned long)regs + SI_CORE_SIZE);

		/* Now point the window at the erom */
		pci_write_config_dword(sii->pbus, PCI_BAR0_WIN, erombase);
		eromptr = regs;
		break;

	case SPI_BUS:
	case SDIO_BUS:
		eromptr = (u32 *)(unsigned long)erombase;
		break;

	default:
		SI_ERROR(("Don't know how to do AXI enumertion on bus %d\n",
			  sih->bustype));
		ASSERT(0);
		return;
	}
	eromlim = eromptr + (ER_REMAPCONTROL / sizeof(u32));

	SI_VMSG(("ai_scan: regs = 0x%p, erombase = 0x%08x, eromptr = 0x%p, eromlim = 0x%p\n", regs, erombase, eromptr, eromlim));
	while (eromptr < eromlim) {
		u32 cia, cib, cid, mfg, crev, nmw, nsw, nmp, nsp;
		u32 mpd, asd, addrl, addrh, sizel, sizeh;
		u32 *base;
		uint i, j, idx;
		bool br;

		br = false;

		/* Grok a component */
		cia = get_erom_ent(sih, &eromptr, ER_TAG, ER_CI);
		if (cia == (ER_END | ER_VALID)) {
			SI_VMSG(("Found END of erom after %d cores\n",
				 sii->numcores));
			ai_hwfixup(sii);
			return;
		}
		base = eromptr - 1;
		cib = get_erom_ent(sih, &eromptr, 0, 0);

		if ((cib & ER_TAG) != ER_CI) {
			SI_ERROR(("CIA not followed by CIB\n"));
			goto error;
		}

		cid = (cia & CIA_CID_MASK) >> CIA_CID_SHIFT;
		mfg = (cia & CIA_MFG_MASK) >> CIA_MFG_SHIFT;
		crev = (cib & CIB_REV_MASK) >> CIB_REV_SHIFT;
		nmw = (cib & CIB_NMW_MASK) >> CIB_NMW_SHIFT;
		nsw = (cib & CIB_NSW_MASK) >> CIB_NSW_SHIFT;
		nmp = (cib & CIB_NMP_MASK) >> CIB_NMP_SHIFT;
		nsp = (cib & CIB_NSP_MASK) >> CIB_NSP_SHIFT;

		SI_VMSG(("Found component 0x%04x/0x%04x rev %d at erom addr 0x%p, with nmw = %d, " "nsw = %d, nmp = %d & nsp = %d\n", mfg, cid, crev, base, nmw, nsw, nmp, nsp));

		if (((mfg == MFGID_ARM) && (cid == DEF_AI_COMP)) || (nsp == 0))
			continue;
		if ((nmw + nsw == 0)) {
			/* A component which is not a core */
			if (cid == OOB_ROUTER_CORE_ID) {
				asd = get_asd(sih, &eromptr, 0, 0, AD_ST_SLAVE,
					      &addrl, &addrh, &sizel, &sizeh);
				if (asd != 0) {
					sii->oob_router = addrl;
				}
			}
			continue;
		}

		idx = sii->numcores;
/*		sii->eromptr[idx] = base; */
		sii->cia[idx] = cia;
		sii->cib[idx] = cib;
		sii->coreid[idx] = cid;

		for (i = 0; i < nmp; i++) {
			mpd = get_erom_ent(sih, &eromptr, ER_VALID, ER_VALID);
			if ((mpd & ER_TAG) != ER_MP) {
				SI_ERROR(("Not enough MP entries for component 0x%x\n", cid));
				goto error;
			}
			SI_VMSG(("  Master port %d, mp: %d id: %d\n", i,
				 (mpd & MPD_MP_MASK) >> MPD_MP_SHIFT,
				 (mpd & MPD_MUI_MASK) >> MPD_MUI_SHIFT));
		}

		/* First Slave Address Descriptor should be port 0:
		 * the main register space for the core
		 */
		asd =
		    get_asd(sih, &eromptr, 0, 0, AD_ST_SLAVE, &addrl, &addrh,
			    &sizel, &sizeh);
		if (asd == 0) {
			/* Try again to see if it is a bridge */
			asd =
			    get_asd(sih, &eromptr, 0, 0, AD_ST_BRIDGE, &addrl,
				    &addrh, &sizel, &sizeh);
			if (asd != 0)
				br = true;
			else if ((addrh != 0) || (sizeh != 0)
				 || (sizel != SI_CORE_SIZE)) {
				SI_ERROR(("First Slave ASD for core 0x%04x malformed " "(0x%08x)\n", cid, asd));
				goto error;
			}
		}
		sii->coresba[idx] = addrl;
		sii->coresba_size[idx] = sizel;
		/* Get any more ASDs in port 0 */
		j = 1;
		do {
			asd =
			    get_asd(sih, &eromptr, 0, j, AD_ST_SLAVE, &addrl,
				    &addrh, &sizel, &sizeh);
			if ((asd != 0) && (j == 1) && (sizel == SI_CORE_SIZE)) {
				sii->coresba2[idx] = addrl;
				sii->coresba2_size[idx] = sizel;
			}
			j++;
		} while (asd != 0);

		/* Go through the ASDs for other slave ports */
		for (i = 1; i < nsp; i++) {
			j = 0;
			do {
				asd =
				    get_asd(sih, &eromptr, i, j++, AD_ST_SLAVE,
					    &addrl, &addrh, &sizel, &sizeh);
			} while (asd != 0);
			if (j == 0) {
				SI_ERROR((" SP %d has no address descriptors\n",
					  i));
				goto error;
			}
		}

		/* Now get master wrappers */
		for (i = 0; i < nmw; i++) {
			asd =
			    get_asd(sih, &eromptr, i, 0, AD_ST_MWRAP, &addrl,
				    &addrh, &sizel, &sizeh);
			if (asd == 0) {
				SI_ERROR(("Missing descriptor for MW %d\n", i));
				goto error;
			}
			if ((sizeh != 0) || (sizel != SI_CORE_SIZE)) {
				SI_ERROR(("Master wrapper %d is not 4KB\n", i));
				goto error;
			}
			if (i == 0)
				sii->wrapba[idx] = addrl;
		}

		/* And finally slave wrappers */
		for (i = 0; i < nsw; i++) {
			uint fwp = (nsp == 1) ? 0 : 1;
			asd =
			    get_asd(sih, &eromptr, fwp + i, 0, AD_ST_SWRAP,
				    &addrl, &addrh, &sizel, &sizeh);
			if (asd == 0) {
				SI_ERROR(("Missing descriptor for SW %d\n", i));
				goto error;
			}
			if ((sizeh != 0) || (sizel != SI_CORE_SIZE)) {
				SI_ERROR(("Slave wrapper %d is not 4KB\n", i));
				goto error;
			}
			if ((nmw == 0) && (i == 0))
				sii->wrapba[idx] = addrl;
		}

		/* Don't record bridges */
		if (br)
			continue;

		/* Done with core */
		sii->numcores++;
	}

	SI_ERROR(("Reached end of erom without finding END"));

 error:
	sii->numcores = 0;
	return;
}

/* This function changes the logical "focus" to the indicated core.
 * Return the current core's virtual address.
 */
void *ai_setcoreidx(si_t *sih, uint coreidx)
{
	si_info_t *sii = SI_INFO(sih);
	u32 addr = sii->coresba[coreidx];
	u32 wrap = sii->wrapba[coreidx];
	void *regs;

	if (coreidx >= sii->numcores)
		return NULL;

	/*
	 * If the user has provided an interrupt mask enabled function,
	 * then assert interrupts are disabled before switching the core.
	 */
	ASSERT((sii->intrsenabled_fn == NULL)
	       || !(*(sii)->intrsenabled_fn) ((sii)->intr_arg));

	switch (sih->bustype) {
	case SI_BUS:
		/* map new one */
		if (!sii->regs[coreidx]) {
			sii->regs[coreidx] = REG_MAP(addr, SI_CORE_SIZE);
			ASSERT(GOODREGS(sii->regs[coreidx]));
		}
		sii->curmap = regs = sii->regs[coreidx];
		if (!sii->wrappers[coreidx]) {
			sii->wrappers[coreidx] = REG_MAP(wrap, SI_CORE_SIZE);
			ASSERT(GOODREGS(sii->wrappers[coreidx]));
		}
		sii->curwrap = sii->wrappers[coreidx];
		break;

	case PCI_BUS:
		/* point bar0 window */
		pci_write_config_dword(sii->pbus, PCI_BAR0_WIN, addr);
		regs = sii->curmap;
		/* point bar0 2nd 4KB window */
		pci_write_config_dword(sii->pbus, PCI_BAR0_WIN2, wrap);
		break;

	case SPI_BUS:
	case SDIO_BUS:
		sii->curmap = regs = (void *)(unsigned long)addr;
		sii->curwrap = (void *)(unsigned long)wrap;
		break;

	default:
		ASSERT(0);
		regs = NULL;
		break;
	}

	sii->curmap = regs;
	sii->curidx = coreidx;

	return regs;
}

/* Return the number of address spaces in current core */
int ai_numaddrspaces(si_t *sih)
{
	return 2;
}

/* Return the address of the nth address space in the current core */
u32 ai_addrspace(si_t *sih, uint asidx)
{
	si_info_t *sii;
	uint cidx;

	sii = SI_INFO(sih);
	cidx = sii->curidx;

	if (asidx == 0)
		return sii->coresba[cidx];
	else if (asidx == 1)
		return sii->coresba2[cidx];
	else {
		SI_ERROR(("%s: Need to parse the erom again to find addr space %d\n", __func__, asidx));
		return 0;
	}
}

/* Return the size of the nth address space in the current core */
u32 ai_addrspacesize(si_t *sih, uint asidx)
{
	si_info_t *sii;
	uint cidx;

	sii = SI_INFO(sih);
	cidx = sii->curidx;

	if (asidx == 0)
		return sii->coresba_size[cidx];
	else if (asidx == 1)
		return sii->coresba2_size[cidx];
	else {
		SI_ERROR(("%s: Need to parse the erom again to find addr space %d\n", __func__, asidx));
		return 0;
	}
}

uint ai_flag(si_t *sih)
{
	si_info_t *sii;
	aidmp_t *ai;

	sii = SI_INFO(sih);
	if (BCM47162_DMP()) {
		SI_ERROR(("%s: Attempting to read MIPS DMP registers on 47162a0", __func__));
		return sii->curidx;
	}
	ai = sii->curwrap;

	return R_REG(&ai->oobselouta30) & 0x1f;
}

void ai_setint(si_t *sih, int siflag)
{
}

void ai_write_wrap_reg(si_t *sih, u32 offset, u32 val)
{
	si_info_t *sii = SI_INFO(sih);
	u32 *w = (u32 *) sii->curwrap;
	W_REG(w + (offset / 4), val);
	return;
}

uint ai_corevendor(si_t *sih)
{
	si_info_t *sii;
	u32 cia;

	sii = SI_INFO(sih);
	cia = sii->cia[sii->curidx];
	return (cia & CIA_MFG_MASK) >> CIA_MFG_SHIFT;
}

uint ai_corerev(si_t *sih)
{
	si_info_t *sii;
	u32 cib;

	sii = SI_INFO(sih);
	cib = sii->cib[sii->curidx];
	return (cib & CIB_REV_MASK) >> CIB_REV_SHIFT;
}

bool ai_iscoreup(si_t *sih)
{
	si_info_t *sii;
	aidmp_t *ai;

	sii = SI_INFO(sih);
	ai = sii->curwrap;

	return (((R_REG(&ai->ioctrl) & (SICF_FGC | SICF_CLOCK_EN)) ==
		 SICF_CLOCK_EN)
		&& ((R_REG(&ai->resetctrl) & AIRC_RESET) == 0));
}

/*
 * Switch to 'coreidx', issue a single arbitrary 32bit register mask&set operation,
 * switch back to the original core, and return the new value.
 *
 * When using the silicon backplane, no fiddling with interrupts or core switches is needed.
 *
 * Also, when using pci/pcie, we can optimize away the core switching for pci registers
 * and (on newer pci cores) chipcommon registers.
 */
uint ai_corereg(si_t *sih, uint coreidx, uint regoff, uint mask, uint val)
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

	if (sih->bustype == SI_BUS) {
		/* If internal bus, we can always get at everything */
		fast = true;
		/* map if does not exist */
		if (!sii->regs[coreidx]) {
			sii->regs[coreidx] = REG_MAP(sii->coresba[coreidx],
						     SI_CORE_SIZE);
			ASSERT(GOODREGS(sii->regs[coreidx]));
		}
		r = (u32 *) ((unsigned char *) sii->regs[coreidx] + regoff);
	} else if (sih->bustype == PCI_BUS) {
		/* If pci/pcie, we can get at pci/pcie regs and on newer cores to chipc */

		if ((sii->coreid[coreidx] == CC_CORE_ID) && SI_FAST(sii)) {
			/* Chipc registers are mapped at 12KB */

			fast = true;
			r = (u32 *) ((char *)sii->curmap +
					PCI_16KB0_CCREGS_OFFSET + regoff);
		} else if (sii->pub.buscoreidx == coreidx) {
			/* pci registers are at either in the last 2KB of an 8KB window
			 * or, in pcie and pci rev 13 at 8KB
			 */
			fast = true;
			if (SI_FAST(sii))
				r = (u32 *) ((char *)sii->curmap +
						PCI_16KB0_PCIREGS_OFFSET +
						regoff);
			else
				r = (u32 *) ((char *)sii->curmap +
						((regoff >= SBCONFIGOFF) ?
						 PCI_BAR0_PCISBR_OFFSET :
						 PCI_BAR0_PCIREGS_OFFSET) +
						regoff);
		}
	}

	if (!fast) {
		INTR_OFF(sii, intr_val);

		/* save current core index */
		origidx = si_coreidx(&sii->pub);

		/* switch core */
		r = (u32 *) ((unsigned char *) ai_setcoreidx(&sii->pub, coreidx) +
				regoff);
	}
	ASSERT(r != NULL);

	/* mask and set */
	if (mask || val) {
		w = (R_REG(r) & ~mask) | val;
		W_REG(r, w);
	}

	/* readback */
	w = R_REG(r);

	if (!fast) {
		/* restore core index */
		if (origidx != coreidx)
			ai_setcoreidx(&sii->pub, origidx);

		INTR_RESTORE(sii, intr_val);
	}

	return w;
}

void ai_core_disable(si_t *sih, u32 bits)
{
	si_info_t *sii;
	volatile u32 dummy;
	aidmp_t *ai;

	sii = SI_INFO(sih);

	ASSERT(GOODREGS(sii->curwrap));
	ai = sii->curwrap;

	/* if core is already in reset, just return */
	if (R_REG(&ai->resetctrl) & AIRC_RESET)
		return;

	W_REG(&ai->ioctrl, bits);
	dummy = R_REG(&ai->ioctrl);
	udelay(10);

	W_REG(&ai->resetctrl, AIRC_RESET);
	udelay(1);
}

/* reset and re-enable a core
 * inputs:
 * bits - core specific bits that are set during and after reset sequence
 * resetbits - core specific bits that are set only during reset sequence
 */
void ai_core_reset(si_t *sih, u32 bits, u32 resetbits)
{
	si_info_t *sii;
	aidmp_t *ai;
	volatile u32 dummy;

	sii = SI_INFO(sih);
	ASSERT(GOODREGS(sii->curwrap));
	ai = sii->curwrap;

	/*
	 * Must do the disable sequence first to work for arbitrary current core state.
	 */
	ai_core_disable(sih, (bits | resetbits));

	/*
	 * Now do the initialization sequence.
	 */
	W_REG(&ai->ioctrl, (bits | SICF_FGC | SICF_CLOCK_EN));
	dummy = R_REG(&ai->ioctrl);
	W_REG(&ai->resetctrl, 0);
	udelay(1);

	W_REG(&ai->ioctrl, (bits | SICF_CLOCK_EN));
	dummy = R_REG(&ai->ioctrl);
	udelay(1);
}

void ai_core_cflags_wo(si_t *sih, u32 mask, u32 val)
{
	si_info_t *sii;
	aidmp_t *ai;
	u32 w;

	sii = SI_INFO(sih);

	if (BCM47162_DMP()) {
		SI_ERROR(("%s: Accessing MIPS DMP register (ioctrl) on 47162a0",
			  __func__));
		return;
	}

	ASSERT(GOODREGS(sii->curwrap));
	ai = sii->curwrap;

	ASSERT((val & ~mask) == 0);

	if (mask || val) {
		w = ((R_REG(&ai->ioctrl) & ~mask) | val);
		W_REG(&ai->ioctrl, w);
	}
}

u32 ai_core_cflags(si_t *sih, u32 mask, u32 val)
{
	si_info_t *sii;
	aidmp_t *ai;
	u32 w;

	sii = SI_INFO(sih);
	if (BCM47162_DMP()) {
		SI_ERROR(("%s: Accessing MIPS DMP register (ioctrl) on 47162a0",
			  __func__));
		return 0;
	}

	ASSERT(GOODREGS(sii->curwrap));
	ai = sii->curwrap;

	ASSERT((val & ~mask) == 0);

	if (mask || val) {
		w = ((R_REG(&ai->ioctrl) & ~mask) | val);
		W_REG(&ai->ioctrl, w);
	}

	return R_REG(&ai->ioctrl);
}

u32 ai_core_sflags(si_t *sih, u32 mask, u32 val)
{
	si_info_t *sii;
	aidmp_t *ai;
	u32 w;

	sii = SI_INFO(sih);
	if (BCM47162_DMP()) {
		SI_ERROR(("%s: Accessing MIPS DMP register (iostatus) on 47162a0", __func__));
		return 0;
	}

	ASSERT(GOODREGS(sii->curwrap));
	ai = sii->curwrap;

	ASSERT((val & ~mask) == 0);
	ASSERT((mask & ~SISF_CORE_BITS) == 0);

	if (mask || val) {
		w = ((R_REG(&ai->iostatus) & ~mask) | val);
		W_REG(&ai->iostatus, w);
	}

	return R_REG(&ai->iostatus);
}

