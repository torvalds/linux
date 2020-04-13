#ifndef __NVIF_USER_H__
#define __NVIF_USER_H__
#include <nvif/object.h>
struct nvif_device;

struct nvif_user {
	const struct nvif_user_func *func;
	struct nvif_object object;
};

struct nvif_user_func {
	void (*doorbell)(struct nvif_user *, u32 token);
	u64 (*time)(struct nvif_user *);
};

int nvif_user_init(struct nvif_device *);
void nvif_user_fini(struct nvif_device *);

extern const struct nvif_user_func nvif_userc361;
#endif
