/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020-2021 NXP
 */

#ifndef _AMPHION_VPU_H
#define _AMPHION_VPU_H

#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-mem2mem.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox_controller.h>
#include <linux/kfifo.h>

#define VPU_TIMEOUT_WAKEUP	msecs_to_jiffies(200)
#define VPU_TIMEOUT		msecs_to_jiffies(1000)
#define VPU_INST_NULL_ID	(-1L)
#define VPU_MSG_BUFFER_SIZE	(8192)

enum imx_plat_type {
	IMX8QXP = 0,
	IMX8QM  = 1,
	IMX8DM,
	IMX8DX,
	PLAT_TYPE_RESERVED
};

enum vpu_core_type {
	VPU_CORE_TYPE_ENC = 0,
	VPU_CORE_TYPE_DEC = 0x10,
};

struct vpu_dev;
struct vpu_resources {
	enum imx_plat_type plat_type;
	u32 mreg_base;
	int (*setup)(struct vpu_dev *vpu);
	int (*setup_encoder)(struct vpu_dev *vpu);
	int (*setup_decoder)(struct vpu_dev *vpu);
	int (*reset)(struct vpu_dev *vpu);
};

struct vpu_buffer {
	void *virt;
	dma_addr_t phys;
	u32 length;
	u32 bytesused;
	struct device *dev;
};

struct vpu_func {
	struct video_device *vfd;
	struct v4l2_m2m_dev *m2m_dev;
	enum vpu_core_type type;
	int function;
};

struct vpu_dev {
	void __iomem *base;
	struct platform_device *pdev;
	struct device *dev;
	struct mutex lock; /* protect vpu device */
	const struct vpu_resources *res;
	struct list_head cores;

	struct v4l2_device v4l2_dev;
	struct vpu_func encoder;
	struct vpu_func decoder;
	struct media_device mdev;

	struct delayed_work watchdog_work;
	void (*get_vpu)(struct vpu_dev *vpu);
	void (*put_vpu)(struct vpu_dev *vpu);
	void (*get_enc)(struct vpu_dev *vpu);
	void (*put_enc)(struct vpu_dev *vpu);
	void (*get_dec)(struct vpu_dev *vpu);
	void (*put_dec)(struct vpu_dev *vpu);
	atomic_t ref_vpu;
	atomic_t ref_enc;
	atomic_t ref_dec;

	struct dentry *debugfs;
};

struct vpu_format {
	u32 pixfmt;
	u32 mem_planes;
	u32 comp_planes;
	u32 type;
	u32 flags;
	u32 width;
	u32 height;
	u32 sizeimage[VIDEO_MAX_PLANES];
	u32 bytesperline[VIDEO_MAX_PLANES];
	u32 field;
	u32 sibling;
};

struct vpu_core_resources {
	enum vpu_core_type type;
	const char *fwname;
	u32 stride;
	u32 max_width;
	u32 min_width;
	u32 step_width;
	u32 max_height;
	u32 min_height;
	u32 step_height;
	u32 rpc_size;
	u32 fwlog_size;
	u32 act_size;
};

struct vpu_mbox {
	char name[20];
	struct mbox_client cl;
	struct mbox_chan *ch;
	bool block;
};

enum vpu_core_state {
	VPU_CORE_DEINIT = 0,
	VPU_CORE_ACTIVE,
	VPU_CORE_HANG
};

struct vpu_core {
	void __iomem *base;
	struct platform_device *pdev;
	struct device *dev;
	struct device *parent;
	struct device *pd;
	struct device_link *pd_link;
	struct mutex lock;     /* protect vpu core */
	struct mutex cmd_lock; /* Lock vpu command */
	struct list_head list;
	enum vpu_core_type type;
	int id;
	const struct vpu_core_resources *res;
	unsigned long instance_mask;
	u32 supported_instance_count;
	unsigned long hang_mask;
	u32 request_count;
	struct list_head instances;
	enum vpu_core_state state;
	u32 fw_version;

	struct vpu_buffer fw;
	struct vpu_buffer rpc;
	struct vpu_buffer log;
	struct vpu_buffer act;

	struct vpu_mbox tx_type;
	struct vpu_mbox tx_data;
	struct vpu_mbox rx;

	wait_queue_head_t ack_wq;
	struct completion cmp;
	struct workqueue_struct *workqueue;
	struct work_struct msg_work;
	struct delayed_work msg_delayed_work;
	struct kfifo msg_fifo;
	void *msg_buffer;

	struct vpu_dev *vpu;
	void *iface;

	struct dentry *debugfs;
	struct dentry *debugfs_fwlog;
};

enum vpu_codec_state {
	VPU_CODEC_STATE_DEINIT = 1,
	VPU_CODEC_STATE_CONFIGURED,
	VPU_CODEC_STATE_START,
	VPU_CODEC_STATE_STARTED,
	VPU_CODEC_STATE_ACTIVE,
	VPU_CODEC_STATE_SEEK,
	VPU_CODEC_STATE_STOP,
	VPU_CODEC_STATE_DRAIN,
	VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE,
};

struct vpu_frame_info {
	u32 type;
	u32 id;
	u32 sequence;
	u32 luma;
	u32 chroma_u;
	u32 chroma_v;
	u32 data_offset;
	u32 flags;
	u32 skipped;
	s64 timestamp;
};

struct vpu_inst;
struct vpu_inst_ops {
	int (*ctrl_init)(struct vpu_inst *inst);
	int (*start)(struct vpu_inst *inst, u32 type);
	int (*stop)(struct vpu_inst *inst, u32 type);
	int (*abort)(struct vpu_inst *inst);
	bool (*check_ready)(struct vpu_inst *inst, unsigned int type);
	void (*buf_done)(struct vpu_inst *inst, struct vpu_frame_info *frame);
	void (*event_notify)(struct vpu_inst *inst, u32 event, void *data);
	void (*release)(struct vpu_inst *inst);
	void (*cleanup)(struct vpu_inst *inst);
	void (*mem_request)(struct vpu_inst *inst,
			    u32 enc_frame_size,
			    u32 enc_frame_num,
			    u32 ref_frame_size,
			    u32 ref_frame_num,
			    u32 act_frame_size,
			    u32 act_frame_num);
	void (*input_done)(struct vpu_inst *inst);
	void (*stop_done)(struct vpu_inst *inst);
	int (*process_output)(struct vpu_inst *inst, struct vb2_buffer *vb);
	int (*process_capture)(struct vpu_inst *inst, struct vb2_buffer *vb);
	int (*get_one_frame)(struct vpu_inst *inst, void *info);
	void (*on_queue_empty)(struct vpu_inst *inst, u32 type);
	int (*get_debug_info)(struct vpu_inst *inst, char *str, u32 size, u32 i);
	void (*wait_prepare)(struct vpu_inst *inst);
	void (*wait_finish)(struct vpu_inst *inst);
	void (*attach_frame_store)(struct vpu_inst *inst, struct vb2_buffer *vb);
	void (*reset_frame_store)(struct vpu_inst *inst);
};

struct vpu_inst {
	struct list_head list;
	struct mutex lock; /* v4l2 and videobuf2 lock */
	struct vpu_dev *vpu;
	struct vpu_core *core;
	struct device *dev;
	int id;

	struct v4l2_fh fh;
	struct v4l2_ctrl_handler ctrl_handler;
	atomic_t ref_count;
	int (*release)(struct vpu_inst *inst);

	enum vpu_codec_state state;
	enum vpu_core_type type;

	struct workqueue_struct *workqueue;
	struct work_struct msg_work;
	struct kfifo msg_fifo;
	u8 msg_buffer[VPU_MSG_BUFFER_SIZE];

	struct vpu_buffer stream_buffer;
	bool use_stream_buffer;
	struct vpu_buffer act;

	struct list_head cmd_q;
	void *pending;
	unsigned long cmd_seq;
	atomic_long_t last_response_cmd;

	struct vpu_inst_ops *ops;
	const struct vpu_format *formats;
	struct vpu_format out_format;
	struct vpu_format cap_format;
	u32 min_buffer_cap;
	u32 min_buffer_out;
	u32 total_input_count;

	struct v4l2_rect crop;
	u32 colorspace;
	u8 ycbcr_enc;
	u8 quantization;
	u8 xfer_func;
	u32 sequence;
	u32 extra_size;

	u32 flows[16];
	u32 flow_idx;

	pid_t pid;
	pid_t tgid;
	struct dentry *debugfs;

	void *priv;
};

#define call_vop(inst, op, args...)					\
	((inst)->ops->op ? (inst)->ops->op(inst, ##args) : 0)		\

#define call_void_vop(inst, op, args...)				\
	do {								\
		if ((inst)->ops->op)					\
			(inst)->ops->op(inst, ##args);				\
	} while (0)

enum {
	VPU_BUF_STATE_IDLE = 0,
	VPU_BUF_STATE_INUSE,
	VPU_BUF_STATE_DECODED,
	VPU_BUF_STATE_READY,
	VPU_BUF_STATE_SKIP,
	VPU_BUF_STATE_ERROR,
	VPU_BUF_STATE_CHANGED
};

struct vpu_vb2_buffer {
	struct v4l2_m2m_buffer m2m_buf;
	dma_addr_t luma;
	dma_addr_t chroma_u;
	dma_addr_t chroma_v;
	unsigned int state;
	u32 average_qp;
	s32 fs_id;
};

void vpu_writel(struct vpu_dev *vpu, u32 reg, u32 val);
u32 vpu_readl(struct vpu_dev *vpu, u32 reg);

static inline struct vpu_vb2_buffer *to_vpu_vb2_buffer(struct vb2_v4l2_buffer *vbuf)
{
	struct v4l2_m2m_buffer *m2m_buf = container_of(vbuf, struct v4l2_m2m_buffer, vb);

	return container_of(m2m_buf, struct vpu_vb2_buffer, m2m_buf);
}

static inline const char *vpu_core_type_desc(enum vpu_core_type type)
{
	return type == VPU_CORE_TYPE_ENC ? "encoder" : "decoder";
}

static inline struct vpu_inst *to_inst(struct file *filp)
{
	return container_of(file_to_v4l2_fh(filp), struct vpu_inst, fh);
}

#define ctrl_to_inst(ctrl)	\
	container_of((ctrl)->handler, struct vpu_inst, ctrl_handler)

const struct v4l2_ioctl_ops *venc_get_ioctl_ops(void);
const struct v4l2_file_operations *venc_get_fops(void);
const struct v4l2_ioctl_ops *vdec_get_ioctl_ops(void);
const struct v4l2_file_operations *vdec_get_fops(void);

int vpu_add_func(struct vpu_dev *vpu, struct vpu_func *func);
void vpu_remove_func(struct vpu_func *func);

struct vpu_inst *vpu_inst_get(struct vpu_inst *inst);
void vpu_inst_put(struct vpu_inst *inst);
struct vpu_core *vpu_request_core(struct vpu_dev *vpu, enum vpu_core_type type);
void vpu_release_core(struct vpu_core *core);
int vpu_inst_register(struct vpu_inst *inst);
int vpu_inst_unregister(struct vpu_inst *inst);
const struct vpu_core_resources *vpu_get_resource(struct vpu_inst *inst);

int vpu_inst_create_dbgfs_file(struct vpu_inst *inst);
int vpu_inst_remove_dbgfs_file(struct vpu_inst *inst);
int vpu_core_create_dbgfs_file(struct vpu_core *core);
int vpu_core_remove_dbgfs_file(struct vpu_core *core);
void vpu_inst_record_flow(struct vpu_inst *inst, u32 flow);

int vpu_core_driver_init(void);
void vpu_core_driver_exit(void);

const char *vpu_id_name(u32 id);
const char *vpu_codec_state_name(enum vpu_codec_state state);

extern bool debug;
#define vpu_trace(dev, fmt, arg...)					\
	do {								\
		if (debug)						\
			dev_info(dev, "%s: " fmt, __func__, ## arg);	\
	} while (0)

#endif
