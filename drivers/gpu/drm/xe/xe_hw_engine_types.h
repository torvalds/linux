/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_HW_ENGINE_TYPES_H_
#define _XE_HW_ENGINE_TYPES_H_

#include "xe_force_wake_types.h"
#include "xe_lrc_types.h"
#include "xe_reg_sr_types.h"

/* See "Engine ID Definition" struct in the Icelake PRM */
enum xe_engine_class {
	XE_ENGINE_CLASS_RENDER = 0,
	XE_ENGINE_CLASS_VIDEO_DECODE = 1,
	XE_ENGINE_CLASS_VIDEO_ENHANCE = 2,
	XE_ENGINE_CLASS_COPY = 3,
	XE_ENGINE_CLASS_OTHER = 4,
	XE_ENGINE_CLASS_COMPUTE = 5,
	XE_ENGINE_CLASS_MAX = 6,
};

enum xe_hw_engine_id {
	XE_HW_ENGINE_RCS0,
#define XE_HW_ENGINE_RCS_MASK	GENMASK_ULL(XE_HW_ENGINE_RCS0, XE_HW_ENGINE_RCS0)
	XE_HW_ENGINE_BCS0,
	XE_HW_ENGINE_BCS1,
	XE_HW_ENGINE_BCS2,
	XE_HW_ENGINE_BCS3,
	XE_HW_ENGINE_BCS4,
	XE_HW_ENGINE_BCS5,
	XE_HW_ENGINE_BCS6,
	XE_HW_ENGINE_BCS7,
	XE_HW_ENGINE_BCS8,
#define XE_HW_ENGINE_BCS_MASK	GENMASK_ULL(XE_HW_ENGINE_BCS8, XE_HW_ENGINE_BCS0)
	XE_HW_ENGINE_VCS0,
	XE_HW_ENGINE_VCS1,
	XE_HW_ENGINE_VCS2,
	XE_HW_ENGINE_VCS3,
	XE_HW_ENGINE_VCS4,
	XE_HW_ENGINE_VCS5,
	XE_HW_ENGINE_VCS6,
	XE_HW_ENGINE_VCS7,
#define XE_HW_ENGINE_VCS_MASK	GENMASK_ULL(XE_HW_ENGINE_VCS7, XE_HW_ENGINE_VCS0)
	XE_HW_ENGINE_VECS0,
	XE_HW_ENGINE_VECS1,
	XE_HW_ENGINE_VECS2,
	XE_HW_ENGINE_VECS3,
#define XE_HW_ENGINE_VECS_MASK	GENMASK_ULL(XE_HW_ENGINE_VECS3, XE_HW_ENGINE_VECS0)
	XE_HW_ENGINE_CCS0,
	XE_HW_ENGINE_CCS1,
	XE_HW_ENGINE_CCS2,
	XE_HW_ENGINE_CCS3,
#define XE_HW_ENGINE_CCS_MASK	GENMASK_ULL(XE_HW_ENGINE_CCS3, XE_HW_ENGINE_CCS0)
	XE_HW_ENGINE_GSCCS0,
#define XE_HW_ENGINE_GSCCS_MASK	GENMASK_ULL(XE_HW_ENGINE_GSCCS0, XE_HW_ENGINE_GSCCS0)
	XE_NUM_HW_ENGINES,
};

/* FIXME: s/XE_HW_ENGINE_MAX_INSTANCE/XE_HW_ENGINE_MAX_COUNT */
#define XE_HW_ENGINE_MAX_INSTANCE	9

struct xe_bo;
struct xe_execlist_port;
struct xe_gt;

/**
 * struct xe_hw_engine_class_intf - per hw engine class struct interface
 *
 * Contains all the hw engine properties per engine class.
 *
 * @sched_props: scheduling properties
 * @defaults: default scheduling properties
 */
struct xe_hw_engine_class_intf {
	/**
	 * @sched_props: scheduling properties
	 * @defaults: default scheduling properties
	 */
	struct {
		/** @sched_props.set_job_timeout: Set job timeout in ms for engine */
		u32 job_timeout_ms;
		/** @sched_props.job_timeout_min: Min job timeout in ms for engine */
		u32 job_timeout_min;
		/** @sched_props.job_timeout_max: Max job timeout in ms for engine */
		u32 job_timeout_max;
		/** @sched_props.timeslice_us: timeslice period in micro-seconds */
		u32 timeslice_us;
		/** @sched_props.timeslice_min: min timeslice period in micro-seconds */
		u32 timeslice_min;
		/** @sched_props.timeslice_max: max timeslice period in micro-seconds */
		u32 timeslice_max;
		/** @sched_props.preempt_timeout_us: preemption timeout in micro-seconds */
		u32 preempt_timeout_us;
		/** @sched_props.preempt_timeout_min: min preemption timeout in micro-seconds */
		u32 preempt_timeout_min;
		/** @sched_props.preempt_timeout_max: max preemption timeout in micro-seconds */
		u32 preempt_timeout_max;
	} sched_props, defaults;
};

/**
 * struct xe_hw_engine - Hardware engine
 *
 * Contains all the hardware engine state for physical instances.
 */
struct xe_hw_engine {
	/** @gt: graphics tile this hw engine belongs to */
	struct xe_gt *gt;
	/** @name: name of this hw engine */
	const char *name;
	/** @class: class of this hw engine */
	enum xe_engine_class class;
	/** @instance: physical instance of this hw engine */
	u16 instance;
	/** @logical_instance: logical instance of this hw engine */
	u16 logical_instance;
	/** @irq_offset: IRQ offset of this hw engine */
	u16 irq_offset;
	/** @mmio_base: MMIO base address of this hw engine*/
	u32 mmio_base;
	/**
	 * @reg_sr: table with registers to be restored on GT init/resume/reset
	 */
	struct xe_reg_sr reg_sr;
	/**
	 * @reg_whitelist: table with registers to be whitelisted
	 */
	struct xe_reg_sr reg_whitelist;
	/**
	 * @reg_lrc: LRC workaround registers
	 */
	struct xe_reg_sr reg_lrc;
	/** @domain: force wake domain of this hw engine */
	enum xe_force_wake_domains domain;
	/** @hwsp: hardware status page buffer object */
	struct xe_bo *hwsp;
	/** @kernel_lrc: Kernel LRC (should be replaced /w an xe_engine) */
	struct xe_lrc kernel_lrc;
	/** @exl_port: execlists port */
	struct xe_execlist_port *exl_port;
	/** @fence_irq: fence IRQ to run when a hw engine IRQ is received */
	struct xe_hw_fence_irq *fence_irq;
	/** @irq_handler: IRQ handler to run when hw engine IRQ is received */
	void (*irq_handler)(struct xe_hw_engine *hwe, u16 intr_vec);
	/** @engine_id: id  for this hw engine */
	enum xe_hw_engine_id engine_id;
	/** @eclass: pointer to per hw engine class interface */
	struct xe_hw_engine_class_intf *eclass;
};

/**
 * struct xe_hw_engine_snapshot - Hardware engine snapshot
 *
 * Contains the snapshot of useful hardware engine info and registers.
 */
struct xe_hw_engine_snapshot {
	/** @name: name of the hw engine */
	char *name;
	/** @hwe: hw engine */
	struct xe_hw_engine *hwe;
	/** @logical_instance: logical instance of this hw engine */
	u16 logical_instance;
	/** @forcewake: Force Wake information snapshot */
	struct {
		/** @forcewake.domain: force wake domain of this hw engine */
		enum xe_force_wake_domains domain;
		/** @forcewake.ref: Forcewake ref for the above domain */
		int ref;
	} forcewake;
	/** @mmio_base: MMIO base address of this hw engine*/
	u32 mmio_base;
	/** @reg: Useful MMIO register snapshot */
	struct {
		/** @reg.ring_execlist_status: RING_EXECLIST_STATUS */
		u64 ring_execlist_status;
		/** @reg.ring_execlist_sq_contents: RING_EXECLIST_SQ_CONTENTS */
		u64 ring_execlist_sq_contents;
		/** @reg.ring_acthd: RING_ACTHD */
		u64 ring_acthd;
		/** @reg.ring_bbaddr: RING_BBADDR */
		u64 ring_bbaddr;
		/** @reg.ring_dma_fadd: RING_DMA_FADD */
		u64 ring_dma_fadd;
		/** @reg.ring_hwstam: RING_HWSTAM */
		u32 ring_hwstam;
		/** @reg.ring_hws_pga: RING_HWS_PGA */
		u32 ring_hws_pga;
		/** @reg.ring_start: RING_START */
		u64 ring_start;
		/** @reg.ring_head: RING_HEAD */
		u32 ring_head;
		/** @reg.ring_tail: RING_TAIL */
		u32 ring_tail;
		/** @reg.ring_ctl: RING_CTL */
		u32 ring_ctl;
		/** @reg.ring_mi_mode: RING_MI_MODE */
		u32 ring_mi_mode;
		/** @reg.ring_mode: RING_MODE */
		u32 ring_mode;
		/** @reg.ring_imr: RING_IMR */
		u32 ring_imr;
		/** @reg.ring_esr: RING_ESR */
		u32 ring_esr;
		/** @reg.ring_emr: RING_EMR */
		u32 ring_emr;
		/** @reg.ring_eir: RING_EIR */
		u32 ring_eir;
		/** @reg.indirect_ring_state: INDIRECT_RING_STATE */
		u32 indirect_ring_state;
		/** @reg.ipehr: IPEHR */
		u32 ipehr;
		/** @reg.rcu_mode: RCU_MODE */
		u32 rcu_mode;
		struct {
			/** @reg.instdone.ring: RING_INSTDONE */
			u32 ring;
			/** @reg.instdone.slice_common: SC_INSTDONE */
			u32 *slice_common;
			/** @reg.instdone.slice_common_extra: SC_INSTDONE_EXTRA */
			u32 *slice_common_extra;
			/** @reg.instdone.slice_common_extra2: SC_INSTDONE_EXTRA2 */
			u32 *slice_common_extra2;
			/** @reg.instdone.sampler: SAMPLER_INSTDONE */
			u32 *sampler;
			/** @reg.instdone.row: ROW_INSTDONE */
			u32 *row;
			/** @reg.instdone.geom_svg: INSTDONE_GEOM_SVGUNIT */
			u32 *geom_svg;
		} instdone;
	} reg;
};

#endif
