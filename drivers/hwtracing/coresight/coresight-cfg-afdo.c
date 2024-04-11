// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2020 Linaro Limited. All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */

#include "coresight-config.h"

/* ETMv4 includes and features */
#if IS_ENABLED(CONFIG_CORESIGHT_SOURCE_ETM4X)
#include "coresight-etm4x-cfg.h"
#include "coresight-cfg-preload.h"

/* preload configurations and features */

/* preload in features for ETMv4 */

/* strobe feature */
static struct cscfg_parameter_desc strobe_params[] = {
	{
		.name = "window",
		.value = 5000,
	},
	{
		.name = "period",
		.value = 10000,
	},
};

static struct cscfg_regval_desc strobe_regs[] = {
	/* resource selectors */
	{
		.type = CS_CFG_REG_TYPE_RESOURCE,
		.offset = TRCRSCTLRn(2),
		.hw_info = ETM4_CFG_RES_SEL,
		.val32 = 0x20001,
	},
	{
		.type = CS_CFG_REG_TYPE_RESOURCE,
		.offset = TRCRSCTLRn(3),
		.hw_info = ETM4_CFG_RES_SEQ,
		.val32 = 0x20002,
	},
	/* strobe window counter 0 - reload from param 0 */
	{
		.type = CS_CFG_REG_TYPE_RESOURCE | CS_CFG_REG_TYPE_VAL_SAVE,
		.offset = TRCCNTVRn(0),
		.hw_info = ETM4_CFG_RES_CTR,
	},
	{
		.type = CS_CFG_REG_TYPE_RESOURCE | CS_CFG_REG_TYPE_VAL_PARAM,
		.offset = TRCCNTRLDVRn(0),
		.hw_info = ETM4_CFG_RES_CTR,
		.val32 = 0,
	},
	{
		.type = CS_CFG_REG_TYPE_RESOURCE,
		.offset = TRCCNTCTLRn(0),
		.hw_info = ETM4_CFG_RES_CTR,
		.val32 = 0x10001,
	},
	/* strobe period counter 1 - reload from param 1 */
	{
		.type = CS_CFG_REG_TYPE_RESOURCE | CS_CFG_REG_TYPE_VAL_SAVE,
		.offset = TRCCNTVRn(1),
		.hw_info = ETM4_CFG_RES_CTR,
	},
	{
		.type = CS_CFG_REG_TYPE_RESOURCE | CS_CFG_REG_TYPE_VAL_PARAM,
		.offset = TRCCNTRLDVRn(1),
		.hw_info = ETM4_CFG_RES_CTR,
		.val32 = 1,
	},
	{
		.type = CS_CFG_REG_TYPE_RESOURCE,
		.offset = TRCCNTCTLRn(1),
		.hw_info = ETM4_CFG_RES_CTR,
		.val32 = 0x8102,
	},
	/* sequencer */
	{
		.type = CS_CFG_REG_TYPE_RESOURCE,
		.offset = TRCSEQEVRn(0),
		.hw_info = ETM4_CFG_RES_SEQ,
		.val32 = 0x0081,
	},
	{
		.type = CS_CFG_REG_TYPE_RESOURCE,
		.offset = TRCSEQEVRn(1),
		.hw_info = ETM4_CFG_RES_SEQ,
		.val32 = 0x0000,
	},
	/* view-inst */
	{
		.type = CS_CFG_REG_TYPE_STD | CS_CFG_REG_TYPE_VAL_MASK,
		.offset = TRCVICTLR,
		.val32 = 0x0003,
		.mask32 = 0x0003,
	},
	/* end of regs */
};

struct cscfg_feature_desc strobe_etm4x = {
	.name = "strobing",
	.description = "Generate periodic trace capture windows.\n"
		       "parameter \'window\': a number of CPU cycles (W)\n"
		       "parameter \'period\': trace enabled for W cycles every period x W cycles\n",
	.match_flags = CS_CFG_MATCH_CLASS_SRC_ETM4,
	.nr_params = ARRAY_SIZE(strobe_params),
	.params_desc = strobe_params,
	.nr_regs = ARRAY_SIZE(strobe_regs),
	.regs_desc = strobe_regs,
};

/* create an autofdo configuration */

/* we will provide 9 sets of preset parameter values */
#define AFDO_NR_PRESETS	9
/* the total number of parameters in used features */
#define AFDO_NR_PARAMS	ARRAY_SIZE(strobe_params)

static const char *afdo_ref_names[] = {
	"strobing",
};

/*
 * set of presets leaves strobing window constant while varying period to allow
 * experimentation with mark / space ratios for various workloads
 */
static u64 afdo_presets[AFDO_NR_PRESETS][AFDO_NR_PARAMS] = {
	{ 5000, 2 },
	{ 5000, 4 },
	{ 5000, 8 },
	{ 5000, 16 },
	{ 5000, 64 },
	{ 5000, 128 },
	{ 5000, 512 },
	{ 5000, 1024 },
	{ 5000, 4096 },
};

struct cscfg_config_desc afdo_etm4x = {
	.name = "autofdo",
	.description = "Setup ETMs with strobing for autofdo\n"
	"Supplied presets allow experimentation with mark-space ratio for various loads\n",
	.nr_feat_refs = ARRAY_SIZE(afdo_ref_names),
	.feat_ref_names = afdo_ref_names,
	.nr_presets = AFDO_NR_PRESETS,
	.nr_total_params = AFDO_NR_PARAMS,
	.presets = &afdo_presets[0][0],
};

/* end of ETM4x configurations */
#endif	/* IS_ENABLED(CONFIG_CORESIGHT_SOURCE_ETM4X) */
