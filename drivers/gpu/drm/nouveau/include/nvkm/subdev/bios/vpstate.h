/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVBIOS_VPSTATE_H__
#define __NVBIOS_VPSTATE_H__
struct nvbios_vpstate_header {
	u32 offset;

	u8 version;
	u8 hlen;
	u8 ecount;
	u8 elen;
	u8 scount;
	u8 slen;

	u8 base_id;
	u8 boost_id;
	u8 tdp_id;
};
struct nvbios_vpstate_entry {
	u8  pstate;
	u16 clock_mhz;
};
int nvbios_vpstate_parse(struct nvkm_bios *, struct nvbios_vpstate_header *);
int nvbios_vpstate_entry(struct nvkm_bios *, struct nvbios_vpstate_header *,
			 u8 idx, struct nvbios_vpstate_entry *);
#endif
