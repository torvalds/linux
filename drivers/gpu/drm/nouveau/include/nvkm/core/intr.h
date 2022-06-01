/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_INTR_H__
#define __NVKM_INTR_H__
#include <core/os.h>
struct nvkm_device;

void nvkm_intr_ctor(struct nvkm_device *);
void nvkm_intr_dtor(struct nvkm_device *);
int nvkm_intr_install(struct nvkm_device *);
void nvkm_intr_unarm(struct nvkm_device *);
void nvkm_intr_rearm(struct nvkm_device *);
#endif
