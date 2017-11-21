#ifndef __NVIF_CL906F_H__
#define __NVIF_CL906F_H__

struct fermi_channel_gpfifo_v0 {
	__u8  version;
	__u8  chid;
	__u8  pad02[2];
	__u32 ilength;
	__u64 ioffset;
	__u64 vmm;
};

#define NV906F_V0_NTFY_NON_STALL_INTERRUPT                                 0x00
#define NV906F_V0_NTFY_KILLED                                              0x01
#endif
