/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef __NVKM_FSP_H__
#define __NVKM_FSP_H__
#include <core/subdev.h>
#include <core/falcon.h>

struct nvkm_fsp {
	const struct nvkm_fsp_func *func;
	struct nvkm_subdev subdev;

	struct nvkm_falcon falcon;
};

bool nvkm_fsp_verify_gsp_fmc(struct nvkm_fsp *, u32 hash_size, u32 pkey_size, u32 sig_size);
int nvkm_fsp_boot_gsp_fmc(struct nvkm_fsp *, u64 args_addr, u32 rsvd_size, bool resume,
			  u64 img_addr, const u8 *hash, const u8 *pkey, const u8 *sig);

int gh100_fsp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fsp **);
int gb100_fsp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fsp **);
int gb202_fsp_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fsp **);
#endif
