/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef __NVKM_RM_HANDLES_H__
#define __NVKM_RM_HANDLES_H__

/* RMAPI handles for various objects allocated from GSP-RM with RM_ALLOC. */

#define NVKM_RM_CLIENT(id)         (0xc1d00000 | (id))
#define NVKM_RM_CLIENT_MASK         0x0000ffff
#define NVKM_RM_DEVICE              0xde1d0000
#define NVKM_RM_SUBDEVICE           0x5d1d0000
#define NVKM_RM_DISP                0x00730000
#define NVKM_RM_VASPACE             0x90f10000
#define NVKM_RM_CHAN(chid)         (0xf1f00000 | (chid))
#define NVKM_RM_THREED              0x97000000
#endif
