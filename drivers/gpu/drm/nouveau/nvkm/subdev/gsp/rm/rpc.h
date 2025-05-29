/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef __NVKM_RM_RPC_H__
#define __NVKM_RM_RPC_H__
#include "rm.h"

#define to_payload_hdr(p, header) \
	container_of((void *)p, typeof(*header), params)

int r535_gsp_rpc_poll(struct nvkm_gsp *, u32 fn);

struct nvfw_gsp_rpc *r535_gsp_msg_recv(struct nvkm_gsp *, int fn, u32 gsp_rpc_len);
int r535_gsp_msg_ntfy_add(struct nvkm_gsp *, u32 fn, nvkm_gsp_msg_ntfy_func, void *priv);

int r535_rpc_status_to_errno(uint32_t rpc_status);
#endif
