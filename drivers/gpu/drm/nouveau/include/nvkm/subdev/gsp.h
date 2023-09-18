#ifndef __NVKM_GSP_H__
#define __NVKM_GSP_H__
#define nvkm_gsp(p) container_of((p), struct nvkm_gsp, subdev)
#include <core/subdev.h>
#include <core/falcon.h>
#include <core/firmware.h>

#define GSP_PAGE_SHIFT 12
#define GSP_PAGE_SIZE  BIT(GSP_PAGE_SHIFT)

struct nvkm_gsp_mem {
	u32 size;
	void *data;
	dma_addr_t addr;
};

struct nvkm_gsp_radix3 {
	struct nvkm_gsp_mem mem[3];
};

int nvkm_gsp_sg(struct nvkm_device *, u64 size, struct sg_table *);
void nvkm_gsp_sg_free(struct nvkm_device *, struct sg_table *);

typedef int (*nvkm_gsp_msg_ntfy_func)(void *priv, u32 fn, void *repv, u32 repc);

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
	} msgq;

	bool running;

	const struct nvkm_gsp_rm {
		void *(*rpc_get)(struct nvkm_gsp *, u32 fn, u32 argc);
		void *(*rpc_push)(struct nvkm_gsp *, void *argv, bool wait, u32 repc);
		void (*rpc_done)(struct nvkm_gsp *gsp, void *repv);
	} *rm;
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

int gv100_gsp_new(struct nvkm_device *, enum nvkm_subdev_type, int, struct nvkm_gsp **);
int tu102_gsp_new(struct nvkm_device *, enum nvkm_subdev_type, int, struct nvkm_gsp **);
int tu116_gsp_new(struct nvkm_device *, enum nvkm_subdev_type, int, struct nvkm_gsp **);
int ga100_gsp_new(struct nvkm_device *, enum nvkm_subdev_type, int, struct nvkm_gsp **);
int ga102_gsp_new(struct nvkm_device *, enum nvkm_subdev_type, int, struct nvkm_gsp **);
int ad102_gsp_new(struct nvkm_device *, enum nvkm_subdev_type, int, struct nvkm_gsp **);
#endif
