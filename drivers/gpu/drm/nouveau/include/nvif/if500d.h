#ifndef __NVIF_IF500D_H__
#define __NVIF_IF500D_H__
#include "if000c.h"

struct nv50_vmm_vn {
	/* nvif_vmm_vX ... */
};

struct nv50_vmm_map_vn {
	/* nvif_vmm_map_vX ... */
};

struct nv50_vmm_map_v0 {
	/* nvif_vmm_map_vX ... */
	__u8  version;
	__u8  ro;
	__u8  priv;
	__u8  kind;
	__u8  comp;
};
#endif
