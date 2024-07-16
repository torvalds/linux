/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2021 Marvell.
 */

#ifndef __OTX2_CPT_DEVLINK_H
#define __OTX2_CPT_DEVLINK_H

#include "otx2_cpt_common.h"
#include "otx2_cptpf.h"

struct otx2_cpt_devlink {
	struct devlink *dl;
	struct otx2_cptpf_dev *cptpf;
};

/* Devlink APIs */
int otx2_cpt_register_dl(struct otx2_cptpf_dev *cptpf);
void otx2_cpt_unregister_dl(struct otx2_cptpf_dev *cptpf);

#endif /* __OTX2_CPT_DEVLINK_H */
