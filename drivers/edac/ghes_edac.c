/*
 * GHES/EDAC Linux driver
 *
 * This file may be distributed under the terms of the GNU General Public
 * License version 2.
 *
 * Copyright (c) 2013 by Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * Red Hat Inc. http://www.redhat.com
 */

#include <acpi/ghes.h>
#include <linux/edac.h>
#include "edac_core.h"

#define GHES_PFX   "ghes_edac: "
#define GHES_EDAC_REVISION " Ver: 1.0.0"

struct ghes_edac_pvt {
	struct list_head list;
	struct ghes *ghes;
	struct mem_ctl_info *mci;
};

static LIST_HEAD(ghes_reglist);
static DEFINE_MUTEX(ghes_edac_lock);
static int ghes_edac_mc_num;

void ghes_edac_report_mem_error(struct ghes *ghes, int sev,
                               struct cper_sec_mem_err *mem_err)
{
}
EXPORT_SYMBOL_GPL(ghes_edac_report_mem_error);

int ghes_edac_register(struct ghes *ghes, struct device *dev)
{
	int rc;
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[1];
	struct csrow_info *csrow;
	struct dimm_info *dimm;
	struct ghes_edac_pvt *pvt;

	layers[0].type = EDAC_MC_LAYER_ALL_MEM;
	layers[0].size = 1;
	layers[0].is_virt_csrow = true;

	/*
	 * We need to serialize edac_mc_alloc() and edac_mc_add_mc(),
	 * to avoid duplicated memory controller numbers
	 */
	mutex_lock(&ghes_edac_lock);
	mci = edac_mc_alloc(ghes_edac_mc_num, ARRAY_SIZE(layers), layers,
			    sizeof(*pvt));
	if (!mci) {
		pr_info(GHES_PFX "Can't allocate memory for EDAC data\n");
		mutex_unlock(&ghes_edac_lock);
		return -ENOMEM;
	}

	pvt = mci->pvt_info;
	memset(pvt, 0, sizeof(*pvt));
        list_add_tail(&pvt->list, &ghes_reglist);
	pvt->ghes = ghes;
	pvt->mci  = mci;
	mci->pdev = dev;

	mci->mtype_cap = MEM_FLAG_EMPTY;
	mci->edac_ctl_cap = EDAC_FLAG_NONE;
	mci->edac_cap = EDAC_FLAG_NONE;
	mci->mod_name = "ghes_edac.c";
	mci->mod_ver = GHES_EDAC_REVISION;
	mci->ctl_name = "ghes_edac";
	mci->dev_name = "ghes";

	csrow = mci->csrows[0];
	dimm = csrow->channels[0]->dimm;

	/* FIXME: FAKE DATA */
	dimm->nr_pages = 1000;
	dimm->grain = 128;
	dimm->mtype = MEM_UNKNOWN;
	dimm->dtype = DEV_UNKNOWN;
	dimm->edac_mode = EDAC_SECDED;

	rc = edac_mc_add_mc(mci);
	if (rc < 0) {
		pr_info(GHES_PFX "Can't register at EDAC core\n");
		edac_mc_free(mci);
		mutex_unlock(&ghes_edac_lock);
		return -ENODEV;
	}

	ghes_edac_mc_num++;
	mutex_unlock(&ghes_edac_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(ghes_edac_register);

void ghes_edac_unregister(struct ghes *ghes)
{
	struct mem_ctl_info *mci;
	struct ghes_edac_pvt *pvt;

	list_for_each_entry(pvt, &ghes_reglist, list) {
		if (ghes == pvt->ghes) {
			mci = pvt->mci;
			edac_mc_del_mc(mci->pdev);
			edac_mc_free(mci);
			list_del(&pvt->list);
		}
	}
}
EXPORT_SYMBOL_GPL(ghes_edac_unregister);
