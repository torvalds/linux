// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver VCAP implementation
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 *
 * The Sparx5 Chip Register Model can be browsed at this location:
 * https://github.com/microchip-ung/sparx-5_reginfo
 */

#include <linux/types.h>
#include <linux/list.h>

#include "vcap_api.h"
#include "vcap_api_client.h"
#include "sparx5_main_regs.h"
#include "sparx5_main.h"
#include "sparx5_vcap_impl.h"
#include "sparx5_vcap_ag_api.h"

#define SUPER_VCAP_BLK_SIZE 3072 /* addresses per Super VCAP block */
#define STREAMSIZE (64 * 4)  /* bytes in the VCAP cache area */

#define SPARX5_IS2_LOOKUPS 4

static struct sparx5_vcap_inst {
	enum vcap_type vtype; /* type of vcap */
	int vinst; /* instance number within the same type */
	int lookups; /* number of lookups in this vcap type */
	int lookups_per_instance; /* number of lookups in this instance */
	int first_cid; /* first chain id in this vcap */
	int last_cid; /* last chain id in this vcap */
	int count; /* number of available addresses, not in super vcap */
	int map_id; /* id in the super vcap block mapping (if applicable) */
	int blockno; /* starting block in super vcap (if applicable) */
	int blocks; /* number of blocks in super vcap (if applicable) */
} sparx5_vcap_inst_cfg[] = {
	{
		.vtype = VCAP_TYPE_IS2, /* IS2-0 */
		.vinst = 0,
		.map_id = 4,
		.lookups = SPARX5_IS2_LOOKUPS,
		.lookups_per_instance = SPARX5_IS2_LOOKUPS / 2,
		.first_cid = SPARX5_VCAP_CID_IS2_L0,
		.last_cid = SPARX5_VCAP_CID_IS2_L2 - 1,
		.blockno = 0, /* Maps block 0-1 */
		.blocks = 2,
	},
	{
		.vtype = VCAP_TYPE_IS2, /* IS2-1 */
		.vinst = 1,
		.map_id = 5,
		.lookups = SPARX5_IS2_LOOKUPS,
		.lookups_per_instance = SPARX5_IS2_LOOKUPS / 2,
		.first_cid = SPARX5_VCAP_CID_IS2_L2,
		.last_cid = SPARX5_VCAP_CID_IS2_MAX,
		.blockno = 2, /* Maps block 2-3 */
		.blocks = 2,
	},
};

static void sparx5_vcap_admin_free(struct vcap_admin *admin)
{
	if (!admin)
		return;
	kfree(admin->cache.keystream);
	kfree(admin->cache.maskstream);
	kfree(admin->cache.actionstream);
	kfree(admin);
}

/* Allocate a vcap instance with a rule list and a cache area */
static struct vcap_admin *
sparx5_vcap_admin_alloc(struct sparx5 *sparx5, struct vcap_control *ctrl,
			const struct sparx5_vcap_inst *cfg)
{
	struct vcap_admin *admin;

	admin = kzalloc(sizeof(*admin), GFP_KERNEL);
	if (!admin)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&admin->list);
	INIT_LIST_HEAD(&admin->rules);
	admin->vtype = cfg->vtype;
	admin->vinst = cfg->vinst;
	admin->lookups = cfg->lookups;
	admin->lookups_per_instance = cfg->lookups_per_instance;
	admin->first_cid = cfg->first_cid;
	admin->last_cid = cfg->last_cid;
	admin->cache.keystream =
		kzalloc(STREAMSIZE, GFP_KERNEL);
	admin->cache.maskstream =
		kzalloc(STREAMSIZE, GFP_KERNEL);
	admin->cache.actionstream =
		kzalloc(STREAMSIZE, GFP_KERNEL);
	if (!admin->cache.keystream || !admin->cache.maskstream ||
	    !admin->cache.actionstream) {
		sparx5_vcap_admin_free(admin);
		return ERR_PTR(-ENOMEM);
	}
	return admin;
}

/* Do block allocations and provide addresses for VCAP instances */
static void sparx5_vcap_block_alloc(struct sparx5 *sparx5,
				    struct vcap_admin *admin,
				    const struct sparx5_vcap_inst *cfg)
{
	int idx;

	/* Super VCAP block mapping and address configuration. Block 0
	 * is assigned addresses 0 through 3071, block 1 is assigned
	 * addresses 3072 though 6143, and so on.
	 */
	for (idx = cfg->blockno; idx < cfg->blockno + cfg->blocks; ++idx) {
		spx5_wr(VCAP_SUPER_IDX_CORE_IDX_SET(idx), sparx5,
			VCAP_SUPER_IDX);
		spx5_wr(VCAP_SUPER_MAP_CORE_MAP_SET(cfg->map_id), sparx5,
			VCAP_SUPER_MAP);
	}
	admin->first_valid_addr = cfg->blockno * SUPER_VCAP_BLK_SIZE;
	admin->last_used_addr = admin->first_valid_addr +
		cfg->blocks * SUPER_VCAP_BLK_SIZE;
	admin->last_valid_addr = admin->last_used_addr - 1;
}

/* Allocate a vcap control and vcap instances and configure the system */
int sparx5_vcap_init(struct sparx5 *sparx5)
{
	const struct sparx5_vcap_inst *cfg;
	struct vcap_control *ctrl;
	struct vcap_admin *admin;
	int err = 0, idx;

	/* Create a VCAP control instance that owns the platform specific VCAP
	 * model with VCAP instances and information about keysets, keys,
	 * actionsets and actions
	 * - Create administrative state for each available VCAP
	 *   - Lists of rules
	 *   - Address information
	 *   - Initialize VCAP blocks
	 */
	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	sparx5->vcap_ctrl = ctrl;
	/* select the sparx5 VCAP model */
	ctrl->vcaps = sparx5_vcaps;
	ctrl->stats = &sparx5_vcap_stats;

	INIT_LIST_HEAD(&ctrl->list);
	for (idx = 0; idx < ARRAY_SIZE(sparx5_vcap_inst_cfg); ++idx) {
		cfg = &sparx5_vcap_inst_cfg[idx];
		admin = sparx5_vcap_admin_alloc(sparx5, ctrl, cfg);
		if (IS_ERR(admin)) {
			err = PTR_ERR(admin);
			pr_err("%s:%d: vcap allocation failed: %d\n",
			       __func__, __LINE__, err);
			return err;
		}
		sparx5_vcap_block_alloc(sparx5, admin, cfg);
		list_add_tail(&admin->list, &ctrl->list);
	}

	return err;
}

void sparx5_vcap_destroy(struct sparx5 *sparx5)
{
	struct vcap_control *ctrl = sparx5->vcap_ctrl;
	struct vcap_admin *admin, *admin_next;

	if (!ctrl)
		return;

	list_for_each_entry_safe(admin, admin_next, &ctrl->list, list) {
		list_del(&admin->list);
		sparx5_vcap_admin_free(admin);
	}
	kfree(ctrl);
}
