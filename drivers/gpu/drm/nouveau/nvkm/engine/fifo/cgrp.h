#ifndef __NVKM_FIFO_CGRP_H__
#define __NVKM_FIFO_CGRP_H__
#include "priv.h"

struct nvkm_fifo_cgrp {
	int id;
	struct list_head head;
	struct list_head chan;
	int chan_nr;
};
#endif
