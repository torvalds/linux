/* drivers/gpu/t6xx/kbase/src/platform/rk/mali_kbase_platform.h
 * Rockchip SoC Mali-T764 platform-dependent codes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

/**
 * @file mali_kbase_platform.h
 * // Platform-dependent init
 * 
 * 声明 platform_dependent_part 的 work_context 类型, pm, clk 等操作的接口. 
 */

#ifndef _KBASE_PLATFORM_H_
#define _KBASE_PLATFORM_H_

/** 
 * work_context_of_platform_dependent_part_of_rk.
 */
struct rk_context {
	/** Indicator if system clock to mail-t604 is active */
	int cmu_pmu_status;
	/** cmd & pmu lock */
	spinlock_t cmu_pmu_lock;
	/** gpu_power_domain. */
	struct clk *mali_pd;
	/** gpu_dvfs_node. */
	struct dvfs_node * mali_clk_node;
#ifdef CONFIG_MALI_MIDGARD_DVFS
	/*To calculate utilization for x sec */
	int time_tick;
	int utilisation;
	u32 time_busy;
	u32 time_idle;
	/** mali_dvfs 是否被使能. */
	bool dvfs_enabled;
	/** 标识当前有 touch_input_event 到来. */
	bool gpu_in_touch;
	spinlock_t gpu_in_touch_lock;
#endif
};

/*-------------------------------------------------------*/

typedef unsigned long mali_bool;

#ifndef MALI_TRUE
#define MALI_TRUE               (1)
#endif

#ifndef MALI_FALSE
#define MALI_FALSE              (0) 
#endif

/*-------------------------------------------------------*/

/**
 * @name Mali error types
 * @brief The common error type for the mali drivers
 * The mali_error type, all driver error handling should be of this type unless
 * it must deal with a specific APIs error type.
 * @{
 *
 * .R : r6p0-02rel0 已经不再 头文件中提供对 mali_error 的定义.
 */
typedef enum
{
	/**
	 * @brief Common Mali errors for the entire driver
	 * MALI_ERROR_NONE is guaranteed to be 0.
	 * @{
	 */
	MALI_ERROR_NONE = 0,
	MALI_ERROR_OUT_OF_GPU_MEMORY,
	MALI_ERROR_OUT_OF_MEMORY,
	MALI_ERROR_FUNCTION_FAILED,
	/* @} */
	/**
	 * @brief Mali errors for Client APIs to pass to EGL when creating EGLImages
	 * These errors must only be returned to EGL from one of the Client APIs as part of the
	 * (clientapi)_egl_image_interface.h
	 * @{
	 */
	MALI_ERROR_EGLP_BAD_ACCESS,
	MALI_ERROR_EGLP_BAD_PARAMETER,
	/* @} */
	/**
	 * @brief Mali errors for the MCL module.
	 * These errors must only be used within the private components of the OpenCL implementation that report
	 * directly to API functions for cases where errors cannot be detected in the entrypoints file. They must
	 * not be passed between driver components.
	 * These are errors in the mali error space specifically for the MCL module, hence the MCLP prefix.
	 * @{
	 */
	MALI_ERROR_MCLP_DEVICE_NOT_FOUND,
	MALI_ERROR_MCLP_DEVICE_NOT_AVAILABLE,
	MALI_ERROR_MCLP_COMPILER_NOT_AVAILABLE,
	MALI_ERROR_MCLP_MEM_OBJECT_ALLOCATION_FAILURE,
	MALI_ERROR_MCLP_PROFILING_INFO_NOT_AVAILABLE,
	MALI_ERROR_MCLP_MEM_COPY_OVERLAP,
	MALI_ERROR_MCLP_IMAGE_FORMAT_MISMATCH,
	MALI_ERROR_MCLP_IMAGE_FORMAT_NOT_SUPPORTED,
	MALI_ERROR_MCLP_BUILD_PROGRAM_FAILURE,
	MALI_ERROR_MCLP_MAP_FAILURE,
	MALI_ERROR_MCLP_MISALIGNED_SUB_BUFFER_OFFSET,
	MALI_ERROR_MCLP_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST,
	MALI_ERROR_MCLP_INVALID_VALUE,
	MALI_ERROR_MCLP_INVALID_DEVICE_TYPE,
	MALI_ERROR_MCLP_INVALID_PLATFORM,
	MALI_ERROR_MCLP_INVALID_DEVICE,
	MALI_ERROR_MCLP_INVALID_CONTEXT,
	MALI_ERROR_MCLP_INVALID_QUEUE_PROPERTIES,
	MALI_ERROR_MCLP_INVALID_COMMAND_QUEUE,
	MALI_ERROR_MCLP_INVALID_HOST_PTR,
	MALI_ERROR_MCLP_INVALID_MEM_OBJECT,
	MALI_ERROR_MCLP_INVALID_IMAGE_FORMAT_DESCRIPTOR,
	MALI_ERROR_MCLP_INVALID_IMAGE_SIZE,
	MALI_ERROR_MCLP_INVALID_SAMPLER,
	MALI_ERROR_MCLP_INVALID_BINARY,
	MALI_ERROR_MCLP_INVALID_BUILD_OPTIONS,
	MALI_ERROR_MCLP_INVALID_PROGRAM,
	MALI_ERROR_MCLP_INVALID_PROGRAM_EXECUTABLE,
	MALI_ERROR_MCLP_INVALID_KERNEL_NAME,
	MALI_ERROR_MCLP_INVALID_KERNEL_DEFINITION,
	MALI_ERROR_MCLP_INVALID_KERNEL,
	MALI_ERROR_MCLP_INVALID_ARG_INDEX,
	MALI_ERROR_MCLP_INVALID_ARG_VALUE,
	MALI_ERROR_MCLP_INVALID_ARG_SIZE,
	MALI_ERROR_MCLP_INVALID_KERNEL_ARGS,
	MALI_ERROR_MCLP_INVALID_WORK_DIMENSION,
	MALI_ERROR_MCLP_INVALID_WORK_GROUP_SIZE,
	MALI_ERROR_MCLP_INVALID_WORK_ITEM_SIZE,
	MALI_ERROR_MCLP_INVALID_GLOBAL_OFFSET,
	MALI_ERROR_MCLP_INVALID_EVENT_WAIT_LIST,
	MALI_ERROR_MCLP_INVALID_EVENT,
	MALI_ERROR_MCLP_INVALID_OPERATION,
	MALI_ERROR_MCLP_INVALID_GL_OBJECT,
	MALI_ERROR_MCLP_INVALID_BUFFER_SIZE,
	MALI_ERROR_MCLP_INVALID_MIP_LEVEL,
	MALI_ERROR_MCLP_INVALID_GLOBAL_WORK_SIZE,
	MALI_ERROR_MCLP_INVALID_GL_SHAREGROUP_REFERENCE_KHR,
	MALI_ERROR_MCLP_INVALID_EGL_OBJECT,
	/* @} */
	/**
	 * @brief Mali errors for the BASE module
	 * These errors must only be used within the private components of the Base implementation. They will not
	 * passed to other modules by the base driver.
	 * These are errors in the mali error space specifically for the BASE module, hence the BASEP prefix.
	 * @{
	 */
	MALI_ERROR_BASEP_INVALID_FUNCTION,
	/* @} */
	/** A dependency exists upon a resource that the client application wants to modify, so the driver must either
	 * create a copy of the resource (if possible) or block until the dependency has been satisfied.
	 */
	MALI_ERROR_RESOURCE_IN_USE,

	/**
	 * @brief A stride value was too big.
	 *
	 * A surface descriptor can store strides of up to 2<sup>31</sup>-1 bytes but strides greater than
	 * 2<sup>28</sup>-1 bytes cannot be expressed in bits without overflow.
	 */
	MALI_ERROR_STRIDE_TOO_BIG

} mali_error;

/**
 * 将 gpu_clk 设置为 'rate', 'rate' 以 KHz 为单位. 
 * @param node: 
 *      指向 gpu_dvfs_node 
 * @param rate
 *      预期设置的 gpu_clk 的 value, KHz 为单位. 
 */
int mali_dvfs_clk_set(struct dvfs_node * node,unsigned long rate);      // 'rate' 以 KHz 为单位. 

/* All things that are needed for the Linux port. */
/**
 * 关闭/开启 gpu 的 power 和 clock.
 * @param kbdev
 *	指向 mali_device.
 * @param control
 *	若是 1, 表征要开启.
 *	若是 0, 表征要关闭.
 */
int kbase_platform_cmu_pmu_control(struct kbase_device *kbdev, int control);
/**
 * 在 sysfs_dir_of_mali_device 下创建 rk_ext_file_nodes.
 */
int kbase_platform_create_sysfs_file(struct device *dev);
/**
 * 删除 sysfs_dir_of_mali_device 下的 rk_ext_file_nodes.
 */
void kbase_platform_remove_sysfs_file(struct device *dev);

/**
 * 返回 gpu_power_domain 是否开启. 
 */
int kbase_platform_is_power_on(void);

mali_error kbase_platform_init(struct kbase_device *kbdev);
void kbase_platform_term(struct kbase_device *kbdev);

/**
 * 使能 clk_of_gpu_dvfs_node.
 */
int kbase_platform_clock_on(struct kbase_device *kbdev);
/**
 * 禁止(关闭) clk_of_gpu_dvfs_node.
 */
int kbase_platform_clock_off(struct kbase_device *kbdev);

/**
 * 开启 gpu_power_domain.
 */
int kbase_platform_power_off(struct kbase_device *kbdev);
/**
 * 关闭 gpu_power_domain.
 */
int kbase_platform_power_on(struct kbase_device *kbdev);

#endif				/* _KBASE_PLATFORM_H_ */
