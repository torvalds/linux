/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef __NVKM_FSP_PRIV_H__
#define __NVKM_FSP_PRIV_H__
#define nvkm_fsp(p) container_of((p), struct nvkm_fsp, subdev)
#include <subdev/fsp.h>

struct nvkm_fsp_func {
	int (*wait_secure_boot)(struct nvkm_fsp *);

	struct {
		u32 version;
		u32 size_hash;
		u32 size_pkey;
		u32 size_sig;
		int (*boot_gsp_fmc)(struct nvkm_fsp *, u64 args_addr, u32 rsvd_size, bool resume,
				    u64 img_addr, const u8 *hash, const u8 *pkey, const u8 *sig);
	} cot;
};

int nvkm_fsp_new_(const struct nvkm_fsp_func *,
		  struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_fsp **);

int gh100_fsp_wait_secure_boot(struct nvkm_fsp *);
int gh100_fsp_boot_gsp_fmc(struct nvkm_fsp *, u64 args_addr, u32 rsvd_size, bool resume,
			   u64 img_addr, const u8 *hash, const u8 *pkey, const u8 *sig);
#endif
