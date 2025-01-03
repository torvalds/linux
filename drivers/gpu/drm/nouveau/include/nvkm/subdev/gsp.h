#ifndef __NVKM_GSP_H__
#define __NVKM_GSP_H__
#define nvkm_gsp(p) container_of((p), struct nvkm_gsp, subdev)
#include <core/subdev.h>
#include <core/falcon.h>
#include <core/firmware.h>

#define GSP_PAGE_SHIFT 12
#define GSP_PAGE_SIZE  BIT(GSP_PAGE_SHIFT)

struct nvkm_gsp_mem {
	size_t size;
	void *data;
	dma_addr_t addr;
};

struct nvkm_gsp_radix3 {
	struct nvkm_gsp_mem lvl0;
	struct nvkm_gsp_mem lvl1;
	struct sg_table lvl2;
};

int nvkm_gsp_sg(struct nvkm_device *, u64 size, struct sg_table *);
void nvkm_gsp_sg_free(struct nvkm_device *, struct sg_table *);

typedef int (*nvkm_gsp_msg_ntfy_func)(void *priv, u32 fn, void *repv, u32 repc);

struct nvkm_gsp_event;
typedef void (*nvkm_gsp_event_func)(struct nvkm_gsp_event *, void *repv, u32 repc);

struct nvkm_gsp {
	const struct nvkm_gsp_func *func;
	struct nvkm_subdev subdev;

	struct nvkm_falcon falcon;

	struct {
		struct {
			const struct firmware *load;
			const struct firmware *unload;
		} booter;
		const struct firmware *bl;
		const struct firmware *rm;
	} fws;

	struct nvkm_firmware fw;
	struct nvkm_gsp_mem sig;
	struct nvkm_gsp_radix3 radix3;

	struct {
		struct {
			struct {
				u64 addr;
				u64 size;
			} vga_workspace;
			u64 addr;
			u64 size;
		} bios;
		struct {
			struct {
				u64 addr;
				u64 size;
			} frts, boot, elf, heap;
			u64 addr;
			u64 size;
		} wpr2;
		struct {
			u64 addr;
			u64 size;
		} heap;
		u64 addr;
		u64 size;

		struct {
			u64 addr;
			u64 size;
		} region[16];
		int region_nr;
		u32 rsvd_size;
	} fb;

	struct {
		struct nvkm_falcon_fw load;
		struct nvkm_falcon_fw unload;
	} booter;

	struct {
		struct nvkm_gsp_mem fw;
		u32 code_offset;
		u32 data_offset;
		u32 manifest_offset;
		u32 app_version;
	} boot;

	struct nvkm_gsp_mem libos;
	struct nvkm_gsp_mem loginit;
	struct nvkm_gsp_mem logintr;
	struct nvkm_gsp_mem logrm;
	struct nvkm_gsp_mem rmargs;

	struct nvkm_gsp_mem wpr_meta;

	struct {
		struct sg_table sgt;
		struct nvkm_gsp_radix3 radix3;
		struct nvkm_gsp_mem meta;
	} sr;

	struct {
		struct nvkm_gsp_mem mem;

		struct {
			int   nr;
			u32 size;
			u64 *ptr;
		} ptes;

		struct {
			u32  size;
			void *ptr;
		} cmdq, msgq;
	} shm;

	struct nvkm_gsp_cmdq {
		struct mutex mutex;
		u32 cnt;
		u32 seq;
		u32 *wptr;
		u32 *rptr;
	} cmdq;

	struct nvkm_gsp_msgq {
		struct mutex mutex;
		u32 cnt;
		u32 *wptr;
		u32 *rptr;
		struct nvkm_gsp_msgq_ntfy {
			u32 fn;
			nvkm_gsp_msg_ntfy_func func;
			void *priv;
		} ntfy[16];
		int ntfy_nr;
		struct work_struct work;
	} msgq;

	bool running;

	/* Internal GSP-RM control handles. */
	struct {
		struct nvkm_gsp_client {
			struct nvkm_gsp_object {
				struct nvkm_gsp_client *client;
				struct nvkm_gsp_object *parent;
				u32 handle;
			} object;

			struct nvkm_gsp *gsp;

			struct list_head events;
		} client;

		struct nvkm_gsp_device {
			struct nvkm_gsp_object object;
			struct nvkm_gsp_object subdevice;
		} device;
	} internal;

	struct {
		enum nvkm_subdev_type type;
		int inst;
		u32 stall;
		u32 nonstall;
	} intr[32];
	int intr_nr;

	struct {
		u64 rm_bar1_pdb;
		u64 rm_bar2_pdb;
	} bar;

	struct {
		u8 gpcs;
		u8 tpcs;
	} gr;

	const struct nvkm_gsp_rm {
		void *(*rpc_get)(struct nvkm_gsp *, u32 fn, u32 argc);
		void *(*rpc_push)(struct nvkm_gsp *, void *argv, bool wait, u32 repc);
		void (*rpc_done)(struct nvkm_gsp *gsp, void *repv);

		void *(*rm_ctrl_get)(struct nvkm_gsp_object *, u32 cmd, u32 argc);
		int (*rm_ctrl_push)(struct nvkm_gsp_object *, void **argv, u32 repc);
		void (*rm_ctrl_done)(struct nvkm_gsp_object *, void *repv);

		void *(*rm_alloc_get)(struct nvkm_gsp_object *, u32 oclass, u32 argc);
		void *(*rm_alloc_push)(struct nvkm_gsp_object *, void *argv, u32 repc);
		void (*rm_alloc_done)(struct nvkm_gsp_object *, void *repv);

		int (*rm_free)(struct nvkm_gsp_object *);

		int (*client_ctor)(struct nvkm_gsp *, struct nvkm_gsp_client *);
		void (*client_dtor)(struct nvkm_gsp_client *);

		int (*device_ctor)(struct nvkm_gsp_client *, struct nvkm_gsp_device *);
		void (*device_dtor)(struct nvkm_gsp_device *);

		int (*event_ctor)(struct nvkm_gsp_device *, u32 handle, u32 id,
				  nvkm_gsp_event_func, struct nvkm_gsp_event *);
		void (*event_dtor)(struct nvkm_gsp_event *);
	} *rm;

	struct {
		struct mutex mutex;
		struct idr idr;
	} client_id;

	/* A linked list of registry items. The registry RPC will be built from it. */
	struct list_head registry_list;

	/* The size of the registry RPC */
	size_t registry_rpc_size;
};

static inline bool
nvkm_gsp_rm(struct nvkm_gsp *gsp)
{
	return gsp && (gsp->fws.rm || gsp->fw.img);
}

static inline void *
nvkm_gsp_rpc_get(struct nvkm_gsp *gsp, u32 fn, u32 argc)
{
	return gsp->rm->rpc_get(gsp, fn, argc);
}

static inline void *
nvkm_gsp_rpc_push(struct nvkm_gsp *gsp, void *argv, bool wait, u32 repc)
{
	return gsp->rm->rpc_push(gsp, argv, wait, repc);
}

static inline void *
nvkm_gsp_rpc_rd(struct nvkm_gsp *gsp, u32 fn, u32 argc)
{
	void *argv = nvkm_gsp_rpc_get(gsp, fn, argc);

	if (IS_ERR_OR_NULL(argv))
		return argv;

	return nvkm_gsp_rpc_push(gsp, argv, true, argc);
}

static inline int
nvkm_gsp_rpc_wr(struct nvkm_gsp *gsp, void *argv, bool wait)
{
	void *repv = nvkm_gsp_rpc_push(gsp, argv, wait, 0);

	if (IS_ERR(repv))
		return PTR_ERR(repv);

	return 0;
}

static inline void
nvkm_gsp_rpc_done(struct nvkm_gsp *gsp, void *repv)
{
	gsp->rm->rpc_done(gsp, repv);
}

static inline void *
nvkm_gsp_rm_ctrl_get(struct nvkm_gsp_object *object, u32 cmd, u32 argc)
{
	return object->client->gsp->rm->rm_ctrl_get(object, cmd, argc);
}

static inline int
nvkm_gsp_rm_ctrl_push(struct nvkm_gsp_object *object, void *argv, u32 repc)
{
	return object->client->gsp->rm->rm_ctrl_push(object, argv, repc);
}

static inline void *
nvkm_gsp_rm_ctrl_rd(struct nvkm_gsp_object *object, u32 cmd, u32 repc)
{
	void *argv = nvkm_gsp_rm_ctrl_get(object, cmd, repc);
	int ret;

	if (IS_ERR(argv))
		return argv;

	ret = nvkm_gsp_rm_ctrl_push(object, &argv, repc);
	if (ret)
		return ERR_PTR(ret);
	return argv;
}

static inline int
nvkm_gsp_rm_ctrl_wr(struct nvkm_gsp_object *object, void *argv)
{
	int ret = nvkm_gsp_rm_ctrl_push(object, &argv, 0);

	if (ret)
		return ret;
	return 0;
}

static inline void
nvkm_gsp_rm_ctrl_done(struct nvkm_gsp_object *object, void *repv)
{
	object->client->gsp->rm->rm_ctrl_done(object, repv);
}

static inline void *
nvkm_gsp_rm_alloc_get(struct nvkm_gsp_object *parent, u32 handle, u32 oclass, u32 argc,
		      struct nvkm_gsp_object *object)
{
	struct nvkm_gsp_client *client = parent->client;
	struct nvkm_gsp *gsp = client->gsp;
	void *argv;

	object->client = parent->client;
	object->parent = parent;
	object->handle = handle;

	argv = gsp->rm->rm_alloc_get(object, oclass, argc);
	if (IS_ERR_OR_NULL(argv)) {
		object->client = NULL;
		return argv;
	}

	return argv;
}

static inline void *
nvkm_gsp_rm_alloc_push(struct nvkm_gsp_object *object, void *argv, u32 repc)
{
	void *repv = object->client->gsp->rm->rm_alloc_push(object, argv, repc);

	if (IS_ERR(repv))
		object->client = NULL;

	return repv;
}

static inline int
nvkm_gsp_rm_alloc_wr(struct nvkm_gsp_object *object, void *argv)
{
	void *repv = nvkm_gsp_rm_alloc_push(object, argv, 0);

	if (IS_ERR(repv))
		return PTR_ERR(repv);

	return 0;
}

static inline void
nvkm_gsp_rm_alloc_done(struct nvkm_gsp_object *object, void *repv)
{
	object->client->gsp->rm->rm_alloc_done(object, repv);
}

static inline int
nvkm_gsp_rm_alloc(struct nvkm_gsp_object *parent, u32 handle, u32 oclass, u32 argc,
		  struct nvkm_gsp_object *object)
{
	void *argv = nvkm_gsp_rm_alloc_get(parent, handle, oclass, argc, object);

	if (IS_ERR_OR_NULL(argv))
		return argv ? PTR_ERR(argv) : -EIO;

	return nvkm_gsp_rm_alloc_wr(object, argv);
}

static inline int
nvkm_gsp_rm_free(struct nvkm_gsp_object *object)
{
	if (object->client)
		return object->client->gsp->rm->rm_free(object);

	return 0;
}

static inline int
nvkm_gsp_client_ctor(struct nvkm_gsp *gsp, struct nvkm_gsp_client *client)
{
	if (WARN_ON(!gsp->rm))
		return -ENOSYS;

	return gsp->rm->client_ctor(gsp, client);
}

static inline void
nvkm_gsp_client_dtor(struct nvkm_gsp_client *client)
{
	if (client->gsp)
		client->gsp->rm->client_dtor(client);
}

static inline int
nvkm_gsp_device_ctor(struct nvkm_gsp_client *client, struct nvkm_gsp_device *device)
{
	return client->gsp->rm->device_ctor(client, device);
}

static inline void
nvkm_gsp_device_dtor(struct nvkm_gsp_device *device)
{
	if (device->object.client)
		device->object.client->gsp->rm->device_dtor(device);
}

static inline int
nvkm_gsp_client_device_ctor(struct nvkm_gsp *gsp,
			    struct nvkm_gsp_client *client, struct nvkm_gsp_device *device)
{
	int ret = nvkm_gsp_client_ctor(gsp, client);

	if (ret == 0) {
		ret = nvkm_gsp_device_ctor(client, device);
		if (ret)
			nvkm_gsp_client_dtor(client);
	}

	return ret;
}

struct nvkm_gsp_event {
	struct nvkm_gsp_device *device;
	u32 id;
	nvkm_gsp_event_func func;

	struct nvkm_gsp_object object;

	struct list_head head;
};

static inline int
nvkm_gsp_device_event_ctor(struct nvkm_gsp_device *device, u32 handle, u32 id,
			   nvkm_gsp_event_func func, struct nvkm_gsp_event *event)
{
	return device->object.client->gsp->rm->event_ctor(device, handle, id, func, event);
}

static inline void
nvkm_gsp_event_dtor(struct nvkm_gsp_event *event)
{
	struct nvkm_gsp_device *device = event->device;

	if (device)
		device->object.client->gsp->rm->event_dtor(event);
}

int nvkm_gsp_intr_stall(struct nvkm_gsp *, enum nvkm_subdev_type, int);
int nvkm_gsp_intr_nonstall(struct nvkm_gsp *, enum nvkm_subdev_type, int);

int gv100_gsp_new(struct nvkm_device *, enum nvkm_subdev_type, int, struct nvkm_gsp **);
int tu102_gsp_new(struct nvkm_device *, enum nvkm_subdev_type, int, struct nvkm_gsp **);
int tu116_gsp_new(struct nvkm_device *, enum nvkm_subdev_type, int, struct nvkm_gsp **);
int ga100_gsp_new(struct nvkm_device *, enum nvkm_subdev_type, int, struct nvkm_gsp **);
int ga102_gsp_new(struct nvkm_device *, enum nvkm_subdev_type, int, struct nvkm_gsp **);
int ad102_gsp_new(struct nvkm_device *, enum nvkm_subdev_type, int, struct nvkm_gsp **);
#endif
