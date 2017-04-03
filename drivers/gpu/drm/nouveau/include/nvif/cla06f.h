#ifndef __NVIF_CLA06F_H__
#define __NVIF_CLA06F_H__

struct kepler_channel_gpfifo_a_v0 {
	__u8  version;
	__u8  pad01[5];
	__u16 chid;
#define NVA06F_V0_ENGINE_SW                                          0x00000001
#define NVA06F_V0_ENGINE_GR                                          0x00000002
#define NVA06F_V0_ENGINE_SEC                                         0x00000004
#define NVA06F_V0_ENGINE_MSVLD                                       0x00000010
#define NVA06F_V0_ENGINE_MSPDEC                                      0x00000020
#define NVA06F_V0_ENGINE_MSPPP                                       0x00000040
#define NVA06F_V0_ENGINE_MSENC                                       0x00000080
#define NVA06F_V0_ENGINE_VIC                                         0x00000100
#define NVA06F_V0_ENGINE_NVDEC                                       0x00000200
#define NVA06F_V0_ENGINE_NVENC0                                      0x00000400
#define NVA06F_V0_ENGINE_NVENC1                                      0x00000800
#define NVA06F_V0_ENGINE_CE0                                         0x00010000
#define NVA06F_V0_ENGINE_CE1                                         0x00020000
#define NVA06F_V0_ENGINE_CE2                                         0x00040000
	__u32 engines;
	__u32 ilength;
	__u64 ioffset;
	__u64 vm;
};

#define NVA06F_V0_NTFY_NON_STALL_INTERRUPT                                 0x00
#define NVA06F_V0_NTFY_KILLED                                              0x01
#endif
