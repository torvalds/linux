#ifndef __NOUVEAU_SUBDEV_DEVICE_H__
#define __NOUVEAU_SUBDEV_DEVICE_H__

#include <core/device.h>

#define nouveau_device_create(p,n,s,c,d,u)                                     \
	nouveau_device_create_((p), (n), (s), (c), (d), sizeof(**u), (void **)u)

int  nouveau_device_create_(struct pci_dev *, u64 name, const char *sname,
			    const char *cfg, const char *dbg, int, void **);

int nv04_identify(struct nouveau_device *);
int nv10_identify(struct nouveau_device *);
int nv20_identify(struct nouveau_device *);
int nv30_identify(struct nouveau_device *);
int nv40_identify(struct nouveau_device *);
int nv50_identify(struct nouveau_device *);
int nvc0_identify(struct nouveau_device *);
int nve0_identify(struct nouveau_device *);

struct nouveau_device *nouveau_device_find(u64 name);

#endif
