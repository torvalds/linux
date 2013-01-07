#ifndef __NOUVEAU_BIOS_H__
#define __NOUVEAU_BIOS_H__

#include <core/subdev.h>
#include <core/device.h>

struct nouveau_bios {
	struct nouveau_subdev base;
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

static inline struct nouveau_bios *
nouveau_bios(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_VBIOS];
}

u8  nvbios_checksum(const u8 *data, int size);
u16 nvbios_findstr(const u8 *data, int size, const char *str, int len);

extern struct nouveau_oclass nouveau_bios_oclass;

#endif
