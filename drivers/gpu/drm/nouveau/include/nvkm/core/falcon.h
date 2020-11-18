#ifndef __NVKM_FALCON_H__
#define __NVKM_FALCON_H__
#include <engine/falcon.h>

int nvkm_falcon_ctor(const struct nvkm_falcon_func *, struct nvkm_subdev *owner,
		     const char *name, u32 addr, struct nvkm_falcon *);
void nvkm_falcon_dtor(struct nvkm_falcon *);

void nvkm_falcon_v1_load_imem(struct nvkm_falcon *,
			      void *, u32, u32, u16, u8, bool);
void nvkm_falcon_v1_load_dmem(struct nvkm_falcon *, void *, u32, u32, u8);
void nvkm_falcon_v1_read_dmem(struct nvkm_falcon *, u32, u32, u8, void *);
void nvkm_falcon_v1_bind_context(struct nvkm_falcon *, struct nvkm_memory *);
int nvkm_falcon_v1_wait_for_halt(struct nvkm_falcon *, u32);
int nvkm_falcon_v1_clear_interrupt(struct nvkm_falcon *, u32);
void nvkm_falcon_v1_set_start_addr(struct nvkm_falcon *, u32 start_addr);
void nvkm_falcon_v1_start(struct nvkm_falcon *);
int nvkm_falcon_v1_enable(struct nvkm_falcon *);
void nvkm_falcon_v1_disable(struct nvkm_falcon *);

void gp102_sec2_flcn_bind_context(struct nvkm_falcon *, struct nvkm_memory *);
int gp102_sec2_flcn_enable(struct nvkm_falcon *);

#define FLCN_PRINTK(t,f,fmt,a...) do {                                         \
	if (nvkm_subdev_name[(f)->owner->index] != (f)->name)                  \
		nvkm_##t((f)->owner, "%s: "fmt"\n", (f)->name, ##a);           \
	else                                                                   \
		nvkm_##t((f)->owner, fmt"\n", ##a);                            \
} while(0)
#define FLCN_DBG(f,fmt,a...) FLCN_PRINTK(debug, (f), fmt, ##a)
#define FLCN_ERR(f,fmt,a...) FLCN_PRINTK(error, (f), fmt, ##a)

/**
 * struct nvfw_falcon_msg - header for all messages
 *
 * @unit_id:	id of firmware process that sent the message
 * @size:	total size of message
 * @ctrl_flags:	control flags
 * @seq_id:	used to match a message from its corresponding command
 */
struct nvfw_falcon_msg {
	u8 unit_id;
	u8 size;
	u8 ctrl_flags;
	u8 seq_id;
};

#define nvfw_falcon_cmd nvfw_falcon_msg
#define NV_FALCON_CMD_UNIT_ID_REWIND                                       0x00

struct nvkm_falcon_qmgr;
int nvkm_falcon_qmgr_new(struct nvkm_falcon *, struct nvkm_falcon_qmgr **);
void nvkm_falcon_qmgr_del(struct nvkm_falcon_qmgr **);

typedef int
(*nvkm_falcon_qmgr_callback)(void *priv, struct nvfw_falcon_msg *);

struct nvkm_falcon_cmdq;
int nvkm_falcon_cmdq_new(struct nvkm_falcon_qmgr *, const char *name,
			 struct nvkm_falcon_cmdq **);
void nvkm_falcon_cmdq_del(struct nvkm_falcon_cmdq **);
void nvkm_falcon_cmdq_init(struct nvkm_falcon_cmdq *,
			   u32 index, u32 offset, u32 size);
void nvkm_falcon_cmdq_fini(struct nvkm_falcon_cmdq *);
int nvkm_falcon_cmdq_send(struct nvkm_falcon_cmdq *, struct nvfw_falcon_cmd *,
			  nvkm_falcon_qmgr_callback, void *priv,
			  unsigned long timeout_jiffies);

struct nvkm_falcon_msgq;
int nvkm_falcon_msgq_new(struct nvkm_falcon_qmgr *, const char *name,
			 struct nvkm_falcon_msgq **);
void nvkm_falcon_msgq_del(struct nvkm_falcon_msgq **);
void nvkm_falcon_msgq_init(struct nvkm_falcon_msgq *,
			   u32 index, u32 offset, u32 size);
int nvkm_falcon_msgq_recv_initmsg(struct nvkm_falcon_msgq *, void *, u32 size);
void nvkm_falcon_msgq_recv(struct nvkm_falcon_msgq *);
#endif
