/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_DEVICE_H
#define PVR_DEVICE_H

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
	 *           associated device. Saves on repeated calls to
	 *           to_pvr_device().
	 */
	struct pvr_device *pvr_dev;
};

#define from_pvr_device(pvr_dev) (&(pvr_dev)->base)

#define to_pvr_device(drm_dev) container_of_const(drm_dev, struct pvr_device, base)

#define from_pvr_file(pvr_file) ((pvr_file)->file)

#define to_pvr_file(file) ((file)->driver_priv)

int pvr_device_init(struct pvr_device *pvr_dev);
void pvr_device_fini(struct pvr_device *pvr_dev);

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

	return !memchr_inv(padding_start, 0, padding_size);
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

#endif /* PVR_DEVICE_H */
