/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_DCB_NL_H_
#define _ICE_DCB_NL_H_

#ifdef CONFIG_DCB
void ice_dcbnl_setup(struct ice_vsi *vsi);
void ice_dcbnl_set_all(struct ice_vsi *vsi);
void
ice_dcbnl_flush_apps(struct ice_pf *pf, struct ice_dcbx_cfg *old_cfg,
		     struct ice_dcbx_cfg *new_cfg);
#else
#define ice_dcbnl_setup(vsi) do {} while (0)
#define ice_dcbnl_set_all(vsi) do {} while (0)
#define ice_dcbnl_flush_apps(pf, old_cfg, new_cfg) do {} while (0)
#endif /* CONFIG_DCB */

#endif /* _ICE_DCB_NL_H_ */
