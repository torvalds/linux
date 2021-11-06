/*
 * Misc utility routines for accessing chip-specific features
 * of the SiliconBackplane-based Broadcom chips.
 *
 * Copyright (C) 2020, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#include <typedefs.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <siutils.h>
#include <bcmdevs.h>
#include <hndsoc.h>
#include <sbchipc.h>
#if !defined(BCMDONGLEHOST)
#include <pci_core.h>
#endif /* !defined(BCMDONGLEHOST) */
#include <pcicfg.h>
#include <sbpcmcia.h>

#include "siutils_priv.h"

/* local prototypes */
static uint _sb_coreidx(const si_info_t *sii, uint32 sba);
static uint _sb_scan(si_info_t *sii, uint32 sba, volatile void *regs, uint bus, uint32 sbba,
                     uint ncores, uint devid);
static uint32 _sb_coresba(const si_info_t *sii);
static volatile void *_sb_setcoreidx(const si_info_t *sii, uint coreidx);
#define	SET_SBREG(sii, r, mask, val)	\
		W_SBREG((sii), (r), ((R_SBREG((sii), (r)) & ~(mask)) | (val)))
#define	REGS2SB(va)	(sbconfig_t*) ((volatile int8*)(va) + SBCONFIGOFF)

/* sonicsrev */
#define	SONICS_2_2	(SBIDL_RV_2_2 >> SBIDL_RV_SHIFT)
#define	SONICS_2_3	(SBIDL_RV_2_3 >> SBIDL_RV_SHIFT)

/*
 * Macros to read/write sbconfig registers.
 */
#define	R_SBREG(sii, sbr)	sb_read_sbreg((sii), (sbr))
#define	W_SBREG(sii, sbr, v)	sb_write_sbreg((sii), (sbr), (v))
#define	AND_SBREG(sii, sbr, v)	W_SBREG((sii), (sbr), (R_SBREG((sii), (sbr)) & (v)))
#define	OR_SBREG(sii, sbr, v)	W_SBREG((sii), (sbr), (R_SBREG((sii), (sbr)) | (v)))

static uint32
sb_read_sbreg(const si_info_t *sii, volatile uint32 *sbr)
{
	return R_REG(sii->osh, sbr);
}

static void
sb_write_sbreg(const si_info_t *sii, volatile uint32 *sbr, uint32 v)
{
	W_REG(sii->osh, sbr, v);
}

uint
sb_coreid(const si_t *sih)
{
	const si_info_t *sii = SI_INFO(sih);
	sbconfig_t *sb = REGS2SB(sii->curmap);

	return ((R_SBREG(sii, &sb->sbidhigh) & SBIDH_CC_MASK) >> SBIDH_CC_SHIFT);
}

uint
sb_intflag(si_t *sih)
{
	const si_info_t *sii = SI_INFO(sih);
	volatile void *corereg;
	sbconfig_t *sb;
	uint origidx, intflag;
	bcm_int_bitmask_t intr_val;

	INTR_OFF(sii, &intr_val);
	origidx = si_coreidx(sih);
	corereg = si_setcore(sih, CC_CORE_ID, 0);
	ASSERT(corereg != NULL);
	sb = REGS2SB(corereg);
	intflag = R_SBREG(sii, &sb->sbflagst);
	sb_setcoreidx(sih, origidx);
	INTR_RESTORE(sii, &intr_val);

	return intflag;
}

uint
sb_flag(const si_t *sih)
{
	const si_info_t *sii = SI_INFO(sih);
	sbconfig_t *sb = REGS2SB(sii->curmap);

	return R_SBREG(sii, &sb->sbtpsflag) & SBTPS_NUM0_MASK;
}

void
sb_setint(const si_t *sih, int siflag)
{
	const si_info_t *sii = SI_INFO(sih);
	sbconfig_t *sb = REGS2SB(sii->curmap);
	uint32 vec;

	if (siflag == -1)
		vec = 0;
	else
		vec = 1 << siflag;
	W_SBREG(sii, &sb->sbintvec, vec);
}

/* return core index of the core with address 'sba' */
static uint
BCMATTACHFN(_sb_coreidx)(const si_info_t *sii, uint32 sba)
{
	uint i;
	const si_cores_info_t *cores_info = (const si_cores_info_t *)sii->cores_info;

	for (i = 0; i < sii->numcores; i ++)
		if (sba == cores_info->coresba[i])
			return i;
	return BADIDX;
}

/* return core address of the current core */
static uint32
BCMATTACHFN(_sb_coresba)(const si_info_t *sii)
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

#ifdef BCMSDIO
	case SPI_BUS:
	case SDIO_BUS:
		sbaddr = (uint32)(uintptr)sii->curmap;
		break;
#endif

	default:
		sbaddr = BADCOREADDR;
		break;
	}

	return sbaddr;
}

uint
sb_corevendor(const si_t *sih)
{
	const si_info_t *sii;
	sbconfig_t *sb;

	sii = SI_INFO(sih);
	sb = REGS2SB(sii->curmap);

	return ((R_SBREG(sii, &sb->sbidhigh) & SBIDH_VC_MASK) >> SBIDH_VC_SHIFT);
}

uint
sb_corerev(const si_t *sih)
{
	const si_info_t *sii;
	sbconfig_t *sb;
	uint sbidh;

	sii = SI_INFO(sih);
	sb = REGS2SB(sii->curmap);
	sbidh = R_SBREG(sii, &sb->sbidhigh);

	return (SBCOREREV(sbidh));
}

/* set core-specific control flags */
void
sb_core_cflags_wo(const si_t *sih, uint32 mask, uint32 val)
{
	const si_info_t *sii = SI_INFO(sih);
	sbconfig_t *sb = REGS2SB(sii->curmap);
	uint32 w;

	ASSERT((val & ~mask) == 0);

	/* mask and set */
	w = (R_SBREG(sii, &sb->sbtmstatelow) & ~(mask << SBTML_SICF_SHIFT)) |
	        (val << SBTML_SICF_SHIFT);
	W_SBREG(sii, &sb->sbtmstatelow, w);
}

/* set/clear core-specific control flags */
uint32
sb_core_cflags(const si_t *sih, uint32 mask, uint32 val)
{
	const si_info_t *sii = SI_INFO(sih);
	sbconfig_t *sb = REGS2SB(sii->curmap);
	uint32 w;

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
sb_core_sflags(const si_t *sih, uint32 mask, uint32 val)
{
	const si_info_t *sii = SI_INFO(sih);
	sbconfig_t *sb = REGS2SB(sii->curmap);
	uint32 w;

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
sb_iscoreup(const si_t *sih)
{
	const si_info_t *sii = SI_INFO(sih);
	sbconfig_t *sb = REGS2SB(sii->curmap);

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
	volatile uint32 *r = NULL;
	uint w;
	bcm_int_bitmask_t intr_val;
	bool fast = FALSE;
	si_info_t *sii = SI_INFO(sih);
	si_cores_info_t *cores_info = (si_cores_info_t *)sii->cores_info;

	ASSERT(GOODIDX(coreidx, sii->numcores));
	ASSERT(regoff < SI_CORE_SIZE);
	ASSERT((val & ~mask) == 0);

	if (coreidx >= SI_MAXCORES)
		return 0;

	if (BUSTYPE(sii->pub.bustype) == SI_BUS) {
		/* If internal bus, we can always get at everything */
		fast = TRUE;
		/* map if does not exist */
		if (!cores_info->regs[coreidx]) {
			cores_info->regs[coreidx] = REG_MAP(cores_info->coresba[coreidx],
			                            SI_CORE_SIZE);
			ASSERT(GOODREGS(cores_info->regs[coreidx]));
		}
		r = (volatile uint32 *)((volatile uchar *)cores_info->regs[coreidx] + regoff);
	} else if (BUSTYPE(sii->pub.bustype) == PCI_BUS) {
		/* If pci/pcie, we can get at pci/pcie regs and on newer cores to chipc */

		if ((cores_info->coreid[coreidx] == CC_CORE_ID) && SI_FAST(sii)) {
			/* Chipc registers are mapped at 12KB */

			fast = TRUE;
			r = (volatile uint32 *)((volatile char *)sii->curmap +
			               PCI_16KB0_CCREGS_OFFSET + regoff);
		} else if (sii->pub.buscoreidx == coreidx) {
			/* pci registers are at either in the last 2KB of an 8KB window
			 * or, in pcie and pci rev 13 at 8KB
			 */
			fast = TRUE;
			if (SI_FAST(sii))
				r = (volatile uint32 *)((volatile char *)sii->curmap +
				               PCI_16KB0_PCIREGS_OFFSET + regoff);
			else
				r = (volatile uint32 *)((volatile char *)sii->curmap +
				               ((regoff >= SBCONFIGOFF) ?
				                PCI_BAR0_PCISBR_OFFSET : PCI_BAR0_PCIREGS_OFFSET) +
				               regoff);
		}
	}

	if (!fast) {
		INTR_OFF(sii, &intr_val);

		/* save current core index */
		origidx = si_coreidx(&sii->pub);

		/* switch core */
		r = (volatile uint32*) ((volatile uchar*)sb_setcoreidx(&sii->pub, coreidx) +
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
	else {
		w = R_REG(sii->osh, r);
	}

	if (!fast) {
		/* restore core index */
		if (origidx != coreidx)
			sb_setcoreidx(&sii->pub, origidx);

		INTR_RESTORE(sii, &intr_val);
	}

	return (w);
}

/*
 * If there is no need for fiddling with interrupts or core switches (typically silicon
 * back plane registers, pci registers and chipcommon registers), this function
 * returns the register offset on this core to a mapped address. This address can
 * be used for W_REG/R_REG directly.
 *
 * For accessing registers that would need a core switch, this function will return
 * NULL.
 */
volatile uint32 *
sb_corereg_addr(const si_t *sih, uint coreidx, uint regoff)
{
	volatile uint32 *r = NULL;
	bool fast = FALSE;
	const si_info_t *sii = SI_INFO(sih);
	si_cores_info_t *cores_info = (si_cores_info_t *)sii->cores_info;

	ASSERT(GOODIDX(coreidx, sii->numcores));
	ASSERT(regoff < SI_CORE_SIZE);

	if (coreidx >= SI_MAXCORES)
		return 0;

	if (BUSTYPE(sii->pub.bustype) == SI_BUS) {
		/* If internal bus, we can always get at everything */
		fast = TRUE;
		/* map if does not exist */
		if (!cores_info->regs[coreidx]) {
			cores_info->regs[coreidx] = REG_MAP(cores_info->coresba[coreidx],
			                            SI_CORE_SIZE);
			ASSERT(GOODREGS(cores_info->regs[coreidx]));
		}
		r = (volatile uint32 *)((volatile uchar *)cores_info->regs[coreidx] + regoff);
	} else if (BUSTYPE(sii->pub.bustype) == PCI_BUS) {
		/* If pci/pcie, we can get at pci/pcie regs and on newer cores to chipc */

		if ((cores_info->coreid[coreidx] == CC_CORE_ID) && SI_FAST(sii)) {
			/* Chipc registers are mapped at 12KB */

			fast = TRUE;
			r = (volatile uint32 *)((volatile char *)sii->curmap +
			               PCI_16KB0_CCREGS_OFFSET + regoff);
		} else if (sii->pub.buscoreidx == coreidx) {
			/* pci registers are at either in the last 2KB of an 8KB window
			 * or, in pcie and pci rev 13 at 8KB
			 */
			fast = TRUE;
			if (SI_FAST(sii))
				r = (volatile uint32 *)((volatile char *)sii->curmap +
				               PCI_16KB0_PCIREGS_OFFSET + regoff);
			else
				r = (volatile uint32 *)((volatile char *)sii->curmap +
				               ((regoff >= SBCONFIGOFF) ?
				                PCI_BAR0_PCISBR_OFFSET : PCI_BAR0_PCIREGS_OFFSET) +
				               regoff);
		}
	}

	if (!fast)
		return 0;

	return (r);
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
BCMATTACHFN(_sb_scan)(si_info_t *sii, uint32 sba, volatile void *regs, uint bus,
	uint32 sbba, uint numcores, uint devid)
{
	uint next;
	uint ncc = 0;
	uint i;
	si_cores_info_t *cores_info = (si_cores_info_t *)sii->cores_info;

	/* bail out in case it is too deep to scan at the specified bus level */
	if (bus >= SB_MAXBUSES) {
		SI_ERROR(("_sb_scan: bus 0x%08x at level %d is too deep to scan\n", sbba, bus));
		return 0;
	}
	SI_MSG(("_sb_scan: scan bus 0x%08x assume %u cores\n", sbba, numcores));

	/* Scan all cores on the bus starting from core 0.
	 * Core addresses must be contiguous on each bus.
	 */
	for (i = 0, next = sii->numcores; i < numcores && next < SB_BUS_MAXCORES; i++, next++) {
		cores_info->coresba[next] = sbba + (i * SI_CORE_SIZE);

		/* keep and reuse the initial register mapping */
		if ((BUSTYPE(sii->pub.bustype) == SI_BUS) && (cores_info->coresba[next] == sba)) {
			SI_VMSG(("_sb_scan: reuse mapped regs %p for core %u\n", regs, next));
			cores_info->regs[next] = regs;
		}

		/* change core to 'next' and read its coreid */
		sii->curmap = _sb_setcoreidx(sii, next);
		sii->curidx = next;

		cores_info->coreid[next] = sb_coreid(&sii->pub);

		/* core specific processing... */
		/* chipc provides # cores */
		if (cores_info->coreid[next] == CC_CORE_ID) {
			chipcregs_t *cc = (chipcregs_t *)sii->curmap;

			/* determine numcores - this is the total # cores in the chip */
			ASSERT(cc);
			numcores = (R_REG(sii->osh, &cc->chipid) & CID_CC_MASK) >> CID_CC_SHIFT;
			SI_VMSG(("_sb_scan: there are %u cores in the chip %s\n", numcores,
				sii->pub.issim ? "QT" : ""));
		}
		/* scan bridged SB(s) and add results to the end of the list */
		else if (cores_info->coreid[next] == OCP_CORE_ID) {
			sbconfig_t *sb = REGS2SB(sii->curmap);
			uint32 nsbba = R_SBREG(sii, &sb->sbadmatch1);
			uint nsbcc;

			sii->numcores = next + 1;

			if ((nsbba & 0xfff00000) != si_enum_base(devid))
				continue;
			nsbba &= 0xfffff000;
			if (_sb_coreidx(sii, nsbba) != BADIDX)
				continue;

			nsbcc = (R_SBREG(sii, &sb->sbtmstatehigh) & 0x000f0000) >> 16;
			nsbcc = _sb_scan(sii, sba, regs, bus + 1, nsbba, nsbcc, devid);
			if (sbba == si_enum_base(devid))
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
BCMATTACHFN(sb_scan)(si_t *sih, volatile void *regs, uint devid)
{
	uint32 origsba;
	sbconfig_t *sb;
	si_info_t *sii = SI_INFO(sih);
	BCM_REFERENCE(devid);

	sb = REGS2SB(sii->curmap);

	sii->pub.socirev = (R_SBREG(sii, &sb->sbidlow) & SBIDL_RV_MASK) >> SBIDL_RV_SHIFT;

	/* Save the current core info and validate it later till we know
	 * for sure what is good and what is bad.
	 */
	origsba = _sb_coresba(sii);

	/* scan all SB(s) starting from SI_ENUM_BASE_DEFAULT */
	sii->numcores = _sb_scan(sii, origsba, regs, 0, si_enum_base(devid), 1, devid);
}

/*
 * This function changes logical "focus" to the indicated core;
 * must be called with interrupts off.
 * Moreover, callers should keep interrupts off during switching out of and back to d11 core
 */
volatile void *
sb_setcoreidx(si_t *sih, uint coreidx)
{
	si_info_t *sii = SI_INFO(sih);

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
static volatile void *
_sb_setcoreidx(const si_info_t *sii, uint coreidx)
{
	si_cores_info_t *cores_info = (si_cores_info_t *)sii->cores_info;
	uint32 sbaddr = cores_info->coresba[coreidx];
	volatile void *regs;

	switch (BUSTYPE(sii->pub.bustype)) {
	case SI_BUS:
		/* map new one */
		if (!cores_info->regs[coreidx]) {
			cores_info->regs[coreidx] = REG_MAP(sbaddr, SI_CORE_SIZE);
			ASSERT(GOODREGS(cores_info->regs[coreidx]));
		}
		regs = cores_info->regs[coreidx];
		break;

	case PCI_BUS:
		/* point bar0 window */
		OSL_PCI_WRITE_CONFIG(sii->osh, PCI_BAR0_WIN, 4, sbaddr);
		regs = sii->curmap;
		break;

#ifdef BCMSDIO
	case SPI_BUS:
	case SDIO_BUS:
		/* map new one */
		if (!cores_info->regs[coreidx]) {
			cores_info->regs[coreidx] = (void *)(uintptr)sbaddr;
			ASSERT(GOODREGS(cores_info->regs[coreidx]));
		}
		regs = cores_info->regs[coreidx];
		break;
#endif	/* BCMSDIO */

	default:
		ASSERT(0);
		regs = NULL;
		break;
	}

	return regs;
}

/* Return the address of sbadmatch0/1/2/3 register */
static volatile uint32 *
sb_admatch(const si_info_t *sii, uint asidx)
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
		SI_ERROR(("sb_admatch: Address space index (%d) out of range\n", asidx));
		return 0;
	}

	return (addrm);
}

/* Return the number of address spaces in current core */
int
sb_numaddrspaces(const si_t *sih)
{
	const si_info_t *sii = SI_INFO(sih);
	sbconfig_t *sb = REGS2SB(sii->curmap);

	/* + 1 because of enumeration space */
	return ((R_SBREG(sii, &sb->sbidlow) & SBIDL_AR_MASK) >> SBIDL_AR_SHIFT) + 1;
}

/* Return the address of the nth address space in the current core */
uint32
sb_addrspace(const si_t *sih, uint asidx)
{
	const si_info_t *sii = SI_INFO(sih);

	return (sb_base(R_SBREG(sii, sb_admatch(sii, asidx))));
}

/* Return the size of the nth address space in the current core */
uint32
sb_addrspacesize(const si_t *sih, uint asidx)
{
	const si_info_t *sii = SI_INFO(sih);

	return (sb_size(R_SBREG(sii, sb_admatch(sii, asidx))));
}

#if defined(BCMDBG_ERR) || defined(BCMASSERT_SUPPORT) || \
	defined(BCMDBG_DUMP)
/* traverse all cores to find and clear source of serror */
static void
sb_serr_clear(si_info_t *sii)
{
	sbconfig_t *sb;
	uint origidx;
	uint i;
	bcm_int_bitmask_t intr_val;
	volatile void *corereg = NULL;

	INTR_OFF(sii, &intr_val);
	origidx = si_coreidx(&sii->pub);

	for (i = 0; i < sii->numcores; i++) {
		corereg = sb_setcoreidx(&sii->pub, i);
		if (NULL != corereg) {
			sb = REGS2SB(corereg);
			if ((R_SBREG(sii, &sb->sbtmstatehigh)) & SBTMH_SERR) {
				AND_SBREG(sii, &sb->sbtmstatehigh, ~SBTMH_SERR);
				SI_ERROR(("sb_serr_clear: SError at core 0x%x\n",
				          sb_coreid(&sii->pub)));
			}
		}
	}

	sb_setcoreidx(&sii->pub, origidx);
	INTR_RESTORE(sii, &intr_val);
}

/*
 * Check if any inband, outband or timeout errors has happened and clear them.
 * Must be called with chip clk on !
 */
bool
sb_taclear(si_t *sih, bool details)
{
	si_info_t *sii = SI_INFO(sih);
	bool rc = FALSE;
	uint32 inband = 0, serror = 0, timeout = 0;
	volatile uint32 imstate;

	BCM_REFERENCE(sii);

	if (BUSTYPE(sii->pub.bustype) == PCI_BUS) {
		volatile uint32 stcmd;

		/* inband error is Target abort for PCI */
		stcmd = OSL_PCI_READ_CONFIG(sii->osh, PCI_CFG_CMD, sizeof(uint32));
		inband = stcmd & PCI_STAT_TA;
		if (inband) {
#ifdef BCMDBG
			if (details) {
				SI_ERROR(("\ninband:\n"));
				si_viewall(sih, FALSE);
			}
#endif
			OSL_PCI_WRITE_CONFIG(sii->osh, PCI_CFG_CMD, sizeof(uint32), stcmd);
		}

		/* serror */
		stcmd = OSL_PCI_READ_CONFIG(sii->osh, PCI_INT_STATUS, sizeof(uint32));
		serror = stcmd & PCI_SBIM_STATUS_SERR;
		if (serror) {
#ifdef BCMDBG
			if (details) {
				SI_ERROR(("\nserror:\n"));
				si_viewall(sih, FALSE);
			}
#endif
			sb_serr_clear(sii);
			OSL_PCI_WRITE_CONFIG(sii->osh, PCI_INT_STATUS, sizeof(uint32), stcmd);
		}

		/* timeout */
		imstate = sb_corereg(sih, sii->pub.buscoreidx,
		                     SBCONFIGOFF + OFFSETOF(sbconfig_t, sbimstate), 0, 0);
		if ((imstate != 0xffffffff) && (imstate & (SBIM_IBE | SBIM_TO))) {
			sb_corereg(sih, sii->pub.buscoreidx,
			           SBCONFIGOFF + OFFSETOF(sbconfig_t, sbimstate), ~0,
			           (imstate & ~(SBIM_IBE | SBIM_TO)));
			/* inband = imstate & SBIM_IBE; same as TA above */
			timeout = imstate & SBIM_TO;
			if (timeout) {
#ifdef BCMDBG
				if (details) {
					SI_ERROR(("\ntimeout:\n"));
					si_viewall(sih, FALSE);
				}
#endif
			}
		}

		if (inband) {
			/* dump errlog for sonics >= 2.3 */
			if (sii->pub.socirev == SONICS_2_2)
				;
			else {
				uint32 imerrlog, imerrloga;
				imerrlog = sb_corereg(sih, sii->pub.buscoreidx, SBIMERRLOG, 0, 0);
				if (imerrlog & SBTMEL_EC) {
					imerrloga = sb_corereg(sih, sii->pub.buscoreidx,
					                       SBIMERRLOGA, 0, 0);
					BCM_REFERENCE(imerrloga);
					/* clear errlog */
					sb_corereg(sih, sii->pub.buscoreidx, SBIMERRLOG, ~0, 0);
					SI_ERROR(("sb_taclear: ImErrLog 0x%x, ImErrLogA 0x%x\n",
						imerrlog, imerrloga));
				}
			}
		}
	}
#ifdef BCMSDIO
	else if ((BUSTYPE(sii->pub.bustype) == SDIO_BUS) ||
	         (BUSTYPE(sii->pub.bustype) == SPI_BUS)) {
		sbconfig_t *sb;
		uint origidx;
		bcm_int_bitmask_t intr_val;
		volatile void *corereg = NULL;
		volatile uint32 tmstate;

		INTR_OFF(sii, &intr_val);
		origidx = si_coreidx(sih);

		corereg = si_setcore(sih, SDIOD_CORE_ID, 0);
		if (corereg != NULL) {
			sb = REGS2SB(corereg);

			imstate = R_SBREG(sii, &sb->sbimstate);
			if ((imstate != 0xffffffff) && (imstate & (SBIM_IBE | SBIM_TO))) {
				AND_SBREG(sii, &sb->sbimstate, ~(SBIM_IBE | SBIM_TO));
				/* inband = imstate & SBIM_IBE; cmd error */
				timeout = imstate & SBIM_TO;
			}
			tmstate = R_SBREG(sii, &sb->sbtmstatehigh);
			if ((tmstate != 0xffffffff) && (tmstate & SBTMH_INT_STATUS)) {
				sb_serr_clear(sii);
				serror = 1;
				OR_SBREG(sii, &sb->sbtmstatelow, SBTML_INT_ACK);
				AND_SBREG(sii, &sb->sbtmstatelow, ~SBTML_INT_ACK);
			}
		}

		sb_setcoreidx(sih, origidx);
		INTR_RESTORE(sii, &intr_val);
	}
#endif /* BCMSDIO */

	if (inband | timeout | serror) {
		rc = TRUE;
		SI_ERROR(("sb_taclear: inband 0x%x, serror 0x%x, timeout 0x%x!\n",
		          inband, serror, timeout));
	}

	return (rc);
}
#endif /* BCMDBG_ERR || BCMASSERT_SUPPORT || BCMDBG_DUMP */

/* do buffered registers update */
void
sb_commit(si_t *sih)
{
	const si_info_t *sii = SI_INFO(sih);
	uint origidx;
	bcm_int_bitmask_t intr_val;

	origidx = sii->curidx;
	ASSERT(GOODIDX(origidx, sii->numcores));

	INTR_OFF(sii, &intr_val);

	/* switch over to chipcommon core if there is one, else use pci */
	if (sii->pub.ccrev != NOREV) {
		chipcregs_t *ccregs = (chipcregs_t *)si_setcore(sih, CC_CORE_ID, 0);
		ASSERT(ccregs != NULL);

		/* do the buffer registers update */
		W_REG(sii->osh, &ccregs->broadcastaddress, SB_COMMIT);
		W_REG(sii->osh, &ccregs->broadcastdata, 0x0);
#if !defined(BCMDONGLEHOST)
	} else if (PCI(sii)) {
		sbpciregs_t *pciregs = (sbpciregs_t *)si_setcore(sih, PCI_CORE_ID, 0);
		ASSERT(pciregs != NULL);

		/* do the buffer registers update */
		W_REG(sii->osh, &pciregs->bcastaddr, SB_COMMIT);
		W_REG(sii->osh, &pciregs->bcastdata, 0x0);
#endif /* !defined(BCMDONGLEHOST) */
	} else
		ASSERT(0);

	/* restore core index */
	sb_setcoreidx(sih, origidx);
	INTR_RESTORE(sii, &intr_val);
}

void
sb_core_disable(const si_t *sih, uint32 bits)
{
	const si_info_t *sii = SI_INFO(sih);
	volatile uint32 dummy;
	sbconfig_t *sb;

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
	BCM_REFERENCE(dummy);
	OSL_DELAY(1);
	SPINWAIT((R_SBREG(sii, &sb->sbtmstatehigh) & SBTMH_BUSY), 100000);
	if (R_SBREG(sii, &sb->sbtmstatehigh) & SBTMH_BUSY)
		SI_ERROR(("sb_core_disable: target state still busy\n"));

	/*
	 * If core is initiator, set the Reject bit and allow Busy to clear.
	 * sonicsrev < 2.3 chips don't have the Reject and Busy bits (nops).
	 * Don't assert - dma engine might be stuck (PR4871).
	 */
	if (R_SBREG(sii, &sb->sbidlow) & SBIDL_INIT) {
		OR_SBREG(sii, &sb->sbimstate, SBIM_RJ);
		dummy = R_SBREG(sii, &sb->sbimstate);
		BCM_REFERENCE(dummy);
		OSL_DELAY(1);
		SPINWAIT((R_SBREG(sii, &sb->sbimstate) & SBIM_BY), 100000);
	}

	/* set reset and reject while enabling the clocks */
	W_SBREG(sii, &sb->sbtmstatelow,
	        (((bits | SICF_FGC | SICF_CLOCK_EN) << SBTML_SICF_SHIFT) |
	         SBTML_REJ | SBTML_RESET));
	dummy = R_SBREG(sii, &sb->sbtmstatelow);
	BCM_REFERENCE(dummy);
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
sb_core_reset(const si_t *sih, uint32 bits, uint32 resetbits)
{
	const si_info_t *sii = SI_INFO(sih);
	sbconfig_t *sb;
	volatile uint32 dummy;

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
	BCM_REFERENCE(dummy);
	OSL_DELAY(1);

	/* PR3158 - clear any serror */
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
	BCM_REFERENCE(dummy);
	OSL_DELAY(1);

	/* leave clock enabled */
	W_SBREG(sii, &sb->sbtmstatelow, ((bits | SICF_CLOCK_EN) << SBTML_SICF_SHIFT));
	dummy = R_SBREG(sii, &sb->sbtmstatelow);
	BCM_REFERENCE(dummy);
	OSL_DELAY(1);
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

#if defined(BCMDBG) || defined(BCMDBG_DUMP)|| defined(BCMDBG_PHYDUMP)
/* print interesting sbconfig registers */
void
sb_dumpregs(si_t *sih, struct bcmstrbuf *b)
{
	sbconfig_t *sb;
	uint origidx, i;
	bcm_int_bitmask_t intr_val;
	const si_info_t *sii = SI_INFO(sih);
	const si_cores_info_t *cores_info = (const si_cores_info_t *)sii->cores_info;

	origidx = sii->curidx;

	INTR_OFF(sii, &intr_val);

	for (i = 0; i < sii->numcores; i++) {
		sb = REGS2SB(sb_setcoreidx(sih, i));

		bcm_bprintf(b, "core 0x%x: \n", cores_info->coreid[i]);

		if (sii->pub.socirev > SONICS_2_2)
			bcm_bprintf(b, "sbimerrlog 0x%x sbimerrloga 0x%x\n",
			          sb_corereg(sih, si_coreidx(&sii->pub), SBIMERRLOG, 0, 0),
			          sb_corereg(sih, si_coreidx(&sii->pub), SBIMERRLOGA, 0, 0));

		bcm_bprintf(b, "sbtmstatelow 0x%x sbtmstatehigh 0x%x sbidhigh 0x%x "
		            "sbimstate 0x%x\n sbimconfiglow 0x%x sbimconfighigh 0x%x\n",
		            R_SBREG(sii, &sb->sbtmstatelow), R_SBREG(sii, &sb->sbtmstatehigh),
		            R_SBREG(sii, &sb->sbidhigh), R_SBREG(sii, &sb->sbimstate),
		            R_SBREG(sii, &sb->sbimconfiglow), R_SBREG(sii, &sb->sbimconfighigh));
	}

	sb_setcoreidx(sih, origidx);
	INTR_RESTORE(sii, &intr_val);
}
#endif	/* BCMDBG || BCMDBG_DUMP || BCMDBG_PHYDUMP */

#if defined(BCMDBG)
void
sb_view(si_t *sih, bool verbose)
{
	const si_info_t *sii = SI_INFO(sih);
	sbconfig_t *sb = REGS2SB(sii->curmap);

	SI_ERROR(("\nCore ID: 0x%x\n", sb_coreid(&sii->pub)));

	if (sii->pub.socirev > SONICS_2_2)
		SI_ERROR(("sbimerrlog 0x%x sbimerrloga 0x%x\n",
		         sb_corereg(sih, si_coreidx(&sii->pub), SBIMERRLOG, 0, 0),
		         sb_corereg(sih, si_coreidx(&sii->pub), SBIMERRLOGA, 0, 0)));

	/* Print important or helpful registers */
	SI_ERROR(("sbtmerrloga 0x%x sbtmerrlog 0x%x\n",
	          R_SBREG(sii, &sb->sbtmerrloga), R_SBREG(sii, &sb->sbtmerrlog)));
	SI_ERROR(("sbimstate 0x%x sbtmstatelow 0x%x sbtmstatehigh 0x%x\n",
	          R_SBREG(sii, &sb->sbimstate),
	          R_SBREG(sii, &sb->sbtmstatelow), R_SBREG(sii, &sb->sbtmstatehigh)));
	SI_ERROR(("sbimconfiglow 0x%x sbtmconfiglow 0x%x\nsbtmconfighigh 0x%x sbidhigh 0x%x\n",
	          R_SBREG(sii, &sb->sbimconfiglow), R_SBREG(sii, &sb->sbtmconfiglow),
	          R_SBREG(sii, &sb->sbtmconfighigh), R_SBREG(sii, &sb->sbidhigh)));

	/* Print more detailed registers that are otherwise not relevant */
	if (verbose) {
		SI_ERROR(("sbipsflag 0x%x sbtpsflag 0x%x\n",
		          R_SBREG(sii, &sb->sbipsflag), R_SBREG(sii, &sb->sbtpsflag)));
		SI_ERROR(("sbadmatch3 0x%x sbadmatch2 0x%x\nsbadmatch1 0x%x sbadmatch0 0x%x\n",
		          R_SBREG(sii, &sb->sbadmatch3), R_SBREG(sii, &sb->sbadmatch2),
		          R_SBREG(sii, &sb->sbadmatch1), R_SBREG(sii, &sb->sbadmatch0)));
		SI_ERROR(("sbintvec 0x%x sbbwa0 0x%x sbimconfighigh 0x%x\n",
		          R_SBREG(sii, &sb->sbintvec), R_SBREG(sii, &sb->sbbwa0),
		          R_SBREG(sii, &sb->sbimconfighigh)));
		SI_ERROR(("sbbconfig 0x%x sbbstate 0x%x\n",
		          R_SBREG(sii, &sb->sbbconfig), R_SBREG(sii, &sb->sbbstate)));
		SI_ERROR(("sbactcnfg 0x%x sbflagst 0x%x sbidlow 0x%x \n\n",
		          R_SBREG(sii, &sb->sbactcnfg), R_SBREG(sii, &sb->sbflagst),
		          R_SBREG(sii, &sb->sbidlow)));
	}
}
#endif	/* BCMDBG */
