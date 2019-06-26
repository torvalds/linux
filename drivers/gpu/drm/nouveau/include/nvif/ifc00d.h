#ifndef __NVIF_IFC00D_H__
#define __NVIF_IFC00D_H__
#include "if000c.h"

struct gp100_vmm_vn {
	/* nvif_vmm_vX ... */
};

struct gp100_vmm_v0 {
	/* nvif_vmm_vX ... */
	__u8  version;
	__u8  fault_replay;
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

#define GP100_VMM_VN_FAULT_REPLAY                         NVIF_VMM_V0_MTHD(0x00)
#define GP100_VMM_VN_FAULT_CANCEL                         NVIF_VMM_V0_MTHD(0x01)

struct gp100_vmm_fault_replay_vn {
};

struct gp100_vmm_fault_cancel_v0 {
	__u8  version;
	__u8  hub;
	__u8  gpc;
	__u8  client;
	__u8  pad04[4];
	__u64 inst;
};
#endif
