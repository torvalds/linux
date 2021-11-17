#ifndef __NVIF_IFB00D_H__
#define __NVIF_IFB00D_H__
#include "if000c.h"

struct gm200_vmm_vn {
	/* nvif_vmm_vX ... */
};

struct gm200_vmm_v0 {
	/* nvif_vmm_vX ... */
	__u8  version;
	__u8  bigpage;
};

struct gm200_vmm_map_vn {
	/* nvif_vmm_map_vX ... */
};

struct gm200_vmm_map_v0 {
	/* nvif_vmm_map_vX ... */
	__u8  version;
	__u8  vol;
	__u8  ro;
	__u8  priv;
	__u8  kind;
};
#endif
