#ifndef __NVKM_FALCON_H__
#define __NVKM_FALCON_H__
#include <core/firmware.h>
#include <engine/falcon.h>

enum nvkm_falcon_mem {
	IMEM,
	DMEM,
	EMEM,
};

static inline const char *
nvkm_falcon_mem(enum nvkm_falcon_mem mem)
{
	switch (mem) {
	case IMEM: return "imem";
	case DMEM: return "dmem";
	case EMEM: return "emem";
	default:
		WARN_ON(1);
		return "?mem";
	}
}

struct nvkm_falcon_func_pio {
	int min;
	int max;
	void (*wr_init)(struct nvkm_falcon *, u8 port, bool sec, u32 mem_base);
	void (*wr)(struct nvkm_falcon *, u8 port, const u8 *img, int len, u16 tag);
	void (*rd_init)(struct nvkm_falcon *, u8 port, u32 mem_base);
	void (*rd)(struct nvkm_falcon *, u8 port, const u8 *img, int len);
};

struct nvkm_falcon_func_dma {
	int (*init)(struct nvkm_falcon *, u64 dma_addr, int xfer_len,
		    enum nvkm_falcon_mem, bool sec, u32 *cmd);
	void (*xfer)(struct nvkm_falcon *, u32 mem_base, u32 dma_base, u32 cmd);
	bool (*done)(struct nvkm_falcon *);
};

int nvkm_falcon_ctor(const struct nvkm_falcon_func *, struct nvkm_subdev *owner,
		     const char *name, u32 addr, struct nvkm_falcon *);
void nvkm_falcon_dtor(struct nvkm_falcon *);
int nvkm_falcon_reset(struct nvkm_falcon *);
int nvkm_falcon_pio_wr(struct nvkm_falcon *, const u8 *img, u32 img_base, u8 port,
		       enum nvkm_falcon_mem mem_type, u32 mem_base, int len, u16 tag, bool sec);
int nvkm_falcon_pio_rd(struct nvkm_falcon *, u8 port, enum nvkm_falcon_mem type, u32 mem_base,
		       const u8 *img, u32 img_base, int len);
int nvkm_falcon_dma_wr(struct nvkm_falcon *, const u8 *img, u64 dma_addr, u32 dma_base,
		       enum nvkm_falcon_mem mem_type, u32 mem_base, int len, bool sec);

int gm200_flcn_reset_wait_mem_scrubbing(struct nvkm_falcon *);
int gm200_flcn_disable(struct nvkm_falcon *);
int gm200_flcn_enable(struct nvkm_falcon *);
void gm200_flcn_bind_inst(struct nvkm_falcon *, int, u64);
int gm200_flcn_bind_stat(struct nvkm_falcon *, bool);
extern const struct nvkm_falcon_func_pio gm200_flcn_imem_pio;
extern const struct nvkm_falcon_func_pio gm200_flcn_dmem_pio;
void gm200_flcn_tracepc(struct nvkm_falcon *);

int gp102_flcn_reset_eng(struct nvkm_falcon *);
extern const struct nvkm_falcon_func_pio gp102_flcn_emem_pio;

int ga102_flcn_select(struct nvkm_falcon *);
int ga102_flcn_reset_prep(struct nvkm_falcon *);
int ga102_flcn_reset_wait_mem_scrubbing(struct nvkm_falcon *);
extern const struct nvkm_falcon_func_dma ga102_flcn_dma;

void nvkm_falcon_v1_load_imem(struct nvkm_falcon *,
			      void *, u32, u32, u16, u8, bool);
void nvkm_falcon_v1_load_dmem(struct nvkm_falcon *, void *, u32, u32, u8);
void nvkm_falcon_v1_start(struct nvkm_falcon *);

#define FLCN_PRINTK(f,l,p,fmt,a...) ({                                                          \
	if ((f)->owner->name != (f)->name)                                                      \
		nvkm_printk___((f)->owner, (f)->user, NV_DBG_##l, p, "%s:"fmt, (f)->name, ##a); \
	else                                                                                    \
		nvkm_printk___((f)->owner, (f)->user, NV_DBG_##l, p, fmt, ##a);                 \
})
#define FLCN_DBG(f,fmt,a...) FLCN_PRINTK((f), DEBUG, info, " "fmt"\n", ##a)
#define FLCN_ERR(f,fmt,a...) FLCN_PRINTK((f), ERROR, err, " "fmt"\n", ##a)
#define FLCN_ERRON(f,c,fmt,a...) \
	({ bool _cond = (c); _cond ? FLCN_ERR(f, fmt, ##a) : FLCN_DBG(f, fmt, ##a); _cond; })


struct nvkm_falcon_fw {
	const struct nvkm_falcon_fw_func {
		int (*signature)(struct nvkm_falcon_fw *, u32 *sig_base_src);
		int (*reset)(struct nvkm_falcon_fw *);
		int (*setup)(struct nvkm_falcon_fw *);
		int (*load)(struct nvkm_falcon_fw *);
		int (*load_bld)(struct nvkm_falcon_fw *);
		int (*boot)(struct nvkm_falcon_fw *,
			    u32 *mbox0, u32 *mbox1, u32 mbox0_ok, u32 irqsclr);
	} *func;
	struct nvkm_firmware fw;

	u32 sig_base_prd;
	u32 sig_base_dbg;
	u32 sig_base_img;
	u32 sig_size;
	int sig_nr;
	u8 *sigs;
	u32 fuse_ver;
	u32 engine_id;
	u32 ucode_id;

	u32 nmem_base_img;
	u32 nmem_base;
	u32 nmem_size;

	u32 imem_base_img;
	u32 imem_base;
	u32 imem_size;

	u32 dmem_base_img;
	u32 dmem_base;
	u32 dmem_size;
	u32 dmem_sign;

	u8 *boot;
	u32 boot_size;
	u32 boot_addr;

	struct nvkm_falcon *falcon;
	struct nvkm_memory *inst;
	struct nvkm_vmm *vmm;
	struct nvkm_vma *vma;
};

int nvkm_falcon_fw_ctor(const struct nvkm_falcon_fw_func *, const char *name, struct nvkm_device *,
		        bool bl, const void *src, u32 len, struct nvkm_falcon *,
			struct nvkm_falcon_fw *);
int nvkm_falcon_fw_ctor_hs(const struct nvkm_falcon_fw_func *, const char *name,
			   struct nvkm_subdev *, const char *bl, const char *img, int ver,
			   struct nvkm_falcon *falcon, struct nvkm_falcon_fw *fw);
int nvkm_falcon_fw_ctor_hs_v2(const struct nvkm_falcon_fw_func *, const char *name,
			      struct nvkm_subdev *, const char *img, int ver, struct nvkm_falcon *,
			      struct nvkm_falcon_fw *);
int nvkm_falcon_fw_sign(struct nvkm_falcon_fw *, u32 sig_base_img, u32 sig_size, const u8 *sigs,
			int sig_nr_prd, u32 sig_base_prd, int sig_nr_dbg, u32 sig_base_dbg);
int nvkm_falcon_fw_patch(struct nvkm_falcon_fw *);
void nvkm_falcon_fw_dtor(struct nvkm_falcon_fw *);
int nvkm_falcon_fw_oneinit(struct nvkm_falcon_fw *, struct nvkm_falcon *, struct nvkm_vmm *,
			   struct nvkm_memory *inst);
int nvkm_falcon_fw_boot(struct nvkm_falcon_fw *, struct nvkm_subdev *user,
			bool release, u32 *pmbox0, u32 *pmbox1, u32 mbox0_ok, u32 irqsclr);

extern const struct nvkm_falcon_fw_func gm200_flcn_fw;
int gm200_flcn_fw_signature(struct nvkm_falcon_fw *, u32 *);
int gm200_flcn_fw_reset(struct nvkm_falcon_fw *);
int gm200_flcn_fw_load(struct nvkm_falcon_fw *);
int gm200_flcn_fw_boot(struct nvkm_falcon_fw *, u32 *, u32 *, u32, u32);

int ga100_flcn_fw_signature(struct nvkm_falcon_fw *, u32 *);

extern const struct nvkm_falcon_fw_func ga102_flcn_fw;
int ga102_flcn_fw_load(struct nvkm_falcon_fw *);
int ga102_flcn_fw_boot(struct nvkm_falcon_fw *, u32 *, u32 *, u32, u32);

#define FLCNFW_PRINTK(f,l,p,fmt,a...) FLCN_PRINTK((f)->falcon, l, p, "%s: "fmt, (f)->fw.name, ##a)
#define FLCNFW_DBG(f,fmt,a...) FLCNFW_PRINTK((f), DEBUG, info, fmt"\n", ##a)
#define FLCNFW_ERR(f,fmt,a...) FLCNFW_PRINTK((f), ERROR, err, fmt"\n", ##a)

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
bool nvkm_falcon_msgq_empty(struct nvkm_falcon_msgq *);
int nvkm_falcon_msgq_recv_initmsg(struct nvkm_falcon_msgq *, void *, u32 size);
void nvkm_falcon_msgq_recv(struct nvkm_falcon_msgq *);
#endif
