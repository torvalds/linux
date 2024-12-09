/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_DEVICE_H
#define PVR_DEVICE_H

#include "pvr_ccb.h"
#include "pvr_device_info.h"
#include "pvr_fw.h"
#include "pvr_params.h"
#include "pvr_rogue_fwif_stream.h"
#include "pvr_stream.h"

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_mm.h>

#include <linux/bits.h>
#include <linux/compiler_attributes.h>
#include <linux/compiler_types.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/math.h>
#include <linux/mutex.h>
#include <linux/spinlock_types.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/xarray.h>

/* Forward declaration from <linux/clk.h>. */
struct clk;

/* Forward declaration from <linux/firmware.h>. */
struct firmware;

/**
 * struct pvr_gpu_id - Hardware GPU ID information for a PowerVR device
 * @b: Branch ID.
 * @v: Version ID.
 * @n: Number of scalable units.
 * @c: Config ID.
 */
struct pvr_gpu_id {
	u16 b, v, n, c;
};

/**
 * struct pvr_fw_version - Firmware version information
 * @major: Major version number.
 * @minor: Minor version number.
 */
struct pvr_fw_version {
	u16 major, minor;
};

/**
 * struct pvr_device - powervr-specific wrapper for &struct drm_device
 */
struct pvr_device {
	/**
	 * @base: The underlying &struct drm_device.
	 *
	 * Do not access this member directly, instead call
	 * from_pvr_device().
	 */
	struct drm_device base;

	/** @gpu_id: GPU ID detected at runtime. */
	struct pvr_gpu_id gpu_id;

	/**
	 * @features: Hardware feature information.
	 *
	 * Do not access this member directly, instead use PVR_HAS_FEATURE()
	 * or PVR_FEATURE_VALUE() macros.
	 */
	struct pvr_device_features features;

	/**
	 * @quirks: Hardware quirk information.
	 *
	 * Do not access this member directly, instead use PVR_HAS_QUIRK().
	 */
	struct pvr_device_quirks quirks;

	/**
	 * @enhancements: Hardware enhancement information.
	 *
	 * Do not access this member directly, instead use
	 * PVR_HAS_ENHANCEMENT().
	 */
	struct pvr_device_enhancements enhancements;

	/** @fw_version: Firmware version detected at runtime. */
	struct pvr_fw_version fw_version;

	/** @regs_resource: Resource representing device control registers. */
	struct resource *regs_resource;

	/**
	 * @regs: Device control registers.
	 *
	 * These are mapped into memory when the device is initialized; that
	 * location is where this pointer points.
	 */
	void __iomem *regs;

	/**
	 * @core_clk: General core clock.
	 *
	 * This is the primary clock used by the entire GPU core.
	 */
	struct clk *core_clk;

	/**
	 * @sys_clk: Optional system bus clock.
	 *
	 * This may be used on some platforms to provide an independent clock to the SoC Interface
	 * (SOCIF). If present, this needs to be enabled/disabled together with @core_clk.
	 */
	struct clk *sys_clk;

	/**
	 * @mem_clk: Optional memory clock.
	 *
	 * This may be used on some platforms to provide an independent clock to the Memory
	 * Interface (MEMIF). If present, this needs to be enabled/disabled together with @core_clk.
	 */
	struct clk *mem_clk;

	/** @irq: IRQ number. */
	int irq;

	/** @fwccb: Firmware CCB. */
	struct pvr_ccb fwccb;

	/**
	 * @kernel_vm_ctx: Virtual memory context used for kernel mappings.
	 *
	 * This is used for mappings in the firmware address region when a META firmware processor
	 * is in use.
	 *
	 * When a MIPS firmware processor is in use, this will be %NULL.
	 */
	struct pvr_vm_context *kernel_vm_ctx;

	/** @fw_dev: Firmware related data. */
	struct pvr_fw_device fw_dev;

	/**
	 * @params: Device-specific parameters.
	 *
	 *          The values of these parameters are initialized from the
	 *          defaults specified as module parameters. They may be
	 *          modified at runtime via debugfs (if enabled).
	 */
	struct pvr_device_params params;

	/** @stream_musthave_quirks: Bit array of "must-have" quirks for stream commands. */
	u32 stream_musthave_quirks[PVR_STREAM_TYPE_MAX][PVR_STREAM_EXTHDR_TYPE_MAX];

	/**
	 * @mmu_flush_cache_flags: Records which MMU caches require flushing
	 * before submitting the next job.
	 */
	atomic_t mmu_flush_cache_flags;

	/**
	 * @ctx_ids: Array of contexts belonging to this device. Array members
	 *           are of type "struct pvr_context *".
	 *
	 * This array is used to allocate IDs used by the firmware.
	 */
	struct xarray ctx_ids;

	/**
	 * @free_list_ids: Array of free lists belonging to this device. Array members
	 *                 are of type "struct pvr_free_list *".
	 *
	 * This array is used to allocate IDs used by the firmware.
	 */
	struct xarray free_list_ids;

	/**
	 * @job_ids: Array of jobs belonging to this device. Array members
	 *           are of type "struct pvr_job *".
	 */
	struct xarray job_ids;

	/**
	 * @queues: Queue-related fields.
	 */
	struct {
		/** @queues.active: Active queue list. */
		struct list_head active;

		/** @queues.idle: Idle queue list. */
		struct list_head idle;

		/** @queues.lock: Lock protecting access to the active/idle
		 *  lists. */
		struct mutex lock;
	} queues;

	/**
	 * @watchdog: Watchdog for communications with firmware.
	 */
	struct {
		/** @watchdog.work: Work item for watchdog callback. */
		struct delayed_work work;

		/**
		 * @watchdog.old_kccb_cmds_executed: KCCB command execution
		 * count at last watchdog poll.
		 */
		u32 old_kccb_cmds_executed;

		/**
		 * @watchdog.kccb_stall_count: Number of watchdog polls
		 * KCCB has been stalled for.
		 */
		u32 kccb_stall_count;
	} watchdog;

	/**
	 * @kccb: Circular buffer for communications with firmware.
	 */
	struct {
		/** @kccb.ccb: Kernel CCB. */
		struct pvr_ccb ccb;

		/** @kccb.rtn_q: Waitqueue for KCCB command return waiters. */
		wait_queue_head_t rtn_q;

		/** @kccb.rtn_obj: Object representing KCCB return slots. */
		struct pvr_fw_object *rtn_obj;

		/**
		 * @kccb.rtn: Pointer to CPU mapping of KCCB return slots.
		 * Must be accessed by READ_ONCE()/WRITE_ONCE().
		 */
		u32 *rtn;

		/** @kccb.slot_count: Total number of KCCB slots available. */
		u32 slot_count;

		/** @kccb.reserved_count: Number of KCCB slots reserved for
		 *  future use. */
		u32 reserved_count;

		/**
		 * @kccb.waiters: List of KCCB slot waiters.
		 */
		struct list_head waiters;

		/** @kccb.fence_ctx: KCCB fence context. */
		struct {
			/** @kccb.fence_ctx.id: KCCB fence context ID
			 *  allocated with dma_fence_context_alloc(). */
			u64 id;

			/** @kccb.fence_ctx.seqno: Sequence number incremented
			 *  each time a fence is created. */
			atomic_t seqno;

			/**
			 * @kccb.fence_ctx.lock: Lock used to synchronize
			 * access to fences allocated by this context.
			 */
			spinlock_t lock;
		} fence_ctx;
	} kccb;

	/**
	 * @lost: %true if the device has been lost.
	 *
	 * This variable is set if the device has become irretrievably unavailable, e.g. if the
	 * firmware processor has stopped responding and can not be revived via a hard reset.
	 */
	bool lost;

	/**
	 * @reset_sem: Reset semaphore.
	 *
	 * GPU reset code will lock this for writing. Any code that submits commands to the firmware
	 * that isn't in an IRQ handler or on the scheduler workqueue must lock this for reading.
	 * Once this has been successfully locked, &pvr_dev->lost _must_ be checked, and -%EIO must
	 * be returned if it is set.
	 */
	struct rw_semaphore reset_sem;

	/** @sched_wq: Workqueue for schedulers. */
	struct workqueue_struct *sched_wq;

	/**
	 * @ctx_list_lock: Lock to be held when accessing the context list in
	 *  struct pvr_file.
	 */
	spinlock_t ctx_list_lock;
};

/**
 * struct pvr_file - powervr-specific data to be assigned to &struct
 * drm_file.driver_priv
 */
struct pvr_file {
	/**
	 * @file: A reference to the parent &struct drm_file.
	 *
	 * Do not access this member directly, instead call from_pvr_file().
	 */
	struct drm_file *file;

	/**
	 * @pvr_dev: A reference to the powervr-specific wrapper for the
	 * associated device. Saves on repeated calls to to_pvr_device().
	 */
	struct pvr_device *pvr_dev;

	/**
	 * @ctx_handles: Array of contexts belonging to this file. Array members
	 * are of type "struct pvr_context *".
	 *
	 * This array is used to allocate handles returned to userspace.
	 */
	struct xarray ctx_handles;

	/**
	 * @free_list_handles: Array of free lists belonging to this file. Array
	 * members are of type "struct pvr_free_list *".
	 *
	 * This array is used to allocate handles returned to userspace.
	 */
	struct xarray free_list_handles;

	/**
	 * @hwrt_handles: Array of HWRT datasets belonging to this file. Array
	 * members are of type "struct pvr_hwrt_dataset *".
	 *
	 * This array is used to allocate handles returned to userspace.
	 */
	struct xarray hwrt_handles;

	/**
	 * @vm_ctx_handles: Array of VM contexts belonging to this file. Array
	 * members are of type "struct pvr_vm_context *".
	 *
	 * This array is used to allocate handles returned to userspace.
	 */
	struct xarray vm_ctx_handles;

	/** @contexts: PVR context list. */
	struct list_head contexts;
};

/**
 * PVR_HAS_FEATURE() - Tests whether a PowerVR device has a given feature
 * @pvr_dev: [IN] Target PowerVR device.
 * @feature: [IN] Hardware feature name.
 *
 * Feature names are derived from those found in &struct pvr_device_features by
 * dropping the 'has_' prefix, which is applied by this macro.
 *
 * Return:
 *  * true if the named feature is present in the hardware
 *  * false if the named feature is not present in the hardware
 */
#define PVR_HAS_FEATURE(pvr_dev, feature) ((pvr_dev)->features.has_##feature)

/**
 * PVR_FEATURE_VALUE() - Gets a PowerVR device feature value
 * @pvr_dev: [IN] Target PowerVR device.
 * @feature: [IN] Feature name.
 * @value_out: [OUT] Feature value.
 *
 * This macro will get a feature value for those features that have values.
 * If the feature is not present, nothing will be stored to @value_out.
 *
 * Feature names are derived from those found in &struct pvr_device_features by
 * dropping the 'has_' prefix.
 *
 * Return:
 *  * 0 on success, or
 *  * -%EINVAL if the named feature is not present in the hardware
 */
#define PVR_FEATURE_VALUE(pvr_dev, feature, value_out)             \
	({                                                         \
		struct pvr_device *_pvr_dev = pvr_dev;             \
		int _ret = -EINVAL;                                \
		if (_pvr_dev->features.has_##feature) {            \
			*(value_out) = _pvr_dev->features.feature; \
			_ret = 0;                                  \
		}                                                  \
		_ret;                                              \
	})

/**
 * PVR_HAS_QUIRK() - Tests whether a physical device has a given quirk
 * @pvr_dev: [IN] Target PowerVR device.
 * @quirk: [IN] Hardware quirk name.
 *
 * Quirk numbers are derived from those found in #pvr_device_quirks by
 * dropping the 'has_brn' prefix, which is applied by this macro.
 *
 * Returns
 *  * true if the quirk is present in the hardware, or
 *  * false if the quirk is not present in the hardware.
 */
#define PVR_HAS_QUIRK(pvr_dev, quirk) ((pvr_dev)->quirks.has_brn##quirk)

/**
 * PVR_HAS_ENHANCEMENT() - Tests whether a physical device has a given
 *                         enhancement
 * @pvr_dev: [IN] Target PowerVR device.
 * @enhancement: [IN] Hardware enhancement name.
 *
 * Enhancement numbers are derived from those found in #pvr_device_enhancements
 * by dropping the 'has_ern' prefix, which is applied by this macro.
 *
 * Returns
 *  * true if the enhancement is present in the hardware, or
 *  * false if the enhancement is not present in the hardware.
 */
#define PVR_HAS_ENHANCEMENT(pvr_dev, enhancement) ((pvr_dev)->enhancements.has_ern##enhancement)

#define from_pvr_device(pvr_dev) (&(pvr_dev)->base)

#define to_pvr_device(drm_dev) container_of_const(drm_dev, struct pvr_device, base)

#define from_pvr_file(pvr_file) ((pvr_file)->file)

#define to_pvr_file(file) ((file)->driver_priv)

/**
 * PVR_PACKED_BVNC() - Packs B, V, N and C values into a 64-bit unsigned integer
 * @b: Branch ID.
 * @v: Version ID.
 * @n: Number of scalable units.
 * @c: Config ID.
 *
 * The packed layout is as follows:
 *
 *    +--------+--------+--------+-------+
 *    | 63..48 | 47..32 | 31..16 | 15..0 |
 *    +========+========+========+=======+
 *    | B      | V      | N      | C     |
 *    +--------+--------+--------+-------+
 *
 * pvr_gpu_id_to_packed_bvnc() should be used instead of this macro when a
 * &struct pvr_gpu_id is available in order to ensure proper type checking.
 *
 * Return: Packed BVNC.
 */
/* clang-format off */
#define PVR_PACKED_BVNC(b, v, n, c) \
	((((u64)(b) & GENMASK_ULL(15, 0)) << 48) | \
	 (((u64)(v) & GENMASK_ULL(15, 0)) << 32) | \
	 (((u64)(n) & GENMASK_ULL(15, 0)) << 16) | \
	 (((u64)(c) & GENMASK_ULL(15, 0)) <<  0))
/* clang-format on */

/**
 * pvr_gpu_id_to_packed_bvnc() - Packs B, V, N and C values into a 64-bit
 * unsigned integer
 * @gpu_id: GPU ID.
 *
 * The packed layout is as follows:
 *
 *    +--------+--------+--------+-------+
 *    | 63..48 | 47..32 | 31..16 | 15..0 |
 *    +========+========+========+=======+
 *    | B      | V      | N      | C     |
 *    +--------+--------+--------+-------+
 *
 * This should be used in preference to PVR_PACKED_BVNC() when a &struct
 * pvr_gpu_id is available in order to ensure proper type checking.
 *
 * Return: Packed BVNC.
 */
static __always_inline u64
pvr_gpu_id_to_packed_bvnc(struct pvr_gpu_id *gpu_id)
{
	return PVR_PACKED_BVNC(gpu_id->b, gpu_id->v, gpu_id->n, gpu_id->c);
}

static __always_inline void
packed_bvnc_to_pvr_gpu_id(u64 bvnc, struct pvr_gpu_id *gpu_id)
{
	gpu_id->b = (bvnc & GENMASK_ULL(63, 48)) >> 48;
	gpu_id->v = (bvnc & GENMASK_ULL(47, 32)) >> 32;
	gpu_id->n = (bvnc & GENMASK_ULL(31, 16)) >> 16;
	gpu_id->c = bvnc & GENMASK_ULL(15, 0);
}

int pvr_device_init(struct pvr_device *pvr_dev);
void pvr_device_fini(struct pvr_device *pvr_dev);
void pvr_device_reset(struct pvr_device *pvr_dev);

bool
pvr_device_has_uapi_quirk(struct pvr_device *pvr_dev, u32 quirk);
bool
pvr_device_has_uapi_enhancement(struct pvr_device *pvr_dev, u32 enhancement);
bool
pvr_device_has_feature(struct pvr_device *pvr_dev, u32 feature);

/**
 * PVR_CR_FIELD_GET() - Extract a single field from a PowerVR control register
 * @val: Value of the target register.
 * @field: Field specifier, as defined in "pvr_rogue_cr_defs.h".
 *
 * Return: The extracted field.
 */
#define PVR_CR_FIELD_GET(val, field) FIELD_GET(~ROGUE_CR_##field##_CLRMSK, val)

/**
 * pvr_cr_read32() - Read a 32-bit register from a PowerVR device
 * @pvr_dev: Target PowerVR device.
 * @reg: Target register.
 *
 * Return: The value of the requested register.
 */
static __always_inline u32
pvr_cr_read32(struct pvr_device *pvr_dev, u32 reg)
{
	return ioread32(pvr_dev->regs + reg);
}

/**
 * pvr_cr_read64() - Read a 64-bit register from a PowerVR device
 * @pvr_dev: Target PowerVR device.
 * @reg: Target register.
 *
 * Return: The value of the requested register.
 */
static __always_inline u64
pvr_cr_read64(struct pvr_device *pvr_dev, u32 reg)
{
	return ioread64(pvr_dev->regs + reg);
}

/**
 * pvr_cr_write32() - Write to a 32-bit register in a PowerVR device
 * @pvr_dev: Target PowerVR device.
 * @reg: Target register.
 * @val: Value to write.
 */
static __always_inline void
pvr_cr_write32(struct pvr_device *pvr_dev, u32 reg, u32 val)
{
	iowrite32(val, pvr_dev->regs + reg);
}

/**
 * pvr_cr_write64() - Write to a 64-bit register in a PowerVR device
 * @pvr_dev: Target PowerVR device.
 * @reg: Target register.
 * @val: Value to write.
 */
static __always_inline void
pvr_cr_write64(struct pvr_device *pvr_dev, u32 reg, u64 val)
{
	iowrite64(val, pvr_dev->regs + reg);
}

/**
 * pvr_cr_poll_reg32() - Wait for a 32-bit register to match a given value by
 *                       polling
 * @pvr_dev: Target PowerVR device.
 * @reg_addr: Address of register.
 * @reg_value: Expected register value (after masking).
 * @reg_mask: Mask of bits valid for comparison with @reg_value.
 * @timeout_usec: Timeout length, in us.
 *
 * Returns:
 *  * 0 on success, or
 *  * -%ETIMEDOUT on timeout.
 */
static __always_inline int
pvr_cr_poll_reg32(struct pvr_device *pvr_dev, u32 reg_addr, u32 reg_value,
		  u32 reg_mask, u64 timeout_usec)
{
	u32 value;

	return readl_poll_timeout(pvr_dev->regs + reg_addr, value,
		(value & reg_mask) == reg_value, 0, timeout_usec);
}

/**
 * pvr_cr_poll_reg64() - Wait for a 64-bit register to match a given value by
 *                       polling
 * @pvr_dev: Target PowerVR device.
 * @reg_addr: Address of register.
 * @reg_value: Expected register value (after masking).
 * @reg_mask: Mask of bits valid for comparison with @reg_value.
 * @timeout_usec: Timeout length, in us.
 *
 * Returns:
 *  * 0 on success, or
 *  * -%ETIMEDOUT on timeout.
 */
static __always_inline int
pvr_cr_poll_reg64(struct pvr_device *pvr_dev, u32 reg_addr, u64 reg_value,
		  u64 reg_mask, u64 timeout_usec)
{
	u64 value;

	return readq_poll_timeout(pvr_dev->regs + reg_addr, value,
		(value & reg_mask) == reg_value, 0, timeout_usec);
}

/**
 * pvr_round_up_to_cacheline_size() - Round up a provided size to be cacheline
 *                                    aligned
 * @pvr_dev: Target PowerVR device.
 * @size: Initial size, in bytes.
 *
 * Returns:
 *  * Size aligned to cacheline size.
 */
static __always_inline size_t
pvr_round_up_to_cacheline_size(struct pvr_device *pvr_dev, size_t size)
{
	u16 slc_cacheline_size_bits = 0;
	u16 slc_cacheline_size_bytes;

	WARN_ON(!PVR_HAS_FEATURE(pvr_dev, slc_cache_line_size_bits));
	PVR_FEATURE_VALUE(pvr_dev, slc_cache_line_size_bits,
			  &slc_cacheline_size_bits);
	slc_cacheline_size_bytes = slc_cacheline_size_bits / 8;

	return round_up(size, slc_cacheline_size_bytes);
}

/**
 * DOC: IOCTL validation helpers
 *
 * To validate the constraints imposed on IOCTL argument structs, a collection
 * of macros and helper functions exist in ``pvr_device.h``.
 *
 * Of the current helpers, it should only be necessary to call
 * PVR_IOCTL_UNION_PADDING_CHECK() directly. This macro should be used once in
 * every code path which extracts a union member from a struct passed from
 * userspace.
 */

/**
 * pvr_ioctl_union_padding_check() - Validate that the implicit padding between
 * the end of a union member and the end of the union itself is zeroed.
 * @instance: Pointer to the instance of the struct to validate.
 * @union_offset: Offset into the type of @instance of the target union. Must
 * be 64-bit aligned.
 * @union_size: Size of the target union in the type of @instance. Must be
 * 64-bit aligned.
 * @member_size: Size of the target member in the target union specified by
 * @union_offset and @union_size. It is assumed that the offset of the target
 * member is zero relative to @union_offset. Must be 64-bit aligned.
 *
 * You probably want to use PVR_IOCTL_UNION_PADDING_CHECK() instead of calling
 * this function directly, since that macro abstracts away much of the setup,
 * and also provides some static validation. See its docs for details.
 *
 * Return:
 *  * %true if every byte between the end of the used member of the union and
 *    the end of that union is zeroed, or
 *  * %false otherwise.
 */
static __always_inline bool
pvr_ioctl_union_padding_check(void *instance, size_t union_offset,
			      size_t union_size, size_t member_size)
{
	/*
	 * void pointer arithmetic is technically illegal - cast to a byte
	 * pointer so this addition works safely.
	 */
	void *padding_start = ((u8 *)instance) + union_offset + member_size;
	size_t padding_size = union_size - member_size;

	return mem_is_zero(padding_start, padding_size);
}

/**
 * PVR_STATIC_ASSERT_64BIT_ALIGNED() - Inline assertion for 64-bit alignment.
 * @static_expr_: Target expression to evaluate.
 *
 * If @static_expr_ does not evaluate to a constant integer which would be a
 * 64-bit aligned address (i.e. a multiple of 8), compilation will fail.
 *
 * Return:
 * The value of @static_expr_.
 */
#define PVR_STATIC_ASSERT_64BIT_ALIGNED(static_expr_)                     \
	({                                                                \
		static_assert(((static_expr_) & (sizeof(u64) - 1)) == 0); \
		(static_expr_);                                           \
	})

/**
 * PVR_IOCTL_UNION_PADDING_CHECK() - Validate that the implicit padding between
 * the end of a union member and the end of the union itself is zeroed.
 * @struct_instance_: An expression which evaluates to a pointer to a UAPI data
 * struct.
 * @union_: The name of the union member of @struct_instance_ to check. If the
 * union member is nested within the type of @struct_instance_, this may
 * contain the member access operator (".").
 * @member_: The name of the member of @union_ to assess.
 *
 * This is a wrapper around pvr_ioctl_union_padding_check() which performs
 * alignment checks and simplifies things for the caller.
 *
 * Return:
 *  * %true if every byte in @struct_instance_ between the end of @member_ and
 *    the end of @union_ is zeroed, or
 *  * %false otherwise.
 */
#define PVR_IOCTL_UNION_PADDING_CHECK(struct_instance_, union_, member_)     \
	({                                                                   \
		typeof(struct_instance_) __instance = (struct_instance_);    \
		size_t __union_offset = PVR_STATIC_ASSERT_64BIT_ALIGNED(     \
			offsetof(typeof(*__instance), union_));              \
		size_t __union_size = PVR_STATIC_ASSERT_64BIT_ALIGNED(       \
			sizeof(__instance->union_));                         \
		size_t __member_size = PVR_STATIC_ASSERT_64BIT_ALIGNED(      \
			sizeof(__instance->union_.member_));                 \
		pvr_ioctl_union_padding_check(__instance, __union_offset,    \
					      __union_size, __member_size);  \
	})

#define PVR_FW_PROCESSOR_TYPE_META  0
#define PVR_FW_PROCESSOR_TYPE_MIPS  1
#define PVR_FW_PROCESSOR_TYPE_RISCV 2

#endif /* PVR_DEVICE_H */
