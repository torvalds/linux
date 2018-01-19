/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVBIOS_POWER_BUDGET_H__
#define __NVBIOS_POWER_BUDGET_H__

#include <nvkm/subdev/bios.h>

struct nvbios_power_budget_entry {
	u32 min_w;
	u32 avg_w;
	u32 max_w;
};

struct nvbios_power_budget {
	u32 offset;
	u8  ver;
	u8  hlen;
	u8  elen;
	u8  ecount;
	u8  cap_entry;
};

int nvbios_power_budget_header(struct nvkm_bios *,
                               struct nvbios_power_budget *);
int nvbios_power_budget_entry(struct nvkm_bios *, struct nvbios_power_budget *,
                              u8 idx, struct nvbios_power_budget_entry *);

#endif
