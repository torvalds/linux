/** @file pcie_core.c
 *
 * Contains PCIe related functions that are shared between different driver models (e.g. firmware
 * builds, DHD builds, BMAC builds), in order to avoid code duplication.
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
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
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: pcie_core.c 591285 2015-10-07 11:56:29Z $
 */

#include <bcm_cfg.h>
#include <typedefs.h>
#include <bcmutils.h>
#include <bcmdefs.h>
#include <osl.h>
#include <siutils.h>
#include <hndsoc.h>
#include <sbchipc.h>

#include "pcie_core.h"

/* local prototypes */

/* local variables */

/* function definitions */

#ifdef BCMDRIVER

void pcie_watchdog_reset(osl_t *osh, si_t *sih, sbpcieregs_t *sbpcieregs)
{
	uint32 val, i, lsc;
	uint16 cfg_offset[] = {PCIECFGREG_STATUS_CMD, PCIECFGREG_PM_CSR,
		PCIECFGREG_MSI_CAP, PCIECFGREG_MSI_ADDR_L,
		PCIECFGREG_MSI_ADDR_H, PCIECFGREG_MSI_DATA,
		PCIECFGREG_LINK_STATUS_CTRL2, PCIECFGREG_RBAR_CTRL,
		PCIECFGREG_PML1_SUB_CTRL1, PCIECFGREG_REG_BAR2_CONFIG,
		PCIECFGREG_REG_BAR3_CONFIG};
	sbpcieregs_t *pcie = NULL;
	uint32 origidx = si_coreidx(sih);

	/* Switch to PCIE2 core */
	pcie = (sbpcieregs_t *)si_setcore(sih, PCIE2_CORE_ID, 0);
	BCM_REFERENCE(pcie);
	ASSERT(pcie != NULL);

	/* Disable/restore ASPM Control to protect the watchdog reset */
	W_REG(osh, &sbpcieregs->configaddr, PCIECFGREG_LINK_STATUS_CTRL);
	lsc = R_REG(osh, &sbpcieregs->configdata);
	val = lsc & (~PCIE_ASPM_ENAB);
	W_REG(osh, &sbpcieregs->configdata, val);

	si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, watchdog), ~0, 4);
	OSL_DELAY(100000);

	W_REG(osh, &sbpcieregs->configaddr, PCIECFGREG_LINK_STATUS_CTRL);
	W_REG(osh, &sbpcieregs->configdata, lsc);

	if (sih->buscorerev <= 13) {
		/* Write configuration registers back to the shadow registers
		 * cause shadow registers are cleared out after watchdog reset.
		 */
		for (i = 0; i < ARRAYSIZE(cfg_offset); i++) {
			W_REG(osh, &sbpcieregs->configaddr, cfg_offset[i]);
			val = R_REG(osh, &sbpcieregs->configdata);
			W_REG(osh, &sbpcieregs->configdata, val);
		}
	}
	si_setcoreidx(sih, origidx);
}


/* CRWLPCIEGEN2-117 pcie_pipe_Iddq should be controlled
 * by the L12 state from MAC to save power by putting the
 * SerDes analog in IDDQ mode
 */
void  pcie_serdes_iddqdisable(osl_t *osh, si_t *sih, sbpcieregs_t *sbpcieregs)
{
	sbpcieregs_t *pcie = NULL;
	uint crwlpciegen2_117_disable = 0;
	uint32 origidx = si_coreidx(sih);

	crwlpciegen2_117_disable = PCIE_PipeIddqDisable0 | PCIE_PipeIddqDisable1;
	/* Switch to PCIE2 core */
	pcie = (sbpcieregs_t *)si_setcore(sih, PCIE2_CORE_ID, 0);
	BCM_REFERENCE(pcie);
	ASSERT(pcie != NULL);

	OR_REG(osh, &sbpcieregs->control,
		crwlpciegen2_117_disable);

	si_setcoreidx(sih, origidx);
}
#endif /* BCMDRIVER */
