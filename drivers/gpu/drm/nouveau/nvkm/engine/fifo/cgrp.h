/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_CGRP_H__
#define __NVKM_CGRP_H__
#include <core/os.h>

struct nvkm_cgrp {
	const struct nvkm_cgrp_func {
	} *func;
	int id;
	struct list_head head;
	struct list_head chan;
	int chan_nr;
};
#endif
