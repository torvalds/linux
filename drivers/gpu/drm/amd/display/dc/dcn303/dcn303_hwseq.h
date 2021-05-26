// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2021 Advanced Micro Devices, Inc.
 *
 * Authors: AMD
 */

#ifndef __DC_HWSS_DCN303_H__
#define __DC_HWSS_DCN303_H__

#include "hw_sequencer_private.h"

void dcn303_dpp_pg_control(struct dce_hwseq *hws, unsigned int dpp_inst, bool power_on);
void dcn303_hubp_pg_control(struct dce_hwseq *hws, unsigned int hubp_inst, bool power_on);
void dcn303_dsc_pg_control(struct dce_hwseq *hws, unsigned int dsc_inst, bool power_on);

#endif /* __DC_HWSS_DCN303_H__ */
