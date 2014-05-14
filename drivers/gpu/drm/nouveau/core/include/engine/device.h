#ifndef __NOUVEAU_SUBDEV_DEVICE_H__
#define __NOUVEAU_SUBDEV_DEVICE_H__

#include <core/device.h>

struct platform_device;

enum nv_bus_type {
	NOUVEAU_BUS_PCI,
	NOUVEAU_BUS_PLATFORM,
};

#define nouveau_device_create(p,t,n,s,c,d,u)                                   \
	nouveau_device_create_((void *)(p), (t), (n), (s), (c), (d),           \
			       sizeof(**u), (void **)u)

int  nouveau_device_create_(void *, enum nv_bus_type type, u64 name,
			    const char *sname, const char *cfg, const char *dbg,
			    int, void **);

int nv04_identify(struct nouveau_device *);
int nv10_identify(struct nouveau_device *);
int nv20_identify(struct nouveau_device *);
int nv30_identify(struct nouveau_device *);
int nv40_identify(struct nouveau_device *);
int nv50_identify(struct nouveau_device *);
int nvc0_identify(struct nouveau_device *);
int nve0_identify(struct nouveau_device *);
int gm100_identify(struct nouveau_device *);

struct nouveau_device *nouveau_device_find(u64 name);

#endif
