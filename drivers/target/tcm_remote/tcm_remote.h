/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <linux/device.h>

#define TCM_REMOTE_VERSION		"v0.1"
#define TL_WWN_ADDR_LEN			256
#define TL_TPGS_PER_HBA			32

struct tcm_remote_tpg {
	unsigned short remote_tpgt;
	struct se_portal_group remote_se_tpg;
	struct tcm_remote_hba *remote_hba;
};

struct tcm_remote_hba {
	u8 remote_proto_id;
	unsigned char remote_wwn_address[TL_WWN_ADDR_LEN];
	struct tcm_remote_tpg remote_hba_tpgs[TL_TPGS_PER_HBA];
	struct se_wwn remote_hba_wwn;
};
