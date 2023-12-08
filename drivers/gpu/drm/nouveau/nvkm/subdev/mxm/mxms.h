/* SPDX-License-Identifier: MIT */
#ifndef __NVMXM_MXMS_H__
#define __NVMXM_MXMS_H__
#include "priv.h"

struct mxms_odev {
	u8 outp_type;
	u8 conn_type;
	u8 ddc_port;
	u8 dig_conn;
};

void mxms_output_device(struct nvkm_mxm *, u8 *, struct mxms_odev *);

u16  mxms_version(struct nvkm_mxm *);
u16  mxms_headerlen(struct nvkm_mxm *);
u16  mxms_structlen(struct nvkm_mxm *);
bool mxms_checksum(struct nvkm_mxm *);
bool mxms_valid(struct nvkm_mxm *);

bool mxms_foreach(struct nvkm_mxm *, u8,
		  bool (*)(struct nvkm_mxm *, u8 *, void *), void *);
#endif
