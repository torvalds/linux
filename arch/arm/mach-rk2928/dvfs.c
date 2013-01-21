/* arch/arm/mach-rk30/rk30_dvfs.c
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <mach/dvfs.h>
#include <mach/clock.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/hrtimer.h>

static int rk_dvfs_clk_notifier_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	struct clk_notifier_data *noti_info;
	struct clk *clk;
	struct clk_node *dvfs_clk;
	noti_info = (struct clk_notifier_data *)ptr;
	clk = noti_info->clk;
	dvfs_clk = clk->dvfs_info;

	switch (event) {
		case CLK_PRE_RATE_CHANGE:
			DVFS_DBG("%s CLK_PRE_RATE_CHANGE\n", __func__);
			break;
		case CLK_POST_RATE_CHANGE:
			DVFS_DBG("%s CLK_POST_RATE_CHANGE\n", __func__);
			break;
		case CLK_ABORT_RATE_CHANGE:
			DVFS_DBG("%s CLK_ABORT_RATE_CHANGE\n", __func__);
			break;
		case CLK_PRE_ENABLE:
			DVFS_DBG("%s CLK_PRE_ENABLE\n", __func__);
			break;
		case CLK_POST_ENABLE:
			DVFS_DBG("%s CLK_POST_ENABLE\n", __func__);
			break;
		case CLK_ABORT_ENABLE:
			DVFS_DBG("%s CLK_ABORT_ENABLE\n", __func__);
			break;
		case CLK_PRE_DISABLE:
			DVFS_DBG("%s CLK_PRE_DISABLE\n", __func__);
			break;
		case CLK_POST_DISABLE:
			DVFS_DBG("%s CLK_POST_DISABLE\n", __func__);
			dvfs_clk->set_freq = 0;
			break;
		case CLK_ABORT_DISABLE:
			DVFS_DBG("%s CLK_ABORT_DISABLE\n", __func__);

			break;
		default:
			break;
	}
	return 0;
}

static struct notifier_block rk_dvfs_clk_notifier = {
	.notifier_call = rk_dvfs_clk_notifier_event,
};


static struct clk_node *dvfs_clk_cpu;
static struct vd_node vd_core;
static int dvfs_target_cpu(struct clk *clk, unsigned long rate_hz)
{
	struct clk_node *dvfs_clk;
	int volt_new = 0, volt_dep_new = 0, clk_volt_store = 0;
	struct cpufreq_frequency_table clk_fv;
	int ret = 0;
	unsigned long rate_new, rate_old;
	
	if (!clk) {
		DVFS_ERR("%s is not a clk\n", __func__);
		return -1;
	}
	dvfs_clk = clk_get_dvfs_info(clk);
	DVFS_DBG("enter %s: clk(%s) rate = %lu Hz\n", __func__, dvfs_clk->name, rate_hz);

	if (!dvfs_clk || dvfs_clk->vd == NULL || IS_ERR_OR_NULL(dvfs_clk->vd->regulator)) {
		DVFS_ERR("dvfs(%s) is not register regulator\n", dvfs_clk->name);
		return -1;
	}

	if (dvfs_clk->vd->volt_set_flag == DVFS_SET_VOLT_FAILURE) {
		/* It means the last time set voltage error */
		ret = dvfs_reset_volt(dvfs_clk->vd);
		if (ret < 0) {
			return -1;
		}
	}

	/* Check limit rate */
	if (dvfs_clk->freq_limit_en) {
		if (rate_hz < dvfs_clk->min_rate) {
			rate_hz = dvfs_clk->min_rate;
		} else if (rate_hz > dvfs_clk->max_rate) {
			rate_hz = dvfs_clk->max_rate;
		}
	}
		
	/* need round rate */
	rate_old = clk_get_rate(clk);
	rate_new = clk_round_rate_nolock(clk, rate_hz);
	if(rate_new == rate_old)
		return 0;
	DVFS_DBG("dvfs(%s) round rate (%lu)(rount %lu) old (%lu)\n", 
			dvfs_clk->name, rate_hz, rate_new, rate_old);

	/* find the clk corresponding voltage */
	if (0 != dvfs_clk_get_ref_volt(dvfs_clk, rate_new / 1000, &clk_fv)) {
		DVFS_ERR("dvfs(%s) rate %luhz is larger,not support\n", dvfs_clk->name, rate_hz);
		return -1;
	}
	clk_volt_store = dvfs_clk->set_volt;
	dvfs_clk->set_volt = clk_fv.index;
	volt_new = dvfs_vd_get_newvolt_byclk(dvfs_clk);

	/* if up the rate */
	if (rate_new > rate_old) {
		if (!list_empty(&dvfs_clk->depend_list)) {
			// update depend's req_volt
			ret = dvfs_get_depend_volt(dvfs_clk, &vd_core, rate_new);
			if (ret <= 0)
				goto fail_roll_back;
			
			volt_dep_new = dvfs_vd_get_newvolt_bypd(&vd_core);
			if (volt_dep_new <= 0) 
				goto fail_roll_back;
			ret = dvfs_scale_volt_direct(dvfs_clk->vd, volt_new);

			//ret = dvfs_scale_volt_bystep(dvfs_clk->vd, &vd_core, volt_new, volt_dep_new, 
			//		ARM_HIGHER_LOGIC, LOGIC_HIGHER_ARM); 
			if (ret < 0) 
				goto fail_roll_back;
		} else {
			ret = dvfs_scale_volt_direct(dvfs_clk->vd, volt_new);
			if (ret < 0) 
				goto fail_roll_back;
		}
	}

	/* scale rate */
	if (dvfs_clk->clk_dvfs_target) {
		ret = dvfs_clk->clk_dvfs_target(clk, rate_new, clk_set_rate_locked);
	} else {
		ret = clk_set_rate_locked(clk, rate_new);
	}

	if (ret < 0) {
		DVFS_ERR("%s set rate err\n", __func__);
		goto fail_roll_back;
	}
	dvfs_clk->set_freq	= rate_new / 1000;

	DVFS_DBG("dvfs %s set rate %lu ok\n", dvfs_clk->name, clk_get_rate(clk));

	/* if down the rate */
	if (rate_new < rate_old) {
		if (!list_empty(&dvfs_clk->depend_list)) {
			// update depend's req_volt
			ret = dvfs_get_depend_volt(dvfs_clk, &vd_core, rate_new);
			if (ret <= 0)
				goto out;
			
			volt_dep_new = dvfs_vd_get_newvolt_bypd(&vd_core);
			if (volt_dep_new <= 0) 
				goto out;
			ret = dvfs_scale_volt_direct(dvfs_clk->vd, volt_new);

			//ret = dvfs_scale_volt_bystep(dvfs_clk->vd, &vd_core, volt_new, volt_dep_new, 
			//		ARM_HIGHER_LOGIC, LOGIC_HIGHER_ARM); 
			if (ret < 0) 
				goto out;
		} else {
			ret = dvfs_scale_volt_direct(dvfs_clk->vd, volt_new);
			if (ret < 0) 
				goto out;
		}
	}

	return ret;
fail_roll_back:
	dvfs_clk->set_volt = clk_volt_store;
	ret = dvfs_get_depend_volt(dvfs_clk, &vd_core, rate_old);
	if (ret <= 0) {
		DVFS_ERR("%s dvfs_get_depend_volt error when roll back!\n", __func__);
	}
out:
	return -1;
}

static int dvfs_target_core(struct clk *clk, unsigned long rate_hz)
{
	struct clk_node *dvfs_clk;
	int volt_new = 0, volt_dep_new = 0, clk_volt_store = 0;
	
	struct cpufreq_frequency_table clk_fv;
	
	int ret = 0;
	unsigned long rate_new, rate_old;
		
	if (!clk) {
		DVFS_ERR("%s is not a clk\n", __func__);
		return -1;
	}
	dvfs_clk = clk_get_dvfs_info(clk);
	DVFS_DBG("enter %s: clk(%s) rate = %lu Hz\n", __func__, dvfs_clk->name, rate_hz);

	if (!dvfs_clk || dvfs_clk->vd == NULL || IS_ERR_OR_NULL(dvfs_clk->vd->regulator)) {
		DVFS_ERR("dvfs(%s) is not register regulator\n", dvfs_clk->name);
		return -1;
	}

	if (dvfs_clk->vd->volt_set_flag == DVFS_SET_VOLT_FAILURE) {
		/* It means the last time set voltage error */
		ret = dvfs_reset_volt(dvfs_clk->vd);
		if (ret < 0) {
			return -1;
		}
	}

	/* Check limit rate */
	if (dvfs_clk->freq_limit_en) {
		if (rate_hz < dvfs_clk->min_rate) {
			rate_hz = dvfs_clk->min_rate;
		} else if (rate_hz > dvfs_clk->max_rate) {
			rate_hz = dvfs_clk->max_rate;
		}
	}
		
	/* need round rate */
	rate_old = clk_get_rate(clk);
	rate_new = clk_round_rate_nolock(clk, rate_hz);
	if(rate_new == rate_old)
		return 0;
	DVFS_DBG("dvfs(%s) round rate (%lu)(rount %lu) old (%lu)\n", 
			dvfs_clk->name, rate_hz, rate_new, rate_old);

	/* find the clk corresponding voltage */
	if (0 != dvfs_clk_get_ref_volt(dvfs_clk, rate_new / 1000, &clk_fv)) {
		DVFS_ERR("dvfs(%s) rate %luhz is larger,not support\n", dvfs_clk->name, rate_hz);
		return -1;
	}
	clk_volt_store = dvfs_clk->set_volt;
	dvfs_clk->set_volt = clk_fv.index;
	volt_new = dvfs_vd_get_newvolt_byclk(dvfs_clk);

	/* if up the rate */
	if (rate_new > rate_old) {
		DVFS_DBG("-----------------------------rate_new > rate_old\n");
		volt_dep_new = dvfs_vd_get_newvolt_byclk(dvfs_clk_cpu);

		if (volt_dep_new < 0) 
			goto fail_roll_back;

		ret = dvfs_scale_volt_direct(dvfs_clk->vd, volt_new);
		//ret = dvfs_scale_volt_bystep(dvfs_clk->vd, dvfs_clk_cpu->vd, volt_new, volt_dep_new, 
		//			LOGIC_HIGHER_ARM, ARM_HIGHER_LOGIC); 
		if (ret < 0) 
			goto fail_roll_back;
	}

	/* scale rate */
	if (dvfs_clk->clk_dvfs_target) {
		ret = dvfs_clk->clk_dvfs_target(clk, rate_new, clk_set_rate_locked);
	} else {
		
		ret = clk_set_rate_locked(clk, rate_new);
	}

	if (ret < 0) {
		DVFS_ERR("%s set rate err\n", __func__);
		goto fail_roll_back;
	}
	dvfs_clk->set_freq	= rate_new / 1000;

	DVFS_DBG("dvfs %s set rate %lu ok\n", dvfs_clk->name, clk_get_rate(clk));

	/* if down the rate */
	if (rate_new < rate_old) {
		DVFS_DBG("-----------------------------rate_new < rate_old\n");
		volt_dep_new = dvfs_vd_get_newvolt_byclk(dvfs_clk_cpu);

		if (volt_dep_new < 0) 
			goto out;

		ret = dvfs_scale_volt_direct(dvfs_clk->vd, volt_new);
		//ret = dvfs_scale_volt_bystep(dvfs_clk->vd, dvfs_clk_cpu->vd, volt_new, volt_dep_new, 
		//			LOGIC_HIGHER_ARM, ARM_HIGHER_LOGIC); 
		if (ret < 0) 
			goto out;
	}

	return ret;
fail_roll_back:	
	dvfs_clk->set_volt = clk_volt_store;
	ret = dvfs_get_depend_volt(dvfs_clk, &vd_core, rate_old);
	if (ret <= 0) {
		DVFS_ERR("%s dvfs_get_depend_volt error when roll back!\n", __func__);
	}

out:
	return -1;
}

/*****************************init**************************/
/**
 * rate must be raising sequence
 */
static struct cpufreq_frequency_table cpu_dvfs_table[] = {
	// {.frequency	= 48 * DVFS_KHZ, .index = 920*DVFS_MV},
	// {.frequency	= 126 * DVFS_KHZ, .index	= 970 * DVFS_MV},
	// {.frequency	= 252 * DVFS_KHZ, .index	= 1040 * DVFS_MV},
	// {.frequency	= 504 * DVFS_KHZ, .index	= 1050 * DVFS_MV},
	{.frequency	= 816 * DVFS_KHZ, .index	= 1050 * DVFS_MV},
	// {.frequency	= 1008 * DVFS_KHZ, .index	= 1100 * DVFS_MV},
	{.frequency	= CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table ddr_dvfs_table[] = {
	// {.frequency = 100 * DVFS_KHZ, .index = 1100 * DVFS_MV},
	{.frequency = 200 * DVFS_KHZ, .index = 1000 * DVFS_MV},
	{.frequency = 300 * DVFS_KHZ, .index = 1050 * DVFS_MV},
	{.frequency = 400 * DVFS_KHZ, .index = 1100 * DVFS_MV},
	{.frequency = 500 * DVFS_KHZ, .index = 1150 * DVFS_MV},
	{.frequency = 600 * DVFS_KHZ, .index = 1200 * DVFS_MV},
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table gpu_dvfs_table[] = {
	{.frequency = 90 * DVFS_KHZ, .index = 1100 * DVFS_MV},
	{.frequency = 180 * DVFS_KHZ, .index = 1150 * DVFS_MV},
	{.frequency = 300 * DVFS_KHZ, .index = 1100 * DVFS_MV},
	{.frequency = 400 * DVFS_KHZ, .index = 1150 * DVFS_MV},
	{.frequency = 500 * DVFS_KHZ, .index = 1200 * DVFS_MV},
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table peri_aclk_dvfs_table[] = {
	{.frequency = 100 * DVFS_KHZ, .index = 1000 * DVFS_MV},
	{.frequency = 200 * DVFS_KHZ, .index = 1050 * DVFS_MV},
	{.frequency = 300 * DVFS_KHZ, .index = 1070 * DVFS_MV},
	{.frequency = 500 * DVFS_KHZ, .index = 1100 * DVFS_MV},
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table vpu_dvfs_table[] = {
	{.frequency = 266 * DVFS_KHZ, .index = 1100 * DVFS_MV},
	{.frequency = 300 * DVFS_KHZ, .index = 1100 * DVFS_MV},
	{.frequency = 400 * DVFS_KHZ, .index = 1200 * DVFS_MV},
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table dep_cpu2core_table[] = {
	// {.frequency = 252 * DVFS_KHZ, .index    = 1025 * DVFS_MV},
	// {.frequency = 504 * DVFS_KHZ, .index    = 1025 * DVFS_MV},
	{.frequency = 816 * DVFS_KHZ, .index    = 1050 * DVFS_MV},//logic 1.050V
	// {.frequency = 1008 * DVFS_KHZ,.index    = 1050 * DVFS_MV},
	// {.frequency = 1200 * DVFS_KHZ,.index    = 1050 * DVFS_MV},
	// {.frequency = 1272 * DVFS_KHZ,.index    = 1050 * DVFS_MV},//logic 1.050V
	// {.frequency = 1416 * DVFS_KHZ,.index    = 1100 * DVFS_MV},//logic 1.100V
	// {.frequency = 1512 * DVFS_KHZ,.index    = 1125 * DVFS_MV},//logic 1.125V
	// {.frequency = 1608 * DVFS_KHZ,.index    = 1175 * DVFS_MV},//logic 1.175V
	{.frequency	= CPUFREQ_TABLE_END},
};

static struct vd_node vd_cpu = {
	.name 		= "vd_cpu",
	.regulator_name	= "vdd_cpu",
	.volt_set_flag	= DVFS_SET_VOLT_FAILURE,
	.vd_dvfs_target	= dvfs_target_cpu,
};

static struct vd_node vd_core = {
	.name 		= "vd_core",
	.regulator_name = "vdd_core",
	.volt_set_flag	= DVFS_SET_VOLT_FAILURE,
	.vd_dvfs_target	= dvfs_target_core,
};

static struct vd_node vd_rtc = {
	.name 		= "vd_rtc",
	.regulator_name	= "vdd_rtc",
	.volt_set_flag	= DVFS_SET_VOLT_FAILURE,
	.vd_dvfs_target	= NULL,
};

static struct vd_node *rk30_vds[] = {&vd_cpu, &vd_core, &vd_rtc};

static struct pd_node pd_a9_0 = {
	.name 			= "pd_a9_0",
	.vd			= &vd_cpu,
};
static struct pd_node pd_a9_1 = {
	.name 			= "pd_a9_1",
	.vd			= &vd_cpu,
};
static struct pd_node pd_debug = {
	.name 			= "pd_debug",
	.vd			= &vd_cpu,
};
static struct pd_node pd_scu = {
	.name 			= "pd_scu",
	.vd			= &vd_cpu,
};
static struct pd_node pd_video = {
	.name 			= "pd_video",
	.vd			= &vd_core,
};
static struct pd_node pd_vio = {
	.name 			= "pd_vio",
	.vd			= &vd_core,
};
static struct pd_node pd_gpu = {
	.name 			= "pd_gpu",
	.vd			= &vd_core,
};
static struct pd_node pd_peri = {
	.name 			= "pd_peri",
	.vd			= &vd_core,
};
static struct pd_node pd_cpu = {
	.name 			= "pd_cpu",
	.vd			= &vd_core,
};
static struct pd_node pd_alive = {
	.name 			= "pd_alive",
	.vd			= &vd_core,
};
static struct pd_node pd_rtc = {
	.name 			= "pd_rtc",
	.vd			= &vd_rtc,
};
#define LOOKUP_PD(_ppd)	\
{	\
	.pd	= _ppd,	\
}
static struct pd_node_lookup rk30_pds[] = {
	LOOKUP_PD(&pd_a9_0),
	LOOKUP_PD(&pd_a9_1),
	LOOKUP_PD(&pd_debug),
	LOOKUP_PD(&pd_scu),
	LOOKUP_PD(&pd_video),
	LOOKUP_PD(&pd_vio),
	LOOKUP_PD(&pd_gpu),
	LOOKUP_PD(&pd_peri),
	LOOKUP_PD(&pd_cpu),
	LOOKUP_PD(&pd_alive),
	LOOKUP_PD(&pd_rtc),
};

#define CLK_PDS(_ppd) \
{	\
	.pd	= _ppd,	\
}

static struct pds_list cpu_pds[] = {
	CLK_PDS(&pd_a9_0),
	CLK_PDS(&pd_a9_1),
	CLK_PDS(NULL),
};

static struct pds_list ddr_pds[] = {
	CLK_PDS(&pd_cpu),
	CLK_PDS(NULL),
};

static struct pds_list gpu_pds[] = {
	CLK_PDS(&pd_gpu),
	CLK_PDS(NULL),
};

static struct pds_list aclk_periph_pds[] = {
	CLK_PDS(&pd_peri),
	CLK_PDS(NULL),
};

static struct pds_list aclk_vepu_pds[] = {
	CLK_PDS(&pd_video),
	CLK_PDS(NULL),
};

#define RK_CLKS(_clk_name, _ppds, _dvfs_table, _dvfs_nb) \
{ \
	.name	= _clk_name, \
	.pds = _ppds,\
	.dvfs_table = _dvfs_table,	\
	.dvfs_nb	= _dvfs_nb,	\
}

static struct clk_node rk30_clks[] = {
	RK_CLKS("cpu", cpu_pds, cpu_dvfs_table, &rk_dvfs_clk_notifier),
	RK_CLKS("ddr", ddr_pds, ddr_dvfs_table, &rk_dvfs_clk_notifier),
	RK_CLKS("gpu", gpu_pds, gpu_dvfs_table, &rk_dvfs_clk_notifier),
	RK_CLKS("aclk_vepu", aclk_vepu_pds, vpu_dvfs_table, &rk_dvfs_clk_notifier),
	//RK_CLKS("aclk_periph", aclk_periph_pds, peri_aclk_dvfs_table, &rk_dvfs_clk_notifier),
};

#define RK_DEPPENDS(_clk_name, _pvd, _dep_table) \
{ \
	.clk_name	= _clk_name, \
	.dep_vd 	= _pvd,\
	.dep_table 	= _dep_table,	\
}

static struct depend_lookup rk30_depends[] = {
	RK_DEPPENDS("cpu", &vd_core, dep_cpu2core_table),
	//RK_DEPPENDS("gpu", &vd_cpu, NULL),
	//RK_DEPPENDS("gpu", &vd_cpu, NULL),
};


static struct avs_ctr_st rk292x_avs_ctr;

int rk292x_dvfs_init(void)
{
	int i = 0;
	for (i = 0; i < ARRAY_SIZE(rk30_vds); i++) {
		rk_regist_vd(rk30_vds[i]);
	}
	for (i = 0; i < ARRAY_SIZE(rk30_pds); i++) {
		rk_regist_pd(&rk30_pds[i]);
	}
	for (i = 0; i < ARRAY_SIZE(rk30_clks); i++) {
		rk_regist_clk(&rk30_clks[i]);
	}
	for (i = 0; i < ARRAY_SIZE(rk30_depends); i++) {
		rk_regist_depends(&rk30_depends[i]);
	}
	dvfs_clk_cpu = dvfs_get_dvfs_clk_byname("cpu");
	avs_board_init(&rk292x_avs_ctr);
	return 0;
}





/******************************rk292x avs**************************************************/
#if 0//def CONFIG_ARCH_RK2928

static void __iomem *rk292x_nandc_base;
#define nandc_readl(offset)	readl_relaxed(rk292x_nandc_base + offset)
#define nandc_writel(v, offset) do { writel_relaxed(v, rk292x_nandc_base + offset); dsb(); } while (0)
static u8 rk292x_get_avs_val(void)
{
	u32 nanc_save_reg[4];
	unsigned long flags;
	u32 paramet = 0;
	u32 count = 100;
	preempt_disable();
	local_irq_save(flags);

	nanc_save_reg[0] = nandc_readl(0);
	nanc_save_reg[1] = nandc_readl(0x130);
	nanc_save_reg[2] = nandc_readl(0x134);
	nanc_save_reg[3] = nandc_readl(0x158);

	nandc_writel(nanc_save_reg[0] | 0x1 << 14, 0);
	nandc_writel(0x5, 0x130);

	nandc_writel(7, 0x158);
	nandc_writel(1, 0x134);

	while(count--) {
		paramet = nandc_readl(0x138);
		if((paramet & 0x1))
			break;
		udelay(1);
	};
	paramet = (paramet >> 1) & 0xff;
	nandc_writel(nanc_save_reg[0], 0);
	nandc_writel(nanc_save_reg[1], 0x130);
	nandc_writel(nanc_save_reg[2], 0x134);
	nandc_writel(nanc_save_reg[3], 0x158);

	local_irq_restore(flags);
	preempt_enable();
	return (u8)paramet;

}

void rk292x_avs_init(void)
{
	rk292x_nandc_base = ioremap(RK2928_NANDC_PHYS, RK2928_NANDC_SIZE);
	//avs_init_val_get(0,1150000,"board_init");
}

static struct avs_ctr_st rk292x_avs_ctr = {
	.avs_init 		=rk292x_avs_init,
	.avs_get_val	= rk292x_get_avs_val,
};
#endif
