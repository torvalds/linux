#ifndef __NVIF_IF500B_H__
#define __NVIF_IF500B_H__
#include "if000a.h"

struct nv50_mem_vn {
	/* nvif_mem_vX ... */
};

struct nv50_mem_v0 {
	/* nvif_mem_vX ... */
	__u8  version;
	__u8  bankswz;
	__u8  contig;
};

struct nv50_mem_map_vn {
};

struct nv50_mem_map_v0 {
	__u8  version;
	__u8  ro;
	__u8  kind;
	__u8  comp;
};
#endif
