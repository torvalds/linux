/*
 *  pSeries firmware setup code.
 *
 *  Portions from arch/powerpc/platforms/pseries/setup.c:
 *   Copyright (C) 1995  Linus Torvalds
 *   Adapted from 'alpha' version by Gary Thomas
 *   Modified by Cort Dougan (cort@cs.nmt.edu)
 *   Modified by PPC64 Team, IBM Corp
 *
 *  Portions from arch/powerpc/kernel/firmware.c
 *   Copyright (C) 2001 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *   Modifications for ppc64:
 *    Copyright (C) 2003 Dave Engebretsen <engebret@us.ibm.com>
 *    Copyright (C) 2005 Stephen Rothwell, IBM Corporation
 *
 *  Copyright 2006 IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */


#include <linux/of_fdt.h>
#include <asm/firmware.h>
#include <asm/prom.h>
#include <asm/udbg.h>

#include "pseries.h"

struct hypertas_fw_feature {
    unsigned long val;
    char * name;
};

/*
 * The names in this table match names in rtas/ibm,hypertas-functions.  If the
 * entry ends in a '*', only upto the '*' is matched.  Otherwise the entire
 * string must match.
 */
static __initdata struct hypertas_fw_feature
hypertas_fw_features_table[] = {
	{FW_FEATURE_PFT,		"hcall-pft"},
	{FW_FEATURE_TCE,		"hcall-tce"},
	{FW_FEATURE_SPRG0,		"hcall-sprg0"},
	{FW_FEATURE_DABR,		"hcall-dabr"},
	{FW_FEATURE_COPY,		"hcall-copy"},
	{FW_FEATURE_ASR,		"hcall-asr"},
	{FW_FEATURE_DEBUG,		"hcall-debug"},
	{FW_FEATURE_PERF,		"hcall-perf"},
	{FW_FEATURE_DUMP,		"hcall-dump"},
	{FW_FEATURE_INTERRUPT,		"hcall-interrupt"},
	{FW_FEATURE_MIGRATE,		"hcall-migrate"},
	{FW_FEATURE_PERFMON,		"hcall-perfmon"},
	{FW_FEATURE_CRQ,		"hcall-crq"},
	{FW_FEATURE_VIO,		"hcall-vio"},
	{FW_FEATURE_RDMA,		"hcall-rdma"},
	{FW_FEATURE_LLAN,		"hcall-lLAN"},
	{FW_FEATURE_BULK_REMOVE,	"hcall-bulk"},
	{FW_FEATURE_XDABR,		"hcall-xdabr"},
	{FW_FEATURE_MULTITCE,		"hcall-multi-tce"},
	{FW_FEATURE_SPLPAR,		"hcall-splpar"},
	{FW_FEATURE_VPHN,		"hcall-vphn"},
	{FW_FEATURE_SET_MODE,		"hcall-set-mode"},
	{FW_FEATURE_BEST_ENERGY,	"hcall-best-energy-1*"},
};

/* Build up the firmware features bitmask using the contents of
 * device-tree/ibm,hypertas-functions.  Ultimately this functionality may
 * be moved into prom.c prom_init().
 */
static void __init fw_hypertas_feature_init(const char *hypertas,
					    unsigned long len)
{
	const char *s;
	int i;

	pr_debug(" -> fw_hypertas_feature_init()\n");

	for (s = hypertas; s < hypertas + len; s += strlen(s) + 1) {
		for (i = 0; i < ARRAY_SIZE(hypertas_fw_features_table); i++) {
			const char *name = hypertas_fw_features_table[i].name;
			size_t size;

			/*
			 * If there is a '*' at the end of name, only check
			 * upto there
			 */
			size = strlen(name);
			if (size && name[size - 1] == '*') {
				if (strncmp(name, s, size - 1))
					continue;
			} else if (strcmp(name, s))
				continue;

			/* we have a match */
			powerpc_firmware_features |=
				hypertas_fw_features_table[i].val;
			break;
		}
	}

	pr_debug(" <- fw_hypertas_feature_init()\n");
}

struct vec5_fw_feature {
	unsigned long	val;
	unsigned int	feature;
};

static __initdata struct vec5_fw_feature
vec5_fw_features_table[] = {
	{FW_FEATURE_TYPE1_AFFINITY,	OV5_TYPE1_AFFINITY},
	{FW_FEATURE_PRRN,		OV5_PRRN},
};

static void __init fw_vec5_feature_init(const char *vec5, unsigned long len)
{
	unsigned int index, feat;
	int i;

	pr_debug(" -> fw_vec5_feature_init()\n");

	for (i = 0; i < ARRAY_SIZE(vec5_fw_features_table); i++) {
		index = OV5_INDX(vec5_fw_features_table[i].feature);
		feat = OV5_FEAT(vec5_fw_features_table[i].feature);

		if (index < len && (vec5[index] & feat))
			powerpc_firmware_features |=
				vec5_fw_features_table[i].val;
	}

	pr_debug(" <- fw_vec5_feature_init()\n");
}

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init probe_fw_features(unsigned long node, const char *uname, int
				    depth, void *data)
{
	const char *prop;
	int len;
	static int hypertas_found;
	static int vec5_found;

	if (depth != 1)
		return 0;

	if (!strcmp(uname, "rtas") || !strcmp(uname, "rtas@0")) {
		prop = of_get_flat_dt_prop(node, "ibm,hypertas-functions",
					   &len);
		if (prop) {
			powerpc_firmware_features |= FW_FEATURE_LPAR;
			fw_hypertas_feature_init(prop, len);
		}

		hypertas_found = 1;
	}

	if (!strcmp(uname, "chosen")) {
		prop = of_get_flat_dt_prop(node, "ibm,architecture-vec-5",
					   &len);
		if (prop)
			fw_vec5_feature_init(prop, len);

		vec5_found = 1;
	}

	return hypertas_found && vec5_found;
}

void __init pseries_probe_fw_features(void)
{
	of_scan_flat_dt(probe_fw_features, NULL);
}
