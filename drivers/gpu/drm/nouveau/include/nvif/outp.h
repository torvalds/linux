/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_OUTP_H__
#define __NVIF_OUTP_H__
#include <nvif/object.h>
struct nvif_disp;

struct nvif_outp {
	struct nvif_object object;

	struct {
		int id;
		int link;
	} or;
};

int nvif_outp_ctor(struct nvif_disp *, const char *name, int id, struct nvif_outp *);
void nvif_outp_dtor(struct nvif_outp *);
int nvif_outp_load_detect(struct nvif_outp *, u32 loadval);
int nvif_outp_acquire_rgb_crt(struct nvif_outp *);
int nvif_outp_acquire_tmds(struct nvif_outp *, bool hda);
int nvif_outp_acquire_lvds(struct nvif_outp *);
int nvif_outp_acquire_dp(struct nvif_outp *, bool hda);
void nvif_outp_release(struct nvif_outp *);
#endif
