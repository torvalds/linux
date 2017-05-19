#ifndef __NVBIOS_INIT_H__
#define __NVBIOS_INIT_H__

struct nvbios_init {
	struct nvkm_subdev *subdev;
	struct nvkm_bios *bios;
	u32 offset;

	struct dcb_output *outp;
	int or;
	int link;
	union {
		int head;
		int crtc;
	};

	/* internal state used during parsing */
	u8 execute;
	u32 nested;
	u32 repeat;
	u32 repend;
	u32 ramcfg;
};

int nvbios_exec(struct nvbios_init *);
int nvbios_post(struct nvkm_subdev *, bool execute);
#endif
