/* SPDX-License-Identifier: MIT */
#ifndef __NVBIOS_EXTDEV_H__
#define __NVBIOS_EXTDEV_H__
enum nvbios_extdev_type {
	NVBIOS_EXTDEV_LM89		= 0x02,
	NVBIOS_EXTDEV_VT1103M		= 0x40,
	NVBIOS_EXTDEV_PX3540		= 0x41,
	NVBIOS_EXTDEV_VT1105M		= 0x42, /* or close enough... */
	NVBIOS_EXTDEV_INA219		= 0x4c,
	NVBIOS_EXTDEV_INA209		= 0x4d,
	NVBIOS_EXTDEV_INA3221		= 0x4e,
	NVBIOS_EXTDEV_ADT7473		= 0x70, /* can also be a LM64 */
	NVBIOS_EXTDEV_HDCP_EEPROM	= 0x90,
	NVBIOS_EXTDEV_NONE		= 0xff,
};

struct nvbios_extdev_func {
	u8 type;
	u8 addr;
	u8 bus;
};

int
nvbios_extdev_parse(struct nvkm_bios *, int, struct nvbios_extdev_func *);

int
nvbios_extdev_find(struct nvkm_bios *, enum nvbios_extdev_type,
		   struct nvbios_extdev_func *);
#endif
