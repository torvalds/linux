/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef __NVKM_RM_GPU_H__
#define __NVKM_RM_GPU_H__
#include "rm.h"

struct nvkm_rm_gpu {
	struct {
		struct {
			u32 root;
			u32 caps;
			u32 core;
			u32 wndw;
			u32 wimm;
			u32 curs;
		} class;
	} disp;

	struct {
		u32 class;
	} usermode;

	struct {
		struct {
			u32 class;
			u32 (*doorbell_handle)(struct nvkm_chan *);
		} chan;
	} fifo;

	struct {
		u32 class;
		u32 (*grce_mask)(struct nvkm_device *);
	} ce;

	struct {
		struct {
			u32 i2m;
			u32 twod;
			u32 threed;
			u32 compute;
		} class;
	} gr;

	struct {
		u32 class;
	} nvdec;

	struct {
		u32 class;
	} nvenc;

	struct {
		u32 class;
	} nvjpg;

	struct {
		u32 class;
	} ofa;
};

extern const struct nvkm_rm_gpu tu1xx_gpu;
extern const struct nvkm_rm_gpu ga100_gpu;
extern const struct nvkm_rm_gpu ga1xx_gpu;
extern const struct nvkm_rm_gpu ad10x_gpu;
extern const struct nvkm_rm_gpu gh100_gpu;
extern const struct nvkm_rm_gpu gb10x_gpu;
extern const struct nvkm_rm_gpu gb20x_gpu;
#endif
