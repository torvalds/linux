/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Renesas RZ System Controller
 *
 * Copyright (C) 2024 Renesas Electronics Corp.
 */

#ifndef __SOC_RENESAS_RZ_SYSC_H__
#define __SOC_RENESAS_RZ_SYSC_H__

#include <linux/types.h>

/**
 * struct rz_syc_soc_id_init_data - RZ SYSC SoC identification initialization data
 * @family: RZ SoC family
 * @id: RZ SoC expected ID
 * @devid_offset: SYSC SoC ID register offset
 * @revision_mask: SYSC SoC ID revision mask
 * @specific_id_mask: SYSC SoC ID specific ID mask
 */
struct rz_sysc_soc_id_init_data {
	const char * const family;
	u32 id;
	u32 devid_offset;
	u32 revision_mask;
	u32 specific_id_mask;
};

/**
 * struct rz_sysc_init_data - RZ SYSC initialization data
 * @soc_id_init_data: RZ SYSC SoC ID initialization data
 */
struct rz_sysc_init_data {
	const struct rz_sysc_soc_id_init_data *soc_id_init_data;
};

#endif /* __SOC_RENESAS_RZ_SYSC_H__ */
