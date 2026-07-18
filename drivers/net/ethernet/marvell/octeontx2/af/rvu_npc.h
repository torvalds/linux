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

void npc_mcam_clear_bit(struct npc_mcam *mcam, u16 index);
void npc_mcam_set_bit(struct npc_mcam *mcam, u16 index);

struct npc_kpu_profile_action *
npc_get_ikpu_nth_entry(struct rvu *rvu, int n);

int
npc_get_num_kpu_cam_entries(struct rvu *rvu,
			    const struct npc_kpu_profile *kpu_pfl);
struct npc_kpu_profile_cam *
npc_get_kpu_cam_nth_entry(struct rvu *rvu,
			  const struct npc_kpu_profile *kpu_pfl, int n);

int
npc_get_num_kpu_action_entries(struct rvu *rvu,
			       const struct npc_kpu_profile *kpu_pfl);
struct npc_kpu_profile_action *
npc_get_kpu_action_nth_entry(struct rvu *rvu,
			     const struct npc_kpu_profile *kpu_pfl, int n);
#endif /* RVU_NPC_H */
