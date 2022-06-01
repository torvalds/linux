/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_OUTP_H__
#define __NVIF_OUTP_H__
#include <nvif/object.h>
struct nvif_disp;

struct nvif_outp {
	struct nvif_object object;
};

int nvif_outp_ctor(struct nvif_disp *, const char *name, int id, struct nvif_outp *);
void nvif_outp_dtor(struct nvif_outp *);
int nvif_outp_load_detect(struct nvif_outp *, u32 loadval);
#endif
