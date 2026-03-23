// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "regs/xe_gtt_defs.h"

#include "xe_assert.h"
#include "xe_ggtt.h"
#include "xe_sriov.h"
#include "xe_sriov_printk.h"
#include "xe_tile_sriov_vf.h"
#include "xe_wopcm.h"

/**
 * DOC: GGTT nodes shifting during VF post-migration recovery
 *
 * The first fixup applied to the VF KMD structures as part of post-migration
 * recovery is shifting nodes within &xe_ggtt instance. The nodes are moved
 * from range previously assigned to this VF, into newly provisioned area.
 *
 * Below is a GGTT layout of example VF, with a certain address range assigned to
 * said VF, and inaccessible areas above and below:
 *
 *  0                                                                        4GiB
 *  |<--------------------------- Total GGTT size ----------------------------->|
 *      WOPCM                                                         GUC_TOP
 *      |<-------------- Area mappable by xe_ggtt instance ---------------->|
 *
 *  +---+---------------------------------+----------+----------------------+---+
 *  |\\\|/////////////////////////////////|  VF mem  |//////////////////////|\\\|
 *  +---+---------------------------------+----------+----------------------+---+
 *
 * Hardware enforced access rules before migration:
 *
 *  |<------- inaccessible for VF ------->|<VF owned>|<-- inaccessible for VF ->|
 *
 * After the migration, GGTT area assigned to the VF might have shifted, either
 * to lower or to higher address. But we expect the total size and extra areas to
 * be identical, as migration can only happen between matching platforms.
 * Below is an example of GGTT layout of the VF after migration. Content of the
 * GGTT for VF has been moved to a new area, and we receive its address from GuC:
 *
 *  +---+----------------------+----------+---------------------------------+---+
 *  |\\\|//////////////////////|  VF mem  |/////////////////////////////////|\\\|
 *  +---+----------------------+----------+---------------------------------+---+
 *
 * Hardware enforced access rules after migration:
 *
 *  |<- inaccessible for VF -->|<VF owned>|<------- inaccessible for VF ------->|
 *
 * So the VF has a new slice of GGTT assigned, and during migration process, the
 * memory content was copied to that new area. But the &xe_ggtt nodes are still
 * tracking allocations using the old addresses. The nodes within VF owned area
 * have to be shifted, and the start offset for GGTT adjusted.
 *
 * Due to use of GPU profiles, we do not expect the old and new GGTT areas to
 * overlap; but our node shifting will fix addresses properly regardless.
 */

/**
 * xe_tile_sriov_vf_lmem - VF LMEM configuration.
 * @tile: the &xe_tile
 *
 * This function is for VF use only.
 *
 * Return: size of the LMEM assigned to VF.
 */
u64 xe_tile_sriov_vf_lmem(struct xe_tile *tile)
{
	struct xe_tile_sriov_vf_selfconfig *config = &tile->sriov.vf.self_config;

	xe_tile_assert(tile, IS_SRIOV_VF(tile_to_xe(tile)));

	return config->lmem_size;
}

/**
 * xe_tile_sriov_vf_lmem_store - Store VF LMEM configuration
 * @tile: the &xe_tile
 * @lmem_size: VF LMEM size to store
 *
 * This function is for VF use only.
 */
void xe_tile_sriov_vf_lmem_store(struct xe_tile *tile, u64 lmem_size)
{
	struct xe_tile_sriov_vf_selfconfig *config = &tile->sriov.vf.self_config;

	xe_tile_assert(tile, IS_SRIOV_VF(tile_to_xe(tile)));

	config->lmem_size = lmem_size;
}

/**
 * xe_tile_sriov_vf_ggtt - VF GGTT configuration.
 * @tile: the &xe_tile
 *
 * This function is for VF use only.
 *
 * Return: size of the GGTT assigned to VF.
 */
u64 xe_tile_sriov_vf_ggtt(struct xe_tile *tile)
{
	struct xe_tile_sriov_vf_selfconfig *config = &tile->sriov.vf.self_config;

	xe_tile_assert(tile, IS_SRIOV_VF(tile_to_xe(tile)));

	return config->ggtt_size;
}

/**
 * xe_tile_sriov_vf_ggtt_store - Store VF GGTT configuration
 * @tile: the &xe_tile
 * @ggtt_size: VF GGTT size to store
 *
 * This function is for VF use only.
 */
void xe_tile_sriov_vf_ggtt_store(struct xe_tile *tile, u64 ggtt_size)
{
	struct xe_tile_sriov_vf_selfconfig *config = &tile->sriov.vf.self_config;

	xe_tile_assert(tile, IS_SRIOV_VF(tile_to_xe(tile)));

	config->ggtt_size = ggtt_size;
}

/**
 * xe_tile_sriov_vf_ggtt_base - VF GGTT base configuration.
 * @tile: the &xe_tile
 *
 * This function is for VF use only.
 *
 * Return: base of the GGTT assigned to VF.
 */
u64 xe_tile_sriov_vf_ggtt_base(struct xe_tile *tile)
{
	struct xe_tile_sriov_vf_selfconfig *config = &tile->sriov.vf.self_config;

	xe_tile_assert(tile, IS_SRIOV_VF(tile_to_xe(tile)));

	return READ_ONCE(config->ggtt_base);
}

/**
 * xe_tile_sriov_vf_ggtt_base_store - Store VF GGTT base configuration
 * @tile: the &xe_tile
 * @ggtt_base: VF GGTT base to store
 *
 * This function is for VF use only.
 */
void xe_tile_sriov_vf_ggtt_base_store(struct xe_tile *tile, u64 ggtt_base)
{
	struct xe_tile_sriov_vf_selfconfig *config = &tile->sriov.vf.self_config;

	xe_tile_assert(tile, IS_SRIOV_VF(tile_to_xe(tile)));

	WRITE_ONCE(config->ggtt_base, ggtt_base);
}
