// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"
#include "lan966x_vcap_ag_api.h"
#include "vcap_api.h"
#include "vcap_api_client.h"

#define LAN966X_VCAP_CID_IS2_L0 VCAP_CID_INGRESS_STAGE2_L0 /* IS2 lookup 0 */
#define LAN966X_VCAP_CID_IS2_L1 VCAP_CID_INGRESS_STAGE2_L1 /* IS2 lookup 1 */
#define LAN966X_VCAP_CID_IS2_MAX (VCAP_CID_INGRESS_STAGE2_L2 - 1) /* IS2 Max */

#define STREAMSIZE (64 * 4)

#define LAN966X_IS2_LOOKUPS 2

static struct lan966x_vcap_inst {
	enum vcap_type vtype; /* type of vcap */
	int tgt_inst; /* hardware instance number */
	int lookups; /* number of lookups in this vcap type */
	int first_cid; /* first chain id in this vcap */
	int last_cid; /* last chain id in this vcap */
	int count; /* number of available addresses */
} lan966x_vcap_inst_cfg[] = {
	{
		.vtype = VCAP_TYPE_IS2, /* IS2-0 */
		.tgt_inst = 2,
		.lookups = LAN966X_IS2_LOOKUPS,
		.first_cid = LAN966X_VCAP_CID_IS2_L0,
		.last_cid = LAN966X_VCAP_CID_IS2_MAX,
		.count = 256,
	},
};

struct lan966x_vcap_cmd_cb {
	struct lan966x *lan966x;
	u32 instance;
};

static u32 lan966x_vcap_read_update_ctrl(const struct lan966x_vcap_cmd_cb *cb)
{
	return lan_rd(cb->lan966x, VCAP_UPDATE_CTRL(cb->instance));
}

static void lan966x_vcap_wait_update(struct lan966x *lan966x, int instance)
{
	const struct lan966x_vcap_cmd_cb cb = { .lan966x = lan966x,
						.instance = instance };
	u32 val;

	readx_poll_timeout(lan966x_vcap_read_update_ctrl, &cb, val,
			   (val & VCAP_UPDATE_CTRL_UPDATE_SHOT) == 0, 10,
			   100000);
}

static void __lan966x_vcap_range_init(struct lan966x *lan966x,
				      struct vcap_admin *admin,
				      u32 addr,
				      u32 count)
{
	lan_wr(VCAP_MV_CFG_MV_NUM_POS_SET(0) |
	       VCAP_MV_CFG_MV_SIZE_SET(count - 1),
	       lan966x, VCAP_MV_CFG(admin->tgt_inst));

	lan_wr(VCAP_UPDATE_CTRL_UPDATE_CMD_SET(VCAP_CMD_INITIALIZE) |
	       VCAP_UPDATE_CTRL_UPDATE_ENTRY_DIS_SET(0) |
	       VCAP_UPDATE_CTRL_UPDATE_ACTION_DIS_SET(0) |
	       VCAP_UPDATE_CTRL_UPDATE_CNT_DIS_SET(0) |
	       VCAP_UPDATE_CTRL_UPDATE_ADDR_SET(addr) |
	       VCAP_UPDATE_CTRL_CLEAR_CACHE_SET(true) |
	       VCAP_UPDATE_CTRL_UPDATE_SHOT_SET(1),
	       lan966x, VCAP_UPDATE_CTRL(admin->tgt_inst));

	lan966x_vcap_wait_update(lan966x, admin->tgt_inst);
}

static void lan966x_vcap_admin_free(struct vcap_admin *admin)
{
	if (!admin)
		return;

	kfree(admin->cache.keystream);
	kfree(admin->cache.maskstream);
	kfree(admin->cache.actionstream);
	mutex_destroy(&admin->lock);
	kfree(admin);
}

static struct vcap_admin *
lan966x_vcap_admin_alloc(struct lan966x *lan966x, struct vcap_control *ctrl,
			 const struct lan966x_vcap_inst *cfg)
{
	struct vcap_admin *admin;

	admin = kzalloc(sizeof(*admin), GFP_KERNEL);
	if (!admin)
		return ERR_PTR(-ENOMEM);

	mutex_init(&admin->lock);
	INIT_LIST_HEAD(&admin->list);
	INIT_LIST_HEAD(&admin->rules);
	admin->vtype = cfg->vtype;
	admin->vinst = 0;
	admin->lookups = cfg->lookups;
	admin->lookups_per_instance = cfg->lookups;
	admin->first_cid = cfg->first_cid;
	admin->last_cid = cfg->last_cid;
	admin->cache.keystream = kzalloc(STREAMSIZE, GFP_KERNEL);
	admin->cache.maskstream = kzalloc(STREAMSIZE, GFP_KERNEL);
	admin->cache.actionstream = kzalloc(STREAMSIZE, GFP_KERNEL);
	if (!admin->cache.keystream ||
	    !admin->cache.maskstream ||
	    !admin->cache.actionstream) {
		lan966x_vcap_admin_free(admin);
		return ERR_PTR(-ENOMEM);
	}

	return admin;
}

static void lan966x_vcap_block_init(struct lan966x *lan966x,
				    struct vcap_admin *admin,
				    struct lan966x_vcap_inst *cfg)
{
	admin->first_valid_addr = 0;
	admin->last_used_addr = cfg->count;
	admin->last_valid_addr = cfg->count - 1;

	lan_wr(VCAP_CORE_IDX_CORE_IDX_SET(0),
	       lan966x, VCAP_CORE_IDX(admin->tgt_inst));
	lan_wr(VCAP_CORE_MAP_CORE_MAP_SET(1),
	       lan966x, VCAP_CORE_MAP(admin->tgt_inst));

	__lan966x_vcap_range_init(lan966x, admin, admin->first_valid_addr,
				  admin->last_valid_addr -
					admin->first_valid_addr);
}

int lan966x_vcap_init(struct lan966x *lan966x)
{
	struct lan966x_vcap_inst *cfg;
	struct vcap_control *ctrl;
	struct vcap_admin *admin;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	ctrl->vcaps = lan966x_vcaps;
	ctrl->stats = &lan966x_vcap_stats;

	INIT_LIST_HEAD(&ctrl->list);
	for (int i = 0; i < ARRAY_SIZE(lan966x_vcap_inst_cfg); ++i) {
		cfg = &lan966x_vcap_inst_cfg[i];

		admin = lan966x_vcap_admin_alloc(lan966x, ctrl, cfg);
		if (IS_ERR(admin))
			return PTR_ERR(admin);

		lan966x_vcap_block_init(lan966x, admin, cfg);
		list_add_tail(&admin->list, &ctrl->list);
	}

	lan966x->vcap_ctrl = ctrl;

	return 0;
}

void lan966x_vcap_deinit(struct lan966x *lan966x)
{
	struct vcap_admin *admin, *admin_next;
	struct vcap_control *ctrl;

	ctrl = lan966x->vcap_ctrl;
	if (!ctrl)
		return;

	list_for_each_entry_safe(admin, admin_next, &ctrl->list, list) {
		vcap_del_rules(ctrl, admin);
		list_del(&admin->list);
		lan966x_vcap_admin_free(admin);
	}

	kfree(ctrl);
}
