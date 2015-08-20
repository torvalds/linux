#ifndef __NVKM_FIFO_PRIV_H__
#define __NVKM_FIFO_PRIV_H__
#include <engine/fifo.h>

void nv04_fifo_pause(struct nvkm_fifo *, unsigned long *);
void nv04_fifo_start(struct nvkm_fifo *, unsigned long *);
#endif
