// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2023  Marvell.
 * Based on coresight-cfg-afdo.c
 */

#include "coresight-config.h"

/* ETMv4 includes and features */
#if IS_ENABLED(CONFIG_CORESIGHT_SOURCE_ETM4X)
#include "coresight-etm4x-cfg.h"

/* preload configurations and features */

/* preload in features for ETMv4 */

/* panic_stop feature */
static struct cscfg_parameter_desc gen_etrig_params[] = {
	{
		.name = "address",
		.value = (u64)panic,
	},
};

static struct cscfg_regval_desc gen_etrig_regs[] = {
	/* resource selector */
	{
		.type = CS_CFG_REG_TYPE_RESOURCE,
		.offset = TRCRSCTLRn(2),
		.hw_info = ETM4_CFG_RES_SEL,
		.val32 = 0x40001,
	},
	/* single address comparator */
	{
		.type = CS_CFG_REG_TYPE_RESOURCE | CS_CFG_REG_TYPE_VAL_64BIT |
			CS_CFG_REG_TYPE_VAL_PARAM,
		.offset =  TRCACVRn(0),
		.val32 = 0x0,
	},
	{
		.type = CS_CFG_REG_TYPE_RESOURCE,
		.offset = TRCACATRn(0),
		.val64 = 0xf00,
	},
	/* Driver external output[0] with comparator out */
	{
		.type = CS_CFG_REG_TYPE_RESOURCE,
		.offset = TRCEVENTCTL0R,
		.val32 = 0x2,
	},
	/* end of regs */
};

struct cscfg_feature_desc gen_etrig_etm4x = {
	.name = "gen_etrig",
	.description = "Generate external trigger on address match\n"
		       "parameter \'address\': address of kernel address\n",
	.match_flags = CS_CFG_MATCH_CLASS_SRC_ETM4,
	.nr_params = ARRAY_SIZE(gen_etrig_params),
	.params_desc = gen_etrig_params,
	.nr_regs = ARRAY_SIZE(gen_etrig_regs),
	.regs_desc = gen_etrig_regs,
};

/* create a panic stop configuration */

/* the total number of parameters in used features */
#define PSTOP_NR_PARAMS	ARRAY_SIZE(gen_etrig_params)

static const char *pstop_ref_names[] = {
	"gen_etrig",
};

struct cscfg_config_desc pstop_etm4x = {
	.name = "panicstop",
	.description = "Stop ETM on kernel panic\n",
	.nr_feat_refs = ARRAY_SIZE(pstop_ref_names),
	.feat_ref_names = pstop_ref_names,
	.nr_total_params = PSTOP_NR_PARAMS,
};

/* end of ETM4x configurations */
#endif	/* IS_ENABLED(CONFIG_CORESIGHT_SOURCE_ETM4X) */
