/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef AMDGPU_UALINK_H
#define AMDGPU_UALINK_H

#include <linux/uuid.h>
#include <linux/bitmap.h>
#include <linux/kobject.h>

struct amdgpu_device;

struct amdgpu_xcp;

#define AMDGPU_UALINK_ACCEL_MAX 256
#define AMDGPU_UALINK_LOCAL_ACCELS_MAX 8
#define AMDGPU_UALINK_STATIONS_MAX 64

/* A vpod_id of 0 is reserved and treated as invalid/no vPod */
#define AMDGPU_UALINK_VPOD_ID_INVALID 0

/* nHT firmware status */
#define AMDGPU_NHT_FW_ST_PREINIT	0xA0
#define AMDGPU_NHT_FW_ST_READY	0xA1
#define AMDGPU_NHT_FW_ST_HALT	0xA2
#define AMDGPU_NHT_FW_ST_ERROR	0xA3
#define AMDGPU_NHT_FW_ST_FATAL	0xF0

#define AMDGPU_UALINK_RESP_TIMEOUT			5000 /* 5s timeout */

#define AMDGPU_UALINK_HANDLE_ACCID_MASK			GENMASK_ULL(9, 0)
#define AMDGPU_UALINK_MESSAGE_HEADER_MASK		GENMASK_ULL(9, 0)
#define AMDGPU_UALINK_HELLO_MSG_ACCID_MASK		GENMASK_U32(9, 0)
#define AMDGPU_UALINK_HELLO_MSG_RECV_ACCID_SHIFT	10
#define AMDGPU_UALINK_HELLO_MSG_SENDER_ACCID_SHIFT	20
#define AMDGPU_UALINK_NPA_FAIL_MSG_FAIL_REASON_MASK	GENMASK_U32(7, 0)

/* GPU-ID is stored in bits 41-50 of the NPA address. However, we
 * store NPA address is GPU PAGE aligned so bottom 12 bits are not used.
 * As a result, we need the GPU-ID shift to be 41 - 12 = 29.
 */
#define AMDGPU_UALINK_NPA_ADDR_GPUID_SHIFT		29
#define AMDGPU_UALINK_NPA_ADDR_GPUID_MASK		GENMASK_ULL(38, 29)
/* Reserve 2M in each 2TB range for ring buffer allocations for
 * remote interrupts. In terms of GPU pages, this is 2M / 4K = 512 pages.
 * So we reserve 512 pages in each 2TB range.
 */
#define AMDGPU_UALINK_NPA_ADDR_RANGE_RESERVED		(1U << 9)
#define AMDGPU_UALINK_NPA_ADDR_RANGE_MASK		GENMASK_ULL(28, 0)

enum AMDGPU_UALINK_NPA_FAIL_REASON {
	AMDGPU_UALINK_NPA_FAIL_NOSPACE			= 1,
	AMDGPU_UALINK_NPA_FAIL_INVALID_HANDLE		= 2,
	AMDGPU_UALINK_NPA_FAIL_DUPLICATE		= 3,
	AMDGPU_UALINK_NPA_FAIL_ERROR			= 4,
};

enum AMDGPU_UALINK_NODE_STATE {
	AMDGPU_UALINK_NODE_NOT_READY			= 0,
	AMDGPU_UALINK_NODE_PENDING			= 1,
	AMDGPU_UALINK_NODE_READY			= 2,
	AMDGPU_UALINK_NODE_TEARDOWN			= 3
};

enum AMDGPU_UALINK_PROTOCOL_MESSAGES {
	AMDGPU_UALINK_HELLO_MSG				= 1,
	AMDGPU_UALINK_HELLO_ACK_MSG			= 2,
	AMDGPU_UALINK_NPA_REQ_MSG			= 3,
	AMDGPU_UALINK_NPA_RSP_MSG			= 4,
	AMDGPU_UALINK_NPA_FAIL_MSG			= 5,
	AMDGPU_UALINK_NPA_REVOKE_MSG			= 6,
	AMDGPU_UALINK_NPA_RELEASE_MSG			= 7,
	AMDGPU_UALINK_MAX_PROTOCOL_MSG
};

#define AMDGPU_UALINK_LIGHTWEIGHT_TLB_SHOOTDOWN		0x1
#define AMDGPU_UALINK_HEAVYWEIGHT_TLB_SHOOTDOWN		0x2

enum amdgpu_ualink_conn_state {
	AMDGPU_UALINK_CONN_NOT_READY			= 0,
	AMDGPU_UALINK_CONN_IN_PROGRESS			= 1,
	AMDGPU_UALINK_CONN_PENDING			= 2,
	AMDGPU_UALINK_CONN_ESTABLISHED			= 3
};

enum amdgpu_ualink_type {
	AMDGPU_UALINK_TYPE_UALOE = 0,
	AMDGPU_UALINK_TYPE_UALINK = 1,
	AMDGPU_UALINK_TYPE_MAX
};

enum amdgpu_ualink_accel_state {
	AMDGPU_UALINK_ACCEL_STATE_UNCONFIGURED = 0,
	AMDGPU_UALINK_ACCEL_STATE_PPOD_CONFIGURED,
	AMDGPU_UALINK_ACCEL_STATE_VPOD_CONFIGURED,
	AMDGPU_UALINK_ACCEL_STATE_READY,
	AMDGPU_UALINK_ACCEL_STATE_ACTIVE,
	AMDGPU_UALINK_ACCEL_STATE_ERROR,
	AMDGPU_UALINK_ACCEL_STATE_MAX
};

enum amdgpu_ualink_addr_mode {
	AMDGPU_UALINK_ADDR_MODE_SOURCE_ALIAS = 0,
	AMDGPU_UALINK_ADDR_MODE_SOURCE_IDENT = 1,
	AMDGPU_UALINK_ADDR_MODE_MAX
};

enum amdgpu_ualink_mgr_state {
	AMDGPU_UALINK_INIT_NONE = 0,
	AMDGPU_UALINK_INIT_HW,
	AMDGPU_UALINK_INIT_COMPLETE,
	AMDGPU_UALINK_INIT_ERROR
};

/* Physical pod info shared between query and setup API */
struct amdgpu_ualink_ppod_info {
	u32 accel_id;
	u32 bandwidth;
	u32 latency;
	u32 size;
	uuid_t id;
};

/* Virtual pod info shared between query and config API */
struct amdgpu_ualink_vpod_info {
	u32 id;
	u32 size;
	u32 addr_mode;
	DECLARE_BITMAP(active_accel_bits, AMDGPU_UALINK_ACCEL_MAX);
};

/* Query current UALink physical and virtual pod info */
struct amdgpu_ualink_info {
	struct kobject kobj;
	enum amdgpu_ualink_type link_type;
	enum amdgpu_ualink_accel_state accel_state;
	struct amdgpu_ualink_ppod_info ppod;
	struct amdgpu_ualink_vpod_info vpod;
	/* Local accelerator array in the vpod derived from
	 * vpod.active_accel_bits, in arbitrary order
	 */
	u32 n_local_accels;
	u32 local_accels[AMDGPU_UALINK_LOCAL_ACCELS_MAX];
};
#define to_ualink_info(ko) container_of(ko, struct amdgpu_ualink_info, kobj)

/* UALink physical pod setup */
struct amdgpu_ualink_ppod_setup {
	struct kobject kobj;
	struct amdgpu_ualink_ppod_info ppod;
	/* Local accelerator array indexed by socket ID */
	u32 n_local_accels;
	u32 local_accels[AMDGPU_UALINK_LOCAL_ACCELS_MAX];
};
#define to_ualink_ppod_setup(ko) container_of(ko, struct amdgpu_ualink_ppod_setup, kobj)

/* UAlink virtual pod config */
struct amdgpu_ualink_vpod_config {
	struct kobject kobj;
	struct amdgpu_ualink_vpod_info vpod;
};
#define to_ualink_vpod_config(ko) container_of(ko, struct amdgpu_ualink_vpod_config, kobj)

/* UALink station configuration */
struct amdgpu_ualink_station_config {
	struct kobject kobj;
	/* Station configuration flags
	 * bits [3:0]: PortPerStation (PPS) - 1, 2, or 4
	 * bits [7:4]: Reserved
	 */
	u8 flags;
	u8 n_stations;
	/* bitmap or enabled lanes for each station in logical station order */
	u8 lane_en_bitmap[AMDGPU_UALINK_STATIONS_MAX];
};
#define to_ualink_station_config(ko) container_of(ko, struct amdgpu_ualink_station_config, kobj)

struct amdgpu_ualink_handle {
	union {
		struct {
			u64 handle_lo;
			u64 handle_hi;
		};
		u64 handle[2];
	};
};

struct amdgpu_ualink_imp_xa_node {
	struct amdgpu_device		*adev;

	/* 128-bit handle for the BO */
	struct amdgpu_ualink_handle	handle;

	/* Use to signal NPA-RSP arrival */
	struct completion		npa_done;

	/* NPA address received in the NPA-RSP message */
	u64				npa_addr;
	u64				size;

	/* GEM handle for the NPA BO */
	u32				gem_handle;

	/* Fail reason received in NPA-FAIL message */
	int				fail_reason;

	/* Node state to signal if node setup is in progress
	 * or is already completed. Node state goes back to
	 * in progress if a HELLO message is received in
	 * response to NPA-REQ message.
	 */
	enum AMDGPU_UALINK_NODE_STATE	node_state;

	/* Used to connect all importer XA nodes from a particular
	 * exporter.
	 */
	struct list_head		list;

	/* Dmabuf corresponding to the NPA BO */
	struct dma_buf			*dmabuf;

	/* Refcount to track lifetime of this node */
	struct kref			refcount;
};

struct amdgpu_ualink_npa_mm {
	struct drm_mm			mm;
	u64				va_start;
	u64				va_size;
	struct mutex			mm_lock;
};

struct amdgpu_ualink_importer_entry {
	struct drm_mm_node			*mm_node;
	u64					npa_addr;

	/* Keep track of if the connection got reset */
	u32					generation_count;

	/* Used to connect all handles exported to a particular importer */
	struct list_head			list;

	/* Pointer to the parent XA node. */
	struct amdgpu_ualink_exp_xa_node	*parent;
};

struct amdgpu_ualink_exp_xa_node {
	/* 128-bit handle for the BO */
	struct amdgpu_ualink_handle		handle;

	/* Pointer to the BO thats exported.*/
	struct amdgpu_bo			*bo;

	/* Dmabuf corresponding to the BO */
	struct dma_buf				*dmabuf;

	/* Mutex to protect the node from concurrent access */
	struct mutex				node_lock;

	/* Used for storing importer info in source identification mode */
	struct amdgpu_ualink_importer_entry	importer_entries[AMDGPU_UALINK_ACCEL_MAX];

	/* Used to track all importers of this BO. This is set when the
	 * exporter sends back the NPA-RSP message.
	 */
	DECLARE_BITMAP(importers_bitmap, AMDGPU_UALINK_ACCEL_MAX);

	/* This bitmap is used to send NPA-REVOKE messages to all the importers.
	 * And to track the NPA-RELEASE response for each NPA-REVOKE message sent.
	 * A bit is cleared in this bitmap when the NPA RELEASE message is
	 * received in response to the NPA-REVOKE message.
	 */
	DECLARE_BITMAP(npa_release_bitmap, AMDGPU_UALINK_ACCEL_MAX);

	/* Use to signal responses received from all importers */
	struct completion			npa_done;

	/* Refcount to track lifetime of this node */
	struct kref				refcount;

	/* Work to cleanup the node. */
	struct work_struct			cleanup_work;
};

struct amdgpu_ualink_connection {
	struct completion hello_done;
	struct mutex lock;
	u32 generation_count;
	enum amdgpu_ualink_conn_state state;
};

struct amdgpu_ualink_remote;

struct amdgpu_ualink_msg_ctl {
	u32 (*check_status)(struct amdgpu_device *adev);
	int (*send_metadata)(struct amdgpu_device *adev, u64 metadata_gpu_addr,
			     u32 accel_data);
	int (*send_halt)(struct amdgpu_device *adev);
};

struct amdgpu_ualink_mgr {
	u64 npa_size;
	u32 psp_if_ver;
	const struct amdgpu_ualink_msg_ctl *msg_ctl;
	struct amdgpu_ualink_info *info;
	struct amdgpu_ualink_ppod_setup *setup;
	struct amdgpu_ualink_vpod_config *config;
	struct amdgpu_ualink_station_config *stations;
	bool sysfs_init;
	enum amdgpu_ualink_mgr_state mgr_state;

	/* For remote interrupt and shootdown */
	struct amdgpu_ualink_remote *remote;

	/* handle irq from ualink client of remote GPUs */
	struct amdgpu_irq_src irq;

	/* Xarray to store info about exported BOs */
	struct xarray	exp_xa;

	/* Xarray to store info about imported ualink handles */
	struct xarray	imp_xa;

	/* Xarray to store handles to be deleted */
	struct xarray	handle_invalid_xa;

	/* Array to store connection state per remote GPU */
	struct amdgpu_ualink_connection conn_state[AMDGPU_UALINK_ACCEL_MAX];

	/* List of exported ualink handles per GPU. Used to quickly traverse
	 * the list of all handles exported to a remote GPU.
	 */
	struct list_head exp_handles_list[AMDGPU_UALINK_ACCEL_MAX];

	/* List of imported ualink handles per GPU. Used to quickly traverse
	 * the list of all handles imported from a remote GPU.
	 */
	struct list_head imp_handles_list[AMDGPU_UALINK_ACCEL_MAX];

	/* WQ to manage revocation of exported memory. */
	struct workqueue_struct *npa_wq;

	/* Memory-manager for managing NPA address space. */
	struct amdgpu_ualink_npa_mm npa_mm;

	/* NPA-VM used on the exporter.*/
	struct amdgpu_vm npa_vm;

	/* DRM client for UALink to manage GEM handles for NPA BOs */
	struct drm_client_dev client;

	/* Sequence number to track the need for TLB flushes */
	atomic64_t last_flushed_tlb_seq;

	/* Debug-only: bitmap of incoming UALink protocol messages to drop.
	 *
	 * Each bit position corresponds to a value from
	 * enum AMDGPU_UALINK_PROTOCOL_MESSAGES:
	 *   BIT(AMDGPU_UALINK_NPA_REQ_MSG) drops one incoming NPA-REQ.
	 *   BIT(AMDGPU_UALINK_NPA_RSP_MSG) drops one incoming NPA-RSP.
	 *
	 *   On reception of a message whose corresponding bit is set, the bit
	 *   is atomically cleared and the message is silently dropped. This
	 *   means at most one packet per set bit is dropped; any further
	 *   incoming packets of the same type are processed normally. This is
	 *   intended to exercise the connection reset / recovery paths from a
	 *   debugfs handle.
	 */
	unsigned long drop_msg_bitmap;
};

int amdgpu_ualink_init_interrupt(struct amdgpu_device *adev,
				 unsigned int client_id, unsigned int src_id);

int amdgpu_ualink_sw_init(struct amdgpu_device *adev);
void amdgpu_ualink_sw_fini(struct amdgpu_device *adev);

int ualink_send_hello(struct amdgpu_device *adev, u32 remote_accel_id);

int amdgpu_ualink_config_update_handler(struct amdgpu_device *adev);
int amdgpu_ualink_pause_handler(struct amdgpu_device *adev);
int amdgpu_ualink_resume_handler(struct amdgpu_device *adev);

int amdgpu_ualink_sysfs_init(struct amdgpu_device *adev);
void amdgpu_ualink_sysfs_fini(struct amdgpu_device *adev);
void amdgpu_ualink_xcp_sysfs_update(struct amdgpu_xcp *xcp);
int amdgpu_ualink_manager_start(struct amdgpu_device *adev);
void amdgpu_ualink_manager_stop(struct amdgpu_device *adev);
int amdgpu_ualink_export_handle(struct drm_device *dev, struct drm_file *filp,
				u32 gem_handle,
				struct amdgpu_ualink_handle *handle_out);
int amdgpu_ualink_import_handle(struct drm_device *dev,
				const struct amdgpu_ualink_handle *ualink_handle,
				int *fd_out);
int amdgpu_gem_ualink_handle_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *filp);
void amdgpu_ualink_revoke_exported_memory(struct amdgpu_bo *bo);

int ualink_ip_hw_init(struct amdgpu_ip_block *ip_block);
int ualink_ip_hw_fini(struct amdgpu_ip_block *ip_block);
int ualink_ip_late_init(struct amdgpu_ip_block *ip_block);
int ualink_ip_sw_init(struct amdgpu_ip_block *ip_block);
int ualink_ip_sw_fini(struct amdgpu_ip_block *ip_block);

extern const struct amdgpu_ip_block_version ualink_v1_0_ip_block;
#endif
