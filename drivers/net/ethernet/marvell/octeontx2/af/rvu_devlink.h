/* SPDX-License-Identifier: GPL-2.0 */
/*  Marvell OcteonTx2 RVU Devlink
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#ifndef RVU_DEVLINK_H
#define  RVU_DEVLINK_H

struct rvu_devlink {
	struct devlink *dl;
	struct rvu *rvu;
};

/* Devlink APIs */
int rvu_register_dl(struct rvu *rvu);
void rvu_unregister_dl(struct rvu *rvu);

#endif /* RVU_DEVLINK_H */
