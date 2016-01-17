#ifndef __NVIF_CLA06F_H__
#define __NVIF_CLA06F_H__

struct kepler_channel_gpfifo_a_v0 {
	__u8  version;
#define KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_GR                               0x01
#define KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_MSPDEC                           0x02
#define KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_MSPPP                            0x04
#define KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_MSVLD                            0x08
#define KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_CE0                              0x10
#define KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_CE1                              0x20
#define KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_ENC                              0x40
	__u8  engine;
	__u16 chid;
	__u32 ilength;
	__u64 ioffset;
	__u64 vm;
};

#define KEPLER_CHANNEL_GPFIFO_A_V0_NTFY_UEVENT                             0x00
#endif
