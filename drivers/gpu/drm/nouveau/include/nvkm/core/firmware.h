/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_FIRMWARE_H__
#define __NVKM_FIRMWARE_H__
#include <core/memory.h>
#include <core/option.h>
#include <core/subdev.h>

struct nvkm_firmware {
	const struct nvkm_firmware_func {
		enum nvkm_firmware_type {
			NVKM_FIRMWARE_IMG_RAM,
			NVKM_FIRMWARE_IMG_DMA,
		} type;
	} *func;
	const char *name;
	struct nvkm_device *device;

	int len;
	u8 *img;
	u64 phys;

	struct nvkm_firmware_mem {
		struct nvkm_memory memory;
		struct scatterlist sgl;
	} mem;
};

int nvkm_firmware_ctor(const struct nvkm_firmware_func *, const char *name, struct nvkm_device *,
		       const void *ptr, int len, struct nvkm_firmware *);
void nvkm_firmware_dtor(struct nvkm_firmware *);

int nvkm_firmware_get(const struct nvkm_subdev *, const char *fwname, int ver,
		      const struct firmware **);
void nvkm_firmware_put(const struct firmware *);

int nvkm_firmware_load_blob(const struct nvkm_subdev *subdev, const char *path,
			    const char *name, int ver, struct nvkm_blob *);
int nvkm_firmware_load_name(const struct nvkm_subdev *subdev, const char *path,
			    const char *name, int ver,
			    const struct firmware **);

#define nvkm_firmware_load(s,l,o,p...) ({                                      \
	struct nvkm_subdev *_s = (s);                                          \
	const char *_opts = (o);                                               \
	char _option[32];                                                      \
	typeof(l[0]) *_list = (l), *_next, *_fwif = NULL;                      \
	int _ver, _fwv, _ret = 0;                                              \
                                                                               \
	snprintf(_option, sizeof(_option), "Nv%sFw", _opts);                   \
	_ver = nvkm_longopt(_s->device->cfgopt, _option, -2);                  \
	if (_ver >= -1) {                                                      \
		for (_next = _list; !_fwif && _next->load; _next++) {          \
			if (_next->version == _ver)                            \
				_fwif = _next;                                 \
		}                                                              \
		_ret = _fwif ? 0 : -EINVAL;                                    \
	}                                                                      \
                                                                               \
	if (_ret == 0) {                                                       \
		snprintf(_option, sizeof(_option), "Nv%sFwVer", _opts);        \
		_fwv = _fwif ? _fwif->version : -1;                            \
		_ver = nvkm_longopt(_s->device->cfgopt, _option, _fwv);        \
		for (_next = _fwif ? _fwif : _list; _next->load; _next++) {    \
			_fwv = (_ver >= 0) ? _ver : _next->version;            \
			_ret = _next->load(p, _fwv, _next);                    \
			if (_ret == 0 || _ver >= 0) {                          \
				_fwif = _next;                                 \
				break;                                         \
			}                                                      \
		}                                                              \
	}                                                                      \
                                                                               \
	if (_ret)                                                              \
		_fwif = ERR_PTR(_ret);                                         \
	_fwif;                                                                 \
})
#endif
