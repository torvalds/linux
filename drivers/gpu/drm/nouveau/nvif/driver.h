#ifndef __NVIF_DRIVER_H__
#define __NVIF_DRIVER_H__

struct nvif_driver {
	const char *name;
	int (*init)(const char *name, u64 device, const char *cfg,
		    const char *dbg, void **priv);
	void (*fini)(void *priv);
	int (*suspend)(void *priv);
	int (*resume)(void *priv);
	int (*ioctl)(void *priv, bool super, void *data, u32 size, void **hack);
	void *(*map)(void *priv, u64 handle, u32 size);
	void (*unmap)(void *priv, void *ptr, u32 size);
	bool keep;
};

extern const struct nvif_driver nvif_driver_nvkm;
extern const struct nvif_driver nvif_driver_drm;
extern const struct nvif_driver nvif_driver_lib;

#endif
