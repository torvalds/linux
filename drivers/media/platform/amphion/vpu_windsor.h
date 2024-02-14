/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020-2021 NXP
 */

#ifndef _AMPHION_VPU_WINDSOR_H
#define _AMPHION_VPU_WINDSOR_H

u32 vpu_windsor_get_data_size(void);
void vpu_windsor_init_rpc(struct vpu_shared_addr *shared,
			  struct vpu_buffer *rpc, dma_addr_t boot_addr);
void vpu_windsor_set_log_buf(struct vpu_shared_addr *shared, struct vpu_buffer *log);
void vpu_windsor_set_system_cfg(struct vpu_shared_addr *shared,
				u32 regs_base, void __iomem *regs, u32 core_id);
int vpu_windsor_get_stream_buffer_size(struct vpu_shared_addr *shared);
int vpu_windsor_pack_cmd(struct vpu_rpc_event *pkt, u32 index, u32 id, void *data);
int vpu_windsor_convert_msg_id(u32 msg_id);
int vpu_windsor_unpack_msg_data(struct vpu_rpc_event *pkt, void *data);
int vpu_windsor_config_memory_resource(struct vpu_shared_addr *shared,
				       u32 instance, u32 type, u32 index,
				       struct vpu_buffer *buf);
int vpu_windsor_config_stream_buffer(struct vpu_shared_addr *shared,
				     u32 instance, struct vpu_buffer *buf);
int vpu_windsor_update_stream_buffer(struct vpu_shared_addr *shared,
				     u32 instance, u32 ptr, bool write);
int vpu_windsor_get_stream_buffer_desc(struct vpu_shared_addr *shared,
				       u32 instance, struct vpu_rpc_buffer_desc *desc);
u32 vpu_windsor_get_version(struct vpu_shared_addr *shared);
int vpu_windsor_set_encode_params(struct vpu_shared_addr *shared,
				  u32 instance,
				  struct vpu_encode_params *params,
				  u32 update);
int vpu_windsor_input_frame(struct vpu_shared_addr *shared,
			    struct vpu_inst *inst, struct vb2_buffer *vb);
u32 vpu_windsor_get_max_instance_count(struct vpu_shared_addr *shared);

#endif
