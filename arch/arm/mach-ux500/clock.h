/*
 *  Copyright (C) 2010 ST-Ericsson
 *  Copyright (C) 2009 STMicroelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/**
 * struct clkops - ux500 clock operations
 * @enable:	function to enable the clock
 * @disable:	function to disable the clock
 * @get_rate:	function to get the current clock rate
 *
 * This structure contains function pointers to functions that will be used to
 * control the clock.  All of these functions are optional.  If get_rate is
 * NULL, the rate in the struct clk will be used.
 */
struct clkops {
	void (*enable) (struct clk *);
	void (*disable) (struct clk *);
	unsigned long (*get_rate) (struct clk *);
	int (*set_parent)(struct clk *, struct clk *);
};

/**
 * struct clk - ux500 clock structure
 * @ops:		pointer to clkops struct used to control this clock
 * @name:		name, for debugging
 * @enabled:		refcount. positive if enabled, zero if disabled
 * @get_rate:		custom callback for getting the clock rate
 * @data:		custom per-clock data for example for the get_rate
 *			callback
 * @rate:		fixed rate for clocks which don't implement
 * 			ops->getrate
 * @prcmu_cg_off:	address offset of the combined enable/disable register
 * 			(used on u8500v1)
 * @prcmu_cg_bit:	bit in the combined enable/disable register (used on
 * 			u8500v1)
 * @prcmu_cg_mgt:	address of the enable/disable register (used on
 * 			u8500ed)
 * @cluster:		peripheral cluster number
 * @prcc_bus:		bit for the bus clock in the peripheral's CLKRST
 * @prcc_kernel:	bit for the kernel clock in the peripheral's CLKRST.
 * 			-1 if no kernel clock exists.
 * @parent_cluster:	pointer to parent's cluster clk struct
 * @parent_periph:	pointer to parent's peripheral clk struct
 *
 * Peripherals are organised into clusters, and each cluster has an associated
 * bus clock.  Some peripherals also have a parent peripheral clock.
 *
 * In order to enable a clock for a peripheral, we need to enable:
 * 	(1) the parent cluster (bus) clock at the PRCMU level
 * 	(2) the parent peripheral clock (if any) at the PRCMU level
 * 	(3) the peripheral's bus & kernel clock at the PRCC level
 *
 * (1) and (2) are handled by defining clk structs (DEFINE_PRCMU_CLK) for each
 * of the cluster and peripheral clocks, and hooking these as the parents of
 * the individual peripheral clocks.
 *
 * (3) is handled by specifying the bits in the PRCC control registers required
 * to enable these clocks and modifying them in the ->enable and
 * ->disable callbacks of the peripheral clocks (DEFINE_PRCC_CLK).
 *
 * This structure describes both the PRCMU-level clocks and PRCC-level clocks.
 * The prcmu_* fields are only used for the PRCMU clocks, and the cluster,
 * prcc, and parent pointers are only used for the PRCC-level clocks.
 */
struct clk {
	const struct clkops	*ops;
	const char 		*name;
	unsigned int		enabled;
	unsigned long		(*get_rate)(struct clk *);
	void			*data;

	unsigned long		rate;
	struct list_head	list;

	/* These three are only for PRCMU clks */

	unsigned int		prcmu_cg_off;
	unsigned int		prcmu_cg_bit;
	unsigned int		prcmu_cg_mgt;

	/* The rest are only for PRCC clks */

	int			cluster;
	unsigned int		prcc_bus;
	unsigned int		prcc_kernel;

	struct clk		*parent_cluster;
	struct clk		*parent_periph;
#if defined(CONFIG_DEBUG_FS)
	struct dentry		*dent;		/* For visible tree hierarchy */
	struct dentry		*dent_bus;	/* For visible tree hierarchy */
#endif
};

#define DEFINE_PRCMU_CLK(_name, _cg_off, _cg_bit, _reg)		\
struct clk clk_##_name = {					\
		.name		= #_name,			\
		.ops    	= &clk_prcmu_ops, 		\
		.prcmu_cg_off	= _cg_off, 			\
		.prcmu_cg_bit	= _cg_bit,			\
		.prcmu_cg_mgt	= PRCM_##_reg##_MGT		\
	}

#define DEFINE_PRCMU_CLK_RATE(_name, _cg_off, _cg_bit, _reg, _rate)	\
struct clk clk_##_name = {						\
		.name		= #_name,				\
		.ops    	= &clk_prcmu_ops, 			\
		.prcmu_cg_off	= _cg_off, 				\
		.prcmu_cg_bit	= _cg_bit,				\
		.rate		= _rate,				\
		.prcmu_cg_mgt	= PRCM_##_reg##_MGT			\
	}

#define DEFINE_PRCC_CLK(_pclust, _name, _bus_en, _kernel_en, _kernclk)	\
struct clk clk_##_name = {						\
		.name		= #_name,				\
		.ops    	= &clk_prcc_ops, 			\
		.cluster 	= _pclust,				\
		.prcc_bus 	= _bus_en, 				\
		.prcc_kernel 	= _kernel_en, 				\
		.parent_cluster = &clk_per##_pclust##clk,		\
		.parent_periph 	= _kernclk				\
	}

#define DEFINE_PRCC_CLK_CUSTOM(_pclust, _name, _bus_en, _kernel_en, _kernclk, _callback, _data) \
struct clk clk_##_name = {						\
		.name		= #_name,				\
		.ops		= &clk_prcc_ops,			\
		.cluster	= _pclust,				\
		.prcc_bus	= _bus_en,				\
		.prcc_kernel	= _kernel_en,				\
		.parent_cluster = &clk_per##_pclust##clk,		\
		.parent_periph	= _kernclk,				\
		.get_rate	= _callback,				\
		.data		= (void *) _data			\
	}


#define CLK(_clk, _devname, _conname)			\
	{						\
		.clk	= &clk_##_clk,			\
		.dev_id	= _devname,			\
		.con_id = _conname,			\
	}

int __init clk_db8500_ed_fixup(void);
int __init clk_init(void);
