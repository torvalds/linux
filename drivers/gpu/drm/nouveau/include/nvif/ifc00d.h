#ifndef __NVIF_IFC00D_H__
#define __NVIF_IFC00D_H__
#include "if000c.h"

struct gp100_vmm_vn {
	/* nvif_vmm_vX ... */
};

struct gp100_vmm_map_vn {
	/* nvif_vmm_map_vX ... */
};

struct gp100_vmm_map_v0 {
	/* nvif_vmm_map_vX ... */
	__u8  version;
	__u8  vol;
	__u8  ro;
	__u8  priv;
	__u8  kind;
};
#endif
