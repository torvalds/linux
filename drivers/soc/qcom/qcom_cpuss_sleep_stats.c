// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <asm/cputype.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/qcom_scm.h>
#include <linux/slab.h>

#define MAX_POSSIBLE_CPUS	8
#define MIN_POSSIBLE_CPUS	1
#define SEQ_LPM_STR_SZ		22
#define CPUNAME_SZ		6
#define STATS_NAME_SZ		25
#define OFFSET_8BYTES		0x08
#define OFFSET_4BYTES		0x04

#define MAX_CPU_LPM_BITS	4
#define MAX_CL_LPM_BITS		4
#define MAX_CPU_RESIDENCY_BITS	5
#define MAX_CL_RESIDENCY_BITS	3

enum {
	APSS_LPM_COUNTER_CPUx_C1_LO_VAL,
	APSS_LPM_COUNTER_CPUx_C2D_LO_VAL,
	APSS_LPM_COUNTER_CPUx_C3_LO_VAL,
	APSS_LPM_COUNTER_CPUx_C4_LO_VAL,
	APSS_CPU_LPM_RESIDENCY_CNTR_CFG_n,
	APSS_CL_LPM_RESIDENCY_CNTR_CFG,
	APSS_LPM_RESIDENCY_C2_D2_CNTR_n,
	APSS_LPM_RESIDENCY_C3_CNTR_n,
	APSS_LPM_RESIDENCY_C4_D4_CNTR_n,
};

static u32 qcom_cpuss_cntr_v1_offsets[] = {
	[APSS_LPM_COUNTER_CPUx_C1_LO_VAL]	=	0x8000,
	[APSS_LPM_COUNTER_CPUx_C2D_LO_VAL]	=	0x8048,
	[APSS_LPM_COUNTER_CPUx_C3_LO_VAL]	=	0x8090,
	[APSS_LPM_COUNTER_CPUx_C4_LO_VAL]	=	0x80D8,
	[APSS_CPU_LPM_RESIDENCY_CNTR_CFG_n]	=	0xC004,
	[APSS_CL_LPM_RESIDENCY_CNTR_CFG]	=	0xC030,
	[APSS_LPM_RESIDENCY_C2_D2_CNTR_n]	=	0xC040,
	[APSS_LPM_RESIDENCY_C3_CNTR_n]		=	0xC090,
	[APSS_LPM_RESIDENCY_C4_D4_CNTR_n]	=	0xC0D0,
};

static u32 qcom_cpuss_cntr_v2_offsets[] = {
	[APSS_LPM_COUNTER_CPUx_C1_LO_VAL]	=	0x8000,
	[APSS_LPM_COUNTER_CPUx_C2D_LO_VAL]	=	0x8028,
	[APSS_LPM_COUNTER_CPUx_C3_LO_VAL]	=	0x8050,
	[APSS_LPM_COUNTER_CPUx_C4_LO_VAL]	=	0x8078,
	[APSS_CPU_LPM_RESIDENCY_CNTR_CFG_n]	=	0xC004,
	[APSS_CL_LPM_RESIDENCY_CNTR_CFG]	=	0xC030,
	[APSS_LPM_RESIDENCY_C2_D2_CNTR_n]	=	0xC040,
	[APSS_LPM_RESIDENCY_C3_CNTR_n]		=	0xC090,
	[APSS_LPM_RESIDENCY_C4_D4_CNTR_n]	=	0xC0D0,
};


static u32 qcom_cpuss_cntr_v3_offsets[] = {
	[APSS_LPM_COUNTER_CPUx_C1_LO_VAL]	=	0x8000,
	[APSS_LPM_COUNTER_CPUx_C2D_LO_VAL]	=	0x8038,
	[APSS_LPM_COUNTER_CPUx_C3_LO_VAL]	=	0x8070,
	[APSS_LPM_COUNTER_CPUx_C4_LO_VAL]	=	0x80A8,
	[APSS_CPU_LPM_RESIDENCY_CNTR_CFG_n]	=	0xC004,
	[APSS_CL_LPM_RESIDENCY_CNTR_CFG]	=	0xC030,
	[APSS_LPM_RESIDENCY_C2_D2_CNTR_n]	=	0xC040,
	[APSS_LPM_RESIDENCY_C3_CNTR_n]		=	0xC090,
	[APSS_LPM_RESIDENCY_C4_D4_CNTR_n]	=	0xC0D0,
};

/* bits are per CPU LPM_CFG register */
#define LPM_COUNTER_EN_C1	BIT(0)
#define LPM_COUNTER_EN_C2D	BIT(1)
#define LPM_COUNTER_EN_C3	BIT(2)
#define LPM_COUNTER_EN_C4	BIT(3)
#define LPM_COUNTER_RESET_C1	BIT(4)
#define LPM_COUNTER_RESET_C2D	BIT(5)
#define LPM_COUNTER_RESET_C3	BIT(6)
#define LPM_COUNTER_RESET_C4	BIT(7)

#define RESET_ALL_CPU_LPM	(LPM_COUNTER_RESET_C1 | \
				LPM_COUNTER_RESET_C2D | LPM_COUNTER_RESET_C3 | \
				LPM_COUNTER_RESET_C4)

/* bits are per CL LPM_CFG register */
#define LPM_COUNTER_EN_D1	BIT(0)
#define LPM_COUNTER_EN_D2D	BIT(1)
#define LPM_COUNTER_EN_D3	BIT(2)
#define LPM_COUNTER_EN_D4	BIT(3)
#define LPM_COUNTER_RESET_D1	BIT(4)
#define LPM_COUNTER_RESET_D2D	BIT(5)
#define LPM_COUNTER_RESET_D3	BIT(6)
#define LPM_COUNTER_RESET_D4	BIT(7)


#define RESET_ALL_CL_LPM	(LPM_COUNTER_RESET_D1 | \
				LPM_COUNTER_RESET_D2D | LPM_COUNTER_RESET_D3 | \
				LPM_COUNTER_RESET_D4)

/* Core residency cfg as per register */
#define RESIDENCY_CNTR_C2_EN	BIT(0)
#define RESIDENCY_CNTR_C2_CLR	BIT(1)
#define RESIDENCY_CNTR_C3_EN	BIT(2)
#define RESIDENCY_CNTR_C3_CLR	BIT(3)
#define RESIDENCY_CNTR_C4_EN	BIT(4)
#define	RESIDENCY_CNTR_C4_CLR	BIT(5)

#define CLR_ALL_CPU_RESIDENCY	(RESIDENCY_CNTR_C2_CLR | \
				RESIDENCY_CNTR_C3_CLR | RESIDENCY_CNTR_C4_CLR)

/* Cluster residency bits as per cfg register*/
#define RESIDENCY_CNTR_D2_EN	BIT(0)
#define RESIDENCY_CNTR_D2_CLR	BIT(1)
#define RESIDENCY_CNTR_D4_EN	BIT(2)
#define RESIDENCY_CNTR_D4_CLR	BIT(3)

#define CLR_CL_RESIDENCY	(RESIDENCY_CNTR_D2_CLR | \
				RESIDENCY_CNTR_D4_CLR)

struct qcom_cpuss_stats {
	char mode_name[20];
	void __iomem *reg;	/* iomapped reg */
	struct list_head node;
};

struct qcom_target_info {
	struct platform_device *pdev;
	int ncpu;
	phys_addr_t per_cpu_lpm_cfg[MAX_POSSIBLE_CPUS];
	u32 per_cpu_lpm_cfg_size[MAX_POSSIBLE_CPUS];
	phys_addr_t apss_seq_mem_base;
	u32 apss_seq_mem_size;
	phys_addr_t l3_seq_lpm_cfg;
	u32 l3_seq_lpm_size;
	u32 *offsets;
	u8 cpu_pcpu_map[MAX_POSSIBLE_CPUS];
	struct qcom_cpuss_stats complete_stats;

	struct dentry *stats_rootdir;
	struct dentry *cpu_dir[MAX_POSSIBLE_CPUS];
	struct dentry *cl_rootdir;
	struct mutex stats_reset_lock;
};

static char *get_str_cpu_lpm_state(u8 state)
{
	switch (state) {
	case LPM_COUNTER_EN_C1:
		return "C1_count";
	case LPM_COUNTER_EN_C2D:
		return "C2D_count";
	case LPM_COUNTER_EN_C3:
		return "C3_count";
	case LPM_COUNTER_EN_C4:
		return "C4_count";
	default:
		return NULL;
	}
}

static char *get_str_cl_lpm_state(u8 state)
{
	switch (state) {
	case LPM_COUNTER_EN_D1:
		return "D1_count";
	case LPM_COUNTER_EN_D2D:
		return "D2D_count";
	case LPM_COUNTER_EN_D3:
		return "D3_count";
	case LPM_COUNTER_EN_D4:
		return "D4_count";
	default:
		return NULL;
	}
}

static char *get_str_cpu_res(u32 cfg)
{
	switch (cfg) {
	case RESIDENCY_CNTR_C2_EN:
		return "C2_residency";
	case RESIDENCY_CNTR_C3_EN:
		return "C3_residency";
	case RESIDENCY_CNTR_C4_EN:
		return "C4_residency";
	default:
		return NULL;
	}
}

static char *get_str_cl_res(u8 cfg)
{
	switch (cfg) {
	case RESIDENCY_CNTR_D2_EN:
		return "D2_residency";
	case RESIDENCY_CNTR_D4_EN:
		return "D4_residency";
	default:
		return NULL;
	}

}

static int get_cpu_lpm_read_offset(u8 cpu, u8 cpu_mode, u32 *regs)
{
	int offset;

	switch (cpu_mode) {
	case LPM_COUNTER_EN_C1:
		offset = regs[APSS_LPM_COUNTER_CPUx_C1_LO_VAL] + (cpu * OFFSET_8BYTES);
		break;
	case LPM_COUNTER_EN_C2D:
		offset = regs[APSS_LPM_COUNTER_CPUx_C2D_LO_VAL] + (cpu * OFFSET_8BYTES);
		break;
	case LPM_COUNTER_EN_C3:
		offset = regs[APSS_LPM_COUNTER_CPUx_C3_LO_VAL] + (cpu * OFFSET_8BYTES);
		break;
	case LPM_COUNTER_EN_C4:
		offset = regs[APSS_LPM_COUNTER_CPUx_C4_LO_VAL] + (cpu * OFFSET_8BYTES);
		break;
	default:
		pr_err("Unknown mode\n");
		offset = -EINVAL;
	}

	return offset;
}

static int get_cpu_residency_read_offset(u8 cpu, u8 cpu_res_cfg, u32 *regs)
{
	int offset;

	switch (cpu_res_cfg) {
	case RESIDENCY_CNTR_C2_EN:
		offset = regs[APSS_LPM_RESIDENCY_C2_D2_CNTR_n] + (cpu * OFFSET_8BYTES);
		break;
	case RESIDENCY_CNTR_C3_EN:
		offset = regs[APSS_LPM_RESIDENCY_C3_CNTR_n] + (cpu * OFFSET_8BYTES);
		break;
	case RESIDENCY_CNTR_C4_EN:
		offset = regs[APSS_LPM_RESIDENCY_C4_D4_CNTR_n] + (cpu * OFFSET_8BYTES);
		break;
	default:
		pr_err("Unknown mode\n");
		offset = -EINVAL;
	}

	return offset;
}

static int get_cl_residency_read_offset(int ncpu, u8 cl_res_cfg, u32 *regs)
{
	int offset;

	switch (cl_res_cfg) {
	case RESIDENCY_CNTR_D2_EN:
		offset = regs[APSS_LPM_RESIDENCY_C2_D2_CNTR_n] + OFFSET_8BYTES * ncpu;
		break;
	case RESIDENCY_CNTR_D4_EN:
		offset = regs[APSS_LPM_RESIDENCY_C4_D4_CNTR_n] + OFFSET_8BYTES * ncpu;
		break;
	default:
		pr_err("Unknown mode\n");
		offset = -EINVAL;
	}

	return offset;
}

static int get_cl_lpm_read_offset(int ncpu, u8 cl_mode, u32 *regs)
{
	int offset;

	switch (cl_mode) {
	case LPM_COUNTER_EN_D1:
		offset = regs[APSS_LPM_COUNTER_CPUx_C1_LO_VAL] + ncpu * OFFSET_8BYTES;
		break;
	case LPM_COUNTER_EN_D2D:
		offset = regs[APSS_LPM_COUNTER_CPUx_C2D_LO_VAL] + ncpu * OFFSET_8BYTES;
		break;
	case LPM_COUNTER_EN_D3:
		offset = regs[APSS_LPM_COUNTER_CPUx_C3_LO_VAL] + ncpu * OFFSET_8BYTES;
		break;
	case LPM_COUNTER_EN_D4:
		offset = regs[APSS_LPM_COUNTER_CPUx_C4_LO_VAL] + ncpu * OFFSET_8BYTES;
		break;
	default:
		pr_err("Unknown mode\n");
		offset = -EINVAL;
	}

	return offset;
}

static int qcom_cpuss_sleep_stats_show(struct seq_file *s, void *d)
{
	void __iomem *reg = (void __iomem *)s->private;
	u64 val;

	val = readq_relaxed(reg);
	seq_printf(s, "%ld\n", val);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(qcom_cpuss_sleep_stats);

static int qcom_cpuss_all_stats_show(struct seq_file *s, void *d)
{
	struct list_head *node1 = (struct list_head *)s->private;
	struct qcom_cpuss_stats *data;
	u64 count;

	list_for_each_entry(data, node1, node) {
		count = readq_relaxed(data->reg);
		seq_printf(s, "%s: %ld\n", data->mode_name, count);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(qcom_cpuss_all_stats);

static ssize_t qcom_cpuss_reset_clear_lpm_residency(phys_addr_t addr, u32 new_val)
{
	u32 val;
	int ret = 0;

	ret = qcom_scm_io_readl(addr, &val);
	if (ret)
		goto error;

	val |= new_val;
	ret = qcom_scm_io_writel(addr, val);
	if (ret)
		goto error;

	/* clear reset */
	val &= ~new_val;
	ret = qcom_scm_io_writel(addr, val);
	if (ret)
		goto error;

	return 0;
error:
	pr_err("Cannot reset lpm hw count and residency\n");
	return -EINVAL;
}

static bool check_val(const char __user *in, size_t count)
{
	loff_t ppos = 0;
	char buffer[2] = {0};
	int ret;

	ret = simple_write_to_buffer(buffer, sizeof(buffer) - 1,
				     &ppos, in, count - 1);
	if (ret > 0)
		return strcmp(buffer, "1") ? false : true;

	return false;
}

static ssize_t qcom_cpuss_stats_reset_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *off)
{
	struct inode *in = file->f_inode;
	struct qcom_target_info *t_info;
	int ncpu;
	u8 i, j;
	ssize_t ret;
	struct platform_device *pdev;

	t_info = (struct qcom_target_info *)in->i_private;
	ncpu = t_info->ncpu;
	pdev = t_info->pdev;

	if (!check_val(buffer, count))
		return -EINVAL;

	/* Reset cpu LPM/Residencies */
	mutex_lock(&t_info->stats_reset_lock);
	for (j = 0; j < ncpu; j++) {
		i = t_info->cpu_pcpu_map[j];
		if (i == U8_MAX)
			continue;

		ret = qcom_cpuss_reset_clear_lpm_residency(t_info->per_cpu_lpm_cfg[j],
							   RESET_ALL_CPU_LPM);
		if (ret)
			goto error;

		ret = qcom_cpuss_reset_clear_lpm_residency(t_info->apss_seq_mem_base +
				t_info->offsets[APSS_CPU_LPM_RESIDENCY_CNTR_CFG_n] + 0x4 * i,
				CLR_ALL_CPU_RESIDENCY);
		if (ret)
			goto error;
	}

	/* Reset cluster LPM/Residencies */
	ret = qcom_cpuss_reset_clear_lpm_residency(t_info->l3_seq_lpm_cfg,
						   RESET_ALL_CL_LPM);
	if (ret)
		goto error;

	ret = qcom_cpuss_reset_clear_lpm_residency(t_info->apss_seq_mem_base +
			t_info->offsets[APSS_CL_LPM_RESIDENCY_CNTR_CFG], CLR_CL_RESIDENCY);
	if (ret)
		goto error;
	mutex_unlock(&t_info->stats_reset_lock);

	return count;

error:
	mutex_unlock(&t_info->stats_reset_lock);
	return ret;
}

static const struct file_operations qcom_cpuss_stats_reset_fops = {
	.owner		= THIS_MODULE,
	.write		= qcom_cpuss_stats_reset_write,
};

static int store_stats_data(struct qcom_target_info *t_info, char *str,
			     void __iomem *reg)
{
	struct qcom_cpuss_stats *store_stats_data;

	store_stats_data = devm_kzalloc(&t_info->pdev->dev, sizeof(*store_stats_data),
					GFP_KERNEL);
	if (!store_stats_data)
		return -ENOMEM;

	store_stats_data->reg = reg;
	strscpy(store_stats_data->mode_name, str,
		sizeof(store_stats_data->mode_name));

	list_add_tail(&store_stats_data->node, &t_info->complete_stats.node);

	return 0;
}

static int qcom_cpuss_sleep_stats_create_cpu_debugfs(struct qcom_target_info *t_info,
					     u8 cpu, u32 lpm_cfg)
{
	void __iomem *reg, *base;
	struct platform_device *pdev = t_info->pdev;
	u32 offset;
	int bit, ret;
	char cpu_name[CPUNAME_SZ] = {0}, stats_name[STATS_NAME_SZ] = {0}, *state;

	snprintf(cpu_name, sizeof(cpu_name), "pcpu%u", cpu);
	t_info->cpu_dir[cpu] = debugfs_create_dir(cpu_name,
						  t_info->stats_rootdir);

	base = devm_ioremap(&pdev->dev, t_info->apss_seq_mem_base,
			    t_info->apss_seq_mem_size);
	if (!base)
		return -ENOMEM;

	for (bit = 0; bit < MAX_CPU_LPM_BITS; bit++) {
		if (lpm_cfg & BIT(bit)) {
			offset = get_cpu_lpm_read_offset(cpu, BIT(bit), t_info->offsets);
			if (offset == -EINVAL)
				return offset;

			reg = base + offset;
			state = get_str_cpu_lpm_state(1 << bit);
			if (state) {
				debugfs_create_file(state, 0444,
						    t_info->cpu_dir[cpu],
						    (void *) reg,
						    &qcom_cpuss_sleep_stats_fops);
				snprintf(stats_name, sizeof(stats_name),
					 "pcpu%u: %s", cpu, state);
				ret = store_stats_data(t_info, stats_name, reg);
				if (ret)
					return ret;
			}
		}
	}

	return 0;
}

static int qcom_cpuss_sleep_stats_create_cluster_debugfs(struct qcom_target_info *t_info,
						u32 cl_cfg)
{
	void __iomem *reg, *base;
	struct platform_device *pdev = t_info->pdev;
	u32 offset;
	int bit, ret;
	char *state;

	t_info->cl_rootdir = debugfs_create_dir("L3", t_info->stats_rootdir);
	if (!t_info->cl_rootdir)
		return -ENOMEM;

	base = devm_ioremap(&pdev->dev, t_info->apss_seq_mem_base,
			    t_info->apss_seq_mem_size);
	if (!base)
		return -ENOMEM;

	for (bit = 0; bit < MAX_CL_LPM_BITS; bit++) {
		if (cl_cfg & BIT(bit)) {
			offset = get_cl_lpm_read_offset(t_info->ncpu, BIT(bit), t_info->offsets);
			if (offset == -EINVAL)
				return offset;

			reg = base + offset;
			state = get_str_cl_lpm_state(1 << bit);
			if (state) {
				debugfs_create_file(state, 0444,
						    t_info->cl_rootdir,
						    (void *) reg,
						    &qcom_cpuss_sleep_stats_fops);
				ret = store_stats_data(t_info, state, reg);
				if (ret)
					return ret;
			}
		}
	}

	return 0;
}

static int qcom_cpuss_sleep_stats_create_cpu_residency_debugfs(struct qcom_target_info *t_info,
						u8 cpu, u32 residency_cfg)
{
	void __iomem *reg, *base;
	struct platform_device *pdev = t_info->pdev;
	u32 offset;
	int bit, ret;
	char stats_name[STATS_NAME_SZ] = {0}, *state;

	base = devm_ioremap(&pdev->dev, t_info->apss_seq_mem_base,
			    t_info->apss_seq_mem_size);
	if (!base)
		return -ENOMEM;

	for (bit = 0; bit < MAX_CPU_RESIDENCY_BITS; bit += 2) {
		if (residency_cfg & BIT(bit)) {
			offset = get_cpu_residency_read_offset(cpu, BIT(bit), t_info->offsets);
			if (offset == -EINVAL)
				return offset;

			reg = base + offset;
			state = get_str_cpu_res(1 << bit);
			if (state) {
				debugfs_create_file(state, 0444,
						    t_info->cpu_dir[cpu],
						    (void *) reg,
						    &qcom_cpuss_sleep_stats_fops);
				snprintf(stats_name, sizeof(stats_name),
					 "pcpu%u: %s", cpu, state);
				ret = store_stats_data(t_info, stats_name, reg);
				if (ret)
					return ret;
			}
		}
	}

	return 0;
}

static int qcom_cpuss_sleep_stats_create_cl_residency_debugfs(struct qcom_target_info *t_info,
						u32 cl_residency_cfg)
{
	void __iomem *reg;
	struct platform_device *pdev = t_info->pdev;
	u32 offset;
	int bit, ret;
	char *state;

	for (bit = 0; bit < MAX_CL_RESIDENCY_BITS; bit += 2) {
		if (cl_residency_cfg & BIT(bit)) {
			offset = get_cl_residency_read_offset(t_info->ncpu,
							      BIT(bit), t_info->offsets);
			if (offset == -EINVAL)
				return offset;

		reg = devm_ioremap(&pdev->dev, t_info->apss_seq_mem_base +
				   offset, 0x4);
		if (!reg)
			return -ENOMEM;
		state = get_str_cl_res(1 << bit);
		if (state) {
			debugfs_create_file(state, 0444, t_info->cl_rootdir,
					    (void *) reg,
					    &qcom_cpuss_sleep_stats_fops);
			ret = store_stats_data(t_info, state, reg);
			if (ret)
				return ret;
		}
		}
	}

	return 0;
}

static int qcom_cpuss_read_lpm_and_residency_cfg_informaion(struct qcom_target_info *t_info)
{
	u32 val;
	u8 i, j;
	int ret;
	phys_addr_t addr;

	/* per cpu lpm and residency */
	for (j = 0; j < t_info->ncpu; j++) {
		i = t_info->cpu_pcpu_map[j];
		if (i == U8_MAX)
			continue;

		addr = t_info->per_cpu_lpm_cfg[j];
		ret = qcom_scm_io_readl(addr, &val);
		if (ret)
			return -EINVAL;

		ret = qcom_cpuss_sleep_stats_create_cpu_debugfs(t_info, i, val);
		if (ret)
			return ret;

		addr = t_info->apss_seq_mem_base;
		addr += (t_info->offsets[APSS_CPU_LPM_RESIDENCY_CNTR_CFG_n] + OFFSET_4BYTES * i);
		ret = qcom_scm_io_readl(addr, &val);
		if (ret)
			return -EINVAL;

		ret = qcom_cpuss_sleep_stats_create_cpu_residency_debugfs(t_info, i, val);
		if (ret)
			return ret;
	}

	/* cluster lpm */
	addr = t_info->l3_seq_lpm_cfg;
	ret = qcom_scm_io_readl(addr, &val);
	if (ret)
		return -EINVAL;

	ret = qcom_cpuss_sleep_stats_create_cluster_debugfs(t_info, val);
	if (ret)
		return ret;

	/* cluster residency */
	addr = t_info->apss_seq_mem_base + t_info->offsets[APSS_CL_LPM_RESIDENCY_CNTR_CFG];
	ret = qcom_scm_io_readl(addr, &val);
	if (ret)
		return -EINVAL;

	ret = qcom_cpuss_sleep_stats_create_cl_residency_debugfs(t_info, val);

	return ret;
}

static void get_mpidr_cpu(void *cpu)
{
	u64 mpidr = read_cpuid_mpidr() & MPIDR_HWID_BITMASK;

	*((uint32_t *)cpu) = MPIDR_AFFINITY_LEVEL(mpidr, 1);
}

static int qcom_cpuss_sleep_stats_probe(struct platform_device *pdev)
{
	int ret;
	struct dentry *root_dir;
	struct qcom_target_info *t_info;
	struct resource *res;
	int cpu, pcpu;

	t_info = devm_kzalloc(&pdev->dev, sizeof(struct qcom_target_info),
			      GFP_KERNEL);
	if (!t_info)
		return -ENOMEM;

	INIT_LIST_HEAD(&t_info->complete_stats.node);

	root_dir = debugfs_create_dir("qcom_cpuss_sleep_stats", NULL);
	t_info->stats_rootdir = root_dir;
	t_info->pdev = pdev;

	memset(t_info->cpu_pcpu_map, U8_MAX, MAX_POSSIBLE_CPUS);
	/* Get cfg address for cpu/cluster */
	for_each_possible_cpu(cpu) {
		char reg_name[SEQ_LPM_STR_SZ] = {0};

		smp_call_function_single(cpu, get_mpidr_cpu, &pcpu, true);
		snprintf(reg_name, sizeof(reg_name), "seq_lpm_cntr_cfg_cpu%u",
			 pcpu);
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   reg_name);
		if (!res)
			continue;

		t_info->cpu_pcpu_map[cpu] = pcpu;
		t_info->per_cpu_lpm_cfg[cpu] = res->start;
		t_info->per_cpu_lpm_cfg_size[cpu] = resource_size(res);
	}

	res =  platform_get_resource_byname(pdev, IORESOURCE_MEM,
					    "apss_seq_mem_base");
	if (!res)
		return -ENODEV;

	t_info->apss_seq_mem_base = res->start;
	t_info->apss_seq_mem_size = resource_size(res);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "l3_seq_lpm_cntr_cfg");
	if (!res)
		return -ENODEV;

	t_info->l3_seq_lpm_cfg = res->start;
	t_info->l3_seq_lpm_size = resource_size(res);

	of_property_read_u32(pdev->dev.of_node, "num-cpus", &t_info->ncpu);
	if (t_info->ncpu < MIN_POSSIBLE_CPUS ||  t_info->ncpu > MAX_POSSIBLE_CPUS)
		return -EINVAL;

	t_info->offsets = (u32 *)device_get_match_data(&pdev->dev);
	if (!t_info->offsets)
		return -ENODEV;

	/*
	 * Function to read cfgs register to know lpm stats per cpu/cluster and
	 * create debugfs
	 */
	ret = qcom_cpuss_read_lpm_and_residency_cfg_informaion(t_info);
	if (ret)
		return ret;

	debugfs_create_file("stats", 0444, root_dir,
				(void *) &t_info->complete_stats.node,
				&qcom_cpuss_all_stats_fops);

	/* Debugfs to reset all LPM and residency */
	mutex_init(&t_info->stats_reset_lock);
	debugfs_create_file("reset", 0220, root_dir, (void *) t_info,
			    &qcom_cpuss_stats_reset_fops);

	platform_set_drvdata(pdev, root_dir);

	return ret;
}

static int qcom_cpuss_sleep_stats_remove(struct platform_device *pdev)
{
	struct dentry *root = platform_get_drvdata(pdev);

	debugfs_remove_recursive(root);

	return 0;
}

static const struct of_device_id qcom_cpuss_stats_table[] = {
		{ .compatible = "qcom,cpuss-sleep-stats", .data = &qcom_cpuss_cntr_v1_offsets },
		{ .compatible = "qcom,cpuss-sleep-stats-v2", .data = &qcom_cpuss_cntr_v2_offsets },
		{ .compatible = "qcom,cpuss-sleep-stats-v3", .data = &qcom_cpuss_cntr_v3_offsets },
		{ },
};

static struct platform_driver qcom_cpuss_sleep_stats = {
	.probe = qcom_cpuss_sleep_stats_probe,
	.remove = qcom_cpuss_sleep_stats_remove,
	.driver	= {
		.name = "qcom_cpuss_sleep_stats",
		.of_match_table	= qcom_cpuss_stats_table,
	},
};

module_platform_driver(qcom_cpuss_sleep_stats);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. (QTI) CPUSS sleep stats driver");
MODULE_LICENSE("GPL v2");
