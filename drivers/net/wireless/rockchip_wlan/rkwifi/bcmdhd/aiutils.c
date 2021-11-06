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
#include <hndsoc.h>
#include <sbchipc.h>
#include <pcicfg.h>
#include <pcie_core.h>

#include "siutils_priv.h"
#include <bcmdevs.h>

#if defined(ETD)
#include <etd.h>
#endif

#if !defined(BCMDONGLEHOST)
#define PMU_DMP()  (cores_info->coreid[sii->curidx] == PMU_CORE_ID)
#define GCI_DMP()  (cores_info->coreid[sii->curidx] == GCI_CORE_ID)
#else
#define PMU_DMP() (0)
#define GCI_DMP() (0)
#endif /* !defined(BCMDONGLEHOST) */

#if defined(AXI_TIMEOUTS_NIC)
static bool ai_get_apb_bridge(const si_t *sih, uint32 coreidx, uint32 *apb_id,
	uint32 *apb_coreunit);
#endif /* AXI_TIMEOUTS_NIC */

#if defined (AXI_TIMEOUTS) || defined (AXI_TIMEOUTS_NIC)
static void ai_reset_axi_to(const si_info_t *sii, aidmp_t *ai);
#endif	/* defined (AXI_TIMEOUTS) || defined (AXI_TIMEOUTS_NIC) */

#ifdef DONGLEBUILD
static uint32 ai_get_sizeof_wrapper_offsets_to_dump(void);
static uint32 ai_get_wrapper_base_addr(uint32 **offset);
#endif /* DONGLEBUILD */

/* AXI ID to CoreID + unit mappings */
typedef struct axi_to_coreidx {
	uint coreid;
	uint coreunit;
} axi_to_coreidx_t;

static const axi_to_coreidx_t axi2coreidx_4369[] = {
	{CC_CORE_ID, 0},	/* 00 Chipcommon */
	{PCIE2_CORE_ID, 0},	/* 01 PCIe */
	{D11_CORE_ID, 0},	/* 02 D11 Main */
	{ARMCR4_CORE_ID, 0},	/* 03 ARM */
	{BT_CORE_ID, 0},	/* 04 BT AHB */
	{D11_CORE_ID, 1},	/* 05 D11 Aux */
	{D11_CORE_ID, 0},	/* 06 D11 Main l1 */
	{D11_CORE_ID, 1},	/* 07 D11 Aux  l1 */
	{D11_CORE_ID, 0},	/* 08 D11 Main l2 */
	{D11_CORE_ID, 1},	/* 09 D11 Aux  l2 */
	{NODEV_CORE_ID, 0},	/* 10 M2M DMA */
	{NODEV_CORE_ID, 0},	/* 11 unused */
	{NODEV_CORE_ID, 0},	/* 12 unused */
	{NODEV_CORE_ID, 0},	/* 13 unused */
	{NODEV_CORE_ID, 0},	/* 14 unused */
	{NODEV_CORE_ID, 0}	/* 15 unused */
};

/* EROM parsing */

static uint32
get_erom_ent(const si_t *sih, uint32 **eromptr, uint32 mask, uint32 match)
{
	uint32 ent;
	uint inv = 0, nom = 0;
	uint32 size = 0;

	while (TRUE) {
		ent = R_REG(SI_INFO(sih)->osh, *eromptr);
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

		/* escape condition related EROM size if it has invalid values */
		size += sizeof(*eromptr);
		if (size >= ER_SZ_MAX) {
			SI_ERROR(("Failed to find end of EROM marker\n"));
			break;
		}

		nom++;
	}

	SI_VMSG(("get_erom_ent: Returning ent 0x%08x\n", ent));
	if (inv + nom) {
		SI_VMSG(("  after %d invalid and %d non-matching entries\n", inv, nom));
	}
	return ent;
}

static uint32
get_asd(const si_t *sih, uint32 **eromptr, uint sp, uint ad, uint st, uint32 *addrl, uint32 *addrh,
	uint32 *sizel, uint32 *sizeh)
{
	uint32 asd, sz, szd;

	BCM_REFERENCE(ad);

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

/* Parse the enumeration rom to identify all cores */
void
BCMATTACHFN(ai_scan)(si_t *sih, void *regs, uint devid)
{
	si_info_t *sii = SI_INFO(sih);
	si_cores_info_t *cores_info = (si_cores_info_t *)sii->cores_info;
	chipcregs_t *cc = (chipcregs_t *)regs;
	uint32 erombase, *eromptr, *eromlim;
	axi_wrapper_t * axi_wrapper = sii->axi_wrapper;

	BCM_REFERENCE(devid);

	erombase = R_REG(sii->osh, &cc->eromptr);

	switch (BUSTYPE(sih->bustype)) {
	case SI_BUS:
		eromptr = (uint32 *)REG_MAP(erombase, SI_CORE_SIZE);
		break;

	case PCI_BUS:
		/* Set wrappers address */
		sii->curwrap = (void *)((uintptr)regs + SI_CORE_SIZE);

		/* Now point the window at the erom */
		OSL_PCI_WRITE_CONFIG(sii->osh, PCI_BAR0_WIN, 4, erombase);
		eromptr = regs;
		break;

#ifdef BCMSDIO
	case SPI_BUS:
	case SDIO_BUS:
		eromptr = (uint32 *)(uintptr)erombase;
		break;
#endif	/* BCMSDIO */

	default:
		SI_ERROR(("Don't know how to do AXI enumeration on bus %d\n", sih->bustype));
		ASSERT(0);
		return;
	}
	eromlim = eromptr + (ER_REMAPCONTROL / sizeof(uint32));
	sii->axi_num_wrappers = 0;

	SI_VMSG(("ai_scan: regs = 0x%p, erombase = 0x%08x, eromptr = 0x%p, eromlim = 0x%p\n",
	         OSL_OBFUSCATE_BUF(regs), erombase,
		OSL_OBFUSCATE_BUF(eromptr), OSL_OBFUSCATE_BUF(eromlim)));
	while (eromptr < eromlim) {
		uint32 cia, cib, cid, mfg, crev, nmw, nsw, nmp, nsp;
		uint32 mpd, asd, addrl, addrh, sizel, sizeh;
		uint i, j, idx;
		bool br;

		br = FALSE;

		/* Grok a component */
		cia = get_erom_ent(sih, &eromptr, ER_TAG, ER_CI);
		if (cia == (ER_END | ER_VALID)) {
			SI_VMSG(("Found END of erom after %d cores\n", sii->numcores));
			return;
		}

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

#ifdef BCMDBG_SI
		SI_VMSG(("Found component 0x%04x/0x%04x rev %d at erom addr 0x%p, with nmw = %d, "
		         "nsw = %d, nmp = %d & nsp = %d\n",
		         mfg, cid, crev, OSL_OBFUSCATE_BUF(eromptr - 1), nmw, nsw, nmp, nsp));
#else
		BCM_REFERENCE(crev);
#endif

		/* Include Default slave wrapper for timeout monitoring */
		if ((nsp == 0 && nsw == 0) ||
#if !defined(AXI_TIMEOUTS) && !defined(AXI_TIMEOUTS_NIC)
			((mfg == MFGID_ARM) && (cid == DEF_AI_COMP)) ||
#else
			((CHIPTYPE(sii->pub.socitype) == SOCI_NAI) &&
			(mfg == MFGID_ARM) && (cid == DEF_AI_COMP)) ||
#endif /* !defined(AXI_TIMEOUTS) && !defined(AXI_TIMEOUTS_NIC) */
			FALSE) {
			continue;
		}

		if ((nmw + nsw == 0)) {
			/* A component which is not a core */
			/* Should record some info */
			if (cid == OOB_ROUTER_CORE_ID) {
				asd = get_asd(sih, &eromptr, 0, 0, AD_ST_SLAVE,
					&addrl, &addrh, &sizel, &sizeh);
				if (asd != 0) {
					if ((sii->oob_router != 0) && (sii->oob_router != addrl)) {
						sii->oob_router1 = addrl;
					} else {
						sii->oob_router = addrl;
					}
				}
			}
			if ((cid != NS_CCB_CORE_ID) && (cid != PMU_CORE_ID) &&
				(cid != GCI_CORE_ID) && (cid != SR_CORE_ID) &&
				(cid != HUB_CORE_ID) && (cid != HND_OOBR_CORE_ID) &&
				(cid != CCI400_CORE_ID) && (cid != SPMI_SLAVE_CORE_ID)) {
				continue;
			}
		}

		idx = sii->numcores;

		cores_info->cia[idx] = cia;
		cores_info->cib[idx] = cib;
		cores_info->coreid[idx] = cid;

		/* workaround the fact the variable buscoretype is used in _ai_set_coreidx()
		 * when checking PCIE_GEN2() for PCI_BUS case before it is setup later...,
		 * both use and setup happen in si_buscore_setup().
		 */
		if (BUSTYPE(sih->bustype) == PCI_BUS &&
		    (cid == PCI_CORE_ID || cid == PCIE_CORE_ID || cid == PCIE2_CORE_ID)) {
			sii->pub.buscoretype = (uint16)cid;
		}

		for (i = 0; i < nmp; i++) {
			mpd = get_erom_ent(sih, &eromptr, ER_VALID, ER_VALID);
			if ((mpd & ER_TAG) != ER_MP) {
				SI_ERROR(("Not enough MP entries for component 0x%x\n", cid));
				goto error;
			}
			/* Record something? */
			SI_VMSG(("  Master port %d, mp: %d id: %d\n", i,
			         (mpd & MPD_MP_MASK) >> MPD_MP_SHIFT,
			         (mpd & MPD_MUI_MASK) >> MPD_MUI_SHIFT));
		}

		/* First Slave Address Descriptor should be port 0:
		 * the main register space for the core
		 */
		asd = get_asd(sih, &eromptr, 0, 0, AD_ST_SLAVE, &addrl, &addrh, &sizel, &sizeh);
		if (asd == 0) {
			do {
			/* Try again to see if it is a bridge */
			asd = get_asd(sih, &eromptr, 0, 0, AD_ST_BRIDGE, &addrl, &addrh,
			              &sizel, &sizeh);
			if (asd != 0)
				br = TRUE;
			else {
					break;
				}
			} while (1);
		} else {
			if (addrl == 0 || sizel == 0) {
				SI_ERROR((" Invalid ASD %x for slave port \n", asd));
				goto error;
			}
			cores_info->coresba[idx] = addrl;
			cores_info->coresba_size[idx] = sizel;
		}

		/* Get any more ASDs in first port */
		j = 1;
		do {
			asd = get_asd(sih, &eromptr, 0, j, AD_ST_SLAVE, &addrl, &addrh,
			              &sizel, &sizeh);
			/* Support ARM debug core ASD with address space > 4K */
			if ((asd != 0) && (j == 1)) {
				SI_VMSG(("Warning: sizel > 0x1000\n"));
				cores_info->coresba2[idx] = addrl;
				cores_info->coresba2_size[idx] = sizel;
			}
			j++;
		} while (asd != 0);

		/* Go through the ASDs for other slave ports */
		for (i = 1; i < nsp; i++) {
			j = 0;
			do {
				asd = get_asd(sih, &eromptr, i, j, AD_ST_SLAVE, &addrl, &addrh,
				              &sizel, &sizeh);
				/* To get the first base address of second slave port */
				if ((asd != 0) && (i == 1) && (j == 0)) {
					cores_info->csp2ba[idx] = addrl;
					cores_info->csp2ba_size[idx] = sizel;
				}
				if (asd == 0)
					break;
				j++;
			} while (1);
			if (j == 0) {
				SI_ERROR((" SP %d has no address descriptors\n", i));
				goto error;
			}
		}

		/* Now get master wrappers */
		for (i = 0; i < nmw; i++) {
			asd = get_asd(sih, &eromptr, i, 0, AD_ST_MWRAP, &addrl, &addrh,
			              &sizel, &sizeh);
			if (asd == 0) {
				SI_ERROR(("Missing descriptor for MW %d\n", i));
				goto error;
			}
			if ((sizeh != 0) || (sizel != SI_CORE_SIZE)) {
				SI_ERROR(("Master wrapper %d is not 4KB\n", i));
				goto error;
			}
			if (i == 0) {
				cores_info->wrapba[idx] = addrl;
			} else if (i == 1) {
				cores_info->wrapba2[idx] = addrl;
			} else if (i == 2) {
				cores_info->wrapba3[idx] = addrl;
			}

			if (axi_wrapper && (sii->axi_num_wrappers < SI_MAX_AXI_WRAPPERS)) {
				axi_wrapper[sii->axi_num_wrappers].mfg = mfg;
				axi_wrapper[sii->axi_num_wrappers].cid = cid;
				axi_wrapper[sii->axi_num_wrappers].rev = crev;
				axi_wrapper[sii->axi_num_wrappers].wrapper_type = AI_MASTER_WRAPPER;
				axi_wrapper[sii->axi_num_wrappers].wrapper_addr = addrl;
				sii->axi_num_wrappers++;
				SI_VMSG(("MASTER WRAPPER: %d, mfg:%x, cid:%x,"
					"rev:%x, addr:%x, size:%x\n",
					sii->axi_num_wrappers, mfg, cid, crev, addrl, sizel));
			}
		}

		/* And finally slave wrappers */
		for (i = 0; i < nsw; i++) {
			uint fwp = (nsp <= 1) ? 0 : 1;
			asd = get_asd(sih, &eromptr, fwp + i, 0, AD_ST_SWRAP, &addrl, &addrh,
			              &sizel, &sizeh);
			if (asd == 0) {
				SI_ERROR(("Missing descriptor for SW %d cid %x eromp %p fwp %d \n",
					i, cid, eromptr, fwp));
				goto error;
			}

			if ((sizeh != 0) || (sizel != SI_CORE_SIZE)) {
				SI_ERROR(("Slave wrapper %d is not 4KB\n", i));
				goto error;
			}

			/* cache APB bridge wrapper address for set/clear timeout */
			if ((mfg == MFGID_ARM) && (cid == APB_BRIDGE_ID)) {
				ASSERT(sii->num_br < SI_MAXBR);
				sii->br_wrapba[sii->num_br++] = addrl;
			}

			if ((mfg == MFGID_ARM) && (cid == ADB_BRIDGE_ID)) {
				br = TRUE;
			}

			BCM_REFERENCE(br);

			if ((nmw == 0) && (i == 0)) {
				cores_info->wrapba[idx] = addrl;
			} else if ((nmw == 0) && (i == 1)) {
				cores_info->wrapba2[idx] = addrl;
			} else if ((nmw == 0) && (i == 2)) {
				cores_info->wrapba3[idx] = addrl;
			}

			/* Include all slave wrappers to the list to
			 * enable and monitor watchdog timeouts
			 */

			if (axi_wrapper && (sii->axi_num_wrappers < SI_MAX_AXI_WRAPPERS)) {
				axi_wrapper[sii->axi_num_wrappers].mfg = mfg;
				axi_wrapper[sii->axi_num_wrappers].cid = cid;
				axi_wrapper[sii->axi_num_wrappers].rev = crev;
				axi_wrapper[sii->axi_num_wrappers].wrapper_type = AI_SLAVE_WRAPPER;
				axi_wrapper[sii->axi_num_wrappers].wrapper_addr = addrl;

				sii->axi_num_wrappers++;

				SI_VMSG(("SLAVE WRAPPER: %d,  mfg:%x, cid:%x,"
					"rev:%x, addr:%x, size:%x\n",
					sii->axi_num_wrappers,  mfg, cid, crev, addrl, sizel));
			}
		}

#ifndef AXI_TIMEOUTS_NIC
		/* Don't record bridges and core with 0 slave ports */
		if (br || (nsp == 0)) {
			continue;
		}
#endif

		/* Done with core */
		sii->numcores++;
	}

	SI_ERROR(("Reached end of erom without finding END\n"));

error:
	sii->numcores = 0;
	return;
}

#define AI_SETCOREIDX_MAPSIZE(coreid) \
	(((coreid) == NS_CCB_CORE_ID) ? 15 * SI_CORE_SIZE : SI_CORE_SIZE)

/* This function changes the logical "focus" to the indicated core.
 * Return the current core's virtual address.
 */
static volatile void *
BCMPOSTTRAPFN(_ai_setcoreidx)(si_t *sih, uint coreidx, uint use_wrapn)
{
	si_info_t *sii = SI_INFO(sih);
	si_cores_info_t *cores_info = (si_cores_info_t *)sii->cores_info;
	uint32 addr, wrap, wrap2, wrap3;
	volatile void *regs;

	if (coreidx >= MIN(sii->numcores, SI_MAXCORES))
		return (NULL);

	addr = cores_info->coresba[coreidx];
	wrap = cores_info->wrapba[coreidx];
	wrap2 = cores_info->wrapba2[coreidx];
	wrap3 = cores_info->wrapba3[coreidx];

#ifdef AXI_TIMEOUTS_NIC
	/* No need to disable interrupts while entering/exiting APB bridge core */
	if ((cores_info->coreid[coreidx] != APB_BRIDGE_CORE_ID) &&
		(cores_info->coreid[sii->curidx] != APB_BRIDGE_CORE_ID))
#endif /* AXI_TIMEOUTS_NIC */
	{
		/*
		 * If the user has provided an interrupt mask enabled function,
		 * then assert interrupts are disabled before switching the core.
		 */
		ASSERT((sii->intrsenabled_fn == NULL) ||
			!(*(sii)->intrsenabled_fn)((sii)->intr_arg));
	}

	switch (BUSTYPE(sih->bustype)) {
	case SI_BUS:
		/* map new one */
		if (!cores_info->regs[coreidx]) {
			cores_info->regs[coreidx] = REG_MAP(addr,
				AI_SETCOREIDX_MAPSIZE(cores_info->coreid[coreidx]));
			ASSERT(GOODREGS(cores_info->regs[coreidx]));
		}
		sii->curmap = regs = cores_info->regs[coreidx];
		if (!cores_info->wrappers[coreidx] && (wrap != 0)) {
			cores_info->wrappers[coreidx] = REG_MAP(wrap, SI_CORE_SIZE);
			ASSERT(GOODREGS(cores_info->wrappers[coreidx]));
		}
		if (!cores_info->wrappers2[coreidx] && (wrap2 != 0)) {
			cores_info->wrappers2[coreidx] = REG_MAP(wrap2, SI_CORE_SIZE);
			ASSERT(GOODREGS(cores_info->wrappers2[coreidx]));
		}
		if (!cores_info->wrappers3[coreidx] && (wrap3 != 0)) {
			cores_info->wrappers3[coreidx] = REG_MAP(wrap3, SI_CORE_SIZE);
			ASSERT(GOODREGS(cores_info->wrappers3[coreidx]));
		}

		if (use_wrapn == 2) {
			sii->curwrap = cores_info->wrappers3[coreidx];
		} else if (use_wrapn == 1) {
			sii->curwrap = cores_info->wrappers2[coreidx];
		} else {
			sii->curwrap = cores_info->wrappers[coreidx];
		}
		break;

	case PCI_BUS:
		regs = sii->curmap;

		/* point bar0 2nd 4KB window to the primary wrapper */
		if (use_wrapn == 2) {
			wrap = wrap3;
		} else if (use_wrapn == 1) {
			wrap = wrap2;
		}

		/* Use BAR0 Window to support dual mac chips... */

		/* TODO: the other mac unit can't be supportd by the current BAR0 window.
		 * need to find other ways to access these cores.
		 */

		switch (sii->slice) {
		case 0: /* main/first slice */
#ifdef AXI_TIMEOUTS_NIC
			/* No need to set the BAR0 if core is APB Bridge.
			 * This is to reduce 2 PCI writes while checkng for errlog
			 */
			if (cores_info->coreid[coreidx] != APB_BRIDGE_CORE_ID)
#endif /* AXI_TIMEOUTS_NIC */
			{
				/* point bar0 window */
				OSL_PCI_WRITE_CONFIG(sii->osh, PCI_BAR0_WIN, 4, addr);
			}

			if (PCIE_GEN2(sii))
				OSL_PCI_WRITE_CONFIG(sii->osh, PCIE2_BAR0_WIN2, 4, wrap);
			else
				OSL_PCI_WRITE_CONFIG(sii->osh, PCI_BAR0_WIN2, 4, wrap);

			break;

		case 1: /* aux/second slice */
			/* PCIE GEN2 only for other slices */
			if (!PCIE_GEN2(sii)) {
				/* other slices not supported */
				SI_ERROR(("PCI GEN not supported for slice %d\n", sii->slice));
				ASSERT(0);
				break;
			}

			/* 0x4000 - 0x4fff: enum space 0x5000 - 0x5fff: wrapper space */
			regs = (volatile uint8 *)regs + PCI_SEC_BAR0_WIN_OFFSET;
			sii->curwrap = (void *)((uintptr)regs + SI_CORE_SIZE);

			/* point bar0 window */
			OSL_PCI_WRITE_CONFIG(sii->osh, PCIE2_BAR0_CORE2_WIN, 4, addr);
			OSL_PCI_WRITE_CONFIG(sii->osh, PCIE2_BAR0_CORE2_WIN2, 4, wrap);
			break;

		case 2: /* scan/third slice */
			/* PCIE GEN2 only for other slices */
			if (!PCIE_GEN2(sii)) {
				/* other slices not supported */
				SI_ERROR(("PCI GEN not supported for slice %d\n", sii->slice));
				ASSERT(0);
				break;
			}

			/* 0x9000 - 0x9fff: enum space 0xa000 - 0xafff: wrapper space */
			regs = (volatile uint8 *)regs + PCI_TER_BAR0_WIN_OFFSET;
			sii->curwrap = (void *)((uintptr)regs + SI_CORE_SIZE);

			/* point bar0 window */
			ai_corereg(sih, sih->buscoreidx, PCIE_TER_BAR0_WIN, ~0, addr);
			ai_corereg(sih, sih->buscoreidx, PCIE_TER_BAR0_WRAPPER, ~0, wrap);
			break;

		default: /* other slices */
			SI_ERROR(("BAR0 Window not supported for slice %d\n", sii->slice));
			ASSERT(0);
			break;
		}

		break;

#ifdef BCMSDIO
	case SPI_BUS:
	case SDIO_BUS:
		sii->curmap = regs = (void *)((uintptr)addr);
		if (use_wrapn)
			sii->curwrap = (void *)((uintptr)wrap2);
		else
			sii->curwrap = (void *)((uintptr)wrap);
		break;
#endif	/* BCMSDIO */

	default:
		ASSERT(0);
		sii->curmap = regs = NULL;
		break;
	}

	sii->curidx = coreidx;

	return regs;
}

volatile void *
BCMPOSTTRAPFN(ai_setcoreidx)(si_t *sih, uint coreidx)
{
	return _ai_setcoreidx(sih, coreidx, 0);
}

volatile void *
BCMPOSTTRAPFN(ai_setcoreidx_2ndwrap)(si_t *sih, uint coreidx)
{
	return _ai_setcoreidx(sih, coreidx, 1);
}

volatile void *
BCMPOSTTRAPFN(ai_setcoreidx_3rdwrap)(si_t *sih, uint coreidx)
{
	return _ai_setcoreidx(sih, coreidx, 2);
}

void
ai_coreaddrspaceX(const si_t *sih, uint asidx, uint32 *addr, uint32 *size)
{
	const si_info_t *sii = SI_INFO(sih);
	const si_cores_info_t *cores_info = (const si_cores_info_t *)sii->cores_info;
	chipcregs_t *cc = NULL;
	uint32 erombase, *eromptr, *eromlim;
	uint i, j, cidx;
	uint32 cia, cib, nmp, nsp;
	uint32 asd, addrl, addrh, sizel, sizeh;

	for (i = 0; i < sii->numcores; i++) {
		if (cores_info->coreid[i] == CC_CORE_ID) {
			cc = (chipcregs_t *)cores_info->regs[i];
			break;
		}
	}
	if (cc == NULL)
		goto error;

	BCM_REFERENCE(erombase);
	erombase = R_REG(sii->osh, &cc->eromptr);
	eromptr = (uint32 *)REG_MAP(erombase, SI_CORE_SIZE);
	eromlim = eromptr + (ER_REMAPCONTROL / sizeof(uint32));

	cidx = sii->curidx;
	cia = cores_info->cia[cidx];
	cib = cores_info->cib[cidx];

	nmp = (cib & CIB_NMP_MASK) >> CIB_NMP_SHIFT;
	nsp = (cib & CIB_NSP_MASK) >> CIB_NSP_SHIFT;

	/* scan for cores */
	while (eromptr < eromlim) {
		if ((get_erom_ent(sih, &eromptr, ER_TAG, ER_CI) == cia) &&
			(get_erom_ent(sih, &eromptr, 0, 0) == cib)) {
			break;
		}
	}

	/* skip master ports */
	for (i = 0; i < nmp; i++)
		get_erom_ent(sih, &eromptr, ER_VALID, ER_VALID);

	/* Skip ASDs in port 0 */
	asd = get_asd(sih, &eromptr, 0, 0, AD_ST_SLAVE, &addrl, &addrh, &sizel, &sizeh);
	if (asd == 0) {
		/* Try again to see if it is a bridge */
		asd = get_asd(sih, &eromptr, 0, 0, AD_ST_BRIDGE, &addrl, &addrh,
		              &sizel, &sizeh);
	}

	j = 1;
	do {
		asd = get_asd(sih, &eromptr, 0, j, AD_ST_SLAVE, &addrl, &addrh,
		              &sizel, &sizeh);
		j++;
	} while (asd != 0);

	/* Go through the ASDs for other slave ports */
	for (i = 1; i < nsp; i++) {
		j = 0;
		do {
			asd = get_asd(sih, &eromptr, i, j, AD_ST_SLAVE, &addrl, &addrh,
				&sizel, &sizeh);
			if (asd == 0)
				break;

			if (!asidx--) {
				*addr = addrl;
				*size = sizel;
				return;
			}
			j++;
		} while (1);

		if (j == 0) {
			SI_ERROR((" SP %d has no address descriptors\n", i));
			break;
		}
	}

error:
	*size = 0;
	return;
}

/* Return the number of address spaces in current core */
int
ai_numaddrspaces(const si_t *sih)
{
	/* TODO: Either save it or parse the EROM on demand, currently hardcode 2 */
	BCM_REFERENCE(sih);

	return 2;
}

/* Return the address of the nth address space in the current core
 * Arguments:
 * sih : Pointer to struct si_t
 * spidx : slave port index
 * baidx : base address index
 */
uint32
ai_addrspace(const si_t *sih, uint spidx, uint baidx)
{
	const si_info_t *sii = SI_INFO(sih);
	const si_cores_info_t *cores_info = (const si_cores_info_t *)sii->cores_info;
	uint cidx;

	cidx = sii->curidx;

	if (spidx == CORE_SLAVE_PORT_0) {
		if (baidx == CORE_BASE_ADDR_0)
			return cores_info->coresba[cidx];
		else if (baidx == CORE_BASE_ADDR_1)
			return cores_info->coresba2[cidx];
	}
	else if (spidx == CORE_SLAVE_PORT_1) {
		if (baidx == CORE_BASE_ADDR_0)
			return cores_info->csp2ba[cidx];
	}

	SI_ERROR(("ai_addrspace: Need to parse the erom again to find %d base addr"
		" in %d slave port\n",
		baidx, spidx));

	return 0;

}

/* Return the size of the nth address space in the current core
* Arguments:
* sih : Pointer to struct si_t
* spidx : slave port index
* baidx : base address index
*/
uint32
ai_addrspacesize(const si_t *sih, uint spidx, uint baidx)
{
	const si_info_t *sii = SI_INFO(sih);
	const si_cores_info_t *cores_info = (const si_cores_info_t *)sii->cores_info;
	uint cidx;

	cidx = sii->curidx;
	if (spidx == CORE_SLAVE_PORT_0) {
		if (baidx == CORE_BASE_ADDR_0)
			return cores_info->coresba_size[cidx];
		else if (baidx == CORE_BASE_ADDR_1)
			return cores_info->coresba2_size[cidx];
	}
	else if (spidx == CORE_SLAVE_PORT_1) {
		if (baidx == CORE_BASE_ADDR_0)
			return cores_info->csp2ba_size[cidx];
	}

	SI_ERROR(("ai_addrspacesize: Need to parse the erom again to find %d"
		" base addr in %d slave port\n",
		baidx, spidx));

	return 0;
}

uint
ai_flag(si_t *sih)
{
	const si_info_t *sii = SI_INFO(sih);
#if !defined(BCMDONGLEHOST)
	const si_cores_info_t *cores_info = (const si_cores_info_t *)sii->cores_info;
#endif
	aidmp_t *ai;

	if (PMU_DMP()) {
		uint idx, flag;
		idx = sii->curidx;
		ai_setcoreidx(sih, SI_CC_IDX);
		flag = ai_flag_alt(sih);
		ai_setcoreidx(sih, idx);
		return flag;
	}

	ai = sii->curwrap;
	ASSERT(ai != NULL);

	return (R_REG(sii->osh, &ai->oobselouta30) & 0x1f);
}

uint
ai_flag_alt(const si_t *sih)
{
	const si_info_t *sii = SI_INFO(sih);
	aidmp_t *ai = sii->curwrap;

	return ((R_REG(sii->osh, &ai->oobselouta30) >> AI_OOBSEL_1_SHIFT) & AI_OOBSEL_MASK);
}

void
ai_setint(const si_t *sih, int siflag)
{
	BCM_REFERENCE(sih);
	BCM_REFERENCE(siflag);

	/* TODO: Figure out how to set interrupt mask in ai */
}

uint
BCMPOSTTRAPFN(ai_wrap_reg)(const si_t *sih, uint32 offset, uint32 mask, uint32 val)
{
	const si_info_t *sii = SI_INFO(sih);
	uint32 *addr = (uint32 *) ((uchar *)(sii->curwrap) + offset);

	if (mask || val) {
		uint32 w = R_REG(sii->osh, addr);
		w &= ~mask;
		w |= val;
		W_REG(sii->osh, addr, w);
	}
	return (R_REG(sii->osh, addr));
}

uint
ai_corevendor(const si_t *sih)
{
	const si_info_t *sii = SI_INFO(sih);
	const si_cores_info_t *cores_info = (const si_cores_info_t *)sii->cores_info;
	uint32 cia;

	cia = cores_info->cia[sii->curidx];
	return ((cia & CIA_MFG_MASK) >> CIA_MFG_SHIFT);
}

uint
BCMPOSTTRAPFN(ai_corerev)(const si_t *sih)
{
	const si_info_t *sii = SI_INFO(sih);
	const si_cores_info_t *cores_info = (const si_cores_info_t *)sii->cores_info;
	uint32 cib;

	cib = cores_info->cib[sii->curidx];
	return ((cib & CIB_REV_MASK) >> CIB_REV_SHIFT);
}

uint
ai_corerev_minor(const si_t *sih)
{
	return (ai_core_sflags(sih, 0, 0) >> SISF_MINORREV_D11_SHIFT) &
			SISF_MINORREV_D11_MASK;
}

bool
BCMPOSTTRAPFN(ai_iscoreup)(const si_t *sih)
{
	const si_info_t *sii = SI_INFO(sih);
	aidmp_t *ai = sii->curwrap;

	return (((R_REG(sii->osh, &ai->ioctrl) & (SICF_FGC | SICF_CLOCK_EN)) == SICF_CLOCK_EN) &&
	        ((R_REG(sii->osh, &ai->resetctrl) & AIRC_RESET) == 0));
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
uint
BCMPOSTTRAPFN(ai_corereg)(si_t *sih, uint coreidx, uint regoff, uint mask, uint val)
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

	if (BUSTYPE(sih->bustype) == SI_BUS) {
		/* If internal bus, we can always get at everything */
		fast = TRUE;
		/* map if does not exist */
		if (!cores_info->regs[coreidx]) {
			cores_info->regs[coreidx] = REG_MAP(cores_info->coresba[coreidx],
			                            SI_CORE_SIZE);
			ASSERT(GOODREGS(cores_info->regs[coreidx]));
		}
		r = (volatile uint32 *)((volatile uchar *)cores_info->regs[coreidx] + regoff);
	} else if (BUSTYPE(sih->bustype) == PCI_BUS) {
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
		r = (volatile uint32*) ((volatile uchar*) ai_setcoreidx(&sii->pub, coreidx) +
		               regoff);
	}
	ASSERT(r != NULL);

	/* mask and set */
	if (mask || val) {
		w = (R_REG(sii->osh, r) & ~mask) | val;
		W_REG(sii->osh, r, w);
	}

	/* readback */
	w = R_REG(sii->osh, r);

	if (!fast) {
		/* restore core index */
		if (origidx != coreidx)
			ai_setcoreidx(&sii->pub, origidx);

		INTR_RESTORE(sii, &intr_val);
	}

	return (w);
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
uint
ai_corereg_writeonly(si_t *sih, uint coreidx, uint regoff, uint mask, uint val)
{
	uint origidx = 0;
	volatile uint32 *r = NULL;
	uint w = 0;
	bcm_int_bitmask_t intr_val;
	bool fast = FALSE;
	si_info_t *sii = SI_INFO(sih);
	si_cores_info_t *cores_info = (si_cores_info_t *)sii->cores_info;

	ASSERT(GOODIDX(coreidx, sii->numcores));
	ASSERT(regoff < SI_CORE_SIZE);
	ASSERT((val & ~mask) == 0);

	if (coreidx >= SI_MAXCORES)
		return 0;

	if (BUSTYPE(sih->bustype) == SI_BUS) {
		/* If internal bus, we can always get at everything */
		fast = TRUE;
		/* map if does not exist */
		if (!cores_info->regs[coreidx]) {
			cores_info->regs[coreidx] = REG_MAP(cores_info->coresba[coreidx],
			                            SI_CORE_SIZE);
			ASSERT(GOODREGS(cores_info->regs[coreidx]));
		}
		r = (volatile uint32 *)((volatile uchar *)cores_info->regs[coreidx] + regoff);
	} else if (BUSTYPE(sih->bustype) == PCI_BUS) {
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
		r = (volatile uint32*) ((volatile uchar*) ai_setcoreidx(&sii->pub, coreidx) +
		               regoff);
	}
	ASSERT(r != NULL);

	/* mask and set */
	if (mask || val) {
		w = (R_REG(sii->osh, r) & ~mask) | val;
		W_REG(sii->osh, r, w);
	}

	if (!fast) {
		/* restore core index */
		if (origidx != coreidx)
			ai_setcoreidx(&sii->pub, origidx);

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
BCMPOSTTRAPFN(ai_corereg_addr)(si_t *sih, uint coreidx, uint regoff)
{
	volatile uint32 *r = NULL;
	bool fast = FALSE;
	si_info_t *sii = SI_INFO(sih);
	si_cores_info_t *cores_info = (si_cores_info_t *)sii->cores_info;

	ASSERT(GOODIDX(coreidx, sii->numcores));
	ASSERT(regoff < SI_CORE_SIZE);

	if (coreidx >= SI_MAXCORES)
		return 0;

	if (BUSTYPE(sih->bustype) == SI_BUS) {
		/* If internal bus, we can always get at everything */
		fast = TRUE;
		/* map if does not exist */
		if (!cores_info->regs[coreidx]) {
			cores_info->regs[coreidx] = REG_MAP(cores_info->coresba[coreidx],
			                            SI_CORE_SIZE);
			ASSERT(GOODREGS(cores_info->regs[coreidx]));
		}
		r = (volatile uint32 *)((volatile uchar *)cores_info->regs[coreidx] + regoff);
	} else if (BUSTYPE(sih->bustype) == PCI_BUS) {
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
		ASSERT(sii->curidx == coreidx);
		r = (volatile uint32*) ((volatile uchar*)sii->curmap + regoff);
	}

	return (r);
}

void
ai_core_disable(const si_t *sih, uint32 bits)
{
	const si_info_t *sii = SI_INFO(sih);
	volatile uint32 dummy;
	uint32 status;
	aidmp_t *ai;

	ASSERT(GOODREGS(sii->curwrap));
	ai = sii->curwrap;

	/* if core is already in reset, just return */
	if (R_REG(sii->osh, &ai->resetctrl) & AIRC_RESET) {
		return;
	}

	/* ensure there are no pending backplane operations */
	SPINWAIT(((status = R_REG(sii->osh, &ai->resetstatus)) != 0), 300);

	/* if pending backplane ops still, try waiting longer */
	if (status != 0) {
		/* 300usecs was sufficient to allow backplane ops to clear for big hammer */
		/* during driver load we may need more time */
		SPINWAIT(((status = R_REG(sii->osh, &ai->resetstatus)) != 0), 10000);
		/* if still pending ops, continue on and try disable anyway */
		/* this is in big hammer path, so don't call wl_reinit in this case... */
#ifdef BCMDBG
		if (status != 0) {
			SI_ERROR(("ai_core_disable: WARN: resetstatus=%0x on core disable\n",
				status));
		}
#endif
	}

	W_REG(sii->osh, &ai->resetctrl, AIRC_RESET);
	dummy = R_REG(sii->osh, &ai->resetctrl);
	BCM_REFERENCE(dummy);
	OSL_DELAY(1);

	W_REG(sii->osh, &ai->ioctrl, bits);
	dummy = R_REG(sii->osh, &ai->ioctrl);
	BCM_REFERENCE(dummy);
	OSL_DELAY(10);
}

/* reset and re-enable a core
 * inputs:
 * bits - core specific bits that are set during and after reset sequence
 * resetbits - core specific bits that are set only during reset sequence
 */
static void
BCMPOSTTRAPFN(_ai_core_reset)(const si_t *sih, uint32 bits, uint32 resetbits)
{
	const si_info_t *sii = SI_INFO(sih);
	aidmp_t *ai;
	volatile uint32 dummy;
	uint loop_counter = 10;

	ASSERT(GOODREGS(sii->curwrap));
	ai = sii->curwrap;

	/* ensure there are no pending backplane operations */
	SPINWAIT(((dummy = R_REG(sii->osh, &ai->resetstatus)) != 0), 300);

#ifdef BCMDBG_ERR
	if (dummy != 0) {
		SI_ERROR(("_ai_core_reset: WARN1: resetstatus=0x%0x\n", dummy));
	}
#endif /* BCMDBG_ERR */

	/* put core into reset state */
	W_REG(sii->osh, &ai->resetctrl, AIRC_RESET);
	OSL_DELAY(10);

	/* ensure there are no pending backplane operations */
	SPINWAIT((R_REG(sii->osh, &ai->resetstatus) != 0), 300);

	W_REG(sii->osh, &ai->ioctrl, (bits | resetbits | SICF_FGC | SICF_CLOCK_EN));
	dummy = R_REG(sii->osh, &ai->ioctrl);
	BCM_REFERENCE(dummy);
#ifdef UCM_CORRUPTION_WAR
	if (si_coreid(sih) == D11_CORE_ID) {
		/* Reset FGC */
		OSL_DELAY(1);
		W_REG(sii->osh, &ai->ioctrl, (dummy & (~SICF_FGC)));
	}
#endif /* UCM_CORRUPTION_WAR */
	/* ensure there are no pending backplane operations */
	SPINWAIT(((dummy = R_REG(sii->osh, &ai->resetstatus)) != 0), 300);

#ifdef BCMDBG_ERR
	if (dummy != 0)
		SI_ERROR(("_ai_core_reset: WARN2: resetstatus=0x%0x\n", dummy));
#endif

	while (R_REG(sii->osh, &ai->resetctrl) != 0 && --loop_counter != 0) {
		/* ensure there are no pending backplane operations */
		SPINWAIT(((dummy = R_REG(sii->osh, &ai->resetstatus)) != 0), 300);

#ifdef BCMDBG_ERR
		if (dummy != 0)
			SI_ERROR(("_ai_core_reset: WARN3 resetstatus=0x%0x\n", dummy));
#endif

		/* take core out of reset */
		W_REG(sii->osh, &ai->resetctrl, 0);

		/* ensure there are no pending backplane operations */
		SPINWAIT((R_REG(sii->osh, &ai->resetstatus) != 0), 300);
	}

#ifdef BCMDBG_ERR
	if (loop_counter == 0) {
		SI_ERROR(("_ai_core_reset: Failed to take core 0x%x out of reset\n",
			si_coreid(sih)));
	}
#endif

#ifdef UCM_CORRUPTION_WAR
	/* Pulse FGC after lifting Reset */
	W_REG(sii->osh, &ai->ioctrl, (bits | SICF_FGC | SICF_CLOCK_EN));
#else
	W_REG(sii->osh, &ai->ioctrl, (bits | SICF_CLOCK_EN));
#endif /* UCM_CORRUPTION_WAR */
	dummy = R_REG(sii->osh, &ai->ioctrl);
	BCM_REFERENCE(dummy);
#ifdef UCM_CORRUPTION_WAR
	if (si_coreid(sih) == D11_CORE_ID) {
		/* Reset FGC */
		OSL_DELAY(1);
		W_REG(sii->osh, &ai->ioctrl, (dummy & (~SICF_FGC)));
	}
#endif /* UCM_CORRUPTION_WAR */
	OSL_DELAY(1);
}

void
BCMPOSTTRAPFN(ai_core_reset)(si_t *sih, uint32 bits, uint32 resetbits)
{
	si_info_t *sii = SI_INFO(sih);
	const si_cores_info_t *cores_info = (const si_cores_info_t *)sii->cores_info;
	uint idx = sii->curidx;

	if (cores_info->wrapba3[idx] != 0) {
		ai_setcoreidx_3rdwrap(sih, idx);
		_ai_core_reset(sih, bits, resetbits);
		ai_setcoreidx(sih, idx);
	}

	if (cores_info->wrapba2[idx] != 0) {
		ai_setcoreidx_2ndwrap(sih, idx);
		_ai_core_reset(sih, bits, resetbits);
		ai_setcoreidx(sih, idx);
	}

	_ai_core_reset(sih, bits, resetbits);
}

#ifdef BOOKER_NIC400_INF
void
BCMPOSTTRAPFN(ai_core_reset_ext)(const si_t *sih, uint32 bits, uint32 resetbits)
{
	_ai_core_reset(sih, bits, resetbits);
}
#endif /* BOOKER_NIC400_INF */

void
ai_core_cflags_wo(const si_t *sih, uint32 mask, uint32 val)
{
	const si_info_t *sii = SI_INFO(sih);
#if !defined(BCMDONGLEHOST)
	const si_cores_info_t *cores_info = (const si_cores_info_t *)sii->cores_info;
#endif
	aidmp_t *ai;
	uint32 w;

	if (PMU_DMP()) {
		SI_ERROR(("ai_core_cflags_wo: Accessing PMU DMP register (ioctrl)\n"));
		return;
	}

	ASSERT(GOODREGS(sii->curwrap));
	ai = sii->curwrap;

	ASSERT((val & ~mask) == 0);

	if (mask || val) {
		w = ((R_REG(sii->osh, &ai->ioctrl) & ~mask) | val);
		W_REG(sii->osh, &ai->ioctrl, w);
	}
}

uint32
BCMPOSTTRAPFN(ai_core_cflags)(const si_t *sih, uint32 mask, uint32 val)
{
	const si_info_t *sii = SI_INFO(sih);
#if !defined(BCMDONGLEHOST)
	const si_cores_info_t *cores_info = (const si_cores_info_t *)sii->cores_info;
#endif
	aidmp_t *ai;
	uint32 w;

	if (PMU_DMP()) {
		SI_ERROR(("ai_core_cflags: Accessing PMU DMP register (ioctrl)\n"));
		return 0;
	}
	ASSERT(GOODREGS(sii->curwrap));
	ai = sii->curwrap;

	ASSERT((val & ~mask) == 0);

	if (mask || val) {
		w = ((R_REG(sii->osh, &ai->ioctrl) & ~mask) | val);
		W_REG(sii->osh, &ai->ioctrl, w);
	}

	return R_REG(sii->osh, &ai->ioctrl);
}

uint32
ai_core_sflags(const si_t *sih, uint32 mask, uint32 val)
{
	const si_info_t *sii = SI_INFO(sih);
#if !defined(BCMDONGLEHOST)
	const si_cores_info_t *cores_info = (const si_cores_info_t *)sii->cores_info;
#endif
	aidmp_t *ai;
	uint32 w;

	if (PMU_DMP()) {
		SI_ERROR(("ai_core_sflags: Accessing PMU DMP register (ioctrl)\n"));
		return 0;
	}

	ASSERT(GOODREGS(sii->curwrap));
	ai = sii->curwrap;

	ASSERT((val & ~mask) == 0);
	ASSERT((mask & ~SISF_CORE_BITS) == 0);

	if (mask || val) {
		w = ((R_REG(sii->osh, &ai->iostatus) & ~mask) | val);
		W_REG(sii->osh, &ai->iostatus, w);
	}

	return R_REG(sii->osh, &ai->iostatus);
}

#if defined(BCMDBG) || defined(BCMDBG_DUMP) || defined(BCMDBG_PHYDUMP)
/* print interesting aidmp registers */
void
ai_dumpregs(const si_t *sih, struct bcmstrbuf *b)
{
	const si_info_t *sii = SI_INFO(sih);
	osl_t *osh;
	aidmp_t *ai;
	uint i;
	uint32 prev_value = 0;
	const axi_wrapper_t * axi_wrapper = sii->axi_wrapper;
	uint32 cfg_reg = 0;
	uint bar0_win_offset = 0;

	osh = sii->osh;

	/* Save and restore wrapper access window */
	if (BUSTYPE(sii->pub.bustype) == PCI_BUS) {
		if (PCIE_GEN2(sii)) {
			cfg_reg = PCIE2_BAR0_CORE2_WIN2;
			bar0_win_offset = PCIE2_BAR0_CORE2_WIN2_OFFSET;
		} else {
			cfg_reg = PCI_BAR0_WIN2;
			bar0_win_offset = PCI_BAR0_WIN2_OFFSET;
		}

		prev_value = OSL_PCI_READ_CONFIG(osh, cfg_reg, 4);

		if (prev_value == ID32_INVALID) {
			SI_PRINT(("ai_dumpregs, PCI_BAR0_WIN2 - %x\n", prev_value));
			return;
		}
	}

	bcm_bprintf(b, "ChipNum:%x, ChipRev;%x, BusType:%x, BoardType:%x, BoardVendor:%x\n\n",
		sih->chip, sih->chiprev, sih->bustype, sih->boardtype, sih->boardvendor);

	for (i = 0; i < sii->axi_num_wrappers; i++) {

		if (BUSTYPE(sii->pub.bustype) == PCI_BUS) {
			/* Set BAR0 window to bridge wapper base address */
			OSL_PCI_WRITE_CONFIG(osh,
				cfg_reg, 4, axi_wrapper[i].wrapper_addr);

			ai = (aidmp_t *) ((volatile uint8*)sii->curmap + bar0_win_offset);
		} else {
			ai = (aidmp_t *)(uintptr) axi_wrapper[i].wrapper_addr;
		}

		bcm_bprintf(b, "core 0x%x: core_rev:%d, %s_WR ADDR:%x \n", axi_wrapper[i].cid,
			axi_wrapper[i].rev,
			axi_wrapper[i].wrapper_type == AI_SLAVE_WRAPPER ? "SLAVE" : "MASTER",
			axi_wrapper[i].wrapper_addr);

		bcm_bprintf(b, "ioctrlset 0x%x ioctrlclear 0x%x ioctrl 0x%x iostatus 0x%x "
			    "ioctrlwidth 0x%x iostatuswidth 0x%x\n"
			    "resetctrl 0x%x resetstatus 0x%x resetreadid 0x%x resetwriteid 0x%x\n"
			    "errlogctrl 0x%x errlogdone 0x%x errlogstatus 0x%x "
			    "errlogaddrlo 0x%x errlogaddrhi 0x%x\n"
			    "errlogid 0x%x errloguser 0x%x errlogflags 0x%x\n"
			    "intstatus 0x%x config 0x%x itcr 0x%x\n\n",
			    R_REG(osh, &ai->ioctrlset),
			    R_REG(osh, &ai->ioctrlclear),
			    R_REG(osh, &ai->ioctrl),
			    R_REG(osh, &ai->iostatus),
			    R_REG(osh, &ai->ioctrlwidth),
			    R_REG(osh, &ai->iostatuswidth),
			    R_REG(osh, &ai->resetctrl),
			    R_REG(osh, &ai->resetstatus),
			    R_REG(osh, &ai->resetreadid),
			    R_REG(osh, &ai->resetwriteid),
			    R_REG(osh, &ai->errlogctrl),
			    R_REG(osh, &ai->errlogdone),
			    R_REG(osh, &ai->errlogstatus),
			    R_REG(osh, &ai->errlogaddrlo),
			    R_REG(osh, &ai->errlogaddrhi),
			    R_REG(osh, &ai->errlogid),
			    R_REG(osh, &ai->errloguser),
			    R_REG(osh, &ai->errlogflags),
			    R_REG(osh, &ai->intstatus),
			    R_REG(osh, &ai->config),
			    R_REG(osh, &ai->itcr));
	}

	/* Restore the initial wrapper space */
	if (BUSTYPE(sii->pub.bustype) == PCI_BUS) {
		if (prev_value && cfg_reg) {
			OSL_PCI_WRITE_CONFIG(osh, cfg_reg, 4, prev_value);
		}
	}
}
#endif	/* BCMDBG || BCMDBG_DUMP || BCMDBG_PHYDUMP */

#ifdef BCMDBG
static void
_ai_view(osl_t *osh, aidmp_t *ai, uint32 cid, uint32 addr, bool verbose)
{
	uint32 config;

	config = R_REG(osh, &ai->config);
	SI_PRINT(("\nCore ID: 0x%x, addr 0x%x, config 0x%x\n", cid, addr, config));

	if (config & AICFG_RST)
		SI_PRINT(("resetctrl 0x%x, resetstatus 0x%x, resetreadid 0x%x, resetwriteid 0x%x\n",
		          R_REG(osh, &ai->resetctrl), R_REG(osh, &ai->resetstatus),
		          R_REG(osh, &ai->resetreadid), R_REG(osh, &ai->resetwriteid)));

	if (config & AICFG_IOC)
		SI_PRINT(("ioctrl 0x%x, width %d\n", R_REG(osh, &ai->ioctrl),
		          R_REG(osh, &ai->ioctrlwidth)));

	if (config & AICFG_IOS)
		SI_PRINT(("iostatus 0x%x, width %d\n", R_REG(osh, &ai->iostatus),
		          R_REG(osh, &ai->iostatuswidth)));

	if (config & AICFG_ERRL) {
		SI_PRINT(("errlogctrl 0x%x, errlogdone 0x%x, errlogstatus 0x%x, intstatus 0x%x\n",
		          R_REG(osh, &ai->errlogctrl), R_REG(osh, &ai->errlogdone),
		          R_REG(osh, &ai->errlogstatus), R_REG(osh, &ai->intstatus)));
		SI_PRINT(("errlogid 0x%x, errloguser 0x%x, errlogflags 0x%x, errlogaddr "
		          "0x%x/0x%x\n",
		          R_REG(osh, &ai->errlogid), R_REG(osh, &ai->errloguser),
		          R_REG(osh, &ai->errlogflags), R_REG(osh, &ai->errlogaddrhi),
		          R_REG(osh, &ai->errlogaddrlo)));
	}

	if (verbose && (config & AICFG_OOB)) {
		SI_PRINT(("oobselina30 0x%x, oobselina74 0x%x\n",
		          R_REG(osh, &ai->oobselina30), R_REG(osh, &ai->oobselina74)));
		SI_PRINT(("oobselinb30 0x%x, oobselinb74 0x%x\n",
		          R_REG(osh, &ai->oobselinb30), R_REG(osh, &ai->oobselinb74)));
		SI_PRINT(("oobselinc30 0x%x, oobselinc74 0x%x\n",
		          R_REG(osh, &ai->oobselinc30), R_REG(osh, &ai->oobselinc74)));
		SI_PRINT(("oobselind30 0x%x, oobselind74 0x%x\n",
		          R_REG(osh, &ai->oobselind30), R_REG(osh, &ai->oobselind74)));
		SI_PRINT(("oobselouta30 0x%x, oobselouta74 0x%x\n",
		          R_REG(osh, &ai->oobselouta30), R_REG(osh, &ai->oobselouta74)));
		SI_PRINT(("oobseloutb30 0x%x, oobseloutb74 0x%x\n",
		          R_REG(osh, &ai->oobseloutb30), R_REG(osh, &ai->oobseloutb74)));
		SI_PRINT(("oobseloutc30 0x%x, oobseloutc74 0x%x\n",
		          R_REG(osh, &ai->oobseloutc30), R_REG(osh, &ai->oobseloutc74)));
		SI_PRINT(("oobseloutd30 0x%x, oobseloutd74 0x%x\n",
		          R_REG(osh, &ai->oobseloutd30), R_REG(osh, &ai->oobseloutd74)));
		SI_PRINT(("oobsynca 0x%x, oobseloutaen 0x%x\n",
		          R_REG(osh, &ai->oobsynca), R_REG(osh, &ai->oobseloutaen)));
		SI_PRINT(("oobsyncb 0x%x, oobseloutben 0x%x\n",
		          R_REG(osh, &ai->oobsyncb), R_REG(osh, &ai->oobseloutben)));
		SI_PRINT(("oobsyncc 0x%x, oobseloutcen 0x%x\n",
		          R_REG(osh, &ai->oobsyncc), R_REG(osh, &ai->oobseloutcen)));
		SI_PRINT(("oobsyncd 0x%x, oobseloutden 0x%x\n",
		          R_REG(osh, &ai->oobsyncd), R_REG(osh, &ai->oobseloutden)));
		SI_PRINT(("oobaextwidth 0x%x, oobainwidth 0x%x, oobaoutwidth 0x%x\n",
		          R_REG(osh, &ai->oobaextwidth), R_REG(osh, &ai->oobainwidth),
		          R_REG(osh, &ai->oobaoutwidth)));
		SI_PRINT(("oobbextwidth 0x%x, oobbinwidth 0x%x, oobboutwidth 0x%x\n",
		          R_REG(osh, &ai->oobbextwidth), R_REG(osh, &ai->oobbinwidth),
		          R_REG(osh, &ai->oobboutwidth)));
		SI_PRINT(("oobcextwidth 0x%x, oobcinwidth 0x%x, oobcoutwidth 0x%x\n",
		          R_REG(osh, &ai->oobcextwidth), R_REG(osh, &ai->oobcinwidth),
		          R_REG(osh, &ai->oobcoutwidth)));
		SI_PRINT(("oobdextwidth 0x%x, oobdinwidth 0x%x, oobdoutwidth 0x%x\n",
		          R_REG(osh, &ai->oobdextwidth), R_REG(osh, &ai->oobdinwidth),
		          R_REG(osh, &ai->oobdoutwidth)));
	}
}

void
ai_view(const si_t *sih, bool verbose)
{
	const si_info_t *sii = SI_INFO(sih);
	const si_cores_info_t *cores_info = (const si_cores_info_t *)sii->cores_info;
	osl_t *osh;
	aidmp_t *ai;
	uint32 cid, addr;

	ai = sii->curwrap;
	osh = sii->osh;

	if (PMU_DMP()) {
		SI_ERROR(("Cannot access pmu DMP\n"));
		return;
	}
	cid = cores_info->coreid[sii->curidx];
	addr = cores_info->wrapba[sii->curidx];
	_ai_view(osh, ai, cid, addr, verbose);
}

void
ai_viewall(si_t *sih, bool verbose)
{
	const si_info_t *sii = SI_INFO(sih);
	const si_cores_info_t *cores_info = (const si_cores_info_t *)sii->cores_info;
	osl_t *osh;
	aidmp_t *ai;
	uint32 cid, addr;
	uint i;

	osh = sii->osh;
	for (i = 0; i < sii->numcores; i++) {
		si_setcoreidx(sih, i);

		if (PMU_DMP()) {
			SI_ERROR(("Skipping pmu DMP\n"));
			continue;
		}
		ai = sii->curwrap;
		cid = cores_info->coreid[sii->curidx];
		addr = cores_info->wrapba[sii->curidx];
		_ai_view(osh, ai, cid, addr, verbose);
	}
}
#endif	/* BCMDBG */

void
ai_update_backplane_timeouts(const si_t *sih, bool enable, uint32 timeout_exp, uint32 cid)
{
#if defined(AXI_TIMEOUTS) || defined(AXI_TIMEOUTS_NIC)
	const si_info_t *sii = SI_INFO(sih);
	aidmp_t *ai;
	uint32 i;
	axi_wrapper_t * axi_wrapper = sii->axi_wrapper;
	uint32 errlogctrl = (enable << AIELC_TO_ENAB_SHIFT) |
		((timeout_exp << AIELC_TO_EXP_SHIFT) & AIELC_TO_EXP_MASK);

#ifdef AXI_TIMEOUTS_NIC
	uint32 prev_value = 0;
	osl_t *osh = sii->osh;
	uint32 cfg_reg = 0;
	uint32 offset = 0;
#endif /* AXI_TIMEOUTS_NIC */

	if ((sii->axi_num_wrappers == 0) ||
#ifdef AXI_TIMEOUTS_NIC
		(!PCIE(sii)) ||
#endif /* AXI_TIMEOUTS_NIC */
		FALSE) {
		SI_VMSG((" iai_update_backplane_timeouts, axi_num_wrappers:%d, Is_PCIE:%d,"
			" BUS_TYPE:%d, ID:%x\n",
			sii->axi_num_wrappers, PCIE(sii),
			BUSTYPE(sii->pub.bustype), sii->pub.buscoretype));
		return;
	}

#ifdef AXI_TIMEOUTS_NIC
	/* Save and restore the wrapper access window */
	if (BUSTYPE(sii->pub.bustype) == PCI_BUS) {
		if (PCIE_GEN1(sii)) {
			cfg_reg = PCI_BAR0_WIN2;
			offset = PCI_BAR0_WIN2_OFFSET;
		} else if (PCIE_GEN2(sii)) {
			cfg_reg = PCIE2_BAR0_CORE2_WIN2;
			offset = PCIE2_BAR0_CORE2_WIN2_OFFSET;
		}
		else {
			ASSERT(!"!PCIE_GEN1 && !PCIE_GEN2");
		}

		prev_value = OSL_PCI_READ_CONFIG(osh, cfg_reg, 4);
		if (prev_value == ID32_INVALID) {
			SI_PRINT(("ai_update_backplane_timeouts, PCI_BAR0_WIN2 - %x\n",
				prev_value));
			return;
		}
	}
#endif /* AXI_TIMEOUTS_NIC */

	for (i = 0; i < sii->axi_num_wrappers; ++i) {
		/* WAR for wrong EROM entries w.r.t slave and master wrapper
		 * for ADB bridge core...so checking actual wrapper config to determine type
		 * http://jira.broadcom.com/browse/HW4388-905
		*/
		if ((cid == 0 || cid == ADB_BRIDGE_ID) &&
				(axi_wrapper[i].cid == ADB_BRIDGE_ID)) {
			/* WAR is applicable only to 89B0 and 89C0 */
			if (CCREV(sih->ccrev) == 70) {
				ai = (aidmp_t *)(uintptr)axi_wrapper[i].wrapper_addr;
				if (R_REG(sii->osh, &ai->config) & WRAPPER_TIMEOUT_CONFIG) {
					axi_wrapper[i].wrapper_type  = AI_SLAVE_WRAPPER;
				} else {
					axi_wrapper[i].wrapper_type  = AI_MASTER_WRAPPER;
				}
			}
		}
		if (axi_wrapper[i].wrapper_type != AI_SLAVE_WRAPPER || ((BCM4389_CHIP(sih->chip) ||
				BCM4388_CHIP(sih->chip)) &&
				(axi_wrapper[i].wrapper_addr == WL_BRIDGE1_S ||
				axi_wrapper[i].wrapper_addr == WL_BRIDGE2_S))) {
			SI_VMSG(("SKIP ENABLE BPT: MFG:%x, CID:%x, ADDR:%x\n",
				axi_wrapper[i].mfg,
				axi_wrapper[i].cid,
				axi_wrapper[i].wrapper_addr));
			continue;
		}

		/* Update only given core if requested */
		if ((cid != 0) && (axi_wrapper[i].cid != cid)) {
			continue;
		}

#ifdef AXI_TIMEOUTS_NIC
		if (BUSTYPE(sii->pub.bustype) == PCI_BUS) {
			/* Set BAR0_CORE2_WIN2 to bridge wapper base address */
			OSL_PCI_WRITE_CONFIG(osh,
				cfg_reg, 4, axi_wrapper[i].wrapper_addr);

			/* set AI to BAR0 + Offset corresponding to Gen1 or gen2 */
			ai = (aidmp_t *) (DISCARD_QUAL(sii->curmap, uint8) + offset);
		}
		else
#endif /* AXI_TIMEOUTS_NIC */
		{
			ai = (aidmp_t *)(uintptr) axi_wrapper[i].wrapper_addr;
		}

		W_REG(sii->osh, &ai->errlogctrl, errlogctrl);

		SI_VMSG(("ENABLED BPT: MFG:%x, CID:%x, ADDR:%x, ERR_CTRL:%x\n",
			axi_wrapper[i].mfg,
			axi_wrapper[i].cid,
			axi_wrapper[i].wrapper_addr,
			R_REG(sii->osh, &ai->errlogctrl)));
	}

#ifdef AXI_TIMEOUTS_NIC
	/* Restore the initial wrapper space */
	if (prev_value) {
		OSL_PCI_WRITE_CONFIG(osh, cfg_reg, 4, prev_value);
	}
#endif /* AXI_TIMEOUTS_NIC */

#endif /* AXI_TIMEOUTS || AXI_TIMEOUTS_NIC */
}

#if defined (AXI_TIMEOUTS) || defined (AXI_TIMEOUTS_NIC)

/* slave error is ignored, so account for those cases */
static uint32 si_ignore_errlog_cnt = 0;

static bool
BCMPOSTTRAPFN(ai_ignore_errlog)(const si_info_t *sii, const aidmp_t *ai,
	uint32 lo_addr, uint32 hi_addr, uint32 err_axi_id, uint32 errsts)
{
	uint32 ignore_errsts = AIELS_SLAVE_ERR;
	uint32 ignore_errsts_2 = 0;
	uint32 ignore_hi = BT_CC_SPROM_BADREG_HI;
	uint32 ignore_lo = BT_CC_SPROM_BADREG_LO;
	uint32 ignore_size = BT_CC_SPROM_BADREG_SIZE;
	bool address_check = TRUE;
	uint32 axi_id = 0;
	uint32 axi_id2 = 0;
	bool extd_axi_id_mask = FALSE;
	uint32 axi_id_mask;

	SI_PRINT(("err check: core %p, error %d, axi id 0x%04x, addr(0x%08x:%08x)\n",
		ai, errsts, err_axi_id, hi_addr, lo_addr));

	/* ignore the BT slave errors if the errlog is to chipcommon addr 0x190 */
	switch (CHIPID(sii->pub.chip)) {
#if defined(BT_WLAN_REG_ON_WAR)
		/*
		 * 4389B0/C0 - WL and BT turn on WAR, ignore AXI error originating from
		 * AHB-AXI bridge i.e, any slave error or timeout from BT access
		 */
		case BCM4389_CHIP_GRPID:
			axi_id = BCM4389_BT_AXI_ID;
			ignore_errsts = AIELS_SLAVE_ERR;
			axi_id2 = BCM4389_BT_AXI_ID;
			ignore_errsts_2 = AIELS_TIMEOUT;
			address_check = FALSE;
			extd_axi_id_mask = TRUE;
			break;
#endif /* BT_WLAN_REG_ON_WAR */
#ifdef BTOVERPCIE
		case BCM4388_CHIP_GRPID:
			axi_id = BCM4388_BT_AXI_ID;
		/* For BT over PCIE, ignore any slave error from BT. */
		/* No need to check any address range */
			address_check = FALSE;
			ignore_errsts_2 = AIELS_DECODE;
			break;
		case BCM4369_CHIP_GRPID:
			axi_id = BCM4369_BT_AXI_ID;
		/* For BT over PCIE, ignore any slave error from BT. */
		/* No need to check any address range */
			address_check = FALSE;
			ignore_errsts_2 = AIELS_DECODE;
			break;
#endif /* BTOVERPCIE */
		case BCM4376_CHIP_GRPID:
		case BCM4378_CHIP_GRPID:
		case BCM4385_CHIP_GRPID:
		case BCM4387_CHIP_GRPID:
#ifdef BTOVERPCIE
			axi_id = BCM4378_BT_AXI_ID;
			/* For BT over PCIE, ignore any slave error from BT. */
			/* No need to check any address range */
			address_check = FALSE;
#endif /* BTOVERPCIE */
			axi_id2 = BCM4378_ARM_PREFETCH_AXI_ID;
			extd_axi_id_mask = TRUE;
			ignore_errsts_2 = AIELS_DECODE;
			break;
#ifdef USE_HOSTMEM
		case BCM43602_CHIP_ID:
			axi_id = BCM43602_BT_AXI_ID;
			address_check = FALSE;
			break;
#endif /* USE_HOSTMEM */
		default:
			return FALSE;
	}

	axi_id_mask = extd_axi_id_mask ? AI_ERRLOGID_AXI_ID_MASK_EXTD : AI_ERRLOGID_AXI_ID_MASK;

	/* AXI ID check */
	err_axi_id &= axi_id_mask;
	errsts &=  AIELS_ERROR_MASK;

	/* check the ignore error cases. 2 checks */
	if (!(((err_axi_id == axi_id) && (errsts == ignore_errsts)) ||
		((err_axi_id == axi_id2) && (errsts == ignore_errsts_2)))) {
		/* not the error ignore cases */
		return FALSE;

	}

	/* check the specific address checks now, if specified */
	if (address_check) {
		/* address range check */
		if ((hi_addr != ignore_hi) ||
		    (lo_addr < ignore_lo) || (lo_addr >= (ignore_lo + ignore_size))) {
			return FALSE;
		}
	}

	SI_PRINT(("err check: ignored\n"));
	return TRUE;
}
#endif /* defined (AXI_TIMEOUTS) || defined (AXI_TIMEOUTS_NIC) */

#ifdef AXI_TIMEOUTS_NIC

/* Function to return the APB bridge details corresponding to the core */
static bool
ai_get_apb_bridge(const si_t * sih, uint32 coreidx, uint32 *apb_id, uint32 * apb_coreunit)
{
	uint i;
	uint32 core_base, core_end;
	const si_info_t *sii = SI_INFO(sih);
	static uint32 coreidx_cached = 0, apb_id_cached = 0, apb_coreunit_cached = 0;
	uint32 tmp_coreunit = 0;
	const si_cores_info_t *cores_info = (si_cores_info_t *)sii->cores_info;

	if (coreidx >= MIN(sii->numcores, SI_MAXCORES))
		return FALSE;

	/* Most of the time apb bridge query will be for d11 core.
	 * Maintain the last cache and return if found rather than iterating the table
	 */
	if (coreidx_cached == coreidx) {
		*apb_id = apb_id_cached;
		*apb_coreunit = apb_coreunit_cached;
		return TRUE;
	}

	core_base = cores_info->coresba[coreidx];
	core_end = core_base + cores_info->coresba_size[coreidx];

	for (i = 0; i < sii->numcores; i++) {
		if (cores_info->coreid[i] == APB_BRIDGE_ID) {
			uint32 apb_base;
			uint32 apb_end;

			apb_base = cores_info->coresba[i];
			apb_end = apb_base + cores_info->coresba_size[i];

			if ((core_base >= apb_base) &&
				(core_end <= apb_end)) {
				/* Current core is attached to this APB bridge */
				*apb_id = apb_id_cached = APB_BRIDGE_ID;
				*apb_coreunit = apb_coreunit_cached = tmp_coreunit;
				coreidx_cached = coreidx;
				return TRUE;
			}
			/* Increment the coreunit */
			tmp_coreunit++;
		}
	}

	return FALSE;
}

uint32
ai_clear_backplane_to_fast(si_t *sih, void *addr)
{
	const si_info_t *sii = SI_INFO(sih);
	volatile const void *curmap = sii->curmap;
	bool core_reg = FALSE;

	/* Use fast path only for core register access */
	if (((uintptr)addr >= (uintptr)curmap) &&
		((uintptr)addr < ((uintptr)curmap + SI_CORE_SIZE))) {
		/* address being accessed is within current core reg map */
		core_reg = TRUE;
	}

	if (core_reg) {
		uint32 apb_id, apb_coreunit;

		if (ai_get_apb_bridge(sih, si_coreidx(&sii->pub),
			&apb_id, &apb_coreunit) == TRUE) {
			/* Found the APB bridge corresponding to current core,
			 * Check for bus errors in APB wrapper
			 */
			return ai_clear_backplane_to_per_core(sih,
				apb_id, apb_coreunit, NULL);
		}
	}

	/* Default is to poll for errors on all slave wrappers */
	return si_clear_backplane_to(sih);
}
#endif /* AXI_TIMEOUTS_NIC */

#if defined (AXI_TIMEOUTS) || defined (AXI_TIMEOUTS_NIC)
static bool g_disable_backplane_logs = FALSE;

static uint32 last_axi_error = AXI_WRAP_STS_NONE;
static uint32 last_axi_error_log_status = 0;
static uint32 last_axi_error_core = 0;
static uint32 last_axi_error_wrap = 0;
static uint32 last_axi_errlog_lo = 0;
static uint32 last_axi_errlog_hi = 0;
static uint32 last_axi_errlog_id = 0;

/*
 * API to clear the back plane timeout per core.
 * Caller may pass optional wrapper address. If present this will be used as
 * the wrapper base address. If wrapper base address is provided then caller
 * must provide the coreid also.
 * If both coreid and wrapper is zero, then err status of current bridge
 * will be verified.
 */
uint32
BCMPOSTTRAPFN(ai_clear_backplane_to_per_core)(si_t *sih, uint coreid, uint coreunit, void *wrap)
{
	int ret = AXI_WRAP_STS_NONE;
	aidmp_t *ai = NULL;
	uint32 errlog_status = 0;
	const si_info_t *sii = SI_INFO(sih);
	uint32 errlog_lo = 0, errlog_hi = 0, errlog_id = 0, errlog_flags = 0;
	uint32 current_coreidx = si_coreidx(sih);
	uint32 target_coreidx = si_findcoreidx(sih, coreid, coreunit);

#if defined(AXI_TIMEOUTS_NIC)
	si_axi_error_t * axi_error = sih->err_info ?
		&sih->err_info->axi_error[sih->err_info->count] : NULL;
#endif /* AXI_TIMEOUTS_NIC */
	bool restore_core = FALSE;

	if ((sii->axi_num_wrappers == 0) ||
#ifdef AXI_TIMEOUTS_NIC
		(!PCIE(sii)) ||
#endif /* AXI_TIMEOUTS_NIC */
		FALSE) {
		SI_VMSG(("ai_clear_backplane_to_per_core, axi_num_wrappers:%d, Is_PCIE:%d,"
			" BUS_TYPE:%d, ID:%x\n",
			sii->axi_num_wrappers, PCIE(sii),
			BUSTYPE(sii->pub.bustype), sii->pub.buscoretype));
		return AXI_WRAP_STS_NONE;
	}

	if (wrap != NULL) {
		ai = (aidmp_t *)wrap;
	} else if (coreid && (target_coreidx != current_coreidx)) {

		if (ai_setcoreidx(sih, target_coreidx) == NULL) {
			/* Unable to set the core */
			SI_PRINT(("Set Code Failed: coreid:%x, unit:%d, target_coreidx:%d\n",
				coreid, coreunit, target_coreidx));
			errlog_lo = target_coreidx;
			ret = AXI_WRAP_STS_SET_CORE_FAIL;
			goto end;
		}

		restore_core = TRUE;
		ai = (aidmp_t *)si_wrapperregs(sih);
	} else {
		/* Read error status of current wrapper */
		ai = (aidmp_t *)si_wrapperregs(sih);

		/* Update CoreID to current Code ID */
		coreid = si_coreid(sih);
	}

	/* read error log status */
	errlog_status = R_REG(sii->osh, &ai->errlogstatus);

	if (errlog_status == ID32_INVALID) {
		/* Do not try to peek further */
		SI_PRINT(("ai_clear_backplane_to_per_core, errlogstatus:%x - Slave Wrapper:%x\n",
			errlog_status, coreid));
		ret = AXI_WRAP_STS_WRAP_RD_ERR;
		errlog_lo = (uint32)(uintptr)&ai->errlogstatus;
		goto end;
	}

	if ((errlog_status & AIELS_ERROR_MASK) != 0) {
		uint32 tmp;
		uint32 count = 0;
		/* set ErrDone to clear the condition */
		W_REG(sii->osh, &ai->errlogdone, AIELD_ERRDONE_MASK);

		/* SPINWAIT on errlogstatus timeout status bits */
		while ((tmp = R_REG(sii->osh, &ai->errlogstatus)) & AIELS_ERROR_MASK) {

			if (tmp == ID32_INVALID) {
				SI_PRINT(("ai_clear_backplane_to_per_core: prev errlogstatus:%x,"
					" errlogstatus:%x\n",
					errlog_status, tmp));
				ret = AXI_WRAP_STS_WRAP_RD_ERR;
				errlog_lo = (uint32)(uintptr)&ai->errlogstatus;
				goto end;
			}
			/*
			 * Clear again, to avoid getting stuck in the loop, if a new error
			 * is logged after we cleared the first timeout
			 */
			W_REG(sii->osh, &ai->errlogdone, AIELD_ERRDONE_MASK);

			count++;
			OSL_DELAY(10);
			if ((10 * count) > AI_REG_READ_TIMEOUT) {
				errlog_status = tmp;
				break;
			}
		}

		errlog_lo = R_REG(sii->osh, &ai->errlogaddrlo);
		errlog_hi = R_REG(sii->osh, &ai->errlogaddrhi);
		errlog_id = R_REG(sii->osh, &ai->errlogid);
		errlog_flags = R_REG(sii->osh, &ai->errlogflags);

		/* we are already in the error path, so OK to check for the  slave error */
		if (ai_ignore_errlog(sii, ai, errlog_lo, errlog_hi, errlog_id,
			errlog_status)) {
			si_ignore_errlog_cnt++;
			goto end;
		}

		/* only reset APB Bridge on timeout (not slave error, or dec error) */
		switch (errlog_status & AIELS_ERROR_MASK) {
			case AIELS_SLAVE_ERR:
				SI_PRINT(("AXI slave error\n"));
				ret |= AXI_WRAP_STS_SLAVE_ERR;
				break;

			case AIELS_TIMEOUT:
				ai_reset_axi_to(sii, ai);
				ret |= AXI_WRAP_STS_TIMEOUT;
				break;

			case AIELS_DECODE:
				SI_PRINT(("AXI decode error\n"));
#ifdef USE_HOSTMEM
				/* Ignore known cases of CR4 prefetch abort bugs */
				if ((errlog_id & (BCM_AXI_ID_MASK | BCM_AXI_ACCESS_TYPE_MASK)) !=
					(BCM43xx_AXI_ACCESS_TYPE_PREFETCH | BCM43xx_CR4_AXI_ID))
#endif
				{
					ret |= AXI_WRAP_STS_DECODE_ERR;
				}
				break;
			default:
				ASSERT(0);	/* should be impossible */
		}

		if (errlog_status & AIELS_MULTIPLE_ERRORS) {
			SI_PRINT(("Multiple AXI Errors\n"));
			/* Set multiple errors bit only if actual error is not ignored */
			if (ret) {
				ret |= AXI_WRAP_STS_MULTIPLE_ERRORS;
			}
		}

		SI_PRINT(("\tCoreID: %x\n", coreid));
		SI_PRINT(("\t errlog: lo 0x%08x, hi 0x%08x, id 0x%08x, flags 0x%08x"
			", status 0x%08x\n",
			errlog_lo, errlog_hi, errlog_id, errlog_flags,
			errlog_status));
	}

end:
	if (ret != AXI_WRAP_STS_NONE) {
		last_axi_error = ret;
		last_axi_error_log_status = errlog_status;
		last_axi_error_core = coreid;
		last_axi_error_wrap = (uint32)ai;
		last_axi_errlog_lo = errlog_lo;
		last_axi_errlog_hi = errlog_hi;
		last_axi_errlog_id = errlog_id;
	}

#if defined(AXI_TIMEOUTS_NIC)
	if (axi_error && (ret != AXI_WRAP_STS_NONE)) {
		axi_error->error = ret;
		axi_error->coreid = coreid;
		axi_error->errlog_lo = errlog_lo;
		axi_error->errlog_hi = errlog_hi;
		axi_error->errlog_id = errlog_id;
		axi_error->errlog_flags = errlog_flags;
		axi_error->errlog_status = errlog_status;
		sih->err_info->count++;

		if (sih->err_info->count == SI_MAX_ERRLOG_SIZE) {
			sih->err_info->count = SI_MAX_ERRLOG_SIZE - 1;
			SI_PRINT(("AXI Error log overflow\n"));
		}
	}
#endif /* AXI_TIMEOUTS_NIC */

	if (restore_core) {
		if (ai_setcoreidx(sih, current_coreidx) == NULL) {
			/* Unable to set the core */
			return ID32_INVALID;
		}
	}

	return ret;
}

/* reset AXI timeout */
static void
BCMPOSTTRAPFN(ai_reset_axi_to)(const si_info_t *sii, aidmp_t *ai)
{
	/* reset APB Bridge */
	OR_REG(sii->osh, &ai->resetctrl, AIRC_RESET);
	/* sync write */
	(void)R_REG(sii->osh, &ai->resetctrl);
	/* clear Reset bit */
	AND_REG(sii->osh, &ai->resetctrl, ~(AIRC_RESET));
	/* sync write */
	(void)R_REG(sii->osh, &ai->resetctrl);
	SI_PRINT(("AXI timeout\n"));
	if (R_REG(sii->osh, &ai->resetctrl) & AIRC_RESET) {
		SI_PRINT(("reset failed on wrapper %p\n", ai));
		g_disable_backplane_logs = TRUE;
	}
}

void
BCMPOSTTRAPFN(ai_wrapper_get_last_error)(const si_t *sih, uint32 *error_status, uint32 *core,
	uint32 *lo, uint32 *hi, uint32 *id)
{
	*error_status = last_axi_error_log_status;
	*core = last_axi_error_core;
	*lo = last_axi_errlog_lo;
	*hi = last_axi_errlog_hi;
	*id = last_axi_errlog_id;
}

/* Function to check whether AXI timeout has been registered on a core */
uint32
ai_get_axi_timeout_reg(void)
{
	return (GOODREGS(last_axi_errlog_lo) ? last_axi_errlog_lo : 0);
}
#endif /* AXI_TIMEOUTS || AXI_TIMEOUTS_NIC */

uint32
BCMPOSTTRAPFN(ai_findcoreidx_by_axiid)(const si_t *sih, uint32 axiid)
{
	uint coreid = 0;
	uint coreunit = 0;
	const axi_to_coreidx_t *axi2coreidx = NULL;
	switch (CHIPID(sih->chip)) {
		case BCM4369_CHIP_GRPID:
			axi2coreidx = axi2coreidx_4369;
			break;
		default:
			SI_PRINT(("Chipid mapping not found\n"));
			break;
	}

	if (!axi2coreidx)
		return (BADIDX);

	coreid = axi2coreidx[axiid].coreid;
	coreunit = axi2coreidx[axiid].coreunit;

	return si_findcoreidx(sih, coreid, coreunit);

}

/*
 * This API polls all slave wrappers for errors and returns bit map of
 * all reported errors.
 * return - bit map of
 *	AXI_WRAP_STS_NONE
 *	AXI_WRAP_STS_TIMEOUT
 *	AXI_WRAP_STS_SLAVE_ERR
 *	AXI_WRAP_STS_DECODE_ERR
 *	AXI_WRAP_STS_PCI_RD_ERR
 *	AXI_WRAP_STS_WRAP_RD_ERR
 *	AXI_WRAP_STS_SET_CORE_FAIL
 * On timeout detection, correspondign bridge will be reset to
 * unblock the bus.
 * Error reported in each wrapper can be retrieved using the API
 * si_get_axi_errlog_info()
 */
uint32
BCMPOSTTRAPFN(ai_clear_backplane_to)(si_t *sih)
{
	uint32 ret = 0;
#if defined (AXI_TIMEOUTS) || defined (AXI_TIMEOUTS_NIC)
	const si_info_t *sii = SI_INFO(sih);
	aidmp_t *ai;
	uint32 i;
	axi_wrapper_t * axi_wrapper = sii->axi_wrapper;

#ifdef AXI_TIMEOUTS_NIC
	uint32 prev_value = 0;
	osl_t *osh = sii->osh;
	uint32 cfg_reg = 0;
	uint32 offset = 0;

	if ((sii->axi_num_wrappers == 0) || (!PCIE(sii)))
#else
	if (sii->axi_num_wrappers == 0)
#endif
	{
		SI_VMSG(("ai_clear_backplane_to, axi_num_wrappers:%d, Is_PCIE:%d, BUS_TYPE:%d,"
			" ID:%x\n",
			sii->axi_num_wrappers, PCIE(sii),
			BUSTYPE(sii->pub.bustype), sii->pub.buscoretype));
		return AXI_WRAP_STS_NONE;
	}

#ifdef AXI_TIMEOUTS_NIC
	/* Save and restore wrapper access window */
	if (BUSTYPE(sii->pub.bustype) == PCI_BUS) {
		if (PCIE_GEN1(sii)) {
			cfg_reg = PCI_BAR0_WIN2;
			offset = PCI_BAR0_WIN2_OFFSET;
		} else if (PCIE_GEN2(sii)) {
			cfg_reg = PCIE2_BAR0_CORE2_WIN2;
			offset = PCIE2_BAR0_CORE2_WIN2_OFFSET;
		}
		else {
			ASSERT(!"!PCIE_GEN1 && !PCIE_GEN2");
		}

		prev_value = OSL_PCI_READ_CONFIG(osh, cfg_reg, 4);

		if (prev_value == ID32_INVALID) {
			si_axi_error_t * axi_error =
				sih->err_info ?
					&sih->err_info->axi_error[sih->err_info->count] :
					NULL;

			SI_PRINT(("ai_clear_backplane_to, PCI_BAR0_WIN2 - %x\n", prev_value));
			if (axi_error) {
				axi_error->error = ret = AXI_WRAP_STS_PCI_RD_ERR;
				axi_error->errlog_lo = cfg_reg;
				sih->err_info->count++;

				if (sih->err_info->count == SI_MAX_ERRLOG_SIZE) {
					sih->err_info->count = SI_MAX_ERRLOG_SIZE - 1;
					SI_PRINT(("AXI Error log overflow\n"));
				}
			}

			return ret;
		}
	}
#endif /* AXI_TIMEOUTS_NIC */

	for (i = 0; i < sii->axi_num_wrappers; ++i) {
		uint32 tmp;

		if (axi_wrapper[i].wrapper_type != AI_SLAVE_WRAPPER) {
			continue;
		}

#ifdef AXI_TIMEOUTS_NIC
		if (BUSTYPE(sii->pub.bustype) == PCI_BUS) {
			/* Set BAR0_CORE2_WIN2 to bridge wapper base address */
			OSL_PCI_WRITE_CONFIG(osh,
				cfg_reg, 4, axi_wrapper[i].wrapper_addr);

			/* set AI to BAR0 + Offset corresponding to Gen1 or gen2 */
			ai = (aidmp_t *) (DISCARD_QUAL(sii->curmap, uint8) + offset);
		}
		else
#endif /* AXI_TIMEOUTS_NIC */
		{
			ai = (aidmp_t *)(uintptr) axi_wrapper[i].wrapper_addr;
		}

		tmp = ai_clear_backplane_to_per_core(sih, axi_wrapper[i].cid, 0,
			DISCARD_QUAL(ai, void));

		ret |= tmp;
	}

#ifdef AXI_TIMEOUTS_NIC
	/* Restore the initial wrapper space */
	if (prev_value) {
		OSL_PCI_WRITE_CONFIG(osh, cfg_reg, 4, prev_value);
	}
#endif /* AXI_TIMEOUTS_NIC */

#endif /* AXI_TIMEOUTS || AXI_TIMEOUTS_NIC */

	return ret;
}

uint
ai_num_slaveports(const si_t *sih, uint coreidx)
{
	const si_info_t *sii = SI_INFO(sih);
	const si_cores_info_t *cores_info = (const si_cores_info_t *)sii->cores_info;
	uint32 cib;

	cib = cores_info->cib[coreidx];
	return ((cib & CIB_NSP_MASK) >> CIB_NSP_SHIFT);
}

#ifdef UART_TRAP_DBG
void
ai_dump_APB_Bridge_registers(const si_t *sih)
{
	aidmp_t *ai;
	const si_info_t *sii = SI_INFO(sih);

	ai = (aidmp_t *)sii->br_wrapba[0];
	printf("APB Bridge 0\n");
	printf("lo 0x%08x, hi 0x%08x, id 0x%08x, flags 0x%08x",
		R_REG(sii->osh, &ai->errlogaddrlo),
		R_REG(sii->osh, &ai->errlogaddrhi),
		R_REG(sii->osh, &ai->errlogid),
		R_REG(sii->osh, &ai->errlogflags));
	printf("\n status 0x%08x\n", R_REG(sii->osh, &ai->errlogstatus));
}
#endif /* UART_TRAP_DBG */

void
ai_force_clocks(const si_t *sih, uint clock_state)
{
	const si_info_t *sii = SI_INFO(sih);
	aidmp_t *ai, *ai_sec = NULL;
	volatile uint32 dummy;
	uint32 ioctrl;
	const si_cores_info_t *cores_info = (const si_cores_info_t *)sii->cores_info;

	ASSERT(GOODREGS(sii->curwrap));
	ai = sii->curwrap;
	if (cores_info->wrapba2[sii->curidx])
		ai_sec = REG_MAP(cores_info->wrapba2[sii->curidx], SI_CORE_SIZE);

	/* ensure there are no pending backplane operations */
	SPINWAIT((R_REG(sii->osh, &ai->resetstatus) != 0), 300);

	if (clock_state == FORCE_CLK_ON) {
		ioctrl = R_REG(sii->osh, &ai->ioctrl);
		W_REG(sii->osh, &ai->ioctrl, (ioctrl | SICF_FGC));
		dummy = R_REG(sii->osh, &ai->ioctrl);
		BCM_REFERENCE(dummy);
		if (ai_sec) {
			ioctrl = R_REG(sii->osh, &ai_sec->ioctrl);
			W_REG(sii->osh, &ai_sec->ioctrl, (ioctrl | SICF_FGC));
			dummy = R_REG(sii->osh, &ai_sec->ioctrl);
			BCM_REFERENCE(dummy);
		}
	} else {
		ioctrl = R_REG(sii->osh, &ai->ioctrl);
		W_REG(sii->osh, &ai->ioctrl, (ioctrl & (~SICF_FGC)));
		dummy = R_REG(sii->osh, &ai->ioctrl);
		BCM_REFERENCE(dummy);
		if (ai_sec) {
			ioctrl = R_REG(sii->osh, &ai_sec->ioctrl);
			W_REG(sii->osh, &ai_sec->ioctrl, (ioctrl & (~SICF_FGC)));
			dummy = R_REG(sii->osh, &ai_sec->ioctrl);
			BCM_REFERENCE(dummy);
		}
	}
	/* ensure there are no pending backplane operations */
	SPINWAIT((R_REG(sii->osh, &ai->resetstatus) != 0), 300);
}

#ifdef DONGLEBUILD
/*
 * this is not declared as static const, although that is the right thing to do
 * reason being if declared as static const, compile/link process would that in
 * read only section...
 * currently this code/array is used to identify the registers which are dumped
 * during trap processing
 * and usually for the trap buffer, .rodata buffer is reused,  so for now just static
*/
static uint32 BCMPOST_TRAP_RODATA(wrapper_offsets_to_dump)[] = {
	OFFSETOF(aidmp_t, ioctrlset),
	OFFSETOF(aidmp_t, ioctrlclear),
	OFFSETOF(aidmp_t, ioctrl),
	OFFSETOF(aidmp_t, iostatus),
	OFFSETOF(aidmp_t, ioctrlwidth),
	OFFSETOF(aidmp_t, iostatuswidth),
	OFFSETOF(aidmp_t, resetctrl),
	OFFSETOF(aidmp_t, resetstatus),
	OFFSETOF(aidmp_t, resetreadid),
	OFFSETOF(aidmp_t, resetwriteid),
	OFFSETOF(aidmp_t, errlogctrl),
	OFFSETOF(aidmp_t, errlogdone),
	OFFSETOF(aidmp_t, errlogstatus),
	OFFSETOF(aidmp_t, errlogaddrlo),
	OFFSETOF(aidmp_t, errlogaddrhi),
	OFFSETOF(aidmp_t, errlogid),
	OFFSETOF(aidmp_t, errloguser),
	OFFSETOF(aidmp_t, errlogflags),
	OFFSETOF(aidmp_t, intstatus),
	OFFSETOF(aidmp_t, config),
	OFFSETOF(aidmp_t, itipoobaout),
	OFFSETOF(aidmp_t, itipoobbout),
	OFFSETOF(aidmp_t, itipoobcout),
	OFFSETOF(aidmp_t, itipoobdout)};

#ifdef ETD

/* This is used for dumping wrapper registers for etd when axierror happens.
 * This should match with the structure hnd_ext_trap_bp_err_t
 */
static uint32 BCMPOST_TRAP_RODATA(etd_wrapper_offsets_axierr)[] = {
	OFFSETOF(aidmp_t, ioctrl),
	OFFSETOF(aidmp_t, iostatus),
	OFFSETOF(aidmp_t, resetctrl),
	OFFSETOF(aidmp_t, resetstatus),
	OFFSETOF(aidmp_t, resetreadid),
	OFFSETOF(aidmp_t, resetwriteid),
	OFFSETOF(aidmp_t, errlogctrl),
	OFFSETOF(aidmp_t, errlogdone),
	OFFSETOF(aidmp_t, errlogstatus),
	OFFSETOF(aidmp_t, errlogaddrlo),
	OFFSETOF(aidmp_t, errlogaddrhi),
	OFFSETOF(aidmp_t, errlogid),
	OFFSETOF(aidmp_t, errloguser),
	OFFSETOF(aidmp_t, errlogflags),
	OFFSETOF(aidmp_t, itipoobaout),
	OFFSETOF(aidmp_t, itipoobbout),
	OFFSETOF(aidmp_t, itipoobcout),
	OFFSETOF(aidmp_t, itipoobdout)};
#endif /* ETD */

/* wrapper function to access the global array wrapper_offsets_to_dump */
static uint32
BCMRAMFN(ai_get_sizeof_wrapper_offsets_to_dump)(void)
{
	return (sizeof(wrapper_offsets_to_dump));
}

static uint32
BCMPOSTTRAPRAMFN(ai_get_wrapper_base_addr)(uint32 **offset)
{
	uint32 arr_size = ARRAYSIZE(wrapper_offsets_to_dump);

	*offset = &wrapper_offsets_to_dump[0];
	return arr_size;
}

uint32
BCMATTACHFN(ai_wrapper_dump_buf_size)(const si_t *sih)
{
	uint32 buf_size = 0;
	uint32 wrapper_count = 0;
	const si_info_t *sii = SI_INFO(sih);

	wrapper_count = sii->axi_num_wrappers;
	if (wrapper_count == 0)
		return 0;

	/* cnt indicates how many registers, tag_id 0 will say these are address/value */
	/* address/value pairs */
	buf_size += 2 * (ai_get_sizeof_wrapper_offsets_to_dump() * wrapper_count);

	return buf_size;
}

static uint32*
BCMPOSTTRAPFN(ai_wrapper_dump_binary_one)(const si_info_t *sii, uint32 *p32, uint32 wrap_ba)
{
	uint i;
	uint32 *addr;
	uint32 arr_size;
	uint32 *offset_base;

	arr_size = ai_get_wrapper_base_addr(&offset_base);

	for (i = 0; i < arr_size; i++) {
		addr = (uint32 *)(wrap_ba + *(offset_base + i));
		*p32++ = (uint32)addr;
		*p32++ = R_REG(sii->osh, addr);
	}
	return p32;
}

#if defined(ETD)
static uint32
BCMPOSTTRAPRAMFN(ai_get_wrapper_base_addr_etd_axierr)(uint32 **offset)
{
	uint32 arr_size = ARRAYSIZE(etd_wrapper_offsets_axierr);

	*offset = &etd_wrapper_offsets_axierr[0];
	return arr_size;
}

uint32
BCMPOSTTRAPFN(ai_wrapper_dump_last_timeout)(const si_t *sih, uint32 *error, uint32 *core,
	uint32 *ba, uchar *p)
{
#if defined (AXI_TIMEOUTS) || defined (AXI_TIMEOUTS_NIC)
	uint32 *p32;
	uint32 wrap_ba = last_axi_error_wrap;
	uint i;
	uint32 *addr;

	const si_info_t *sii = SI_INFO(sih);

	if (last_axi_error != AXI_WRAP_STS_NONE)
	{
		if (wrap_ba)
		{
			p32 = (uint32 *)p;
			uint32 arr_size;
			uint32 *offset_base;

			arr_size = ai_get_wrapper_base_addr_etd_axierr(&offset_base);
			for (i = 0; i < arr_size; i++) {
				addr = (uint32 *)(wrap_ba + *(offset_base + i));
				*p32++ = R_REG(sii->osh, addr);
			}
		}
		*error = last_axi_error;
		*core = last_axi_error_core;
		*ba = wrap_ba;
	}
#else
	*error = 0;
	*core = 0;
	*ba = 0;
#endif /* AXI_TIMEOUTS || AXI_TIMEOUTS_NIC */
	return 0;
}
#endif /* ETD */

uint32
BCMPOSTTRAPFN(ai_wrapper_dump_binary)(const si_t *sih, uchar *p)
{
	uint32 *p32 = (uint32 *)p;
	uint32 i;
	const si_info_t *sii = SI_INFO(sih);

	for (i = 0; i < sii->axi_num_wrappers; i++) {
		p32 = ai_wrapper_dump_binary_one(sii, p32, sii->axi_wrapper[i].wrapper_addr);
	}
	return 0;
}

bool
BCMPOSTTRAPFN(ai_check_enable_backplane_log)(const si_t *sih)
{
#if defined (AXI_TIMEOUTS) || defined (AXI_TIMEOUTS_NIC)
	if (g_disable_backplane_logs) {
		return FALSE;
	}
	else {
		return TRUE;
	}
#else /*  (AXI_TIMEOUTS) || defined (AXI_TIMEOUTS_NIC) */
	return FALSE;
#endif /*  (AXI_TIMEOUTS) || defined (AXI_TIMEOUTS_NIC) */
}
#endif /* DONGLEBUILD */
