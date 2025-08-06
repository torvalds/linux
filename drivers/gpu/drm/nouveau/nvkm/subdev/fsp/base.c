/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "priv.h"

int
nvkm_fsp_boot_gsp_fmc(struct nvkm_fsp *fsp, u64 args_addr, u32 rsvd_size, bool resume,
		      u64 img_addr, const u8 *hash, const u8 *pkey, const u8 *sig)
{
	return fsp->func->cot.boot_gsp_fmc(fsp, args_addr, rsvd_size, resume,
					   img_addr, hash, pkey, sig);
}

bool
nvkm_fsp_verify_gsp_fmc(struct nvkm_fsp *fsp, u32 hash_size, u32 pkey_size, u32 sig_size)
{
	return hash_size == fsp->func->cot.size_hash &&
	       pkey_size == fsp->func->cot.size_pkey &&
	        sig_size == fsp->func->cot.size_sig;
}

static int
nvkm_fsp_preinit(struct nvkm_subdev *subdev)
{
	struct nvkm_fsp *fsp = nvkm_fsp(subdev);

	return fsp->func->wait_secure_boot(fsp);
}

static void *
nvkm_fsp_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_fsp *fsp = nvkm_fsp(subdev);

	nvkm_falcon_dtor(&fsp->falcon);
	return fsp;
}

static const struct nvkm_falcon_func
nvkm_fsp_flcn = {
	.emem_pio = &gp102_flcn_emem_pio,
};

static const struct nvkm_subdev_func
nvkm_fsp = {
	.dtor = nvkm_fsp_dtor,
	.preinit = nvkm_fsp_preinit,
};

int
nvkm_fsp_new_(const struct nvkm_fsp_func *func,
	      struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_fsp **pfsp)
{
	struct nvkm_fsp *fsp;

	fsp = *pfsp = kzalloc(sizeof(*fsp), GFP_KERNEL);
	if (!fsp)
		return -ENOMEM;

	fsp->func = func;
	nvkm_subdev_ctor(&nvkm_fsp, device, type, inst, &fsp->subdev);

	return nvkm_falcon_ctor(&nvkm_fsp_flcn, &fsp->subdev, "fsp", 0x8f2000, &fsp->falcon);
}
