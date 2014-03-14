/*
 * Versatile Express Serial Power Controller (SPC) support
 *
 * Copyright (C) 2013 ARM Ltd.
 *
 * Authors: Sudeep KarkadaNagesha <sudeep.karkadanagesha@arm.com>
 *          Achin Gupta           <achin.gupta@arm.com>
 *          Lorenzo Pieralisi     <lorenzo.pieralisi@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/vexpress.h>

#include <asm/cacheflush.h>

#define SCC_CFGREG19		0x120
#define SCC_CFGREG20		0x124
#define A15_CONF		0x400
#define A7_CONF			0x500
#define SYS_INFO		0x700
#define PERF_LVL_A15		0xB00
#define PERF_REQ_A15		0xB04
#define PERF_LVL_A7		0xB08
#define PERF_REQ_A7		0xB0c
#define SYS_CFGCTRL		0xB10
#define SYS_CFGCTRL_REQ		0xB14
#define PWC_STATUS		0xB18
#define PWC_FLAG		0xB1c
#define WAKE_INT_MASK		0xB24
#define WAKE_INT_RAW		0xB28
#define WAKE_INT_STAT		0xB2c
#define A15_PWRDN_EN		0xB30
#define A7_PWRDN_EN		0xB34
#define A7_PWRDNACK		0xB54
#define A15_BX_ADDR0		0xB68
#define SYS_CFG_WDATA		0xB70
#define SYS_CFG_RDATA		0xB74
#define A7_BX_ADDR0		0xB78

#define GBL_WAKEUP_INT_MSK	(0x3 << 10)

#define CLKF_SHIFT		16
#define CLKF_MASK		0x1FFF
#define CLKR_SHIFT		0
#define CLKR_MASK		0x3F
#define CLKOD_SHIFT		8
#define CLKOD_MASK		0xF

#define OPP_FUNCTION		6
#define OPP_BASE_DEVICE		0x300
#define OPP_A15_OFFSET		0x4
#define OPP_A7_OFFSET		0xc

#define SYS_CFGCTRL_START	(1 << 31)
#define SYS_CFGCTRL_WRITE	(1 << 30)
#define SYS_CFGCTRL_FUNC(n)	(((n) & 0x3f) << 20)
#define SYS_CFGCTRL_DEVICE(n)	(((n) & 0xfff) << 0)

#define MAX_OPPS	8
#define MAX_CLUSTERS	2

enum {
	A15_OPP_TYPE		= 0,
	A7_OPP_TYPE		= 1,
	SYS_CFGCTRL_TYPE	= 2,
	INVALID_TYPE
};

#define STAT_COMPLETE(type)	((1 << 0) << (type << 2))
#define STAT_ERR(type)		((1 << 1) << (type << 2))
#define RESPONSE_MASK(type)	(STAT_COMPLETE(type) | STAT_ERR(type))

struct vexpress_spc_drvdata {
	void __iomem *baseaddr;
	u32 a15_clusid;
	int irq;
	u32 cur_req_type;
	u32 freqs[MAX_CLUSTERS][MAX_OPPS];
	int freqs_cnt[MAX_CLUSTERS];
};

enum spc_func_type {
	CONFIG_FUNC = 0,
	PERF_FUNC   = 1,
};

struct vexpress_spc_func {
	enum spc_func_type type;
	u32 function;
	u32 device;
};

static struct vexpress_spc_drvdata *info;
static u32 *vexpress_spc_config_data;
static struct vexpress_config_bridge *vexpress_spc_config_bridge;
static struct vexpress_config_func *opp_func, *perf_func;

static int vexpress_spc_load_result = -EAGAIN;

static bool vexpress_spc_initialized(void)
{
	return vexpress_spc_load_result == 0;
}

/**
 * vexpress_spc_write_resume_reg() - set the jump address used for warm boot
 *
 * @cluster: mpidr[15:8] bitfield describing cluster affinity level
 * @cpu: mpidr[7:0] bitfield describing cpu affinity level
 * @addr: physical resume address
 */
void vexpress_spc_write_resume_reg(u32 cluster, u32 cpu, u32 addr)
{
	void __iomem *baseaddr;

	if (WARN_ON_ONCE(cluster >= MAX_CLUSTERS))
		return;

	if (cluster != info->a15_clusid)
		baseaddr = info->baseaddr + A7_BX_ADDR0 + (cpu << 2);
	else
		baseaddr = info->baseaddr + A15_BX_ADDR0 + (cpu << 2);

	writel_relaxed(addr, baseaddr);
}

/**
 * vexpress_spc_get_nb_cpus() - get number of cpus in a cluster
 *
 * @cluster: mpidr[15:8] bitfield describing cluster affinity level
 *
 * Return: number of cpus in the cluster
 *         -EINVAL if cluster number invalid
 */
int vexpress_spc_get_nb_cpus(u32 cluster)
{
	u32 val;

	if (WARN_ON_ONCE(cluster >= MAX_CLUSTERS))
		return -EINVAL;

	val = readl_relaxed(info->baseaddr + SYS_INFO);
	val = (cluster != info->a15_clusid) ? (val >> 20) : (val >> 16);
	return val & 0xf;
}
EXPORT_SYMBOL_GPL(vexpress_spc_get_nb_cpus);

/**
 * vexpress_spc_get_performance - get current performance level of cluster
 * @cluster: mpidr[15:8] bitfield describing cluster affinity level
 * @freq: pointer to the performance level to be assigned
 *
 * Return: 0 on success
 *         < 0 on read error
 */
int vexpress_spc_get_performance(u32 cluster, u32 *freq)
{
	u32 perf_cfg_reg;
	int perf, ret;

	if (!vexpress_spc_initialized() || (cluster >= MAX_CLUSTERS))
		return -EINVAL;

	perf_cfg_reg = cluster != info->a15_clusid ? PERF_LVL_A7 : PERF_LVL_A15;
	ret = vexpress_config_read(perf_func, perf_cfg_reg, &perf);

	if (!ret)
		*freq = info->freqs[cluster][perf];

	return ret;
}
EXPORT_SYMBOL_GPL(vexpress_spc_get_performance);

/**
 * vexpress_spc_get_perf_index - get performance level corresponding to
 *				 a frequency
 * @cluster: mpidr[15:8] bitfield describing cluster affinity level
 * @freq: frequency to be looked-up
 *
 * Return: perf level index on success
 *         -EINVAL on error
 */
static int vexpress_spc_find_perf_index(u32 cluster, u32 freq)
{
	int idx;

	for (idx = 0; idx < info->freqs_cnt[cluster]; idx++)
		if (info->freqs[cluster][idx] == freq)
			break;
	return (idx == info->freqs_cnt[cluster]) ? -EINVAL : idx;
}

/**
 * vexpress_spc_set_performance - set current performance level of cluster
 *
 * @cluster: mpidr[15:8] bitfield describing cluster affinity level
 * @freq: performance level to be programmed
 *
 * Returns: 0 on success
 *          < 0 on write error
 */
int vexpress_spc_set_performance(u32 cluster, u32 freq)
{
	int ret, perf, offset;

	if (!vexpress_spc_initialized() || (cluster >= MAX_CLUSTERS))
		return -EINVAL;

	offset = (cluster != info->a15_clusid) ? PERF_LVL_A7 : PERF_LVL_A15;

	perf = vexpress_spc_find_perf_index(cluster, freq);

	if (perf < 0 || perf >= MAX_OPPS)
		return -EINVAL;

	ret = vexpress_config_write(perf_func, offset, perf);

	return ret;
}
EXPORT_SYMBOL_GPL(vexpress_spc_set_performance);

static void vexpress_spc_set_wake_intr(u32 mask)
{
	writel_relaxed(mask & VEXPRESS_SPC_WAKE_INTR_MASK,
		       info->baseaddr + WAKE_INT_MASK);
}

static inline void reg_bitmask(u32 *reg, u32 mask, bool set)
{
	if (set)
		*reg |= mask;
	else
		*reg &= ~mask;
}

/**
 * vexpress_spc_set_global_wakeup_intr()
 *
 * Function to set/clear global wakeup IRQs. Not protected by locking since
 * it might be used in code paths where normal cacheable locks are not
 * working. Locking must be provided by the caller to ensure atomicity.
 *
 * @set: if true, global wake-up IRQs are set, if false they are cleared
 */
void vexpress_spc_set_global_wakeup_intr(bool set)
{
	u32 wake_int_mask_reg = 0;

	wake_int_mask_reg = readl_relaxed(info->baseaddr + WAKE_INT_MASK);
	reg_bitmask(&wake_int_mask_reg, GBL_WAKEUP_INT_MSK, set);
	vexpress_spc_set_wake_intr(wake_int_mask_reg);
}

/**
 * vexpress_spc_set_cpu_wakeup_irq()
 *
 * Function to set/clear per-CPU wake-up IRQs. Not protected by locking since
 * it might be used in code paths where normal cacheable locks are not
 * working. Locking must be provided by the caller to ensure atomicity.
 *
 * @cpu: mpidr[7:0] bitfield describing cpu affinity level
 * @cluster: mpidr[15:8] bitfield describing cluster affinity level
 * @set: if true, wake-up IRQs are set, if false they are cleared
 */
void vexpress_spc_set_cpu_wakeup_irq(u32 cpu, u32 cluster, bool set)
{
	u32 mask = 0;
	u32 wake_int_mask_reg = 0;

	mask = 1 << cpu;
	if (info->a15_clusid != cluster)
		mask <<= 4;

	wake_int_mask_reg = readl_relaxed(info->baseaddr + WAKE_INT_MASK);
	reg_bitmask(&wake_int_mask_reg, mask, set);
	vexpress_spc_set_wake_intr(wake_int_mask_reg);
}

/**
 * vexpress_spc_powerdown_enable()
 *
 * Function to enable/disable cluster powerdown. Not protected by locking
 * since it might be used in code paths where normal cacheable locks are not
 * working. Locking must be provided by the caller to ensure atomicity.
 *
 * @cluster: mpidr[15:8] bitfield describing cluster affinity level
 * @enable: if true enables powerdown, if false disables it
 */
void vexpress_spc_powerdown_enable(u32 cluster, bool enable)
{
	u32 pwdrn_reg = 0;

	if (cluster >= MAX_CLUSTERS)
		return;
	pwdrn_reg = cluster != info->a15_clusid ? A7_PWRDN_EN : A15_PWRDN_EN;
	writel_relaxed(enable, info->baseaddr + pwdrn_reg);
}

irqreturn_t vexpress_spc_irq_handler(int irq, void *data)
{
	int ret;
	u32 status = readl_relaxed(info->baseaddr + PWC_STATUS);

	if (!(status & RESPONSE_MASK(info->cur_req_type)))
		return IRQ_NONE;

	if ((status == STAT_COMPLETE(SYS_CFGCTRL_TYPE))
			&& vexpress_spc_config_data) {
		*vexpress_spc_config_data =
				readl_relaxed(info->baseaddr + SYS_CFG_RDATA);
		vexpress_spc_config_data = NULL;
	}

	ret = STAT_COMPLETE(info->cur_req_type) ? 0 : -EIO;
	info->cur_req_type = INVALID_TYPE;
	vexpress_config_complete(vexpress_spc_config_bridge, ret);
	return IRQ_HANDLED;
}

/**
 * Based on the firmware documentation, this is always fixed to 20
 * All the 4 OSC: A15 PLL0/1, A7 PLL0/1 must be programmed same
 * values for both control and value registers.
 * This function uses A15 PLL 0 registers to compute multiple factor
 * F out = F in * (CLKF + 1) / ((CLKOD + 1) * (CLKR + 1))
 */
static inline int __get_mult_factor(void)
{
	int i_div, o_div, f_div;
	u32 tmp;

	tmp = readl(info->baseaddr + SCC_CFGREG19);
	f_div = (tmp >> CLKF_SHIFT) & CLKF_MASK;

	tmp = readl(info->baseaddr + SCC_CFGREG20);
	o_div = (tmp >> CLKOD_SHIFT) & CLKOD_MASK;
	i_div = (tmp >> CLKR_SHIFT) & CLKR_MASK;

	return (f_div + 1) / ((o_div + 1) * (i_div + 1));
}

/**
 * vexpress_spc_populate_opps() - initialize opp tables from microcontroller
 *
 * @cluster: mpidr[15:8] bitfield describing cluster affinity level
 *
 * Return: 0 on success
 *         < 0 on error
 */
static int vexpress_spc_populate_opps(u32 cluster)
{
	u32 data = 0, ret, i, offset;
	int mult_fact = __get_mult_factor();

	if (WARN_ON_ONCE(cluster >= MAX_CLUSTERS))
		return -EINVAL;

	offset = cluster != info->a15_clusid ? OPP_A7_OFFSET : OPP_A15_OFFSET;
	for (i = 0; i < MAX_OPPS; i++) {
		ret = vexpress_config_read(opp_func, i + offset, &data);
		if (!ret)
			info->freqs[cluster][i] = (data & 0xFFFFF) * mult_fact;
		else
			break;
	}

	info->freqs_cnt[cluster] = i;
	return ret;
}

/**
 * vexpress_spc_get_freq_table() - Retrieve a pointer to the frequency
 *				   table for a given cluster
 *
 * @cluster: mpidr[15:8] bitfield describing cluster affinity level
 * @fptr: pointer to be initialized
 * Return: operating points count on success
 *         -EINVAL on pointer error
 */
int vexpress_spc_get_freq_table(u32 cluster, u32 **fptr)
{
	if (WARN_ON_ONCE(!fptr || cluster >= MAX_CLUSTERS))
		return -EINVAL;
	*fptr = info->freqs[cluster];
	return info->freqs_cnt[cluster];
}
EXPORT_SYMBOL_GPL(vexpress_spc_get_freq_table);

static void *vexpress_spc_func_get(struct device *dev,
		struct device_node *node, const char *id)
{
	struct vexpress_spc_func *spc_func;
	u32 func_device[2];
	int err = 0;

	spc_func = kzalloc(sizeof(*spc_func), GFP_KERNEL);
	if (!spc_func)
		return NULL;

	if (strcmp(id, "opp") == 0) {
		spc_func->type = CONFIG_FUNC;
		spc_func->function = OPP_FUNCTION;
		spc_func->device = OPP_BASE_DEVICE;
	} else if (strcmp(id, "perf") == 0) {
		spc_func->type = PERF_FUNC;
	} else if (node) {
		of_node_get(node);
		err = of_property_read_u32_array(node,
				"arm,vexpress-sysreg,func", func_device,
				ARRAY_SIZE(func_device));
		of_node_put(node);
		spc_func->type = CONFIG_FUNC;
		spc_func->function = func_device[0];
		spc_func->device = func_device[1];
	}

	if (WARN_ON(err)) {
		kfree(spc_func);
		return NULL;
	}

	pr_debug("func 0x%p = 0x%x, %d %d\n", spc_func,
					     spc_func->function,
					     spc_func->device,
					     spc_func->type);

	return spc_func;
}

static void vexpress_spc_func_put(void *func)
{
	kfree(func);
}

static int vexpress_spc_func_exec(void *func, int offset, bool write,
				  u32 *data)
{
	struct vexpress_spc_func *spc_func = func;
	u32 command;

	if (!data)
		return -EINVAL;
	/*
	 * Setting and retrieval of operating points is not part of
	 * DCC config interface. It was made to go through the same
	 * code path so that requests to the M3 can be serialized
	 * properly with config reads/writes through the common
	 * vexpress config interface
	 */
	switch (spc_func->type) {
	case PERF_FUNC:
		if (write) {
			info->cur_req_type = (offset == PERF_LVL_A15) ?
					A15_OPP_TYPE : A7_OPP_TYPE;
			writel_relaxed(*data, info->baseaddr + offset);
			return VEXPRESS_CONFIG_STATUS_WAIT;
		} else {
			*data = readl_relaxed(info->baseaddr + offset);
			return VEXPRESS_CONFIG_STATUS_DONE;
		}
	case CONFIG_FUNC:
		info->cur_req_type = SYS_CFGCTRL_TYPE;

		command = SYS_CFGCTRL_START;
		command |= write ? SYS_CFGCTRL_WRITE : 0;
		command |= SYS_CFGCTRL_FUNC(spc_func->function);
		command |= SYS_CFGCTRL_DEVICE(spc_func->device + offset);

		pr_debug("command %x\n", command);

		if (!write)
			vexpress_spc_config_data = data;
		else
			writel_relaxed(*data, info->baseaddr + SYS_CFG_WDATA);
		writel_relaxed(command, info->baseaddr + SYS_CFGCTRL);

		return VEXPRESS_CONFIG_STATUS_WAIT;
	default:
		return -EINVAL;
	}
}

struct vexpress_config_bridge_info vexpress_spc_config_bridge_info = {
	.name = "vexpress-spc",
	.func_get = vexpress_spc_func_get,
	.func_put = vexpress_spc_func_put,
	.func_exec = vexpress_spc_func_exec,
};

static const struct of_device_id vexpress_spc_ids[] __initconst = {
	{ .compatible = "arm,vexpress-spc,v2p-ca15_a7" },
	{ .compatible = "arm,vexpress-spc" },
	{},
};

static int __init vexpress_spc_init(void)
{
	int ret;
	struct device_node *node = of_find_matching_node(NULL,
							 vexpress_spc_ids);

	if (!node)
		return -ENODEV;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		pr_err("%s: unable to allocate mem\n", __func__);
		return -ENOMEM;
	}
	info->cur_req_type = INVALID_TYPE;

	info->baseaddr = of_iomap(node, 0);
	if (WARN_ON(!info->baseaddr)) {
		ret = -ENXIO;
		goto mem_free;
	}

	info->irq = irq_of_parse_and_map(node, 0);

	if (WARN_ON(!info->irq)) {
		ret = -ENXIO;
		goto unmap;
	}

	readl_relaxed(info->baseaddr + PWC_STATUS);

	ret = request_irq(info->irq, vexpress_spc_irq_handler,
		IRQF_DISABLED | IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
		"arm-spc", info);

	if (ret) {
		pr_err("IRQ %d request failed\n", info->irq);
		ret = -ENODEV;
		goto unmap;
	}

	info->a15_clusid = readl_relaxed(info->baseaddr + A15_CONF) & 0xf;

	vexpress_spc_config_bridge = vexpress_config_bridge_register(
			node, &vexpress_spc_config_bridge_info);

	if (WARN_ON(!vexpress_spc_config_bridge)) {
		ret = -ENODEV;
		goto unmap;
	}

	opp_func = vexpress_config_func_get(vexpress_spc_config_bridge, "opp");
	perf_func =
		vexpress_config_func_get(vexpress_spc_config_bridge, "perf");

	if (!opp_func || !perf_func) {
		ret = -ENODEV;
		goto unmap;
	}

	if (vexpress_spc_populate_opps(0) || vexpress_spc_populate_opps(1)) {
		if (info->irq)
			free_irq(info->irq, info);
		pr_err("failed to build OPP table\n");
		ret = -ENODEV;
		goto unmap;
	}
	/*
	 * Multi-cluster systems may need this data when non-coherent, during
	 * cluster power-up/power-down. Make sure it reaches main memory:
	 */
	sync_cache_w(info);
	sync_cache_w(&info);
	pr_info("vexpress-spc loaded at %p\n", info->baseaddr);
	return 0;

unmap:
	iounmap(info->baseaddr);

mem_free:
	kfree(info);
	return ret;
}

static bool __init __vexpress_spc_check_loaded(void);
/*
 * Pointer spc_check_loaded is swapped after init hence it is safe
 * to initialize it to a function in the __init section
 */
static bool (*spc_check_loaded)(void) __refdata = &__vexpress_spc_check_loaded;

static bool __init __vexpress_spc_check_loaded(void)
{
	if (vexpress_spc_load_result == -EAGAIN)
		vexpress_spc_load_result = vexpress_spc_init();
	spc_check_loaded = &vexpress_spc_initialized;
	return vexpress_spc_initialized();
}

/*
 * Function exported to manage early_initcall ordering.
 * SPC code is needed very early in the boot process
 * to bring CPUs out of reset and initialize power
 * management back-end. After boot swap pointers to
 * make the functionality check available to loadable
 * modules, when early boot init functions have been
 * already freed from kernel address space.
 */
bool vexpress_spc_check_loaded(void)
{
	return spc_check_loaded();
}
EXPORT_SYMBOL_GPL(vexpress_spc_check_loaded);

static int __init vexpress_spc_early_init(void)
{
	__vexpress_spc_check_loaded();
	return vexpress_spc_load_result;
}
early_initcall(vexpress_spc_early_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Serial Power Controller (SPC) support");
