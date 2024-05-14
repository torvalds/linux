#ifndef __NVIF_IF900B_H__
#define __NVIF_IF900B_H__
#include "if000a.h"

struct gf100_mem_vn {
	/* nvif_mem_vX ... */
};

struct gf100_mem_v0 {
	/* nvif_mem_vX ... */
	__u8  version;
	__u8  contig;
};

struct gf100_mem_map_vn {
};

struct gf100_mem_map_v0 {
	__u8  version;
	__u8  ro;
	__u8  kind;
};
#endif
