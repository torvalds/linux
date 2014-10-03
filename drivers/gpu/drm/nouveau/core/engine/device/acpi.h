#ifndef __NVKM_DEVICE_ACPI_H__
#define __NVKM_DEVICE_ACPI_H__

#include <engine/device.h>

int nvkm_acpi_init(struct nouveau_device *);
int nvkm_acpi_fini(struct nouveau_device *, bool);

#endif
