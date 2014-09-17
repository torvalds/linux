#ifndef __NVBIOS_IMAGE_H__
#define __NVBIOS_IMAGE_H__

struct nvbios_image {
	u32  base;
	u32  size;
	u8   type;
	bool last;
};

bool nvbios_image(struct nouveau_bios *, int, struct nvbios_image *);

#endif
