#ifndef __NVKM_DEVICE_ACPI_H__
#define __NVKM_DEVICE_ACPI_H__
#include <core/os.h>
struct nvkm_device;

int nvkm_acpi_init(struct nvkm_device *);
int nvkm_acpi_fini(struct nvkm_device *, bool);
#endif
