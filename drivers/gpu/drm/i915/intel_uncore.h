/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#ifndef __INTEL_UNCORE_H__
#define __INTEL_UNCORE_H__

#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <linux/hrtimer.h>
#include <linux/io-64-nonatomic-lo-hi.h>

#include "i915_reg.h"

struct drm_i915_private;
struct intel_runtime_pm;
struct intel_uncore;
struct intel_gt;

struct intel_uncore_mmio_debug {
	spinlock_t lock; /** lock is also taken in irq contexts. */
	int unclaimed_mmio_check;
	int saved_mmio_check;
	u32 suspend_count;
};

enum forcewake_domain_id {
	FW_DOMAIN_ID_RENDER = 0,
	FW_DOMAIN_ID_GT,        /* also includes blitter engine */
	FW_DOMAIN_ID_MEDIA,
	FW_DOMAIN_ID_MEDIA_VDBOX0,
	FW_DOMAIN_ID_MEDIA_VDBOX1,
	FW_DOMAIN_ID_MEDIA_VDBOX2,
	FW_DOMAIN_ID_MEDIA_VDBOX3,
	FW_DOMAIN_ID_MEDIA_VEBOX0,
	FW_DOMAIN_ID_MEDIA_VEBOX1,

	FW_DOMAIN_ID_COUNT
};

enum forcewake_domains {
	FORCEWAKE_RENDER	= BIT(FW_DOMAIN_ID_RENDER),
	FORCEWAKE_GT		= BIT(FW_DOMAIN_ID_GT),
	FORCEWAKE_MEDIA		= BIT(FW_DOMAIN_ID_MEDIA),
	FORCEWAKE_MEDIA_VDBOX0	= BIT(FW_DOMAIN_ID_MEDIA_VDBOX0),
	FORCEWAKE_MEDIA_VDBOX1	= BIT(FW_DOMAIN_ID_MEDIA_VDBOX1),
	FORCEWAKE_MEDIA_VDBOX2	= BIT(FW_DOMAIN_ID_MEDIA_VDBOX2),
	FORCEWAKE_MEDIA_VDBOX3	= BIT(FW_DOMAIN_ID_MEDIA_VDBOX3),
	FORCEWAKE_MEDIA_VEBOX0	= BIT(FW_DOMAIN_ID_MEDIA_VEBOX0),
	FORCEWAKE_MEDIA_VEBOX1	= BIT(FW_DOMAIN_ID_MEDIA_VEBOX1),

	FORCEWAKE_ALL = BIT(FW_DOMAIN_ID_COUNT) - 1
};

struct intel_uncore_funcs {
	void (*force_wake_get)(struct intel_uncore *uncore,
			       enum forcewake_domains domains);
	void (*force_wake_put)(struct intel_uncore *uncore,
			       enum forcewake_domains domains);

	enum forcewake_domains (*read_fw_domains)(struct intel_uncore *uncore,
						  i915_reg_t r);
	enum forcewake_domains (*write_fw_domains)(struct intel_uncore *uncore,
						   i915_reg_t r);

	u8 (*mmio_readb)(struct intel_uncore *uncore,
			 i915_reg_t r, bool trace);
	u16 (*mmio_readw)(struct intel_uncore *uncore,
			  i915_reg_t r, bool trace);
	u32 (*mmio_readl)(struct intel_uncore *uncore,
			  i915_reg_t r, bool trace);
	u64 (*mmio_readq)(struct intel_uncore *uncore,
			  i915_reg_t r, bool trace);

	void (*mmio_writeb)(struct intel_uncore *uncore,
			    i915_reg_t r, u8 val, bool trace);
	void (*mmio_writew)(struct intel_uncore *uncore,
			    i915_reg_t r, u16 val, bool trace);
	void (*mmio_writel)(struct intel_uncore *uncore,
			    i915_reg_t r, u32 val, bool trace);
};

struct intel_forcewake_range {
	u32 start;
	u32 end;

	enum forcewake_domains domains;
};

struct intel_uncore {
	void __iomem *regs;

	struct drm_i915_private *i915;
	struct intel_runtime_pm *rpm;

	spinlock_t lock; /** lock is also taken in irq contexts. */

	unsigned int flags;
#define UNCORE_HAS_FORCEWAKE		BIT(0)
#define UNCORE_HAS_FPGA_DBG_UNCLAIMED	BIT(1)
#define UNCORE_HAS_DBG_UNCLAIMED	BIT(2)
#define UNCORE_HAS_FIFO			BIT(3)

	const struct intel_forcewake_range *fw_domains_table;
	unsigned int fw_domains_table_entries;

	struct notifier_block pmic_bus_access_nb;
	struct intel_uncore_funcs funcs;

	unsigned int fifo_count;

	enum forcewake_domains fw_domains;
	enum forcewake_domains fw_domains_active;
	enum forcewake_domains fw_domains_timer;
	enum forcewake_domains fw_domains_saved; /* user domains saved for S3 */

	struct intel_uncore_forcewake_domain {
		struct intel_uncore *uncore;
		enum forcewake_domain_id id;
		enum forcewake_domains mask;
		unsigned int wake_count;
		bool active;
		struct hrtimer timer;
		u32 __iomem *reg_set;
		u32 __iomem *reg_ack;
	} *fw_domain[FW_DOMAIN_ID_COUNT];

	unsigned int user_forcewake_count;

	struct intel_uncore_mmio_debug *debug;
};

/* Iterate over initialised fw domains */
#define for_each_fw_domain_masked(domain__, mask__, uncore__, tmp__) \
	for (tmp__ = (mask__); tmp__ ;) \
		for_each_if(domain__ = (uncore__)->fw_domain[__mask_next_bit(tmp__)])

#define for_each_fw_domain(domain__, uncore__, tmp__) \
	for_each_fw_domain_masked(domain__, (uncore__)->fw_domains, uncore__, tmp__)

static inline bool
intel_uncore_has_forcewake(const struct intel_uncore *uncore)
{
	return uncore->flags & UNCORE_HAS_FORCEWAKE;
}

static inline bool
intel_uncore_has_fpga_dbg_unclaimed(const struct intel_uncore *uncore)
{
	return uncore->flags & UNCORE_HAS_FPGA_DBG_UNCLAIMED;
}

static inline bool
intel_uncore_has_dbg_unclaimed(const struct intel_uncore *uncore)
{
	return uncore->flags & UNCORE_HAS_DBG_UNCLAIMED;
}

static inline bool
intel_uncore_has_fifo(const struct intel_uncore *uncore)
{
	return uncore->flags & UNCORE_HAS_FIFO;
}

void
intel_uncore_mmio_debug_init_early(struct intel_uncore_mmio_debug *mmio_debug);
void intel_uncore_init_early(struct intel_uncore *uncore,
			     struct drm_i915_private *i915);
int intel_uncore_init_mmio(struct intel_uncore *uncore);
void intel_uncore_prune_engine_fw_domains(struct intel_uncore *uncore,
					  struct intel_gt *gt);
bool intel_uncore_unclaimed_mmio(struct intel_uncore *uncore);
bool intel_uncore_arm_unclaimed_mmio_detection(struct intel_uncore *uncore);
void intel_uncore_fini_mmio(struct intel_uncore *uncore);
void intel_uncore_suspend(struct intel_uncore *uncore);
void intel_uncore_resume_early(struct intel_uncore *uncore);
void intel_uncore_runtime_resume(struct intel_uncore *uncore);

void assert_forcewakes_inactive(struct intel_uncore *uncore);
void assert_forcewakes_active(struct intel_uncore *uncore,
			      enum forcewake_domains fw_domains);
const char *intel_uncore_forcewake_domain_to_str(const enum forcewake_domain_id id);

enum forcewake_domains
intel_uncore_forcewake_for_reg(struct intel_uncore *uncore,
			       i915_reg_t reg, unsigned int op);
#define FW_REG_READ  (1)
#define FW_REG_WRITE (2)

void intel_uncore_forcewake_get(struct intel_uncore *uncore,
				enum forcewake_domains domains);
void intel_uncore_forcewake_put(struct intel_uncore *uncore,
				enum forcewake_domains domains);
void intel_uncore_forcewake_flush(struct intel_uncore *uncore,
				  enum forcewake_domains fw_domains);

/*
 * Like above but the caller must manage the uncore.lock itself.
 * Must be used with intel_uncore_read_fw() and friends.
 */
void intel_uncore_forcewake_get__locked(struct intel_uncore *uncore,
					enum forcewake_domains domains);
void intel_uncore_forcewake_put__locked(struct intel_uncore *uncore,
					enum forcewake_domains domains);

void intel_uncore_forcewake_user_get(struct intel_uncore *uncore);
void intel_uncore_forcewake_user_put(struct intel_uncore *uncore);

int __intel_wait_for_register(struct intel_uncore *uncore,
			      i915_reg_t reg,
			      u32 mask,
			      u32 value,
			      unsigned int fast_timeout_us,
			      unsigned int slow_timeout_ms,
			      u32 *out_value);
static inline int
intel_wait_for_register(struct intel_uncore *uncore,
			i915_reg_t reg,
			u32 mask,
			u32 value,
			unsigned int timeout_ms)
{
	return __intel_wait_for_register(uncore, reg, mask, value, 2,
					 timeout_ms, NULL);
}

int __intel_wait_for_register_fw(struct intel_uncore *uncore,
				 i915_reg_t reg,
				 u32 mask,
				 u32 value,
				 unsigned int fast_timeout_us,
				 unsigned int slow_timeout_ms,
				 u32 *out_value);
static inline int
intel_wait_for_register_fw(struct intel_uncore *uncore,
			   i915_reg_t reg,
			   u32 mask,
			   u32 value,
			       unsigned int timeout_ms)
{
	return __intel_wait_for_register_fw(uncore, reg, mask, value,
					    2, timeout_ms, NULL);
}

/* register access functions */
#define __raw_read(x__, s__) \
static inline u##x__ __raw_uncore_read##x__(const struct intel_uncore *uncore, \
					    i915_reg_t reg) \
{ \
	return read##s__(uncore->regs + i915_mmio_reg_offset(reg)); \
}

#define __raw_write(x__, s__) \
static inline void __raw_uncore_write##x__(const struct intel_uncore *uncore, \
					   i915_reg_t reg, u##x__ val) \
{ \
	write##s__(val, uncore->regs + i915_mmio_reg_offset(reg)); \
}
__raw_read(8, b)
__raw_read(16, w)
__raw_read(32, l)
__raw_read(64, q)

__raw_write(8, b)
__raw_write(16, w)
__raw_write(32, l)
__raw_write(64, q)

#undef __raw_read
#undef __raw_write

#define __uncore_read(name__, x__, s__, trace__) \
static inline u##x__ intel_uncore_##name__(struct intel_uncore *uncore, \
					   i915_reg_t reg) \
{ \
	return uncore->funcs.mmio_read##s__(uncore, reg, (trace__)); \
}

#define __uncore_write(name__, x__, s__, trace__) \
static inline void intel_uncore_##name__(struct intel_uncore *uncore, \
					 i915_reg_t reg, u##x__ val) \
{ \
	uncore->funcs.mmio_write##s__(uncore, reg, val, (trace__)); \
}

__uncore_read(read8, 8, b, true)
__uncore_read(read16, 16, w, true)
__uncore_read(read, 32, l, true)
__uncore_read(read16_notrace, 16, w, false)
__uncore_read(read_notrace, 32, l, false)

__uncore_write(write8, 8, b, true)
__uncore_write(write16, 16, w, true)
__uncore_write(write, 32, l, true)
__uncore_write(write_notrace, 32, l, false)

/* Be very careful with read/write 64-bit values. On 32-bit machines, they
 * will be implemented using 2 32-bit writes in an arbitrary order with
 * an arbitrary delay between them. This can cause the hardware to
 * act upon the intermediate value, possibly leading to corruption and
 * machine death. For this reason we do not support I915_WRITE64, or
 * uncore->funcs.mmio_writeq.
 *
 * When reading a 64-bit value as two 32-bit values, the delay may cause
 * the two reads to mismatch, e.g. a timestamp overflowing. Also note that
 * occasionally a 64-bit register does not actually support a full readq
 * and must be read using two 32-bit reads.
 *
 * You have been warned.
 */
__uncore_read(read64, 64, q, true)

static inline u64
intel_uncore_read64_2x32(struct intel_uncore *uncore,
			 i915_reg_t lower_reg, i915_reg_t upper_reg)
{
	u32 upper, lower, old_upper, loop = 0;
	upper = intel_uncore_read(uncore, upper_reg);
	do {
		old_upper = upper;
		lower = intel_uncore_read(uncore, lower_reg);
		upper = intel_uncore_read(uncore, upper_reg);
	} while (upper != old_upper && loop++ < 2);
	return (u64)upper << 32 | lower;
}

#define intel_uncore_posting_read(...) ((void)intel_uncore_read_notrace(__VA_ARGS__))
#define intel_uncore_posting_read16(...) ((void)intel_uncore_read16_notrace(__VA_ARGS__))

#undef __uncore_read
#undef __uncore_write

/* These are untraced mmio-accessors that are only valid to be used inside
 * critical sections, such as inside IRQ handlers, where forcewake is explicitly
 * controlled.
 *
 * Think twice, and think again, before using these.
 *
 * As an example, these accessors can possibly be used between:
 *
 * spin_lock_irq(&uncore->lock);
 * intel_uncore_forcewake_get__locked();
 *
 * and
 *
 * intel_uncore_forcewake_put__locked();
 * spin_unlock_irq(&uncore->lock);
 *
 *
 * Note: some registers may not need forcewake held, so
 * intel_uncore_forcewake_{get,put} can be omitted, see
 * intel_uncore_forcewake_for_reg().
 *
 * Certain architectures will die if the same cacheline is concurrently accessed
 * by different clients (e.g. on Ivybridge). Access to registers should
 * therefore generally be serialised, by either the dev_priv->uncore.lock or
 * a more localised lock guarding all access to that bank of registers.
 */
#define intel_uncore_read_fw(...) __raw_uncore_read32(__VA_ARGS__)
#define intel_uncore_write_fw(...) __raw_uncore_write32(__VA_ARGS__)
#define intel_uncore_write64_fw(...) __raw_uncore_write64(__VA_ARGS__)
#define intel_uncore_posting_read_fw(...) ((void)intel_uncore_read_fw(__VA_ARGS__))

static inline void intel_uncore_rmw(struct intel_uncore *uncore,
				    i915_reg_t reg, u32 clear, u32 set)
{
	u32 old, val;

	old = intel_uncore_read(uncore, reg);
	val = (old & ~clear) | set;
	if (val != old)
		intel_uncore_write(uncore, reg, val);
}

static inline void intel_uncore_rmw_fw(struct intel_uncore *uncore,
				       i915_reg_t reg, u32 clear, u32 set)
{
	u32 old, val;

	old = intel_uncore_read_fw(uncore, reg);
	val = (old & ~clear) | set;
	if (val != old)
		intel_uncore_write_fw(uncore, reg, val);
}

static inline int intel_uncore_write_and_verify(struct intel_uncore *uncore,
						i915_reg_t reg, u32 val,
						u32 mask, u32 expected_val)
{
	u32 reg_val;

	intel_uncore_write(uncore, reg, val);
	reg_val = intel_uncore_read(uncore, reg);

	return (reg_val & mask) != expected_val ? -EINVAL : 0;
}

#define raw_reg_read(base, reg) \
	readl(base + i915_mmio_reg_offset(reg))
#define raw_reg_write(base, reg, value) \
	writel(value, base + i915_mmio_reg_offset(reg))

#endif /* !__INTEL_UNCORE_H__ */
