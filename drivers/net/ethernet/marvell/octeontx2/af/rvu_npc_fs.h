/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2022 Marvell.
 *
 */

#ifndef __RVU_NPC_FS_H
#define __RVU_NPC_FS_H

#define IPV6_WORDS	4

void npc_update_entry(struct rvu *rvu, enum key_fields type,
		      struct mcam_entry *entry, u64 val_lo,
		      u64 val_hi, u64 mask_lo, u64 mask_hi, u8 intf);

#endif /* RVU_NPC_FS_H */
