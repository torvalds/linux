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
	struct qmem             *vf_mbox_addr;
};

/* Mbox related APIs */
int cn20k_rvu_mbox_init(struct rvu *rvu, int type, int num);
int cn20k_rvu_get_mbox_regions(struct rvu *rvu, void **mbox_addr,
			       int num, int type, unsigned long *pf_bmap);
void cn20k_free_mbox_memory(struct rvu *rvu);
int cn20k_register_afpf_mbox_intr(struct rvu *rvu);
int cn20k_register_afvf_mbox_intr(struct rvu *rvu, int pf_vec_start);
void cn20k_rvu_enable_mbox_intr(struct rvu *rvu);
void cn20k_rvu_unregister_interrupts(struct rvu *rvu);
int cn20k_mbox_setup(struct otx2_mbox *mbox, struct pci_dev *pdev,
		     void *reg_base, int direction, int ndevs);
void cn20k_rvu_enable_afvf_intr(struct rvu *rvu, int vfs);
void cn20k_rvu_disable_afvf_intr(struct rvu *rvu, int vfs);
#endif /* CN20K_API_H */
