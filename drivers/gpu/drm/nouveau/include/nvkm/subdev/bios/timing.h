/* SPDX-License-Identifier: MIT */
#ifndef __NVBIOS_TIMING_H__
#define __NVBIOS_TIMING_H__
#include <subdev/bios/ramcfg.h>

u32 nvbios_timingTe(struct nvkm_bios *,
		    u8 *ver, u8 *hdr, u8 *cnt, u8 *len, u8 *snr, u8 *ssz);
u32 nvbios_timingEe(struct nvkm_bios *, int idx,
		    u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u32 nvbios_timingEp(struct nvkm_bios *, int idx,
		    u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_ramcfg *);
#endif
