/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020-2021 NXP
 */

#ifndef _AMPHION_VPU_MALONE_H
#define _AMPHION_VPU_MALONE_H

u32 vpu_malone_get_data_size(void);
void vpu_malone_init_rpc(struct vpu_shared_addr *shared,
			 struct vpu_buffer *rpc, dma_addr_t boot_addr);
void vpu_malone_set_log_buf(struct vpu_shared_addr *shared,
			    struct vpu_buffer *log);
void vpu_malone_set_system_cfg(struct vpu_shared_addr *shared,
			       u32 regs_base, void __iomem *regs, u32 core_id);
u32 vpu_malone_get_version(struct vpu_shared_addr *shared);
int vpu_malone_get_stream_buffer_size(struct vpu_shared_addr *shared);
int vpu_malone_config_stream_buffer(struct vpu_shared_addr *shared,
				    u32 instance, struct vpu_buffer *buf);
int vpu_malone_get_stream_buffer_desc(struct vpu_shared_addr *shared,
				      u32 instance,
				      struct vpu_rpc_buffer_desc *desc);
int vpu_malone_update_stream_buffer(struct vpu_shared_addr *shared,
				    u32 instance, u32 ptr, bool write);
int vpu_malone_set_decode_params(struct vpu_shared_addr *shared,
				 u32 instance,
				 struct vpu_decode_params *params, u32 update);
int vpu_malone_pack_cmd(struct vpu_rpc_event *pkt, u32 index, u32 id, void *data);
int vpu_malone_convert_msg_id(u32 msg_id);
int vpu_malone_unpack_msg_data(struct vpu_rpc_event *pkt, void *data);
int vpu_malone_add_scode(struct vpu_shared_addr *shared,
			 u32 instance,
			 struct vpu_buffer *stream_buffer,
			 u32 pixelformat,
			 u32 scode_type);
int vpu_malone_input_frame(struct vpu_shared_addr *shared,
			   struct vpu_inst *inst, struct vb2_buffer *vb);
bool vpu_malone_is_ready(struct vpu_shared_addr *shared, u32 instance);
int vpu_malone_pre_cmd(struct vpu_shared_addr *shared, u32 instance);
int vpu_malone_post_cmd(struct vpu_shared_addr *shared, u32 instance);
int vpu_malone_init_instance(struct vpu_shared_addr *shared, u32 instance);
u32 vpu_malone_get_max_instance_count(struct vpu_shared_addr *shared);

#endif
