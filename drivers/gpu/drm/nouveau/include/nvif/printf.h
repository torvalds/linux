/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_PRINTF_H__
#define __NVIF_PRINTF_H__
#include <nvif/client.h>
#include <nvif/parent.h>

#define NVIF_PRINT(l,o,f,a...) do {                                                                \
	struct nvif_object *_o = (o);                                                              \
	struct nvif_parent *_p = _o->parent;                                                       \
	_p->func->l(_o, "[%s/%08x:%s] "f"\n", _o->client->object.name, _o->handle, _o->name, ##a); \
} while(0)

#ifndef NVIF_DEBUG_PRINT_DISABLE
#define NVIF_DEBUG(o,f,a...) NVIF_PRINT(debugf, (o), f, ##a)
#else
#define NVIF_DEBUG(o,f,a...)
#endif

#define NVIF_ERROR(o,f,a...) NVIF_PRINT(errorf, (o), f, ##a)
#define NVIF_ERRON(c,o,f,a...) do {                            \
	struct nvif_object *_object = (o);                     \
	int _cond = (c);                                       \
	if (_cond) {                                           \
		NVIF_ERROR(_object, f" (ret:%d)", ##a, _cond); \
	} else {                                               \
		NVIF_DEBUG(_object, f, ##a);                   \
	}                                                      \
} while(0)
#endif
