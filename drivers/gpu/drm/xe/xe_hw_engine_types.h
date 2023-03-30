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
	XE_HW_ENGINE_BCS0,
	XE_HW_ENGINE_BCS1,
	XE_HW_ENGINE_BCS2,
	XE_HW_ENGINE_BCS3,
	XE_HW_ENGINE_BCS4,
	XE_HW_ENGINE_BCS5,
	XE_HW_ENGINE_BCS6,
	XE_HW_ENGINE_BCS7,
	XE_HW_ENGINE_BCS8,
	XE_HW_ENGINE_VCS0,
	XE_HW_ENGINE_VCS1,
	XE_HW_ENGINE_VCS2,
	XE_HW_ENGINE_VCS3,
	XE_HW_ENGINE_VCS4,
	XE_HW_ENGINE_VCS5,
	XE_HW_ENGINE_VCS6,
	XE_HW_ENGINE_VCS7,
	XE_HW_ENGINE_VECS0,
	XE_HW_ENGINE_VECS1,
	XE_HW_ENGINE_VECS2,
	XE_HW_ENGINE_VECS3,
	XE_HW_ENGINE_CCS0,
	XE_HW_ENGINE_CCS1,
	XE_HW_ENGINE_CCS2,
	XE_HW_ENGINE_CCS3,
	XE_NUM_HW_ENGINES,
};

/* FIXME: s/XE_HW_ENGINE_MAX_INSTANCE/XE_HW_ENGINE_MAX_COUNT */
#define XE_HW_ENGINE_MAX_INSTANCE	9

struct xe_bo;
struct xe_execlist_port;
struct xe_gt;

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
	void (*irq_handler)(struct xe_hw_engine *, u16);
	/** @engine_id: id  for this hw engine */
	enum xe_hw_engine_id engine_id;
};

#endif
