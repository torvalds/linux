/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU PF/VF Netdev Devlink
 *
 * Copyright (C) 2021 Marvell.
 *
 */

#ifndef	OTX2_DEVLINK_H
#define	OTX2_DEVLINK_H

struct otx2_devlink {
	struct devlink *dl;
	struct otx2_nic *pfvf;
};

/* Devlink APIs */
int otx2_register_dl(struct otx2_nic *pfvf);
void otx2_unregister_dl(struct otx2_nic *pfvf);

#endif /* RVU_DEVLINK_H */
