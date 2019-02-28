/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_DCB_LIB_H_
#define _ICE_DCB_LIB_H_

#include "ice.h"
#include "ice_lib.h"

#ifdef CONFIG_DCB
u8 ice_dcb_get_ena_tc(struct ice_dcbx_cfg *dcbcfg);
u8 ice_dcb_get_num_tc(struct ice_dcbx_cfg *dcbcfg);
int ice_init_pf_dcb(struct ice_pf *pf);
void
ice_dcb_process_lldp_set_mib_change(struct ice_pf *pf,
				    struct ice_rq_event_info *event);
#else
static inline u8 ice_dcb_get_ena_tc(struct ice_dcbx_cfg __always_unused *dcbcfg)
{
	return ICE_DFLT_TRAFFIC_CLASS;
}

static inline u8 ice_dcb_get_num_tc(struct ice_dcbx_cfg __always_unused *dcbcfg)
{
	return 1;
}

static inline int ice_init_pf_dcb(struct ice_pf *pf)
{
	dev_dbg(&pf->pdev->dev, "DCB not supported\n");
	return -EOPNOTSUPP;
}

#define ice_dcb_process_lldp_set_mib_change(pf, event) do {} while (0)
#endif /* CONFIG_DCB */
#endif /* _ICE_DCB_LIB_H_ */
