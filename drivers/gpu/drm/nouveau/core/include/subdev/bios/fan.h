#ifndef __NVBIOS_FAN_H__
#define __NVBIOS_FAN_H__

#include <subdev/bios/therm.h>

u16 nvbios_fan_parse(struct nouveau_bios *bios, struct nvbios_therm_fan *fan);

#endif
