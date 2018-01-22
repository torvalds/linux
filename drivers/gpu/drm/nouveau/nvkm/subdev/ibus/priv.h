/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_IBUS_PRIV_H__
#define __NVKM_IBUS_PRIV_H__

#include <subdev/ibus.h>

void gf100_ibus_intr(struct nvkm_subdev *);
void gk104_ibus_intr(struct nvkm_subdev *);
#endif
