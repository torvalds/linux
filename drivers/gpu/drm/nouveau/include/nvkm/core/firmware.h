/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_FIRMWARE_H__
#define __NVKM_FIRMWARE_H__

#include <core/device.h>

int nvkm_firmware_get(struct nvkm_device *device, const char *fwname,
		      const struct firmware **fw);

void nvkm_firmware_put(const struct firmware *fw);

#endif
