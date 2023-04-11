/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * omap_hwmod macros, structures
 *
 * Copyright (C) 2009-2011 Nokia Corporation
 * Copyright (C) 2011-2012 Texas Instruments, Inc.
 * Paul Walmsley
 *
 * Created in collaboration with (alphabetical order): Benoît Cousson,
 * Kevin Hilman, Tony Lindgren, Rajendra Nayak, Vikram Pandita, Sakari
 * Poussa, Anand Sawant, Santosh Shilimkar, Richard Woodruff
 *
 * These headers and macros are used to define OMAP on-chip module
 * data and their integration with other OMAP modules and Linux.
 * Copious documentation and references can also be found in the
 * omap_hwmod code, in arch/arm/mach-omap2/omap_hwmod.c (as of this
 * writing).
 *
 * To do:
 * - add interconnect error log structures
 * - init_conn_id_bit (CONNID_BIT_VECTOR)
 * - implement default hwmod SMS/SDRC flags?
 * - move Linux-specific data ("non-ROM data") out
 */
#ifndef __ARCH_ARM_PLAT_OMAP_INCLUDE_MACH_OMAP_HWMOD_H
#define __ARCH_ARM_PLAT_OMAP_INCLUDE_MACH_OMAP_HWMOD_H

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/ioport.h>
#include <linux/spinlock.h>

struct omap_device;

extern struct sysc_regbits omap_hwmod_sysc_type1;
extern struct sysc_regbits omap_hwmod_sysc_type2;
extern struct sysc_regbits omap_hwmod_sysc_type3;
extern struct sysc_regbits omap34xx_sr_sysc_fields;
extern struct sysc_regbits omap36xx_sr_sysc_fields;
extern struct sysc_regbits omap3_sham_sysc_fields;
extern struct sysc_regbits omap3xxx_aes_sysc_fields;
extern struct sysc_regbits omap_hwmod_sysc_type_mcasp;
extern struct sysc_regbits omap_hwmod_sysc_type_usb_host_fs;

/*
 * OCP SYSCONFIG bit shifts/masks TYPE1. These are for IPs compliant
 * with the original PRCM protocol defined for OMAP2420
 */
#define SYSC_TYPE1_MIDLEMODE_SHIFT	12
#define SYSC_TYPE1_MIDLEMODE_MASK	(0x3 << SYSC_TYPE1_MIDLEMODE_SHIFT)
#define SYSC_TYPE1_CLOCKACTIVITY_SHIFT	8
#define SYSC_TYPE1_CLOCKACTIVITY_MASK	(0x3 << SYSC_TYPE1_CLOCKACTIVITY_SHIFT)
#define SYSC_TYPE1_SIDLEMODE_SHIFT	3
#define SYSC_TYPE1_SIDLEMODE_MASK	(0x3 << SYSC_TYPE1_SIDLEMODE_SHIFT)
#define SYSC_TYPE1_ENAWAKEUP_SHIFT	2
#define SYSC_TYPE1_ENAWAKEUP_MASK	(1 << SYSC_TYPE1_ENAWAKEUP_SHIFT)
#define SYSC_TYPE1_SOFTRESET_SHIFT	1
#define SYSC_TYPE1_SOFTRESET_MASK	(1 << SYSC_TYPE1_SOFTRESET_SHIFT)
#define SYSC_TYPE1_AUTOIDLE_SHIFT	0
#define SYSC_TYPE1_AUTOIDLE_MASK	(1 << SYSC_TYPE1_AUTOIDLE_SHIFT)

/*
 * OCP SYSCONFIG bit shifts/masks TYPE2. These are for IPs compliant
 * with the new PRCM protocol defined for new OMAP4 IPs.
 */
#define SYSC_TYPE2_SOFTRESET_SHIFT	0
#define SYSC_TYPE2_SOFTRESET_MASK	(1 << SYSC_TYPE2_SOFTRESET_SHIFT)
#define SYSC_TYPE2_SIDLEMODE_SHIFT	2
#define SYSC_TYPE2_SIDLEMODE_MASK	(0x3 << SYSC_TYPE2_SIDLEMODE_SHIFT)
#define SYSC_TYPE2_MIDLEMODE_SHIFT	4
#define SYSC_TYPE2_MIDLEMODE_MASK	(0x3 << SYSC_TYPE2_MIDLEMODE_SHIFT)
#define SYSC_TYPE2_DMADISABLE_SHIFT	16
#define SYSC_TYPE2_DMADISABLE_MASK	(0x1 << SYSC_TYPE2_DMADISABLE_SHIFT)

/*
 * OCP SYSCONFIG bit shifts/masks TYPE3.
 * This is applicable for some IPs present in AM33XX
 */
#define SYSC_TYPE3_SIDLEMODE_SHIFT	0
#define SYSC_TYPE3_SIDLEMODE_MASK	(0x3 << SYSC_TYPE3_SIDLEMODE_SHIFT)
#define SYSC_TYPE3_MIDLEMODE_SHIFT	2
#define SYSC_TYPE3_MIDLEMODE_MASK	(0x3 << SYSC_TYPE3_MIDLEMODE_SHIFT)

/* OCP SYSSTATUS bit shifts/masks */
#define SYSS_RESETDONE_SHIFT		0
#define SYSS_RESETDONE_MASK		(1 << SYSS_RESETDONE_SHIFT)

/* Master standby/slave idle mode flags */
#define HWMOD_IDLEMODE_FORCE		(1 << 0)
#define HWMOD_IDLEMODE_NO		(1 << 1)
#define HWMOD_IDLEMODE_SMART		(1 << 2)
#define HWMOD_IDLEMODE_SMART_WKUP	(1 << 3)

/* modulemode control type (SW or HW) */
#define MODULEMODE_HWCTRL		1
#define MODULEMODE_SWCTRL		2

#define DEBUG_OMAP2UART1_FLAGS	0
#define DEBUG_OMAP2UART2_FLAGS	0
#define DEBUG_OMAP2UART3_FLAGS	0
#define DEBUG_OMAP3UART3_FLAGS	0
#define DEBUG_OMAP3UART4_FLAGS	0
#define DEBUG_OMAP4UART3_FLAGS	0
#define DEBUG_OMAP4UART4_FLAGS	0
#define DEBUG_TI81XXUART1_FLAGS	0
#define DEBUG_TI81XXUART2_FLAGS	0
#define DEBUG_TI81XXUART3_FLAGS	0
#define DEBUG_AM33XXUART1_FLAGS	0

#define DEBUG_OMAPUART_FLAGS	(HWMOD_INIT_NO_IDLE | HWMOD_INIT_NO_RESET)

#ifdef CONFIG_OMAP_GPMC_DEBUG
#define DEBUG_OMAP_GPMC_HWMOD_FLAGS	HWMOD_INIT_NO_RESET
#else
#define DEBUG_OMAP_GPMC_HWMOD_FLAGS	0
#endif

#if defined(CONFIG_DEBUG_OMAP2UART1)
#undef DEBUG_OMAP2UART1_FLAGS
#define DEBUG_OMAP2UART1_FLAGS DEBUG_OMAPUART_FLAGS
#elif defined(CONFIG_DEBUG_OMAP2UART2)
#undef DEBUG_OMAP2UART2_FLAGS
#define DEBUG_OMAP2UART2_FLAGS DEBUG_OMAPUART_FLAGS
#elif defined(CONFIG_DEBUG_OMAP2UART3)
#undef DEBUG_OMAP2UART3_FLAGS
#define DEBUG_OMAP2UART3_FLAGS DEBUG_OMAPUART_FLAGS
#elif defined(CONFIG_DEBUG_OMAP3UART3)
#undef DEBUG_OMAP3UART3_FLAGS
#define DEBUG_OMAP3UART3_FLAGS DEBUG_OMAPUART_FLAGS
#elif defined(CONFIG_DEBUG_OMAP3UART4)
#undef DEBUG_OMAP3UART4_FLAGS
#define DEBUG_OMAP3UART4_FLAGS DEBUG_OMAPUART_FLAGS
#elif defined(CONFIG_DEBUG_OMAP4UART3)
#undef DEBUG_OMAP4UART3_FLAGS
#define DEBUG_OMAP4UART3_FLAGS DEBUG_OMAPUART_FLAGS
#elif defined(CONFIG_DEBUG_OMAP4UART4)
#undef DEBUG_OMAP4UART4_FLAGS
#define DEBUG_OMAP4UART4_FLAGS DEBUG_OMAPUART_FLAGS
#elif defined(CONFIG_DEBUG_TI81XXUART1)
#undef DEBUG_TI81XXUART1_FLAGS
#define DEBUG_TI81XXUART1_FLAGS DEBUG_OMAPUART_FLAGS
#elif defined(CONFIG_DEBUG_TI81XXUART2)
#undef DEBUG_TI81XXUART2_FLAGS
#define DEBUG_TI81XXUART2_FLAGS DEBUG_OMAPUART_FLAGS
#elif defined(CONFIG_DEBUG_TI81XXUART3)
#undef DEBUG_TI81XXUART3_FLAGS
#define DEBUG_TI81XXUART3_FLAGS DEBUG_OMAPUART_FLAGS
#elif defined(CONFIG_DEBUG_AM33XXUART1)
#undef DEBUG_AM33XXUART1_FLAGS
#define DEBUG_AM33XXUART1_FLAGS DEBUG_OMAPUART_FLAGS
#endif

/**
 * struct omap_hwmod_rst_info - IPs reset lines use by hwmod
 * @name: name of the reset line (module local name)
 * @rst_shift: Offset of the reset bit
 * @st_shift: Offset of the reset status bit (OMAP2/3 only)
 *
 * @name should be something short, e.g., "cpu0" or "rst". It is defined
 * locally to the hwmod.
 */
struct omap_hwmod_rst_info {
	const char	*name;
	u8		rst_shift;
	u8		st_shift;
};

/**
 * struct omap_hwmod_opt_clk - optional clocks used by this hwmod
 * @role: "sys", "32k", "tv", etc -- for use in clk_get()
 * @clk: opt clock: OMAP clock name
 * @_clk: pointer to the struct clk (filled in at runtime)
 *
 * The module's interface clock and main functional clock should not
 * be added as optional clocks.
 */
struct omap_hwmod_opt_clk {
	const char	*role;
	const char	*clk;
	struct clk	*_clk;
};


/* omap_hwmod_omap2_firewall.flags bits */
#define OMAP_FIREWALL_L3		(1 << 0)
#define OMAP_FIREWALL_L4		(1 << 1)

/**
 * struct omap_hwmod_omap2_firewall - OMAP2/3 device firewall data
 * @l3_perm_bit: bit shift for L3_PM_*_PERMISSION_*
 * @l4_fw_region: L4 firewall region ID
 * @l4_prot_group: L4 protection group ID
 * @flags: (see omap_hwmod_omap2_firewall.flags macros above)
 */
struct omap_hwmod_omap2_firewall {
	u8 l3_perm_bit;
	u8 l4_fw_region;
	u8 l4_prot_group;
	u8 flags;
};

/*
 * omap_hwmod_ocp_if.user bits: these indicate the initiators that use this
 * interface to interact with the hwmod.  Used to add sleep dependencies
 * when the module is enabled or disabled.
 */
#define OCP_USER_MPU			(1 << 0)
#define OCP_USER_SDMA			(1 << 1)
#define OCP_USER_DSP			(1 << 2)
#define OCP_USER_IVA			(1 << 3)

/* omap_hwmod_ocp_if.flags bits */
#define OCPIF_SWSUP_IDLE		(1 << 0)
#define OCPIF_CAN_BURST			(1 << 1)

/* omap_hwmod_ocp_if._int_flags possibilities */
#define _OCPIF_INT_FLAGS_REGISTERED	(1 << 0)


/**
 * struct omap_hwmod_ocp_if - OCP interface data
 * @master: struct omap_hwmod that initiates OCP transactions on this link
 * @slave: struct omap_hwmod that responds to OCP transactions on this link
 * @addr: address space associated with this link
 * @clk: interface clock: OMAP clock name
 * @_clk: pointer to the interface struct clk (filled in at runtime)
 * @fw: interface firewall data
 * @width: OCP data width
 * @user: initiators using this interface (see OCP_USER_* macros above)
 * @flags: OCP interface flags (see OCPIF_* macros above)
 * @_int_flags: internal flags (see _OCPIF_INT_FLAGS* macros above)
 *
 * It may also be useful to add a tag_cnt field for OCP2.x devices.
 *
 * Parameter names beginning with an underscore are managed internally by
 * the omap_hwmod code and should not be set during initialization.
 */
struct omap_hwmod_ocp_if {
	struct omap_hwmod		*master;
	struct omap_hwmod		*slave;
	struct omap_hwmod_addr_space	*addr;
	const char			*clk;
	struct clk			*_clk;
	struct list_head		node;
	union {
		struct omap_hwmod_omap2_firewall omap2;
	}				fw;
	u8				width;
	u8				user;
	u8				flags;
	u8				_int_flags;
};


/* Macros for use in struct omap_hwmod_sysconfig */

/* Flags for use in omap_hwmod_sysconfig.idlemodes */
#define MASTER_STANDBY_SHIFT	4
#define SLAVE_IDLE_SHIFT	0
#define SIDLE_FORCE		(HWMOD_IDLEMODE_FORCE << SLAVE_IDLE_SHIFT)
#define SIDLE_NO		(HWMOD_IDLEMODE_NO << SLAVE_IDLE_SHIFT)
#define SIDLE_SMART		(HWMOD_IDLEMODE_SMART << SLAVE_IDLE_SHIFT)
#define SIDLE_SMART_WKUP	(HWMOD_IDLEMODE_SMART_WKUP << SLAVE_IDLE_SHIFT)
#define MSTANDBY_FORCE		(HWMOD_IDLEMODE_FORCE << MASTER_STANDBY_SHIFT)
#define MSTANDBY_NO		(HWMOD_IDLEMODE_NO << MASTER_STANDBY_SHIFT)
#define MSTANDBY_SMART		(HWMOD_IDLEMODE_SMART << MASTER_STANDBY_SHIFT)
#define MSTANDBY_SMART_WKUP	(HWMOD_IDLEMODE_SMART_WKUP << MASTER_STANDBY_SHIFT)

/* omap_hwmod_sysconfig.sysc_flags capability flags */
#define SYSC_HAS_AUTOIDLE	(1 << 0)
#define SYSC_HAS_SOFTRESET	(1 << 1)
#define SYSC_HAS_ENAWAKEUP	(1 << 2)
#define SYSC_HAS_EMUFREE	(1 << 3)
#define SYSC_HAS_CLOCKACTIVITY	(1 << 4)
#define SYSC_HAS_SIDLEMODE	(1 << 5)
#define SYSC_HAS_MIDLEMODE	(1 << 6)
#define SYSS_HAS_RESET_STATUS	(1 << 7)
#define SYSC_NO_CACHE		(1 << 8)  /* XXX SW flag, belongs elsewhere */
#define SYSC_HAS_RESET_STATUS	(1 << 9)
#define SYSC_HAS_DMADISABLE	(1 << 10)

/* omap_hwmod_sysconfig.clockact flags */
#define CLOCKACT_TEST_BOTH	0x0
#define CLOCKACT_TEST_MAIN	0x1
#define CLOCKACT_TEST_ICLK	0x2
#define CLOCKACT_TEST_NONE	0x3

/**
 * struct omap_hwmod_class_sysconfig - hwmod class OCP_SYS* data
 * @rev_offs: IP block revision register offset (from module base addr)
 * @sysc_offs: OCP_SYSCONFIG register offset (from module base addr)
 * @syss_offs: OCP_SYSSTATUS register offset (from module base addr)
 * @srst_udelay: Delay needed after doing a softreset in usecs
 * @idlemodes: One or more of {SIDLE,MSTANDBY}_{OFF,FORCE,SMART}
 * @sysc_flags: SYS{C,S}_HAS* flags indicating SYSCONFIG bits supported
 * @clockact: the default value of the module CLOCKACTIVITY bits
 *
 * @clockact describes to the module which clocks are likely to be
 * disabled when the PRCM issues its idle request to the module.  Some
 * modules have separate clockdomains for the interface clock and main
 * functional clock, and can check whether they should acknowledge the
 * idle request based on the internal module functionality that has
 * been associated with the clocks marked in @clockact.  This field is
 * only used if HWMOD_SET_DEFAULT_CLOCKACT is set (see below)
 *
 * @sysc_fields: structure containing the offset positions of various bits in
 * SYSCONFIG register. This can be populated using omap_hwmod_sysc_type1 or
 * omap_hwmod_sysc_type2 defined in omap_hwmod_common_data.c depending on
 * whether the device ip is compliant with the original PRCM protocol
 * defined for OMAP2420 or the new PRCM protocol for new OMAP4 IPs.
 * If the device follows a different scheme for the sysconfig register ,
 * then this field has to be populated with the correct offset structure.
 */
struct omap_hwmod_class_sysconfig {
	s32 rev_offs;
	s32 sysc_offs;
	s32 syss_offs;
	u16 sysc_flags;
	struct sysc_regbits *sysc_fields;
	u8 srst_udelay;
	u8 idlemodes;
};

/**
 * struct omap_hwmod_omap2_prcm - OMAP2/3-specific PRCM data
 * @module_offs: PRCM submodule offset from the start of the PRM/CM
 * @idlest_reg_id: IDLEST register ID (e.g., 3 for CM_IDLEST3)
 * @idlest_idle_bit: register bit shift for CM_IDLEST slave idle bit
 *
 * @prcm_reg_id and @module_bit are specific to the AUTOIDLE, WKST,
 * WKEN, GRPSEL registers.  In an ideal world, no extra information
 * would be needed for IDLEST information, but alas, there are some
 * exceptions, so @idlest_reg_id, @idlest_idle_bit, @idlest_stdby_bit
 * are needed for the IDLEST registers (c.f. 2430 I2CHS, 3430 USBHOST)
 */
struct omap_hwmod_omap2_prcm {
	s16 module_offs;
	u8 idlest_reg_id;
	u8 idlest_idle_bit;
};

/*
 * Possible values for struct omap_hwmod_omap4_prcm.flags
 *
 * HWMOD_OMAP4_NO_CONTEXT_LOSS_BIT: Some IP blocks don't have a PRCM
 *     module-level context loss register associated with them; this
 *     flag bit should be set in those cases
 * HWMOD_OMAP4_ZERO_CLKCTRL_OFFSET: Some IP blocks have a valid CLKCTRL
 *	offset of zero; this flag bit should be set in those cases to
 *	distinguish from hwmods that have no clkctrl offset.
 * HWMOD_OMAP4_CLKFWK_CLKCTR_CLOCK: Module clockctrl clock is managed
 *	by the common clock framework and not hwmod.
 */
#define HWMOD_OMAP4_NO_CONTEXT_LOSS_BIT		(1 << 0)
#define HWMOD_OMAP4_ZERO_CLKCTRL_OFFSET		(1 << 1)
#define HWMOD_OMAP4_CLKFWK_CLKCTR_CLOCK		(1 << 2)

/**
 * struct omap_hwmod_omap4_prcm - OMAP4-specific PRCM data
 * @clkctrl_offs: offset of the PRCM clock control register
 * @rstctrl_offs: offset of the XXX_RSTCTRL register located in the PRM
 * @context_offs: offset of the RM_*_CONTEXT register
 * @lostcontext_mask: bitmask for selecting bits from RM_*_CONTEXT register
 * @rstst_reg: (AM33XX only) address of the XXX_RSTST register in the PRM
 * @submodule_wkdep_bit: bit shift of the WKDEP range
 * @flags: PRCM register capabilities for this IP block
 * @modulemode: allowable modulemodes
 * @context_lost_counter: Count of module level context lost
 *
 * If @lostcontext_mask is not defined, context loss check code uses
 * whole register without masking. @lostcontext_mask should only be
 * defined in cases where @context_offs register is shared by two or
 * more hwmods.
 */
struct omap_hwmod_omap4_prcm {
	u16		clkctrl_offs;
	u16		rstctrl_offs;
	u16		rstst_offs;
	u16		context_offs;
	u32		lostcontext_mask;
	u8		submodule_wkdep_bit;
	u8		modulemode;
	u8		flags;
	int		context_lost_counter;
};


/*
 * omap_hwmod.flags definitions
 *
 * HWMOD_SWSUP_SIDLE: omap_hwmod code should manually bring module in and out
 *     of idle, rather than relying on module smart-idle
 * HWMOD_SWSUP_MSTANDBY: omap_hwmod code should manually bring module in and
 *     out of standby, rather than relying on module smart-standby
 * HWMOD_INIT_NO_RESET: don't reset this module at boot - important for
 *     SDRAM controller, etc. XXX probably belongs outside the main hwmod file
 *     XXX Should be HWMOD_SETUP_NO_RESET
 * HWMOD_INIT_NO_IDLE: don't idle this module at boot - important for SDRAM
 *     controller, etc. XXX probably belongs outside the main hwmod file
 *     XXX Should be HWMOD_SETUP_NO_IDLE
 * HWMOD_NO_OCP_AUTOIDLE: disable module autoidle (OCP_SYSCONFIG.AUTOIDLE)
 *     when module is enabled, rather than the default, which is to
 *     enable autoidle
 * HWMOD_SET_DEFAULT_CLOCKACT: program CLOCKACTIVITY bits at startup
 * HWMOD_NO_IDLEST: this module does not have idle status - this is the case
 *     only for few initiator modules on OMAP2 & 3.
 * HWMOD_CONTROL_OPT_CLKS_IN_RESET: Enable all optional clocks during reset.
 *     This is needed for devices like DSS that require optional clocks enabled
 *     in order to complete the reset. Optional clocks will be disabled
 *     again after the reset.
 * HWMOD_16BIT_REG: Module has 16bit registers
 * HWMOD_EXT_OPT_MAIN_CLK: The only main functional clock source for
 *     this IP block comes from an off-chip source and is not always
 *     enabled.  This prevents the hwmod code from being able to
 *     enable and reset the IP block early.  XXX Eventually it should
 *     be possible to query the clock framework for this information.
 * HWMOD_BLOCK_WFI: Some OMAP peripherals apparently don't work
 *     correctly if the MPU is allowed to go idle while the
 *     peripherals are active.  This is apparently true for the I2C on
 *     OMAP2420, and also the EMAC on AM3517/3505.  It's unlikely that
 *     this is really true -- we're probably not configuring something
 *     correctly, or this is being abused to deal with some PM latency
 *     issues -- but we're currently suffering from a shortage of
 *     folks who are able to track these issues down properly.
 * HWMOD_FORCE_MSTANDBY: Always keep MIDLEMODE bits cleared so that device
 *     is kept in force-standby mode. Failing to do so causes PM problems
 *     with musb on OMAP3630 at least. Note that musb has a dedicated register
 *     to control MSTANDBY signal when MIDLEMODE is set to force-standby.
 * HWMOD_SWSUP_SIDLE_ACT: omap_hwmod code should manually bring the module
 *     out of idle, but rely on smart-idle to the put it back in idle,
 *     so the wakeups are still functional (Only known case for now is UART)
 * HWMOD_RECONFIG_IO_CHAIN: omap_hwmod code needs to reconfigure wake-up 
 *     events by calling _reconfigure_io_chain() when a device is enabled
 *     or idled.
 * HWMOD_OPT_CLKS_NEEDED: The optional clocks are needed for the module to
 *     operate and they need to be handled at the same time as the main_clk.
 * HWMOD_NO_IDLE: Do not idle the hwmod at all. Useful to handle certain
 *     IPs like CPSW on DRA7, where clocks to this module cannot be disabled.
 * HWMOD_CLKDM_NOAUTO: Allows the hwmod's clockdomain to be prevented from
 *     entering HW_AUTO while hwmod is active. This is needed to workaround
 *     some modules which don't function correctly with HW_AUTO. For example,
 *     DCAN on DRA7x SoC needs this to workaround errata i893.
 */
#define HWMOD_SWSUP_SIDLE			(1 << 0)
#define HWMOD_SWSUP_MSTANDBY			(1 << 1)
#define HWMOD_INIT_NO_RESET			(1 << 2)
#define HWMOD_INIT_NO_IDLE			(1 << 3)
#define HWMOD_NO_OCP_AUTOIDLE			(1 << 4)
#define HWMOD_SET_DEFAULT_CLOCKACT		(1 << 5)
#define HWMOD_NO_IDLEST				(1 << 6)
#define HWMOD_CONTROL_OPT_CLKS_IN_RESET		(1 << 7)
#define HWMOD_16BIT_REG				(1 << 8)
#define HWMOD_EXT_OPT_MAIN_CLK			(1 << 9)
#define HWMOD_BLOCK_WFI				(1 << 10)
#define HWMOD_FORCE_MSTANDBY			(1 << 11)
#define HWMOD_SWSUP_SIDLE_ACT			(1 << 12)
#define HWMOD_RECONFIG_IO_CHAIN			(1 << 13)
#define HWMOD_OPT_CLKS_NEEDED			(1 << 14)
#define HWMOD_NO_IDLE				(1 << 15)
#define HWMOD_CLKDM_NOAUTO			(1 << 16)

/*
 * omap_hwmod._int_flags definitions
 * These are for internal use only and are managed by the omap_hwmod code.
 *
 * _HWMOD_NO_MPU_PORT: no path exists for the MPU to write to this module
 * _HWMOD_SYSCONFIG_LOADED: set when the OCP_SYSCONFIG value has been cached
 * _HWMOD_SKIP_ENABLE: set if hwmod enabled during init (HWMOD_INIT_NO_IDLE) -
 *     causes the first call to _enable() to only update the pinmux
 */
#define _HWMOD_NO_MPU_PORT			(1 << 0)
#define _HWMOD_SYSCONFIG_LOADED			(1 << 1)
#define _HWMOD_SKIP_ENABLE			(1 << 2)

/*
 * omap_hwmod._state definitions
 *
 * INITIALIZED: reset (optionally), initialized, enabled, disabled
 *              (optionally)
 *
 *
 */
#define _HWMOD_STATE_UNKNOWN			0
#define _HWMOD_STATE_REGISTERED			1
#define _HWMOD_STATE_CLKS_INITED		2
#define _HWMOD_STATE_INITIALIZED		3
#define _HWMOD_STATE_ENABLED			4
#define _HWMOD_STATE_IDLE			5
#define _HWMOD_STATE_DISABLED			6

#ifdef CONFIG_PM
#define _HWMOD_STATE_DEFAULT			_HWMOD_STATE_IDLE
#else
#define _HWMOD_STATE_DEFAULT			_HWMOD_STATE_ENABLED
#endif

/**
 * struct omap_hwmod_class - the type of an IP block
 * @name: name of the hwmod_class
 * @sysc: device SYSCONFIG/SYSSTATUS register data
 * @pre_shutdown: ptr to fn to be executed immediately prior to device shutdown
 * @reset: ptr to fn to be executed in place of the standard hwmod reset fn
 * @lock: ptr to fn to be executed to lock IP registers
 * @unlock: ptr to fn to be executed to unlock IP registers
 *
 * Represent the class of a OMAP hardware "modules" (e.g. timer,
 * smartreflex, gpio, uart...)
 *
 * @pre_shutdown is a function that will be run immediately before
 * hwmod clocks are disabled, etc.  It is intended for use for hwmods
 * like the MPU watchdog, which cannot be disabled with the standard
 * omap_hwmod_shutdown().  The function should return 0 upon success,
 * or some negative error upon failure.  Returning an error will cause
 * omap_hwmod_shutdown() to abort the device shutdown and return an
 * error.
 *
 * If @reset is defined, then the function it points to will be
 * executed in place of the standard hwmod _reset() code in
 * mach-omap2/omap_hwmod.c.  This is needed for IP blocks which have
 * unusual reset sequences - usually processor IP blocks like the IVA.
 */
struct omap_hwmod_class {
	const char				*name;
	struct omap_hwmod_class_sysconfig	*sysc;
	int					(*pre_shutdown)(struct omap_hwmod *oh);
	int					(*reset)(struct omap_hwmod *oh);
	void					(*lock)(struct omap_hwmod *oh);
	void					(*unlock)(struct omap_hwmod *oh);
};

/**
 * struct omap_hwmod - integration data for OMAP hardware "modules" (IP blocks)
 * @name: name of the hwmod
 * @class: struct omap_hwmod_class * to the class of this hwmod
 * @od: struct omap_device currently associated with this hwmod (internal use)
 * @prcm: PRCM data pertaining to this hwmod
 * @main_clk: main clock: OMAP clock name
 * @_clk: pointer to the main struct clk (filled in at runtime)
 * @opt_clks: other device clocks that drivers can request (0..*)
 * @voltdm: pointer to voltage domain (filled in at runtime)
 * @dev_attr: arbitrary device attributes that can be passed to the driver
 * @_sysc_cache: internal-use hwmod flags
 * @mpu_rt_idx: index of device address space for register target (for DT boot)
 * @_mpu_rt_va: cached register target start address (internal use)
 * @_mpu_port: cached MPU register target slave (internal use)
 * @opt_clks_cnt: number of @opt_clks
 * @master_cnt: number of @master entries
 * @slaves_cnt: number of @slave entries
 * @response_lat: device OCP response latency (in interface clock cycles)
 * @_int_flags: internal-use hwmod flags
 * @_state: internal-use hwmod state
 * @_postsetup_state: internal-use state to leave the hwmod in after _setup()
 * @flags: hwmod flags (documented below)
 * @_lock: spinlock serializing operations on this hwmod
 * @node: list node for hwmod list (internal use)
 * @parent_hwmod: (temporary) a pointer to the hierarchical parent of this hwmod
 *
 * @main_clk refers to this module's "main clock," which for our
 * purposes is defined as "the functional clock needed for register
 * accesses to complete."  Modules may not have a main clock if the
 * interface clock also serves as a main clock.
 *
 * Parameter names beginning with an underscore are managed internally by
 * the omap_hwmod code and should not be set during initialization.
 *
 * @masters and @slaves are now deprecated.
 *
 * @parent_hwmod is temporary; there should be no need for it, as this
 * information should already be expressed in the OCP interface
 * structures.  @parent_hwmod is present as a workaround until we improve
 * handling for hwmods with multiple parents (e.g., OMAP4+ DSS with
 * multiple register targets across different interconnects).
 */
struct omap_hwmod {
	const char			*name;
	struct omap_hwmod_class		*class;
	struct omap_device		*od;
	struct omap_hwmod_rst_info	*rst_lines;
	union {
		struct omap_hwmod_omap2_prcm omap2;
		struct omap_hwmod_omap4_prcm omap4;
	}				prcm;
	const char			*main_clk;
	struct clk			*_clk;
	struct omap_hwmod_opt_clk	*opt_clks;
	const char			*clkdm_name;
	struct clockdomain		*clkdm;
	struct list_head		slave_ports; /* connect to *_TA */
	void				*dev_attr;
	u32				_sysc_cache;
	void __iomem			*_mpu_rt_va;
	spinlock_t			_lock;
	struct lock_class_key		hwmod_key; /* unique lock class */
	struct list_head		node;
	struct omap_hwmod_ocp_if	*_mpu_port;
	u32				flags;
	u8				mpu_rt_idx;
	u8				response_lat;
	u8				rst_lines_cnt;
	u8				opt_clks_cnt;
	u8				slaves_cnt;
	u8				hwmods_cnt;
	u8				_int_flags;
	u8				_state;
	u8				_postsetup_state;
	struct omap_hwmod		*parent_hwmod;
};

#ifdef CONFIG_OMAP_HWMOD

struct device_node;

struct omap_hwmod *omap_hwmod_lookup(const char *name);
int omap_hwmod_for_each(int (*fn)(struct omap_hwmod *oh, void *data),
			void *data);

int omap_hwmod_parse_module_range(struct omap_hwmod *oh,
				  struct device_node *np,
				  struct resource *res);

struct ti_sysc_module_data;
struct ti_sysc_cookie;

int omap_hwmod_init_module(struct device *dev,
			   const struct ti_sysc_module_data *data,
			   struct ti_sysc_cookie *cookie);

int omap_hwmod_enable(struct omap_hwmod *oh);
int omap_hwmod_idle(struct omap_hwmod *oh);
int omap_hwmod_shutdown(struct omap_hwmod *oh);

int omap_hwmod_assert_hardreset(struct omap_hwmod *oh, const char *name);
int omap_hwmod_deassert_hardreset(struct omap_hwmod *oh, const char *name);

void omap_hwmod_write(u32 v, struct omap_hwmod *oh, u16 reg_offs);
u32 omap_hwmod_read(struct omap_hwmod *oh, u16 reg_offs);
int omap_hwmod_softreset(struct omap_hwmod *oh);

void __iomem *omap_hwmod_get_mpu_rt_va(struct omap_hwmod *oh);

int omap_hwmod_for_each_by_class(const char *classname,
				 int (*fn)(struct omap_hwmod *oh,
					   void *user),
				 void *user);

int omap_hwmod_set_postsetup_state(struct omap_hwmod *oh, u8 state);

extern void __init omap_hwmod_init(void);

#else	/* CONFIG_OMAP_HWMOD */

static inline int
omap_hwmod_for_each_by_class(const char *classname,
			     int (*fn)(struct omap_hwmod *oh, void *user),
			     void *user)
{
	return 0;
}
#endif	/* CONFIG_OMAP_HWMOD */

/*
 * Chip variant-specific hwmod init routines - XXX should be converted
 * to use initcalls once the initial boot ordering is straightened out
 */
extern int omap2420_hwmod_init(void);
extern int omap2430_hwmod_init(void);
extern int omap3xxx_hwmod_init(void);
extern int dm814x_hwmod_init(void);
extern int dm816x_hwmod_init(void);

extern int __init omap_hwmod_register_links(struct omap_hwmod_ocp_if **ois);

#endif
