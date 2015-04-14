#ifndef __NVKM_BIOS_H__
#define __NVKM_BIOS_H__
#include <core/subdev.h>

struct nvkm_bios {
	struct nvkm_subdev base;
	u32 size;
	u8 *data;

	u32 bmp_offset;
	u32 bit_offset;

	struct {
		u8 major;
		u8 chip;
		u8 minor;
		u8 micro;
		u8 patch;
	} version;
};

static inline struct nvkm_bios *
nvkm_bios(void *obj)
{
	return (void *)nvkm_subdev(obj, NVDEV_SUBDEV_VBIOS);
}

u8  nvbios_checksum(const u8 *data, int size);
u16 nvbios_findstr(const u8 *data, int size, const char *str, int len);

extern struct nvkm_oclass nvkm_bios_oclass;
#endif
