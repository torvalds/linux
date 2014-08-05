/*
 * Copyright (C) 2013 Broadcom Corporation
 * Copyright 2013 Linaro Limited
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _CLK_KONA_H
#define _CLK_KONA_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/clk-provider.h>

#define	BILLION		1000000000

/* The common clock framework uses u8 to represent a parent index */
#define PARENT_COUNT_MAX	((u32)U8_MAX)

#define BAD_CLK_INDEX		U8_MAX	/* Can't ever be valid */
#define BAD_CLK_NAME		((const char *)-1)

#define BAD_SCALED_DIV_VALUE	U64_MAX

/*
 * Utility macros for object flag management.  If possible, flags
 * should be defined such that 0 is the desired default value.
 */
#define FLAG(type, flag)		BCM_CLK_ ## type ## _FLAGS_ ## flag
#define FLAG_SET(obj, type, flag)	((obj)->flags |= FLAG(type, flag))
#define FLAG_CLEAR(obj, type, flag)	((obj)->flags &= ~(FLAG(type, flag)))
#define FLAG_FLIP(obj, type, flag)	((obj)->flags ^= FLAG(type, flag))
#define FLAG_TEST(obj, type, flag)	(!!((obj)->flags & FLAG(type, flag)))

/* CCU field state tests */

#define ccu_policy_exists(ccu_policy)	((ccu_policy)->enable.offset != 0)

/* Clock field state tests */

#define policy_exists(policy)		((policy)->offset != 0)

#define gate_exists(gate)		FLAG_TEST(gate, GATE, EXISTS)
#define gate_is_enabled(gate)		FLAG_TEST(gate, GATE, ENABLED)
#define gate_is_hw_controllable(gate)	FLAG_TEST(gate, GATE, HW)
#define gate_is_sw_controllable(gate)	FLAG_TEST(gate, GATE, SW)
#define gate_is_sw_managed(gate)	FLAG_TEST(gate, GATE, SW_MANAGED)
#define gate_is_no_disable(gate)	FLAG_TEST(gate, GATE, NO_DISABLE)

#define gate_flip_enabled(gate)		FLAG_FLIP(gate, GATE, ENABLED)

#define hyst_exists(hyst)		((hyst)->offset != 0)

#define divider_exists(div)		FLAG_TEST(div, DIV, EXISTS)
#define divider_is_fixed(div)		FLAG_TEST(div, DIV, FIXED)
#define divider_has_fraction(div)	(!divider_is_fixed(div) && \
						(div)->u.s.frac_width > 0)

#define selector_exists(sel)		((sel)->width != 0)
#define trigger_exists(trig)		FLAG_TEST(trig, TRIG, EXISTS)

#define policy_lvm_en_exists(enable)	((enable)->offset != 0)
#define policy_ctl_exists(control)	((control)->offset != 0)

/* Clock type, used to tell common block what it's part of */
enum bcm_clk_type {
	bcm_clk_none,		/* undefined clock type */
	bcm_clk_bus,
	bcm_clk_core,
	bcm_clk_peri
};

/*
 * CCU policy control for clocks.  Clocks can be enabled or disabled
 * based on the CCU policy in effect.  One bit in each policy mask
 * register (one per CCU policy) represents whether the clock is
 * enabled when that policy is effect or not.  The CCU policy engine
 * must be stopped to update these bits, and must be restarted again
 * afterward.
 */
struct bcm_clk_policy {
	u32 offset;		/* first policy mask register offset */
	u32 bit;		/* bit used in all mask registers */
};

/* Policy initialization macro */

#define POLICY(_offset, _bit)						\
	{								\
		.offset = (_offset),					\
		.bit = (_bit),						\
	}

/*
 * Gating control and status is managed by a 32-bit gate register.
 *
 * There are several types of gating available:
 * - (no gate)
 *     A clock with no gate is assumed to be always enabled.
 * - hardware-only gating (auto-gating)
 *     Enabling or disabling clocks with this type of gate is
 *     managed automatically by the hardware.  Such clocks can be
 *     considered by the software to be enabled.  The current status
 *     of auto-gated clocks can be read from the gate status bit.
 * - software-only gating
 *     Auto-gating is not available for this type of clock.
 *     Instead, software manages whether it's enabled by setting or
 *     clearing the enable bit.  The current gate status of a gate
 *     under software control can be read from the gate status bit.
 *     To ensure a change to the gating status is complete, the
 *     status bit can be polled to verify that the gate has entered
 *     the desired state.
 * - selectable hardware or software gating
 *     Gating for this type of clock can be configured to be either
 *     under software or hardware control.  Which type is in use is
 *     determined by the hw_sw_sel bit of the gate register.
 */
struct bcm_clk_gate {
	u32 offset;		/* gate register offset */
	u32 status_bit;		/* 0: gate is disabled; 0: gatge is enabled */
	u32 en_bit;		/* 0: disable; 1: enable */
	u32 hw_sw_sel_bit;	/* 0: hardware gating; 1: software gating */
	u32 flags;		/* BCM_CLK_GATE_FLAGS_* below */
};

/*
 * Gate flags:
 *   HW         means this gate can be auto-gated
 *   SW         means the state of this gate can be software controlled
 *   NO_DISABLE means this gate is (only) enabled if under software control
 *   SW_MANAGED means the status of this gate is under software control
 *   ENABLED    means this software-managed gate is *supposed* to be enabled
 */
#define BCM_CLK_GATE_FLAGS_EXISTS	((u32)1 << 0)	/* Gate is valid */
#define BCM_CLK_GATE_FLAGS_HW		((u32)1 << 1)	/* Can auto-gate */
#define BCM_CLK_GATE_FLAGS_SW		((u32)1 << 2)	/* Software control */
#define BCM_CLK_GATE_FLAGS_NO_DISABLE	((u32)1 << 3)	/* HW or enabled */
#define BCM_CLK_GATE_FLAGS_SW_MANAGED	((u32)1 << 4)	/* SW now in control */
#define BCM_CLK_GATE_FLAGS_ENABLED	((u32)1 << 5)	/* If SW_MANAGED */

/*
 * Gate initialization macros.
 *
 * Any gate initially under software control will be enabled.
 */

/* A hardware/software gate initially under software control */
#define HW_SW_GATE(_offset, _status_bit, _en_bit, _hw_sw_sel_bit)	\
	{								\
		.offset = (_offset),					\
		.status_bit = (_status_bit),				\
		.en_bit = (_en_bit),					\
		.hw_sw_sel_bit = (_hw_sw_sel_bit),			\
		.flags = FLAG(GATE, HW)|FLAG(GATE, SW)|			\
			FLAG(GATE, SW_MANAGED)|FLAG(GATE, ENABLED)|	\
			FLAG(GATE, EXISTS),				\
	}

/* A hardware/software gate initially under hardware control */
#define HW_SW_GATE_AUTO(_offset, _status_bit, _en_bit, _hw_sw_sel_bit)	\
	{								\
		.offset = (_offset),					\
		.status_bit = (_status_bit),				\
		.en_bit = (_en_bit),					\
		.hw_sw_sel_bit = (_hw_sw_sel_bit),			\
		.flags = FLAG(GATE, HW)|FLAG(GATE, SW)|			\
			FLAG(GATE, EXISTS),				\
	}

/* A hardware-or-enabled gate (enabled if not under hardware control) */
#define HW_ENABLE_GATE(_offset, _status_bit, _en_bit, _hw_sw_sel_bit)	\
	{								\
		.offset = (_offset),					\
		.status_bit = (_status_bit),				\
		.en_bit = (_en_bit),					\
		.hw_sw_sel_bit = (_hw_sw_sel_bit),			\
		.flags = FLAG(GATE, HW)|FLAG(GATE, SW)|			\
			FLAG(GATE, NO_DISABLE)|FLAG(GATE, EXISTS),	\
	}

/* A software-only gate */
#define SW_ONLY_GATE(_offset, _status_bit, _en_bit)			\
	{								\
		.offset = (_offset),					\
		.status_bit = (_status_bit),				\
		.en_bit = (_en_bit),					\
		.flags = FLAG(GATE, SW)|FLAG(GATE, SW_MANAGED)|		\
			FLAG(GATE, ENABLED)|FLAG(GATE, EXISTS),		\
	}

/* A hardware-only gate */
#define HW_ONLY_GATE(_offset, _status_bit)				\
	{								\
		.offset = (_offset),					\
		.status_bit = (_status_bit),				\
		.flags = FLAG(GATE, HW)|FLAG(GATE, EXISTS),		\
	}

/* Gate hysteresis for clocks */
struct bcm_clk_hyst {
	u32 offset;		/* hyst register offset (normally CLKGATE) */
	u32 en_bit;		/* bit used to enable hysteresis */
	u32 val_bit;		/* if enabled: 0 = low delay; 1 = high delay */
};

/* Hysteresis initialization macro */

#define HYST(_offset, _en_bit, _val_bit)				\
	{								\
		.offset = (_offset),					\
		.en_bit = (_en_bit),					\
		.val_bit = (_val_bit),					\
	}

/*
 * Each clock can have zero, one, or two dividers which change the
 * output rate of the clock.  Each divider can be either fixed or
 * variable.  If there are two dividers, they are the "pre-divider"
 * and the "regular" or "downstream" divider.  If there is only one,
 * there is no pre-divider.
 *
 * A fixed divider is any non-zero (positive) value, and it
 * indicates how the input rate is affected by the divider.
 *
 * The value of a variable divider is maintained in a sub-field of a
 * 32-bit divider register.  The position of the field in the
 * register is defined by its offset and width.  The value recorded
 * in this field is always 1 less than the value it represents.
 *
 * In addition, a variable divider can indicate that some subset
 * of its bits represent a "fractional" part of the divider.  Such
 * bits comprise the low-order portion of the divider field, and can
 * be viewed as representing the portion of the divider that lies to
 * the right of the decimal point.  Most variable dividers have zero
 * fractional bits.  Variable dividers with non-zero fraction width
 * still record a value 1 less than the value they represent; the
 * added 1 does *not* affect the low-order bit in this case, it
 * affects the bits above the fractional part only.  (Often in this
 * code a divider field value is distinguished from the value it
 * represents by referring to the latter as a "divisor".)
 *
 * In order to avoid dealing with fractions, divider arithmetic is
 * performed using "scaled" values.  A scaled value is one that's
 * been left-shifted by the fractional width of a divider.  Dividing
 * a scaled value by a scaled divisor produces the desired quotient
 * without loss of precision and without any other special handling
 * for fractions.
 *
 * The recorded value of a variable divider can be modified.  To
 * modify either divider (or both), a clock must be enabled (i.e.,
 * using its gate).  In addition, a trigger register (described
 * below) must be used to commit the change, and polled to verify
 * the change is complete.
 */
struct bcm_clk_div {
	union {
		struct {	/* variable divider */
			u32 offset;	/* divider register offset */
			u32 shift;	/* field shift */
			u32 width;	/* field width */
			u32 frac_width;	/* field fraction width */

			u64 scaled_div;	/* scaled divider value */
		} s;
		u32 fixed;	/* non-zero fixed divider value */
	} u;
	u32 flags;		/* BCM_CLK_DIV_FLAGS_* below */
};

/*
 * Divider flags:
 *   EXISTS means this divider exists
 *   FIXED means it is a fixed-rate divider
 */
#define BCM_CLK_DIV_FLAGS_EXISTS	((u32)1 << 0)	/* Divider is valid */
#define BCM_CLK_DIV_FLAGS_FIXED		((u32)1 << 1)	/* Fixed-value */

/* Divider initialization macros */

/* A fixed (non-zero) divider */
#define FIXED_DIVIDER(_value)						\
	{								\
		.u.fixed = (_value),					\
		.flags = FLAG(DIV, EXISTS)|FLAG(DIV, FIXED),		\
	}

/* A divider with an integral divisor */
#define DIVIDER(_offset, _shift, _width)				\
	{								\
		.u.s.offset = (_offset),				\
		.u.s.shift = (_shift),					\
		.u.s.width = (_width),					\
		.u.s.scaled_div = BAD_SCALED_DIV_VALUE,			\
		.flags = FLAG(DIV, EXISTS),				\
	}

/* A divider whose divisor has an integer and fractional part */
#define FRAC_DIVIDER(_offset, _shift, _width, _frac_width)		\
	{								\
		.u.s.offset = (_offset),				\
		.u.s.shift = (_shift),					\
		.u.s.width = (_width),					\
		.u.s.frac_width = (_frac_width),			\
		.u.s.scaled_div = BAD_SCALED_DIV_VALUE,			\
		.flags = FLAG(DIV, EXISTS),				\
	}

/*
 * Clocks may have multiple "parent" clocks.  If there is more than
 * one, a selector must be specified to define which of the parent
 * clocks is currently in use.  The selected clock is indicated in a
 * sub-field of a 32-bit selector register.  The range of
 * representable selector values typically exceeds the number of
 * available parent clocks.  Occasionally the reset value of a
 * selector field is explicitly set to a (specific) value that does
 * not correspond to a defined input clock.
 *
 * We register all known parent clocks with the common clock code
 * using a packed array (i.e., no empty slots) of (parent) clock
 * names, and refer to them later using indexes into that array.
 * We maintain an array of selector values indexed by common clock
 * index values in order to map between these common clock indexes
 * and the selector values used by the hardware.
 *
 * Like dividers, a selector can be modified, but to do so a clock
 * must be enabled, and a trigger must be used to commit the change.
 */
struct bcm_clk_sel {
	u32 offset;		/* selector register offset */
	u32 shift;		/* field shift */
	u32 width;		/* field width */

	u32 parent_count;	/* number of entries in parent_sel[] */
	u32 *parent_sel;	/* array of parent selector values */
	u8 clk_index;		/* current selected index in parent_sel[] */
};

/* Selector initialization macro */
#define SELECTOR(_offset, _shift, _width)				\
	{								\
		.offset = (_offset),					\
		.shift = (_shift),					\
		.width = (_width),					\
		.clk_index = BAD_CLK_INDEX,				\
	}

/*
 * Making changes to a variable divider or a selector for a clock
 * requires the use of a trigger.  A trigger is defined by a single
 * bit within a register.  To signal a change, a 1 is written into
 * that bit.  To determine when the change has been completed, that
 * trigger bit is polled; the read value will be 1 while the change
 * is in progress, and 0 when it is complete.
 *
 * Occasionally a clock will have more than one trigger.  In this
 * case, the "pre-trigger" will be used when changing a clock's
 * selector and/or its pre-divider.
 */
struct bcm_clk_trig {
	u32 offset;		/* trigger register offset */
	u32 bit;		/* trigger bit */
	u32 flags;		/* BCM_CLK_TRIG_FLAGS_* below */
};

/*
 * Trigger flags:
 *   EXISTS means this trigger exists
 */
#define BCM_CLK_TRIG_FLAGS_EXISTS	((u32)1 << 0)	/* Trigger is valid */

/* Trigger initialization macro */
#define TRIGGER(_offset, _bit)						\
	{								\
		.offset = (_offset),					\
		.bit = (_bit),						\
		.flags = FLAG(TRIG, EXISTS),				\
	}

struct peri_clk_data {
	struct bcm_clk_policy policy;
	struct bcm_clk_gate gate;
	struct bcm_clk_hyst hyst;
	struct bcm_clk_trig pre_trig;
	struct bcm_clk_div pre_div;
	struct bcm_clk_trig trig;
	struct bcm_clk_div div;
	struct bcm_clk_sel sel;
	const char *clocks[];	/* must be last; use CLOCKS() to declare */
};
#define CLOCKS(...)	{ __VA_ARGS__, NULL, }
#define NO_CLOCKS	{ NULL, }	/* Must use of no parent clocks */

struct kona_clk {
	struct clk_hw hw;
	struct clk_init_data init_data;	/* includes name of this clock */
	struct ccu_data *ccu;	/* ccu this clock is associated with */
	enum bcm_clk_type type;
	union {
		void *data;
		struct peri_clk_data *peri;
	} u;
};
#define to_kona_clk(_hw) \
	container_of(_hw, struct kona_clk, hw)

/* Initialization macro for an entry in a CCU's kona_clks[] array. */
#define KONA_CLK(_ccu_name, _clk_name, _type)				\
	{								\
		.init_data	= {					\
			.name = #_clk_name,				\
			.ops = &kona_ ## _type ## _clk_ops,		\
		},							\
		.ccu		= &_ccu_name ## _ccu_data,		\
		.type		= bcm_clk_ ## _type,			\
		.u.data		= &_clk_name ## _data,			\
	}
#define LAST_KONA_CLK	{ .type = bcm_clk_none }

/*
 * CCU policy control.  To enable software update of the policy
 * tables the CCU policy engine must be stopped by setting the
 * software update enable bit (LVM_EN).  After an update the engine
 * is restarted using the GO bit and either the GO_ATL or GO_AC bit.
 */
struct bcm_lvm_en {
	u32 offset;		/* LVM_EN register offset */
	u32 bit;		/* POLICY_CONFIG_EN bit in register */
};

/* Policy enable initialization macro */
#define CCU_LVM_EN(_offset, _bit)					\
	{								\
		.offset = (_offset),					\
		.bit = (_bit),						\
	}

struct bcm_policy_ctl {
	u32 offset;		/* POLICY_CTL register offset */
	u32 go_bit;
	u32 atl_bit;		/* GO, GO_ATL, and GO_AC bits */
	u32 ac_bit;
};

/* Policy control initialization macro */
#define CCU_POLICY_CTL(_offset, _go_bit, _ac_bit, _atl_bit)		\
	{								\
		.offset = (_offset),					\
		.go_bit = (_go_bit),					\
		.ac_bit = (_ac_bit),					\
		.atl_bit = (_atl_bit),					\
	}

struct ccu_policy {
	struct bcm_lvm_en enable;
	struct bcm_policy_ctl control;
};

/*
 * Each CCU defines a mapped area of memory containing registers
 * used to manage clocks implemented by the CCU.  Access to memory
 * within the CCU's space is serialized by a spinlock.  Before any
 * (other) address can be written, a special access "password" value
 * must be written to its WR_ACCESS register (located at the base
 * address of the range).  We keep track of the name of each CCU as
 * it is set up, and maintain them in a list.
 */
struct ccu_data {
	void __iomem *base;	/* base of mapped address space */
	spinlock_t lock;	/* serialization lock */
	bool write_enabled;	/* write access is currently enabled */
	struct ccu_policy policy;
	struct list_head links;	/* for ccu_list */
	struct device_node *node;
	struct clk_onecell_data clk_data;
	const char *name;
	u32 range;		/* byte range of address space */
	struct kona_clk kona_clks[];	/* must be last */
};

/* Initialization for common fields in a Kona ccu_data structure */
#define KONA_CCU_COMMON(_prefix, _name, _ccuname)			    \
	.name		= #_name "_ccu",				    \
	.lock		= __SPIN_LOCK_UNLOCKED(_name ## _ccu_data.lock),    \
	.links		= LIST_HEAD_INIT(_name ## _ccu_data.links),	    \
	.clk_data	= {						    \
		.clk_num = _prefix ## _ ## _ccuname ## _CCU_CLOCK_COUNT,    \
	}

/* Exported globals */

extern struct clk_ops kona_peri_clk_ops;

/* Externally visible functions */

extern u64 do_div_round_closest(u64 dividend, unsigned long divisor);
extern u64 scaled_div_max(struct bcm_clk_div *div);
extern u64 scaled_div_build(struct bcm_clk_div *div, u32 div_value,
				u32 billionths);

extern struct clk *kona_clk_setup(struct kona_clk *bcm_clk);
extern void __init kona_dt_ccu_setup(struct ccu_data *ccu,
				struct device_node *node);
extern bool __init kona_ccu_init(struct ccu_data *ccu);

#endif /* _CLK_KONA_H */
