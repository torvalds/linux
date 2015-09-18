#ifndef __NVKM_BIOS_H__
#define __NVKM_BIOS_H__
#include <core/subdev.h>

struct nvkm_bios {
	struct nvkm_subdev subdev;
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

u8  nvbios_checksum(const u8 *data, int size);
u16 nvbios_findstr(const u8 *data, int size, const char *str, int len);
int nvbios_memcmp(struct nvkm_bios *, u32 addr, const char *, u32 len);

#define nvbios_rd08(b,o) (b)->data[(o)]
#define nvbios_rd16(b,o) get_unaligned_le16(&(b)->data[(o)])
#define nvbios_rd32(b,o) get_unaligned_le32(&(b)->data[(o)])

int nvkm_bios_new(struct nvkm_device *, int, struct nvkm_bios **);
#endif
