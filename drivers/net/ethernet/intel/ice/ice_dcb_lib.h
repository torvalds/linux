/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_DCB_LIB_H_
#define _ICE_DCB_LIB_H_

#include "ice.h"
#include "ice_lib.h"

#ifdef CONFIG_DCB
int ice_init_pf_dcb(struct ice_pf *pf);
#else
static inline int ice_init_pf_dcb(struct ice_pf *pf)
{
	dev_dbg(&pf->pdev->dev, "DCB not supported\n");
	return -EOPNOTSUPP;
}
#endif /* CONFIG_DCB */
#endif /* _ICE_DCB_LIB_H_ */
