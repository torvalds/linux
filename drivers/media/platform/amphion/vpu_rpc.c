// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020-2021 NXP
 */

#include <linux/init.h>
#include <linux/interconnect.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/firmware/imx/ipc.h>
#include <linux/firmware/imx/svc/misc.h>
#include "vpu.h"
#include "vpu_rpc.h"
#include "vpu_imx8q.h"
#include "vpu_windsor.h"
#include "vpu_malone.h"

int vpu_iface_check_memory_region(struct vpu_core *core, dma_addr_t addr, u32 size)
{
	struct vpu_iface_ops *ops = vpu_core_get_iface(core);

	if (!ops || !ops->check_memory_region)
		return VPU_CORE_MEMORY_INVALID;

	return ops->check_memory_region(core->fw.phys, addr, size);
}

static u32 vpu_rpc_check_buffer_space(struct vpu_rpc_buffer_desc *desc, bool write)
{
	u32 ptr1;
	u32 ptr2;
	u32 size;

	size = desc->end - desc->start;
	if (write) {
		ptr1 = desc->wptr;
		ptr2 = desc->rptr;
	} else {
		ptr1 = desc->rptr;
		ptr2 = desc->wptr;
	}

	if (ptr1 == ptr2) {
		if (!write)
			return 0;
		else
			return size;
	}

	return (ptr2 + size - ptr1) % size;
}

static int vpu_rpc_send_cmd_buf(struct vpu_shared_addr *shared, struct vpu_rpc_event *cmd)
{
	struct vpu_rpc_buffer_desc *desc;
	u32 space = 0;
	u32 *data;
	u32 wptr;
	u32 i;

	if (cmd->hdr.num > 0xff || cmd->hdr.num >= ARRAY_SIZE(cmd->data))
		return -EINVAL;
	desc = shared->cmd_desc;
	space = vpu_rpc_check_buffer_space(desc, true);
	if (space < (((cmd->hdr.num + 1) << 2) + 16))
		return -EINVAL;
	wptr = desc->wptr;
	data = (u32 *)(shared->cmd_mem_vir + desc->wptr - desc->start);
	*data = 0;
	*data |= ((cmd->hdr.index & 0xff) << 24);
	*data |= ((cmd->hdr.num & 0xff) << 16);
	*data |= (cmd->hdr.id & 0x3fff);
	wptr += 4;
	data++;
	if (wptr >= desc->end) {
		wptr = desc->start;
		data = shared->cmd_mem_vir;
	}

	for (i = 0; i < cmd->hdr.num; i++) {
		*data = cmd->data[i];
		wptr += 4;
		data++;
		if (wptr >= desc->end) {
			wptr = desc->start;
			data = shared->cmd_mem_vir;
		}
	}

	/*update wptr after data is written*/
	mb();
	desc->wptr = wptr;

	return 0;
}

static bool vpu_rpc_check_msg(struct vpu_shared_addr *shared)
{
	struct vpu_rpc_buffer_desc *desc;
	u32 space = 0;
	u32 msgword;
	u32 msgnum;

	desc = shared->msg_desc;
	space = vpu_rpc_check_buffer_space(desc, 0);
	space = (space >> 2);

	if (space) {
		msgword = *(u32 *)(shared->msg_mem_vir + desc->rptr - desc->start);
		msgnum = (msgword & 0xff0000) >> 16;
		if (msgnum <= space)
			return true;
	}

	return false;
}

static int vpu_rpc_receive_msg_buf(struct vpu_shared_addr *shared, struct vpu_rpc_event *msg)
{
	struct vpu_rpc_buffer_desc *desc;
	u32 *data;
	u32 msgword;
	u32 rptr;
	u32 i;

	if (!vpu_rpc_check_msg(shared))
		return -EINVAL;

	desc = shared->msg_desc;
	data = (u32 *)(shared->msg_mem_vir + desc->rptr - desc->start);
	rptr = desc->rptr;
	msgword = *data;
	data++;
	rptr += 4;
	if (rptr >= desc->end) {
		rptr = desc->start;
		data = shared->msg_mem_vir;
	}

	msg->hdr.index = (msgword >> 24) & 0xff;
	msg->hdr.num = (msgword >> 16) & 0xff;
	msg->hdr.id = msgword & 0x3fff;

	if (msg->hdr.num > ARRAY_SIZE(msg->data))
		return -EINVAL;

	for (i = 0; i < msg->hdr.num; i++) {
		msg->data[i] = *data;
		data++;
		rptr += 4;
		if (rptr >= desc->end) {
			rptr = desc->start;
			data = shared->msg_mem_vir;
		}
	}

	/*update rptr after data is read*/
	mb();
	desc->rptr = rptr;

	return 0;
}

static struct vpu_iface_ops imx8q_rpc_ops[] = {
	[VPU_CORE_TYPE_ENC] = {
		.check_codec = vpu_imx8q_check_codec,
		.check_fmt = vpu_imx8q_check_fmt,
		.boot_core = vpu_imx8q_boot_core,
		.get_power_state = vpu_imx8q_get_power_state,
		.on_firmware_loaded = vpu_imx8q_on_firmware_loaded,
		.get_data_size = vpu_windsor_get_data_size,
		.check_memory_region = vpu_imx8q_check_memory_region,
		.init_rpc = vpu_windsor_init_rpc,
		.set_log_buf = vpu_windsor_set_log_buf,
		.set_system_cfg = vpu_windsor_set_system_cfg,
		.get_version = vpu_windsor_get_version,
		.send_cmd_buf = vpu_rpc_send_cmd_buf,
		.receive_msg_buf = vpu_rpc_receive_msg_buf,
		.pack_cmd = vpu_windsor_pack_cmd,
		.convert_msg_id = vpu_windsor_convert_msg_id,
		.unpack_msg_data = vpu_windsor_unpack_msg_data,
		.config_memory_resource = vpu_windsor_config_memory_resource,
		.get_stream_buffer_size = vpu_windsor_get_stream_buffer_size,
		.config_stream_buffer = vpu_windsor_config_stream_buffer,
		.get_stream_buffer_desc = vpu_windsor_get_stream_buffer_desc,
		.update_stream_buffer = vpu_windsor_update_stream_buffer,
		.set_encode_params = vpu_windsor_set_encode_params,
		.input_frame = vpu_windsor_input_frame,
		.get_max_instance_count = vpu_windsor_get_max_instance_count,
	},
	[VPU_CORE_TYPE_DEC] = {
		.check_codec = vpu_imx8q_check_codec,
		.check_fmt = vpu_imx8q_check_fmt,
		.boot_core = vpu_imx8q_boot_core,
		.get_power_state = vpu_imx8q_get_power_state,
		.on_firmware_loaded = vpu_imx8q_on_firmware_loaded,
		.get_data_size = vpu_malone_get_data_size,
		.check_memory_region = vpu_imx8q_check_memory_region,
		.init_rpc = vpu_malone_init_rpc,
		.set_log_buf = vpu_malone_set_log_buf,
		.set_system_cfg = vpu_malone_set_system_cfg,
		.get_version = vpu_malone_get_version,
		.send_cmd_buf = vpu_rpc_send_cmd_buf,
		.receive_msg_buf = vpu_rpc_receive_msg_buf,
		.get_stream_buffer_size = vpu_malone_get_stream_buffer_size,
		.config_stream_buffer = vpu_malone_config_stream_buffer,
		.set_decode_params = vpu_malone_set_decode_params,
		.pack_cmd = vpu_malone_pack_cmd,
		.convert_msg_id = vpu_malone_convert_msg_id,
		.unpack_msg_data = vpu_malone_unpack_msg_data,
		.get_stream_buffer_desc = vpu_malone_get_stream_buffer_desc,
		.update_stream_buffer = vpu_malone_update_stream_buffer,
		.add_scode = vpu_malone_add_scode,
		.input_frame = vpu_malone_input_frame,
		.pre_send_cmd = vpu_malone_pre_cmd,
		.post_send_cmd = vpu_malone_post_cmd,
		.init_instance = vpu_malone_init_instance,
		.get_max_instance_count = vpu_malone_get_max_instance_count,
	},
};

static struct vpu_iface_ops *vpu_get_iface(struct vpu_dev *vpu, enum vpu_core_type type)
{
	struct vpu_iface_ops *rpc_ops = NULL;
	u32 size = 0;

	switch (vpu->res->plat_type) {
	case IMX8QXP:
	case IMX8QM:
		rpc_ops = imx8q_rpc_ops;
		size = ARRAY_SIZE(imx8q_rpc_ops);
		break;
	default:
		return NULL;
	}

	if (type >= size)
		return NULL;

	return &rpc_ops[type];
}

struct vpu_iface_ops *vpu_core_get_iface(struct vpu_core *core)
{
	return vpu_get_iface(core->vpu, core->type);
}

struct vpu_iface_ops *vpu_inst_get_iface(struct vpu_inst *inst)
{
	if (inst->core)
		return vpu_core_get_iface(inst->core);

	return vpu_get_iface(inst->vpu, inst->type);
}
