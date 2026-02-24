/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2026 Marvell.
 *
 */

#ifndef RVU_NPC_H
#define RVU_NPC_H

u64 npc_enable_mask(int count);
void npc_load_kpu_profile(struct rvu *rvu);
void npc_config_kpuaction(struct rvu *rvu, int blkaddr,
			  const struct npc_kpu_profile_action *kpuaction,
			  int kpu, int entry, bool pkind);
int npc_fwdb_prfl_img_map(struct rvu *rvu, void __iomem **prfl_img_addr,
			  u64 *size);

#endif /* RVU_NPC_H */
