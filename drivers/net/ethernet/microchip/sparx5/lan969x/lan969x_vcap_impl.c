// SPDX-License-Identifier: GPL-2.0+

#include "vcap_api.h"
#include "lan969x.h"

const struct sparx5_vcap_inst lan969x_vcap_inst_cfg[] = {
	{
		.vtype = VCAP_TYPE_IS0, /* CLM-0 */
		.vinst = 0,
		.map_id = 1,
		.lookups = SPARX5_IS0_LOOKUPS,
		.lookups_per_instance = SPARX5_IS0_LOOKUPS / 3,
		.first_cid = SPARX5_VCAP_CID_IS0_L0,
		.last_cid = SPARX5_VCAP_CID_IS0_L2 - 1,
		.blockno = 2,
		.blocks = 1,
		.ingress = true,
	},
	{
		.vtype = VCAP_TYPE_IS0, /* CLM-1 */
		.vinst = 1,
		.map_id = 2,
		.lookups = SPARX5_IS0_LOOKUPS,
		.lookups_per_instance = SPARX5_IS0_LOOKUPS / 3,
		.first_cid = SPARX5_VCAP_CID_IS0_L2,
		.last_cid = SPARX5_VCAP_CID_IS0_L4 - 1,
		.blockno = 3,
		.blocks = 1,
		.ingress = true,
	},
	{
		.vtype = VCAP_TYPE_IS0, /* CLM-2 */
		.vinst = 2,
		.map_id = 3,
		.lookups = SPARX5_IS0_LOOKUPS,
		.lookups_per_instance = SPARX5_IS0_LOOKUPS / 3,
		.first_cid = SPARX5_VCAP_CID_IS0_L4,
		.last_cid = SPARX5_VCAP_CID_IS0_MAX,
		.blockno = 4,
		.blocks = 1,
		.ingress = true,
	},
	{
		.vtype = VCAP_TYPE_IS2, /* IS2-0 */
		.vinst = 0,
		.map_id = 4,
		.lookups = SPARX5_IS2_LOOKUPS,
		.lookups_per_instance = SPARX5_IS2_LOOKUPS / 2,
		.first_cid = SPARX5_VCAP_CID_IS2_L0,
		.last_cid = SPARX5_VCAP_CID_IS2_L2 - 1,
		.blockno = 0,
		.blocks = 1,
		.ingress = true,
	},
	{
		.vtype = VCAP_TYPE_IS2, /* IS2-1 */
		.vinst = 1,
		.map_id = 5,
		.lookups = SPARX5_IS2_LOOKUPS,
		.lookups_per_instance = SPARX5_IS2_LOOKUPS / 2,
		.first_cid = SPARX5_VCAP_CID_IS2_L2,
		.last_cid = SPARX5_VCAP_CID_IS2_MAX,
		.blockno = 1,
		.blocks = 1,
		.ingress = true,
	},
	{
		.vtype = VCAP_TYPE_ES0,
		.lookups = SPARX5_ES0_LOOKUPS,
		.lookups_per_instance = SPARX5_ES0_LOOKUPS,
		.first_cid = SPARX5_VCAP_CID_ES0_L0,
		.last_cid = SPARX5_VCAP_CID_ES0_MAX,
		.count = 1536,
		.ingress = false,
	},
	{
		.vtype = VCAP_TYPE_ES2,
		.lookups = SPARX5_ES2_LOOKUPS,
		.lookups_per_instance = SPARX5_ES2_LOOKUPS,
		.first_cid = SPARX5_VCAP_CID_ES2_L0,
		.last_cid = SPARX5_VCAP_CID_ES2_MAX,
		.count = 1024,
		.ingress = false,
	},
};
