/*
 * omap_hwmod macros, structures
 *
 * Copyright (C) 2009-2010 Nokia Corporation
 * Paul Walmsley
 *
 * Created in collaboration with (alphabetical order): Benoît Cousson,
 * Kevin Hilman, Tony Lindgren, Rajendra Nayak, Vikram Pandita, Sakari
 * Poussa, Anand Sawant, Santosh Shilimkar, Richard Woodruff
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * These headers and macros are used to define OMAP on-chip module
 * data and their integration with other OMAP modules and Linux.
 *
 * References:
 * - OMAP2420 Multimedia Processor Silicon Revision 2.1.1, 2.2 (SWPU064)
 * - OMAP2430 Multimedia Device POP Silicon Revision 2.1 (SWPU090)
 * - OMAP34xx Multimedia Device Silicon Revision 3.1 (SWPU108)
 * - OMAP4430 Multimedia Device Silicon Revision 1.0 (SWPU140)
 * - Open Core Protocol Specification 2.2
 *
 * To do:
 * - add interconnect error log structures
 * - add pinmuxing
 * - init_conn_id_bit (CONNID_BIT_VECTOR)
 * - implement default hwmod SMS/SDRC flags?
 *
 */
#ifndef __ARCH_ARM_PLAT_OMAP_INCLUDE_MACH_OMAP_HWMOD_H
#define __ARCH_ARM_PLAT_OMAP_INCLUDE_MACH_OMAP_HWMOD_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/ioport.h>
#include <plat/cpu.h>

struct omap_device;

extern struct omap_hwmod_sysc_fields omap_hwmod_sysc_type1;
extern struct omap_hwmod_sysc_fields omap_hwmod_sysc_type2;

/*
 * OCP SYSCONFIG bit shifts/masks TYPE1. These are for IPs compliant
 * with the original PRCM protocol defined for OMAP2420
 */
#define SYSC_TYPE1_MIDLEMODE_SHIFT	12
#define SYSC_TYPE1_MIDLEMODE_MASK	(0x3 << SYSC_MIDLEMODE_SHIFT)
#define SYSC_TYPE1_CLOCKACTIVITY_SHIFT	8
#define SYSC_TYPE1_CLOCKACTIVITY_MASK	(0x3 << SYSC_CLOCKACTIVITY_SHIFT)
#define SYSC_TYPE1_SIDLEMODE_SHIFT	3
#define SYSC_TYPE1_SIDLEMODE_MASK	(0x3 << SYSC_SIDLEMODE_SHIFT)
#define SYSC_TYPE1_ENAWAKEUP_SHIFT	2
#define SYSC_TYPE1_ENAWAKEUP_MASK	(1 << SYSC_ENAWAKEUP_SHIFT)
#define SYSC_TYPE1_SOFTRESET_SHIFT	1
#define SYSC_TYPE1_SOFTRESET_MASK	(1 << SYSC_SOFTRESET_SHIFT)
#define SYSC_TYPE1_AUTOIDLE_SHIFT	0
#define SYSC_TYPE1_AUTOIDLE_MASK	(1 << SYSC_AUTOIDLE_SHIFT)

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

/* OCP SYSSTATUS bit shifts/masks */
#define SYSS_RESETDONE_SHIFT		0
#define SYSS_RESETDONE_MASK		(1 << SYSS_RESETDONE_SHIFT)

/* Master standby/slave idle mode flags */
#define HWMOD_IDLEMODE_FORCE		(1 << 0)
#define HWMOD_IDLEMODE_NO		(1 << 1)
#define HWMOD_IDLEMODE_SMART		(1 << 2)

/**
 * struct omap_hwmod_irq_info - MPU IRQs used by the hwmod
 * @name: name of the IRQ channel (module local name)
 * @irq_ch: IRQ channel ID
 *
 * @name should be something short, e.g., "tx" or "rx".  It is for use
 * by platform_get_resource_byname().  It is defined locally to the
 * hwmod.
 */
struct omap_hwmod_irq_info {
	const char	*name;
	u16		irq;
};

/**
 * struct omap_hwmod_dma_info - DMA channels used by the hwmod
 * @name: name of the DMA channel (module local name)
 * @dma_ch: DMA channel ID
 *
 * @name should be something short, e.g., "tx" or "rx".  It is for use
 * by platform_get_resource_byname().  It is defined locally to the
 * hwmod.
 */
struct omap_hwmod_dma_info {
	const char	*name;
	u16		dma_ch;
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
 * omap_hwmod_addr_space.flags bits
 *
 * ADDR_MAP_ON_INIT: Map this address space during omap_hwmod init.
 * ADDR_TYPE_RT: Address space contains module register target data.
 */
#define ADDR_MAP_ON_INIT	(1 << 0)
#define ADDR_TYPE_RT		(1 << 1)

/**
 * struct omap_hwmod_addr_space - MPU address space handled by the hwmod
 * @pa_start: starting physical address
 * @pa_end: ending physical address
 * @flags: (see omap_hwmod_addr_space.flags macros above)
 *
 * Address space doesn't necessarily follow physical interconnect
 * structure.  GPMC is one example.
 */
struct omap_hwmod_addr_space {
	u32 pa_start;
	u32 pa_end;
	u8 flags;
};


/*
 * omap_hwmod_ocp_if.user bits: these indicate the initiators that use this
 * interface to interact with the hwmod.  Used to add sleep dependencies
 * when the module is enabled or disabled.
 */
#define OCP_USER_MPU			(1 << 0)
#define OCP_USER_SDMA			(1 << 1)

/* omap_hwmod_ocp_if.flags bits */
#define OCPIF_SWSUP_IDLE		(1 << 0)
#define OCPIF_CAN_BURST			(1 << 1)

/**
 * struct omap_hwmod_ocp_if - OCP interface data
 * @master: struct omap_hwmod that initiates OCP transactions on this link
 * @slave: struct omap_hwmod that responds to OCP transactions on this link
 * @addr: address space associated with this link
 * @clk: interface clock: OMAP clock name
 * @_clk: pointer to the interface struct clk (filled in at runtime)
 * @fw: interface firewall data
 * @addr_cnt: ARRAY_SIZE(@addr)
 * @width: OCP data width
 * @thread_cnt: number of threads
 * @max_burst_len: maximum burst length in @width sized words (0 if unlimited)
 * @user: initiators using this interface (see OCP_USER_* macros above)
 * @flags: OCP interface flags (see OCPIF_* macros above)
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
	union {
		struct omap_hwmod_omap2_firewall omap2;
	}				fw;
	u8				addr_cnt;
	u8				width;
	u8				thread_cnt;
	u8				max_burst_len;
	u8				user;
	u8				flags;
};


/* Macros for use in struct omap_hwmod_sysconfig */

/* Flags for use in omap_hwmod_sysconfig.idlemodes */
#define MASTER_STANDBY_SHIFT	2
#define SLAVE_IDLE_SHIFT	0
#define SIDLE_FORCE		(HWMOD_IDLEMODE_FORCE << SLAVE_IDLE_SHIFT)
#define SIDLE_NO		(HWMOD_IDLEMODE_NO << SLAVE_IDLE_SHIFT)
#define SIDLE_SMART		(HWMOD_IDLEMODE_SMART << SLAVE_IDLE_SHIFT)
#define MSTANDBY_FORCE		(HWMOD_IDLEMODE_FORCE << MASTER_STANDBY_SHIFT)
#define MSTANDBY_NO		(HWMOD_IDLEMODE_NO << MASTER_STANDBY_SHIFT)
#define MSTANDBY_SMART		(HWMOD_IDLEMODE_SMART << MASTER_STANDBY_SHIFT)

/* omap_hwmod_sysconfig.sysc_flags capability flags */
#define SYSC_HAS_AUTOIDLE	(1 << 0)
#define SYSC_HAS_SOFTRESET	(1 << 1)
#define SYSC_HAS_ENAWAKEUP	(1 << 2)
#define SYSC_HAS_EMUFREE	(1 << 3)
#define SYSC_HAS_CLOCKACTIVITY	(1 << 4)
#define SYSC_HAS_SIDLEMODE	(1 << 5)
#define SYSC_HAS_MIDLEMODE	(1 << 6)
#define SYSS_MISSING		(1 << 7)
#define SYSC_NO_CACHE		(1 << 8)  /* XXX SW flag, belongs elsewhere */

/* omap_hwmod_sysconfig.clockact flags */
#define CLOCKACT_TEST_BOTH	0x0
#define CLOCKACT_TEST_MAIN	0x1
#define CLOCKACT_TEST_ICLK	0x2
#define CLOCKACT_TEST_NONE	0x3

/**
 * struct omap_hwmod_sysc_fields - hwmod OCP_SYSCONFIG register field offsets.
 * @midle_shift: Offset of the midle bit
 * @clkact_shift: Offset of the clockactivity bit
 * @sidle_shift: Offset of the sidle bit
 * @enwkup_shift: Offset of the enawakeup bit
 * @srst_shift: Offset of the softreset bit
 * @autoidle_shift: Offset of the autoidle bit
 */
struct omap_hwmod_sysc_fields {
	u8 midle_shift;
	u8 clkact_shift;
	u8 sidle_shift;
	u8 enwkup_shift;
	u8 srst_shift;
	u8 autoidle_shift;
};

/**
 * struct omap_hwmod_class_sysconfig - hwmod class OCP_SYS* data
 * @rev_offs: IP block revision register offset (from module base addr)
 * @sysc_offs: OCP_SYSCONFIG register offset (from module base addr)
 * @syss_offs: OCP_SYSSTATUS register offset (from module base addr)
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
	u16 rev_offs;
	u16 sysc_offs;
	u16 syss_offs;
	u16 sysc_flags;
	u8 idlemodes;
	u8 clockact;
	struct omap_hwmod_sysc_fields *sysc_fields;
};

/**
 * struct omap_hwmod_omap2_prcm - OMAP2/3-specific PRCM data
 * @module_offs: PRCM submodule offset from the start of the PRM/CM
 * @prcm_reg_id: PRCM register ID (e.g., 3 for CM_AUTOIDLE3)
 * @module_bit: register bit shift for AUTOIDLE, WKST, WKEN, GRPSEL regs
 * @idlest_reg_id: IDLEST register ID (e.g., 3 for CM_IDLEST3)
 * @idlest_idle_bit: register bit shift for CM_IDLEST slave idle bit
 * @idlest_stdby_bit: register bit shift for CM_IDLEST master standby bit
 *
 * @prcm_reg_id and @module_bit are specific to the AUTOIDLE, WKST,
 * WKEN, GRPSEL registers.  In an ideal world, no extra information
 * would be needed for IDLEST information, but alas, there are some
 * exceptions, so @idlest_reg_id, @idlest_idle_bit, @idlest_stdby_bit
 * are needed for the IDLEST registers (c.f. 2430 I2CHS, 3430 USBHOST)
 */
struct omap_hwmod_omap2_prcm {
	s16 module_offs;
	u8 prcm_reg_id;
	u8 module_bit;
	u8 idlest_reg_id;
	u8 idlest_idle_bit;
	u8 idlest_stdby_bit;
};


/**
 * struct omap_hwmod_omap4_prcm - OMAP4-specific PRCM data
 * @clkctrl_reg: PRCM address of the clock control register
 * @submodule_wkdep_bit: bit shift of the WKDEP range
 */
struct omap_hwmod_omap4_prcm {
	void __iomem	*clkctrl_reg;
	u8		submodule_wkdep_bit;
};


/*
 * omap_hwmod.flags definitions
 *
 * HWMOD_SWSUP_SIDLE: omap_hwmod code should manually bring module in and out
 *     of idle, rather than relying on module smart-idle
 * HWMOD_SWSUP_MSTDBY: omap_hwmod code should manually bring module in and out
 *     of standby, rather than relying on module smart-standby
 * HWMOD_INIT_NO_RESET: don't reset this module at boot - important for
 *     SDRAM controller, etc.
 * HWMOD_INIT_NO_IDLE: don't idle this module at boot - important for SDRAM
 *     controller, etc.
 * HWMOD_NO_AUTOIDLE: disable module autoidle (OCP_SYSCONFIG.AUTOIDLE)
 *     when module is enabled, rather than the default, which is to
 *     enable autoidle
 * HWMOD_SET_DEFAULT_CLOCKACT: program CLOCKACTIVITY bits at startup
 * HWMOD_NO_IDLEST : this module does not have idle status - this is the case
 *     only for few initiator modules on OMAP2 & 3.
 */
#define HWMOD_SWSUP_SIDLE			(1 << 0)
#define HWMOD_SWSUP_MSTANDBY			(1 << 1)
#define HWMOD_INIT_NO_RESET			(1 << 2)
#define HWMOD_INIT_NO_IDLE			(1 << 3)
#define HWMOD_NO_OCP_AUTOIDLE			(1 << 4)
#define HWMOD_SET_DEFAULT_CLOCKACT		(1 << 5)
#define HWMOD_NO_IDLEST				(1 << 6)

/*
 * omap_hwmod._int_flags definitions
 * These are for internal use only and are managed by the omap_hwmod code.
 *
 * _HWMOD_NO_MPU_PORT: no path exists for the MPU to write to this module
 * _HWMOD_WAKEUP_ENABLED: set when the omap_hwmod code has enabled ENAWAKEUP
 * _HWMOD_SYSCONFIG_LOADED: set when the OCP_SYSCONFIG value has been cached
 */
#define _HWMOD_NO_MPU_PORT			(1 << 0)
#define _HWMOD_WAKEUP_ENABLED			(1 << 1)
#define _HWMOD_SYSCONFIG_LOADED			(1 << 2)

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

/**
 * struct omap_hwmod_class - the type of an IP block
 * @name: name of the hwmod_class
 * @sysc: device SYSCONFIG/SYSSTATUS register data
 * @rev: revision of the IP class
 *
 * Represent the class of a OMAP hardware "modules" (e.g. timer,
 * smartreflex, gpio, uart...)
 */
struct omap_hwmod_class {
	const char				*name;
	struct omap_hwmod_class_sysconfig	*sysc;
	u32					rev;
};

/**
 * struct omap_hwmod - integration data for OMAP hardware "modules" (IP blocks)
 * @name: name of the hwmod
 * @class: struct omap_hwmod_class * to the class of this hwmod
 * @od: struct omap_device currently associated with this hwmod (internal use)
 * @mpu_irqs: ptr to an array of MPU IRQs (see also mpu_irqs_cnt)
 * @sdma_chs: ptr to an array of SDMA channel IDs (see also sdma_chs_cnt)
 * @prcm: PRCM data pertaining to this hwmod
 * @main_clk: main clock: OMAP clock name
 * @_clk: pointer to the main struct clk (filled in at runtime)
 * @opt_clks: other device clocks that drivers can request (0..*)
 * @masters: ptr to array of OCP ifs that this hwmod can initiate on
 * @slaves: ptr to array of OCP ifs that this hwmod can respond on
 * @dev_attr: arbitrary device attributes that can be passed to the driver
 * @_sysc_cache: internal-use hwmod flags
 * @_mpu_rt_va: cached register target start address (internal use)
 * @_mpu_port_index: cached MPU register target slave ID (internal use)
 * @msuspendmux_reg_id: CONTROL_MSUSPENDMUX register ID (1-6)
 * @msuspendmux_shift: CONTROL_MSUSPENDMUX register bit shift
 * @mpu_irqs_cnt: number of @mpu_irqs
 * @sdma_chs_cnt: number of @sdma_chs
 * @opt_clks_cnt: number of @opt_clks
 * @master_cnt: number of @master entries
 * @slaves_cnt: number of @slave entries
 * @response_lat: device OCP response latency (in interface clock cycles)
 * @_int_flags: internal-use hwmod flags
 * @_state: internal-use hwmod state
 * @flags: hwmod flags (documented below)
 * @omap_chip: OMAP chips this hwmod is present on
 * @node: list node for hwmod list (internal use)
 *
 * @main_clk refers to this module's "main clock," which for our
 * purposes is defined as "the functional clock needed for register
 * accesses to complete."  Modules may not have a main clock if the
 * interface clock also serves as a main clock.
 *
 * Parameter names beginning with an underscore are managed internally by
 * the omap_hwmod code and should not be set during initialization.
 */
struct omap_hwmod {
	const char			*name;
	struct omap_hwmod_class		*class;
	struct omap_device		*od;
	struct omap_hwmod_irq_info	*mpu_irqs;
	struct omap_hwmod_dma_info	*sdma_chs;
	union {
		struct omap_hwmod_omap2_prcm omap2;
		struct omap_hwmod_omap4_prcm omap4;
	}				prcm;
	const char			*main_clk;
	struct clk			*_clk;
	struct omap_hwmod_opt_clk	*opt_clks;
	struct omap_hwmod_ocp_if	**masters; /* connect to *_IA */
	struct omap_hwmod_ocp_if	**slaves;  /* connect to *_TA */
	void				*dev_attr;
	u32				_sysc_cache;
	void __iomem			*_mpu_rt_va;
	struct list_head		node;
	u16				flags;
	u8				_mpu_port_index;
	u8				msuspendmux_reg_id;
	u8				msuspendmux_shift;
	u8				response_lat;
	u8				mpu_irqs_cnt;
	u8				sdma_chs_cnt;
	u8				opt_clks_cnt;
	u8				masters_cnt;
	u8				slaves_cnt;
	u8				hwmods_cnt;
	u8				_int_flags;
	u8				_state;
	const struct omap_chip_id	omap_chip;
};

int omap_hwmod_init(struct omap_hwmod **ohs);
int omap_hwmod_register(struct omap_hwmod *oh);
int omap_hwmod_unregister(struct omap_hwmod *oh);
struct omap_hwmod *omap_hwmod_lookup(const char *name);
int omap_hwmod_for_each(int (*fn)(struct omap_hwmod *oh, void *data),
			void *data);
int omap_hwmod_late_init(u8 skip_setup_idle);

int omap_hwmod_enable(struct omap_hwmod *oh);
int _omap_hwmod_enable(struct omap_hwmod *oh);
int omap_hwmod_idle(struct omap_hwmod *oh);
int _omap_hwmod_idle(struct omap_hwmod *oh);
int omap_hwmod_shutdown(struct omap_hwmod *oh);

int omap_hwmod_enable_clocks(struct omap_hwmod *oh);
int omap_hwmod_disable_clocks(struct omap_hwmod *oh);

int omap_hwmod_set_slave_idlemode(struct omap_hwmod *oh, u8 idlemode);

int omap_hwmod_reset(struct omap_hwmod *oh);
void omap_hwmod_ocp_barrier(struct omap_hwmod *oh);

void omap_hwmod_writel(u32 v, struct omap_hwmod *oh, u16 reg_offs);
u32 omap_hwmod_readl(struct omap_hwmod *oh, u16 reg_offs);

int omap_hwmod_count_resources(struct omap_hwmod *oh);
int omap_hwmod_fill_resources(struct omap_hwmod *oh, struct resource *res);

struct powerdomain *omap_hwmod_get_pwrdm(struct omap_hwmod *oh);
void __iomem *omap_hwmod_get_mpu_rt_va(struct omap_hwmod *oh);

int omap_hwmod_add_initiator_dep(struct omap_hwmod *oh,
				 struct omap_hwmod *init_oh);
int omap_hwmod_del_initiator_dep(struct omap_hwmod *oh,
				 struct omap_hwmod *init_oh);

int omap_hwmod_set_clockact_both(struct omap_hwmod *oh);
int omap_hwmod_set_clockact_main(struct omap_hwmod *oh);
int omap_hwmod_set_clockact_iclk(struct omap_hwmod *oh);
int omap_hwmod_set_clockact_none(struct omap_hwmod *oh);

int omap_hwmod_enable_wakeup(struct omap_hwmod *oh);
int omap_hwmod_disable_wakeup(struct omap_hwmod *oh);

int omap_hwmod_for_each_by_class(const char *classname,
				 int (*fn)(struct omap_hwmod *oh,
					   void *user),
				 void *user);

/*
 * Chip variant-specific hwmod init routines - XXX should be converted
 * to use initcalls once the initial boot ordering is straightened out
 */
extern int omap2420_hwmod_init(void);
extern int omap2430_hwmod_init(void);
extern int omap3xxx_hwmod_init(void);

#endif
