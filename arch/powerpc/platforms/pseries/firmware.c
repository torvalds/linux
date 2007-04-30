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

#undef DEBUG

#include <asm/firmware.h>
#include <asm/prom.h>

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

typedef struct {
    unsigned long val;
    char * name;
} firmware_feature_t;

static __initdata firmware_feature_t
firmware_features_table[FIRMWARE_MAX_FEATURES] = {
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
	{FW_FEATURE_BULK,		"hcall-bulk"},
	{FW_FEATURE_XDABR,		"hcall-xdabr"},
	{FW_FEATURE_MULTITCE,		"hcall-multi-tce"},
	{FW_FEATURE_SPLPAR,		"hcall-splpar"},
	{FW_FEATURE_BULK_REMOVE,	"hcall-bulk"},
};

/* Build up the firmware features bitmask using the contents of
 * device-tree/ibm,hypertas-functions.  Ultimately this functionality may
 * be moved into prom.c prom_init().
 */
void __init fw_feature_init(void)
{
	struct device_node *dn;
	const char *hypertas, *s;
	int len, i;

	DBG(" -> fw_feature_init()\n");

	dn = of_find_node_by_path("/rtas");
	if (dn == NULL) {
		printk(KERN_ERR "WARNING! Cannot find RTAS in device-tree!\n");
		goto out;
	}

	hypertas = of_get_property(dn, "ibm,hypertas-functions", &len);
	if (hypertas == NULL)
		goto out;

	for (s = hypertas; s < hypertas + len; s += strlen(s) + 1) {
		for (i = 0; i < FIRMWARE_MAX_FEATURES; i++) {
			/* check value against table of strings */
			if (!firmware_features_table[i].name ||
			    strcmp(firmware_features_table[i].name, s))
				continue;

			/* we have a match */
			powerpc_firmware_features |=
				firmware_features_table[i].val;
			break;
		}
	}

out:
	of_node_put(dn);
	DBG(" <- fw_feature_init()\n");
}
