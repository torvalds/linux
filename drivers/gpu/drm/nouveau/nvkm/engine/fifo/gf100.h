/* SPDX-License-Identifier: MIT */
#ifndef __GF100_FIFO_H__
#define __GF100_FIFO_H__
#define gf100_fifo(p) container_of((p), struct gf100_fifo, base)
#include "priv.h"

#include <subdev/mmu.h>

struct gf100_fifo {
	struct nvkm_fifo base;
};
#endif
