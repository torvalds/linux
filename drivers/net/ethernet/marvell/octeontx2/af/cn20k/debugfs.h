/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell OcteonTx2 CGX driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#ifndef DEBUFS_H
#define DEBUFS_H

#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "struct.h"
#include "../mbox.h"

void print_nix_cn20k_sq_ctx(struct seq_file *m,
			    struct nix_cn20k_sq_ctx_s *sq_ctx);
void print_nix_cn20k_cq_ctx(struct seq_file *m,
			    struct nix_cn20k_aq_enq_rsp *rsp);
void print_npa_cn20k_aura_ctx(struct seq_file *m,
			      struct npa_cn20k_aq_enq_rsp *rsp);
void print_npa_cn20k_pool_ctx(struct seq_file *m,
			      struct npa_cn20k_aq_enq_rsp *rsp);

#endif
