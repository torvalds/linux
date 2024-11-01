/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020-2021 NXP
 */

#ifndef _AMPHION_VPU_RPC_H
#define _AMPHION_VPU_RPC_H

#include <media/videobuf2-core.h>
#include "vpu_codec.h"

struct vpu_rpc_buffer_desc {
	u32 wptr;
	u32 rptr;
	u32 start;
	u32 end;
};

struct vpu_shared_addr {
	void *iface;
	struct vpu_rpc_buffer_desc *cmd_desc;
	void *cmd_mem_vir;
	struct vpu_rpc_buffer_desc *msg_desc;
	void *msg_mem_vir;

	unsigned long boot_addr;
	struct vpu_core *core;
	void *priv;
};

struct vpu_rpc_event_header {
	u32 index;
	u32 id;
	u32 num;
};

struct vpu_rpc_event {
	struct vpu_rpc_event_header hdr;
	u32 data[128];
};

struct vpu_iface_ops {
	bool (*check_codec)(enum vpu_core_type type);
	bool (*check_fmt)(enum vpu_core_type type, u32 pixelfmt);
	u32 (*get_data_size)(void);
	int (*check_memory_region)(dma_addr_t base, dma_addr_t addr, u32 size);
	int (*boot_core)(struct vpu_core *core);
	int (*shutdown_core)(struct vpu_core *core);
	int (*restore_core)(struct vpu_core *core);
	int (*get_power_state)(struct vpu_core *core);
	int (*on_firmware_loaded)(struct vpu_core *core);
	void (*init_rpc)(struct vpu_shared_addr *shared,
			 struct vpu_buffer *rpc, dma_addr_t boot_addr);
	void (*set_log_buf)(struct vpu_shared_addr *shared,
			    struct vpu_buffer *log);
	void (*set_system_cfg)(struct vpu_shared_addr *shared,
			       u32 regs_base, void __iomem *regs, u32 index);
	void (*set_stream_cfg)(struct vpu_shared_addr *shared, u32 index);
	u32 (*get_version)(struct vpu_shared_addr *shared);
	u32 (*get_max_instance_count)(struct vpu_shared_addr *shared);
	int (*get_stream_buffer_size)(struct vpu_shared_addr *shared);
	int (*send_cmd_buf)(struct vpu_shared_addr *shared,
			    struct vpu_rpc_event *cmd);
	int (*receive_msg_buf)(struct vpu_shared_addr *shared,
			       struct vpu_rpc_event *msg);
	int (*pack_cmd)(struct vpu_rpc_event *pkt, u32 index, u32 id, void *data);
	int (*convert_msg_id)(u32 msg_id);
	int (*unpack_msg_data)(struct vpu_rpc_event *pkt, void *data);
	int (*input_frame)(struct vpu_shared_addr *shared,
			   struct vpu_inst *inst, struct vb2_buffer *vb);
	int (*config_memory_resource)(struct vpu_shared_addr *shared,
				      u32 instance,
				      u32 type,
				      u32 index,
				      struct vpu_buffer *buf);
	int (*config_stream_buffer)(struct vpu_shared_addr *shared,
				    u32 instance,
				    struct vpu_buffer *buf);
	int (*update_stream_buffer)(struct vpu_shared_addr *shared,
				    u32 instance, u32 ptr, bool write);
	int (*get_stream_buffer_desc)(struct vpu_shared_addr *shared,
				      u32 instance,
				      struct vpu_rpc_buffer_desc *desc);
	int (*set_encode_params)(struct vpu_shared_addr *shared,
				 u32 instance,
				 struct vpu_encode_params *params,
				 u32 update);
	int (*set_decode_params)(struct vpu_shared_addr *shared,
				 u32 instance,
				 struct vpu_decode_params *params,
				 u32 update);
	int (*add_scode)(struct vpu_shared_addr *shared,
			 u32 instance,
			 struct vpu_buffer *stream_buffer,
			 u32 pixelformat,
			 u32 scode_type);
	int (*pre_send_cmd)(struct vpu_shared_addr *shared, u32 instance);
	int (*post_send_cmd)(struct vpu_shared_addr *shared, u32 instance);
	int (*init_instance)(struct vpu_shared_addr *shared, u32 instance);
};

enum {
	VPU_CORE_MEMORY_INVALID = 0,
	VPU_CORE_MEMORY_CACHED,
	VPU_CORE_MEMORY_UNCACHED
};

struct vpu_rpc_region_t {
	dma_addr_t start;
	dma_addr_t end;
	dma_addr_t type;
};

struct vpu_iface_ops *vpu_core_get_iface(struct vpu_core *core);
struct vpu_iface_ops *vpu_inst_get_iface(struct vpu_inst *inst);
int vpu_iface_check_memory_region(struct vpu_core *core, dma_addr_t addr, u32 size);

static inline bool vpu_iface_check_codec(struct vpu_core *core)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (ops && ops->check_codec)
		return ops->check_codec(core->type);

	return true;
}

static inline bool vpu_iface_check_format(struct vpu_inst *inst, u32 pixelfmt)
{
	struct vpu_iface_ops *ops = vpu_inst_get_iface(inst);

	if (ops && ops->check_fmt)
		return ops->check_fmt(inst->type, pixelfmt);

	return true;
}

static inline int vpu_iface_boot_core(struct vpu_core *core)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (ops && ops->boot_core)
		return ops->boot_core(core);
	return 0;
}

static inline int vpu_iface_get_power_state(struct vpu_core *core)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (ops && ops->get_power_state)
		return ops->get_power_state(core);
	return 1;
}

static inline int vpu_iface_shutdown_core(struct vpu_core *core)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (ops && ops->shutdown_core)
		return ops->shutdown_core(core);
	return 0;
}

static inline int vpu_iface_restore_core(struct vpu_core *core)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (ops && ops->restore_core)
		return ops->restore_core(core);
	return 0;
}

static inline int vpu_iface_on_firmware_loaded(struct vpu_core *core)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (ops && ops->on_firmware_loaded)
		return ops->on_firmware_loaded(core);

	return 0;
}

static inline u32 vpu_iface_get_data_size(struct vpu_core *core)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (!ops || !ops->get_data_size)
		return 0;

	return ops->get_data_size();
}

static inline int vpu_iface_init(struct vpu_core *core,
				 struct vpu_shared_addr *shared,
				 struct vpu_buffer *rpc,
				 dma_addr_t boot_addr)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (!ops || !ops->init_rpc)
		return -EINVAL;

	ops->init_rpc(shared, rpc, boot_addr);
	core->iface = shared;
	shared->core = core;
	if (rpc->bytesused > rpc->length)
		return -ENOSPC;
	return 0;
}

static inline int vpu_iface_set_log_buf(struct vpu_core *core,
					struct vpu_buffer *log)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (!ops)
		return -EINVAL;

	if (ops->set_log_buf)
		ops->set_log_buf(core->iface, log);

	return 0;
}

static inline int vpu_iface_config_system(struct vpu_core *core, u32 regs_base, void __iomem *regs)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (!ops)
		return -EINVAL;
	if (ops->set_system_cfg)
		ops->set_system_cfg(core->iface, regs_base, regs, core->id);

	return 0;
}

static inline int vpu_iface_get_stream_buffer_size(struct vpu_core *core)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (!ops || !ops->get_stream_buffer_size)
		return 0;

	return ops->get_stream_buffer_size(core->iface);
}

static inline int vpu_iface_config_stream(struct vpu_inst *inst)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(inst->core);

	if (!ops || inst->id < 0)
		return -EINVAL;
	if (ops->set_stream_cfg)
		ops->set_stream_cfg(inst->core->iface, inst->id);
	return 0;
}

static inline int vpu_iface_send_cmd(struct vpu_core *core, struct vpu_rpc_event *cmd)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (!ops || !ops->send_cmd_buf)
		return -EINVAL;

	return ops->send_cmd_buf(core->iface, cmd);
}

static inline int vpu_iface_receive_msg(struct vpu_core *core, struct vpu_rpc_event *msg)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (!ops || !ops->receive_msg_buf)
		return -EINVAL;

	return ops->receive_msg_buf(core->iface, msg);
}

static inline int vpu_iface_pack_cmd(struct vpu_core *core,
				     struct vpu_rpc_event *pkt,
				     u32 index, u32 id, void *data)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (!ops || !ops->pack_cmd)
		return -EINVAL;
	return ops->pack_cmd(pkt, index, id, data);
}

static inline int vpu_iface_convert_msg_id(struct vpu_core *core, u32 msg_id)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (!ops || !ops->convert_msg_id)
		return -EINVAL;

	return ops->convert_msg_id(msg_id);
}

static inline int vpu_iface_unpack_msg_data(struct vpu_core *core,
					    struct vpu_rpc_event *pkt, void *data)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (!ops || !ops->unpack_msg_data)
		return -EINVAL;

	return ops->unpack_msg_data(pkt, data);
}

static inline int vpu_iface_input_frame(struct vpu_inst *inst,
					struct vb2_buffer *vb)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(inst->core);
	int ret;

	if (!ops || !ops->input_frame)
		return -EINVAL;

	ret = ops->input_frame(inst->core->iface, inst, vb);
	if (ret < 0)
		return ret;
	inst->total_input_count++;
	return ret;
}

static inline int vpu_iface_config_memory_resource(struct vpu_inst *inst,
						   u32 type,
						   u32 index,
						   struct vpu_buffer *buf)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(inst->core);

	if (!ops || !ops->config_memory_resource || inst->id < 0)
		return -EINVAL;

	return ops->config_memory_resource(inst->core->iface,
					inst->id,
					type, index, buf);
}

static inline int vpu_iface_config_stream_buffer(struct vpu_inst *inst,
						 struct vpu_buffer *buf)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(inst->core);

	if (!ops || !ops->config_stream_buffer || inst->id < 0)
		return -EINVAL;

	if ((buf->phys % 4) || (buf->length % 4))
		return -EINVAL;
	if (buf->phys + buf->length > (u64)UINT_MAX)
		return -EINVAL;

	return ops->config_stream_buffer(inst->core->iface, inst->id, buf);
}

static inline int vpu_iface_update_stream_buffer(struct vpu_inst *inst,
						 u32 ptr, bool write)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(inst->core);

	if (!ops || !ops->update_stream_buffer || inst->id < 0)
		return -EINVAL;

	return ops->update_stream_buffer(inst->core->iface, inst->id, ptr, write);
}

static inline int vpu_iface_get_stream_buffer_desc(struct vpu_inst *inst,
						   struct vpu_rpc_buffer_desc *desc)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(inst->core);

	if (!ops || !ops->get_stream_buffer_desc || inst->id < 0)
		return -EINVAL;

	if (!desc)
		return 0;

	return ops->get_stream_buffer_desc(inst->core->iface, inst->id, desc);
}

static inline u32 vpu_iface_get_version(struct vpu_core *core)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (!ops || !ops->get_version)
		return 0;

	return ops->get_version(core->iface);
}

static inline u32 vpu_iface_get_max_instance_count(struct vpu_core *core)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (!ops || !ops->get_max_instance_count)
		return 0;

	return ops->get_max_instance_count(core->iface);
}

static inline int vpu_iface_set_encode_params(struct vpu_inst *inst,
					      struct vpu_encode_params *params, u32 update)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(inst->core);

	if (!ops || !ops->set_encode_params || inst->id < 0)
		return -EINVAL;

	return ops->set_encode_params(inst->core->iface, inst->id, params, update);
}

static inline int vpu_iface_set_decode_params(struct vpu_inst *inst,
					      struct vpu_decode_params *params, u32 update)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(inst->core);

	if (!ops || !ops->set_decode_params  || inst->id < 0)
		return -EINVAL;

	return ops->set_decode_params(inst->core->iface, inst->id, params, update);
}

static inline int vpu_iface_add_scode(struct vpu_inst *inst, u32 scode_type)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(inst->core);

	if (!ops || !ops->add_scode  || inst->id < 0)
		return -EINVAL;

	return ops->add_scode(inst->core->iface, inst->id,
				&inst->stream_buffer,
				inst->out_format.pixfmt,
				scode_type);
}

static inline int vpu_iface_pre_send_cmd(struct vpu_inst *inst)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(inst->core);

	if (ops && ops->pre_send_cmd && inst->id >= 0)
		return ops->pre_send_cmd(inst->core->iface, inst->id);
	return 0;
}

static inline int vpu_iface_post_send_cmd(struct vpu_inst *inst)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(inst->core);

	if (ops && ops->post_send_cmd && inst->id >= 0)
		return ops->post_send_cmd(inst->core->iface, inst->id);
	return 0;
}

static inline int vpu_iface_init_instance(struct vpu_inst *inst)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(inst->core);

	if (ops && ops->init_instance && inst->id >= 0)
		return ops->init_instance(inst->core->iface, inst->id);

	return 0;
}

#endif
