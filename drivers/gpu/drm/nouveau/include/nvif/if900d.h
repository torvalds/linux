#ifndef __NVIF_IF900D_H__
#define __NVIF_IF900D_H__
#include "if000c.h"

struct gf100_vmm_vn {
	/* nvif_vmm_vX ... */
};

struct gf100_vmm_map_vn {
	/* nvif_vmm_map_vX ... */
};

struct gf100_vmm_map_v0 {
	/* nvif_vmm_map_vX ... */
	__u8  version;
	__u8  vol;
	__u8  ro;
	__u8  priv;
	__u8  kind;
};
#endif
