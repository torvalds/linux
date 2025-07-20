/* SPDX-License-Identifier: GPL-2.0 or MIT */
/* Copyright 2023 Collabora ltd. */

#ifndef __PANTHOR_MCU_H__
#define __PANTHOR_MCU_H__

#include <linux/types.h>

struct panthor_device;
struct panthor_kernel_bo;

#define MAX_CSGS				31
#define MAX_CS_PER_CSG                          32

struct panthor_fw_ringbuf_input_iface {
	u64 insert;
	u64 extract;
};

struct panthor_fw_ringbuf_output_iface {
	u64 extract;
	u32 active;
};

struct panthor_fw_cs_control_iface {
#define CS_FEATURES_WORK_REGS(x)		(((x) & GENMASK(7, 0)) + 1)
#define CS_FEATURES_SCOREBOARDS(x)		(((x) & GENMASK(15, 8)) >> 8)
#define CS_FEATURES_COMPUTE			BIT(16)
#define CS_FEATURES_FRAGMENT			BIT(17)
#define CS_FEATURES_TILER			BIT(18)
	u32 features;
	u32 input_va;
	u32 output_va;
};

struct panthor_fw_cs_input_iface {
#define CS_STATE_MASK				GENMASK(2, 0)
#define CS_STATE_STOP				0
#define CS_STATE_START				1
#define CS_EXTRACT_EVENT			BIT(4)
#define CS_IDLE_SYNC_WAIT			BIT(8)
#define CS_IDLE_PROTM_PENDING			BIT(9)
#define CS_IDLE_EMPTY				BIT(10)
#define CS_IDLE_RESOURCE_REQ			BIT(11)
#define CS_TILER_OOM				BIT(26)
#define CS_PROTM_PENDING			BIT(27)
#define CS_FATAL				BIT(30)
#define CS_FAULT				BIT(31)
#define CS_REQ_MASK				(CS_STATE_MASK | \
						 CS_EXTRACT_EVENT | \
						 CS_IDLE_SYNC_WAIT | \
						 CS_IDLE_PROTM_PENDING | \
						 CS_IDLE_EMPTY | \
						 CS_IDLE_RESOURCE_REQ)
#define CS_EVT_MASK				(CS_TILER_OOM | \
						 CS_PROTM_PENDING | \
						 CS_FATAL | \
						 CS_FAULT)
	u32 req;

#define CS_CONFIG_PRIORITY(x)			((x) & GENMASK(3, 0))
#define CS_CONFIG_DOORBELL(x)			(((x) << 8) & GENMASK(15, 8))
	u32 config;
	u32 reserved1;
	u32 ack_irq_mask;
	u64 ringbuf_base;
	u32 ringbuf_size;
	u32 reserved2;
	u64 heap_start;
	u64 heap_end;
	u64 ringbuf_input;
	u64 ringbuf_output;
	u32 instr_config;
	u32 instrbuf_size;
	u64 instrbuf_base;
	u64 instrbuf_offset_ptr;
};

struct panthor_fw_cs_output_iface {
	u32 ack;
	u32 reserved1[15];
	u64 status_cmd_ptr;

#define CS_STATUS_WAIT_SB_MASK			GENMASK(15, 0)
#define CS_STATUS_WAIT_SB_SRC_MASK		GENMASK(19, 16)
#define CS_STATUS_WAIT_SB_SRC_NONE		(0 << 16)
#define CS_STATUS_WAIT_SB_SRC_WAIT		(8 << 16)
#define CS_STATUS_WAIT_SYNC_COND_LE		(0 << 24)
#define CS_STATUS_WAIT_SYNC_COND_GT		(1 << 24)
#define CS_STATUS_WAIT_SYNC_COND_MASK		GENMASK(27, 24)
#define CS_STATUS_WAIT_PROGRESS			BIT(28)
#define CS_STATUS_WAIT_PROTM			BIT(29)
#define CS_STATUS_WAIT_SYNC_64B			BIT(30)
#define CS_STATUS_WAIT_SYNC			BIT(31)
	u32 status_wait;
	u32 status_req_resource;
	u64 status_wait_sync_ptr;
	u32 status_wait_sync_value;
	u32 status_scoreboards;

#define CS_STATUS_BLOCKED_REASON_UNBLOCKED	0
#define CS_STATUS_BLOCKED_REASON_SB_WAIT	1
#define CS_STATUS_BLOCKED_REASON_PROGRESS_WAIT	2
#define CS_STATUS_BLOCKED_REASON_SYNC_WAIT	3
#define CS_STATUS_BLOCKED_REASON_DEFERRED	4
#define CS_STATUS_BLOCKED_REASON_RESOURCE	5
#define CS_STATUS_BLOCKED_REASON_FLUSH		6
#define CS_STATUS_BLOCKED_REASON_MASK		GENMASK(3, 0)
	u32 status_blocked_reason;
	u32 status_wait_sync_value_hi;
	u32 reserved2[6];

#define CS_EXCEPTION_TYPE(x)			((x) & GENMASK(7, 0))
#define CS_EXCEPTION_DATA(x)			(((x) >> 8) & GENMASK(23, 0))
	u32 fault;
	u32 fatal;
	u64 fault_info;
	u64 fatal_info;
	u32 reserved3[10];
	u32 heap_vt_start;
	u32 heap_vt_end;
	u32 reserved4;
	u32 heap_frag_end;
	u64 heap_address;
};

struct panthor_fw_csg_control_iface {
	u32 features;
	u32 input_va;
	u32 output_va;
	u32 suspend_size;
	u32 protm_suspend_size;
	u32 stream_num;
	u32 stream_stride;
};

struct panthor_fw_csg_input_iface {
#define CSG_STATE_MASK				GENMASK(2, 0)
#define CSG_STATE_TERMINATE			0
#define CSG_STATE_START				1
#define CSG_STATE_SUSPEND			2
#define CSG_STATE_RESUME			3
#define CSG_ENDPOINT_CONFIG			BIT(4)
#define CSG_STATUS_UPDATE			BIT(5)
#define CSG_SYNC_UPDATE				BIT(28)
#define CSG_IDLE				BIT(29)
#define CSG_DOORBELL				BIT(30)
#define CSG_PROGRESS_TIMER_EVENT		BIT(31)
#define CSG_REQ_MASK				(CSG_STATE_MASK | \
						 CSG_ENDPOINT_CONFIG | \
						 CSG_STATUS_UPDATE)
#define CSG_EVT_MASK				(CSG_SYNC_UPDATE | \
						 CSG_IDLE | \
						 CSG_PROGRESS_TIMER_EVENT)
	u32 req;
	u32 ack_irq_mask;

	u32 doorbell_req;
	u32 cs_irq_ack;
	u32 reserved1[4];
	u64 allow_compute;
	u64 allow_fragment;
	u32 allow_other;

#define CSG_EP_REQ_COMPUTE(x)			((x) & GENMASK(7, 0))
#define CSG_EP_REQ_FRAGMENT(x)			(((x) << 8) & GENMASK(15, 8))
#define CSG_EP_REQ_TILER(x)			(((x) << 16) & GENMASK(19, 16))
#define CSG_EP_REQ_EXCL_COMPUTE			BIT(20)
#define CSG_EP_REQ_EXCL_FRAGMENT		BIT(21)
#define CSG_EP_REQ_PRIORITY(x)			(((x) << 28) & GENMASK(31, 28))
#define CSG_EP_REQ_PRIORITY_MASK		GENMASK(31, 28)
	u32 endpoint_req;
	u32 reserved2[2];
	u64 suspend_buf;
	u64 protm_suspend_buf;
	u32 config;
	u32 iter_trace_config;
};

struct panthor_fw_csg_output_iface {
	u32 ack;
	u32 reserved1;
	u32 doorbell_ack;
	u32 cs_irq_req;
	u32 status_endpoint_current;
	u32 status_endpoint_req;

#define CSG_STATUS_STATE_IS_IDLE		BIT(0)
	u32 status_state;
	u32 resource_dep;
};

struct panthor_fw_global_control_iface {
	u32 version;
	u32 features;
	u32 input_va;
	u32 output_va;
	u32 group_num;
	u32 group_stride;
	u32 perfcnt_size;
	u32 instr_features;
};

struct panthor_fw_global_input_iface {
#define GLB_HALT				BIT(0)
#define GLB_CFG_PROGRESS_TIMER			BIT(1)
#define GLB_CFG_ALLOC_EN			BIT(2)
#define GLB_CFG_POWEROFF_TIMER			BIT(3)
#define GLB_PROTM_ENTER				BIT(4)
#define GLB_PERFCNT_EN				BIT(5)
#define GLB_PERFCNT_SAMPLE			BIT(6)
#define GLB_COUNTER_EN				BIT(7)
#define GLB_PING				BIT(8)
#define GLB_FWCFG_UPDATE			BIT(9)
#define GLB_IDLE_EN				BIT(10)
#define GLB_SLEEP				BIT(12)
#define GLB_INACTIVE_COMPUTE			BIT(20)
#define GLB_INACTIVE_FRAGMENT			BIT(21)
#define GLB_INACTIVE_TILER			BIT(22)
#define GLB_PROTM_EXIT				BIT(23)
#define GLB_PERFCNT_THRESHOLD			BIT(24)
#define GLB_PERFCNT_OVERFLOW			BIT(25)
#define GLB_IDLE				BIT(26)
#define GLB_DBG_CSF				BIT(30)
#define GLB_DBG_HOST				BIT(31)
#define GLB_REQ_MASK				GENMASK(10, 0)
#define GLB_EVT_MASK				GENMASK(26, 20)
	u32 req;
	u32 ack_irq_mask;
	u32 doorbell_req;
	u32 reserved1;
	u32 progress_timer;

#define GLB_TIMER_VAL(x)			((x) & GENMASK(30, 0))
#define GLB_TIMER_SOURCE_GPU_COUNTER		BIT(31)
	u32 poweroff_timer;
	u64 core_en_mask;
	u32 reserved2;
	u32 perfcnt_as;
	u64 perfcnt_base;
	u32 perfcnt_extract;
	u32 reserved3[3];
	u32 perfcnt_config;
	u32 perfcnt_csg_select;
	u32 perfcnt_fw_enable;
	u32 perfcnt_csg_enable;
	u32 perfcnt_csf_enable;
	u32 perfcnt_shader_enable;
	u32 perfcnt_tiler_enable;
	u32 perfcnt_mmu_l2_enable;
	u32 reserved4[8];
	u32 idle_timer;
};

enum panthor_fw_halt_status {
	PANTHOR_FW_HALT_OK = 0,
	PANTHOR_FW_HALT_ON_PANIC = 0x4e,
	PANTHOR_FW_HALT_ON_WATCHDOG_EXPIRATION = 0x4f,
};

struct panthor_fw_global_output_iface {
	u32 ack;
	u32 reserved1;
	u32 doorbell_ack;
	u32 reserved2;
	u32 halt_status;
	u32 perfcnt_status;
	u32 perfcnt_insert;
};

/**
 * struct panthor_fw_cs_iface - Firmware command stream slot interface
 */
struct panthor_fw_cs_iface {
	/**
	 * @lock: Lock protecting access to the panthor_fw_cs_input_iface::req
	 * field.
	 *
	 * Needed so we can update the req field concurrently from the interrupt
	 * handler and the scheduler logic.
	 *
	 * TODO: Ideally we'd want to use a cmpxchg() to update the req, but FW
	 * interface sections are mapped uncached/write-combined right now, and
	 * using cmpxchg() on such mappings leads to SError faults. Revisit when
	 * we have 'SHARED' GPU mappings hooked up.
	 */
	spinlock_t lock;

	/**
	 * @control: Command stream slot control interface.
	 *
	 * Used to expose command stream slot properties.
	 *
	 * This interface is read-only.
	 */
	struct panthor_fw_cs_control_iface *control;

	/**
	 * @input: Command stream slot input interface.
	 *
	 * Used for host updates/events.
	 */
	struct panthor_fw_cs_input_iface *input;

	/**
	 * @output: Command stream slot output interface.
	 *
	 * Used for FW updates/events.
	 *
	 * This interface is read-only.
	 */
	const struct panthor_fw_cs_output_iface *output;
};

/**
 * struct panthor_fw_csg_iface - Firmware command stream group slot interface
 */
struct panthor_fw_csg_iface {
	/**
	 * @lock: Lock protecting access to the panthor_fw_csg_input_iface::req
	 * field.
	 *
	 * Needed so we can update the req field concurrently from the interrupt
	 * handler and the scheduler logic.
	 *
	 * TODO: Ideally we'd want to use a cmpxchg() to update the req, but FW
	 * interface sections are mapped uncached/write-combined right now, and
	 * using cmpxchg() on such mappings leads to SError faults. Revisit when
	 * we have 'SHARED' GPU mappings hooked up.
	 */
	spinlock_t lock;

	/**
	 * @control: Command stream group slot control interface.
	 *
	 * Used to expose command stream group slot properties.
	 *
	 * This interface is read-only.
	 */
	const struct panthor_fw_csg_control_iface *control;

	/**
	 * @input: Command stream slot input interface.
	 *
	 * Used for host updates/events.
	 */
	struct panthor_fw_csg_input_iface *input;

	/**
	 * @output: Command stream group slot output interface.
	 *
	 * Used for FW updates/events.
	 *
	 * This interface is read-only.
	 */
	const struct panthor_fw_csg_output_iface *output;
};

/**
 * struct panthor_fw_global_iface - Firmware global interface
 */
struct panthor_fw_global_iface {
	/**
	 * @lock: Lock protecting access to the panthor_fw_global_input_iface::req
	 * field.
	 *
	 * Needed so we can update the req field concurrently from the interrupt
	 * handler and the scheduler/FW management logic.
	 *
	 * TODO: Ideally we'd want to use a cmpxchg() to update the req, but FW
	 * interface sections are mapped uncached/write-combined right now, and
	 * using cmpxchg() on such mappings leads to SError faults. Revisit when
	 * we have 'SHARED' GPU mappings hooked up.
	 */
	spinlock_t lock;

	/**
	 * @control: Command stream group slot control interface.
	 *
	 * Used to expose global FW properties.
	 *
	 * This interface is read-only.
	 */
	const struct panthor_fw_global_control_iface *control;

	/**
	 * @input: Global input interface.
	 *
	 * Used for host updates/events.
	 */
	struct panthor_fw_global_input_iface *input;

	/**
	 * @output: Global output interface.
	 *
	 * Used for FW updates/events.
	 *
	 * This interface is read-only.
	 */
	const struct panthor_fw_global_output_iface *output;
};

/**
 * panthor_fw_toggle_reqs() - Toggle acknowledge bits to send an event to the FW
 * @__iface: The interface to operate on.
 * @__in_reg: Name of the register to update in the input section of the interface.
 * @__out_reg: Name of the register to take as a reference in the output section of the
 * interface.
 * @__mask: Mask to apply to the update.
 *
 * The Host -> FW event/message passing was designed to be lockless, with each side of
 * the channel having its writeable section. Events are signaled as a difference between
 * the host and FW side in the req/ack registers (when a bit differs, there's an event
 * pending, when they are the same, nothing needs attention).
 *
 * This helper allows one to update the req register based on the current value of the
 * ack register managed by the FW. Toggling a specific bit will flag an event. In order
 * for events to be re-evaluated, the interface doorbell needs to be rung.
 *
 * Concurrent accesses to the same req register is covered.
 *
 * Anything requiring atomic updates to multiple registers requires a dedicated lock.
 */
#define panthor_fw_toggle_reqs(__iface, __in_reg, __out_reg, __mask) \
	do { \
		u32 __cur_val, __new_val, __out_val; \
		spin_lock(&(__iface)->lock); \
		__cur_val = READ_ONCE((__iface)->input->__in_reg); \
		__out_val = READ_ONCE((__iface)->output->__out_reg); \
		__new_val = ((__out_val ^ (__mask)) & (__mask)) | (__cur_val & ~(__mask)); \
		WRITE_ONCE((__iface)->input->__in_reg, __new_val); \
		spin_unlock(&(__iface)->lock); \
	} while (0)

/**
 * panthor_fw_update_reqs() - Update bits to reflect a configuration change
 * @__iface: The interface to operate on.
 * @__in_reg: Name of the register to update in the input section of the interface.
 * @__val: Value to set.
 * @__mask: Mask to apply to the update.
 *
 * Some configuration get passed through req registers that are also used to
 * send events to the FW. Those req registers being updated from the interrupt
 * handler, they require special helpers to update the configuration part as well.
 *
 * Concurrent accesses to the same req register is covered.
 *
 * Anything requiring atomic updates to multiple registers requires a dedicated lock.
 */
#define panthor_fw_update_reqs(__iface, __in_reg, __val, __mask) \
	do { \
		u32 __cur_val, __new_val; \
		spin_lock(&(__iface)->lock); \
		__cur_val = READ_ONCE((__iface)->input->__in_reg); \
		__new_val = (__cur_val & ~(__mask)) | ((__val) & (__mask)); \
		WRITE_ONCE((__iface)->input->__in_reg, __new_val); \
		spin_unlock(&(__iface)->lock); \
	} while (0)

struct panthor_fw_global_iface *
panthor_fw_get_glb_iface(struct panthor_device *ptdev);

struct panthor_fw_csg_iface *
panthor_fw_get_csg_iface(struct panthor_device *ptdev, u32 csg_slot);

struct panthor_fw_cs_iface *
panthor_fw_get_cs_iface(struct panthor_device *ptdev, u32 csg_slot, u32 cs_slot);

int panthor_fw_csg_wait_acks(struct panthor_device *ptdev, u32 csg_id, u32 req_mask,
			     u32 *acked, u32 timeout_ms);

int panthor_fw_glb_wait_acks(struct panthor_device *ptdev, u32 req_mask, u32 *acked,
			     u32 timeout_ms);

void panthor_fw_ring_csg_doorbells(struct panthor_device *ptdev, u32 csg_slot);

struct panthor_kernel_bo *
panthor_fw_alloc_queue_iface_mem(struct panthor_device *ptdev,
				 struct panthor_fw_ringbuf_input_iface **input,
				 const struct panthor_fw_ringbuf_output_iface **output,
				 u32 *input_fw_va, u32 *output_fw_va);
struct panthor_kernel_bo *
panthor_fw_alloc_suspend_buf_mem(struct panthor_device *ptdev, size_t size);

struct panthor_vm *panthor_fw_vm(struct panthor_device *ptdev);

void panthor_fw_pre_reset(struct panthor_device *ptdev, bool on_hang);
int panthor_fw_post_reset(struct panthor_device *ptdev);

static inline void panthor_fw_suspend(struct panthor_device *ptdev)
{
	panthor_fw_pre_reset(ptdev, false);
}

static inline int panthor_fw_resume(struct panthor_device *ptdev)
{
	return panthor_fw_post_reset(ptdev);
}

int panthor_fw_init(struct panthor_device *ptdev);
void panthor_fw_unplug(struct panthor_device *ptdev);

#endif
