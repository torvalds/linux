/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NOUVEAU_VGA_H__
#define __NOUVEAU_VGA_H__
#include <core/subdev.h>

/* access to various legacy io ports */
u8   nvkm_rdport(struct nvkm_device *, int head, u16 port);
void nvkm_wrport(struct nvkm_device *, int head, u16 port, u8 value);

/* VGA Sequencer */
u8   nvkm_rdvgas(struct nvkm_device *, int head, u8 index);
void nvkm_wrvgas(struct nvkm_device *, int head, u8 index, u8 value);

/* VGA Graphics */
u8   nvkm_rdvgag(struct nvkm_device *, int head, u8 index);
void nvkm_wrvgag(struct nvkm_device *, int head, u8 index, u8 value);

/* VGA CRTC */
u8   nvkm_rdvgac(struct nvkm_device *, int head, u8 index);
void nvkm_wrvgac(struct nvkm_device *, int head, u8 index, u8 value);

/* VGA indexed port access dispatcher */
u8   nvkm_rdvgai(struct nvkm_device *, int head, u16 port, u8 index);
void nvkm_wrvgai(struct nvkm_device *, int head, u16 port, u8 index, u8 value);

bool nvkm_lockvgac(struct nvkm_device *, bool lock);
u8   nvkm_rdvgaowner(struct nvkm_device *);
void nvkm_wrvgaowner(struct nvkm_device *, u8);
#endif
