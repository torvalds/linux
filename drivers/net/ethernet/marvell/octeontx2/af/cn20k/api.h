/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#ifndef CN20K_API_H
#define CN20K_API_H

#include "../rvu.h"

struct ng_rvu {
	struct mbox_ops         *rvu_mbox_ops;
	struct qmem             *pf_mbox_addr;
};

/* Mbox related APIs */
int cn20k_rvu_mbox_init(struct rvu *rvu, int type, int num);
int cn20k_rvu_get_mbox_regions(struct rvu *rvu, void **mbox_addr,
			       int num, int type, unsigned long *pf_bmap);
void cn20k_free_mbox_memory(struct rvu *rvu);
#endif /* CN20K_API_H */
