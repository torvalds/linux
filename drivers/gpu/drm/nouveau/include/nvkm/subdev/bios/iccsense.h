/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVBIOS_ICCSENSE_H__
#define __NVBIOS_ICCSENSE_H__
struct pwr_rail_resistor_t {
	u8 mohm;
	bool enabled;
};

struct pwr_rail_t {
	u8 mode;
	u8 extdev_id;
	u8 resistor_count;
	struct pwr_rail_resistor_t resistors[3];
	u16 config;
};

struct nvbios_iccsense {
	int nr_entry;
	struct pwr_rail_t *rail;
};

int nvbios_iccsense_parse(struct nvkm_bios *, struct nvbios_iccsense *);
#endif
