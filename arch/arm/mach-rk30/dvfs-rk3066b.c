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

#include <mach/io.h>
#include <mach/cru.h>
#include <mach/grf-rk3066b.h>

#define MHZ			(1000 * 1000)
#define KHZ			(1000)
#define CLK_LOOPS_JIFFY_REF 11996091ULL
#define CLK_LOOPS_RATE_REF (1200) //Mhz
#define CLK_LOOPS_RECALC(new_rate)  div_u64(CLK_LOOPS_JIFFY_REF*(new_rate),CLK_LOOPS_RATE_REF*MHZ)
static struct clk *clk_cpu = NULL, *clk_cpu_div = NULL, *arm_pll_clk = NULL, *general_pll_clk = NULL;
static unsigned long lpj_24m;

struct gate_delay_table {
	unsigned long arm_perf;
	unsigned long log_perf;
	unsigned long delay;
};

struct cycle_by_rate {
	unsigned long rate_khz;
	unsigned long cycle_ns;
};

struct uoc_val_xx2delay {
	unsigned long volt;
	unsigned long perf;
	unsigned long uoc_val_01;
	unsigned long uoc_val_11;
};

struct dvfs_volt_performance {
	unsigned long	volt;
	unsigned long	perf;	// Gate performance
};
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

static unsigned long dvfs_volt_arm_support_table[] = {
	850 * 1000,
	875 * 1000,
	900 * 1000,
	925 * 1000,
	950 * 1000,
	975 * 1000,
	1000 * 1000,
	1025 * 1000,
	1050 * 1000,
	1075 * 1000,
	1100 * 1000,
	1125 * 1000,
	1150 * 1000,
	1175 * 1000,
	1200 * 1000,
	1225 * 1000,
	1250 * 1000,
	1275 * 1000,
	1300 * 1000,
};
static unsigned long dvfs_volt_log_support_table[] = {
	850 * 1000,
	875 * 1000,
	900 * 1000,
	925 * 1000,
	950 * 1000,
	975 * 1000,
	1000 * 1000,
	1025 * 1000,
	1050 * 1000,
	1075 * 1000,
	1100 * 1000,
	1125 * 1000,
	1150 * 1000,
	1175 * 1000,
	1200 * 1000,
	1225 * 1000,
	1250 * 1000,
	1275 * 1000,
	1300 * 1000,
};

/*
 * 电压 dly_line 每增加0.1V的增量 每增加0.1V增加的比例 与1v对比增加的比例
 * 1.00  128
 * 1.10  157 29 1.23  1.23
 * 1.20  184 27 1.17  1.44
 * 1.30  209 25 1.14  1.63
 * 1.40  231 22 1.11  1.80
 * 1.50  251 20 1.09  1.96
 * This table is calc form func:
 * dly_line = 536 * volt - 116 * volt * volt - 292
 * volt unit:		V
 * dly_line unit:	Gate
 *
 * The table standard voltage is 1.0V, delay_line = 128(Gates)
 * 
 * */

#define VP_TABLE_END	(~0)
static struct dvfs_volt_performance dvfs_vp_table[] = {
	{.volt = 850 * 1000,		.perf = 350},	//623
	{.volt = 875 * 1000,		.perf = 350},	//689
	{.volt = 900 * 1000,		.perf = 350},	//753 make low arm freq uoc as small as posible
	{.volt = 925 * 1000,		.perf = 450},	//817
	{.volt = 950 * 1000,		.perf = 550},	//879
	{.volt = 975 * 1000,		.perf = 650},	//940
	{.volt = 1000 * 1000,		.perf = 750},
	{.volt = 1025 * 1000,		.perf = 1100},
	{.volt = 1050 * 1000,		.perf = 1125},
	{.volt = 1075 * 1000,		.perf = 1173},
	{.volt = 1100 * 1000,		.perf = 1230},
	{.volt = 1125 * 1000,		.perf = 1283},
	{.volt = 1150 * 1000,		.perf = 1336},
	{.volt = 1175 * 1000,		.perf = 1388},
	{.volt = 1200 * 1000,		.perf = 1440},
	{.volt = 1225 * 1000,		.perf = 1620},	//1489
	{.volt = 1250 * 1000,		.perf = 1660},	//1537
	{.volt = 1275 * 1000,		.perf = 1700},	//1585
	{.volt = 1300 * 1000,		.perf = 1720},	//1630 1.6Garm 600Mgpu, make uoc=2b'01
	{.volt = 1325 * 1000,		.perf = 1740},	//1676
	{.volt = 1350 * 1000,		.perf = 1760},	//1720
	{.volt = 1375 * 1000,		.perf = 1780},	//1763
	{.volt = 1400 * 1000,		.perf = 1800},
	{.volt = 1425 * 1000,		.perf = 1846},
	{.volt = 1450 * 1000,		.perf = 1885},
	{.volt = 1475 * 1000,		.perf = 1924},
	{.volt = 1500 * 1000,		.perf = 1960},
	{.volt = VP_TABLE_END},
};
//>1.2V step = 50mV
//ns (Magnified 10^6 times)
#define VD_DELAY_ZOOM	(1000UL * 1000UL)
#define VD_ARM_DELAY	1350000UL
#define VD_LOG_DELAY	877500UL
int uoc_val = 0;
#define L2_HOLD 	40UL	//located at 40%
#define L2_SETUP	70UL	//located at 70%

#define UOC_VAL_00	0UL
#define UOC_VAL_01	165000UL	//0.9V(125`C):220000
#define UOC_VAL_11	285000UL	//0.9V(125`C):380000
#define UOC_VAL_MIN	100UL		//to work around get_delay=0

#define SIZE_SUPPORT_ARM_VOLT	ARRAY_SIZE(dvfs_volt_arm_support_table)
#define SIZE_SUPPORT_LOG_VOLT	ARRAY_SIZE(dvfs_volt_log_support_table)
#define SIZE_VP_TABLE		ARRAY_SIZE(dvfs_vp_table)
#define SIZE_ARM_FREQ_TABLE	10
static struct cycle_by_rate rate_cycle[SIZE_ARM_FREQ_TABLE];
static int size_dvfs_arm_table = 0;

static struct clk_node *dvfs_clk_cpu;
static struct vd_node vd_core;
static struct vd_node vd_cpu;

static struct uoc_val_xx2delay uoc_val_xx[SIZE_VP_TABLE];
static struct gate_delay_table gate_delay[SIZE_VP_TABLE][SIZE_VP_TABLE];

static unsigned long dvfs_get_perf_byvolt(unsigned long volt)
{
	int i = 0;
	for (i = 0; dvfs_vp_table[i].volt != VP_TABLE_END; i++) {
		if (volt <= dvfs_vp_table[i].volt)
			return dvfs_vp_table[i].perf;
	}
	return 0;
}

static unsigned long dvfs_get_gate_delay_per_volt(unsigned long arm_perf, unsigned long log_perf)
{
	unsigned long gate_arm_delay, gate_log_delay;
	if (arm_perf == 0)
		arm_perf = 1;
	if (log_perf == 0)
		log_perf = 1;
	gate_arm_delay = VD_ARM_DELAY * 1000 / arm_perf;
	gate_log_delay = VD_LOG_DELAY * 1000 / log_perf;

	return (gate_arm_delay > gate_log_delay ? (gate_arm_delay - gate_log_delay) : 0);
}

static int dvfs_gate_delay_init(void)
{

	int i = 0, j = 0;
	for (i = 0; i < SIZE_VP_TABLE - 1; i++)
		for (j = 0; j < SIZE_VP_TABLE - 1; j++) {
			gate_delay[i][j].arm_perf = dvfs_vp_table[i].perf;
			gate_delay[i][j].log_perf = dvfs_vp_table[j].perf;
			gate_delay[i][j].delay = dvfs_get_gate_delay_per_volt(gate_delay[i][j].arm_perf,
					gate_delay[i][j].log_perf);

			//DVFS_DBG("%s: arm_perf=%lu, log_perf=%lu, delay=%lu\n", __func__,
			//		gate_delay[i][j].arm_perf, gate_delay[i][j].log_perf,
			//		gate_delay[i][j].delay);
		}
	return 0;
}

static unsigned long dvfs_get_gate_delay(unsigned long arm_perf, unsigned long log_perf)
{
	int i = 0, j = 0;
	for (i = 0; i < SIZE_VP_TABLE - 1; i++) {
		if (gate_delay[i][0].arm_perf == arm_perf)
			break;
	}
	for (j = 0; j < SIZE_VP_TABLE - 1; j++) {
		if (gate_delay[i][j].log_perf == log_perf)
			break;
	}

	//DVFS_DBG("%s index_arm=%d, index_log=%d, delay=%lu\n", __func__, i, j, gate_delay[i][j].delay);
	//DVFS_DBG("%s perf_arm=%d, perf_log=%d, delay=%lu\n",
	//		__func__, gate_delay[i][j].arm_perf , gate_delay[i][j].log_perf , gate_delay[i][j].delay);
	return gate_delay[i][j].delay;
}
static int dvfs_uoc_val_delay_init(void)
{
	int i = 0;
	for (i = 0; i < SIZE_VP_TABLE - 1; i++) {
		uoc_val_xx[i].volt = dvfs_vp_table[i].volt;
		uoc_val_xx[i].perf = dvfs_vp_table[i].perf;
		uoc_val_xx[i].uoc_val_01 = UOC_VAL_01 * 1000 / uoc_val_xx[i].perf;
		uoc_val_xx[i].uoc_val_11 = UOC_VAL_11 * 1000 / uoc_val_xx[i].perf;
		//DVFS_DBG("volt=%lu, perf=%lu, uoc_01=%lu, uoc_11=%lu\n", uoc_val_xx[i].volt, uoc_val_xx[i].perf,
		//		uoc_val_xx[i].uoc_val_01, uoc_val_xx[i].uoc_val_11);
	}
	return 0;
}
static unsigned long dvfs_get_uoc_val_xx_by_volt(unsigned long uoc_val_xx_delay, unsigned long volt)
{
	int i = 0;
	if (uoc_val_xx_delay == UOC_VAL_01) {
		for (i = 0; i < SIZE_VP_TABLE - 1; i++) {
			if (uoc_val_xx[i].volt == volt)
				return uoc_val_xx[i].uoc_val_01;
		}

	} else if (uoc_val_xx_delay == UOC_VAL_11) {
		for (i = 0; i < SIZE_VP_TABLE - 1; i++) {
			if (uoc_val_xx[i].volt == volt)
				return uoc_val_xx[i].uoc_val_11;
		}

	} else {
		DVFS_ERR("%s UNKNOWN uoc_val_xx\n", __func__);
	}
	DVFS_ERR("%s can not find uoc_val_xx=%lu, with volt=%lu\n", __func__, uoc_val_xx_delay, volt);
	return uoc_val_xx_delay;
}
struct dvfs_volt_uoc {
	unsigned long	volt_log;
	unsigned long	volt_arm_new;
	unsigned long	volt_log_new;
	int	uoc_val;
};
struct dvfs_uoc_val_table {
	unsigned long	rate_arm;
	unsigned long	volt_arm;
	struct dvfs_volt_uoc	vu_list[SIZE_SUPPORT_LOG_VOLT];
};
static struct dvfs_uoc_val_table dvfs_uoc_val_list[SIZE_ARM_FREQ_TABLE];

static int dvfs_get_uoc_val_init(unsigned long *p_volt_arm_new, unsigned long *p_volt_log_new, 
		unsigned long rate_khz);
static int dvfs_with_uoc_init(void)
{
	struct cpufreq_frequency_table *dvfs_arm_table;
	struct clk *cpu_clk;
	int i = 0, j = 0;
	unsigned long arm_volt_save = 0;
	cpu_clk = clk_get(NULL, "cpu");
	if (IS_ERR(cpu_clk))
		return PTR_ERR(cpu_clk);

	dvfs_arm_table = dvfs_get_freq_volt_table(cpu_clk);
	lpj_24m = CLK_LOOPS_RECALC(24 * MHZ);
	DVFS_DBG("24M=%lu cur_rate=%lu lpj=%lu\n", lpj_24m, arm_pll_clk->rate, loops_per_jiffy);
	dvfs_gate_delay_init();
	dvfs_uoc_val_delay_init();

	for (i = 0; dvfs_arm_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		if (i > SIZE_ARM_FREQ_TABLE - 1) {
			DVFS_WARNING("mach-rk30/dvfs.c:%s:%d: dvfs arm table to large, use only [%d] frequency\n",
					__func__, __LINE__, SIZE_ARM_FREQ_TABLE);
			break;
		}
		rate_cycle[i].rate_khz = dvfs_arm_table[i].frequency;
		rate_cycle[i].cycle_ns = (1000UL * VD_DELAY_ZOOM) / (rate_cycle[i].rate_khz / 1000);
		DVFS_DBG("%s: rate=%lu, cycle_ns=%lu\n",
				__func__, rate_cycle[i].rate_khz, rate_cycle[i].cycle_ns);
	}
	size_dvfs_arm_table = i + 1;

	//dvfs_uoc_val_list[];
	for (i = 0; dvfs_arm_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		dvfs_uoc_val_list[i].rate_arm = dvfs_arm_table[i].frequency;
		dvfs_uoc_val_list[i].volt_arm = dvfs_arm_table[i].index;
		arm_volt_save = dvfs_uoc_val_list[i].volt_arm;
		for (j = 0; j < SIZE_SUPPORT_LOG_VOLT - 1; j++) {
			dvfs_uoc_val_list[i].vu_list[j].volt_log = dvfs_volt_log_support_table[j];
			dvfs_uoc_val_list[i].vu_list[j].volt_arm_new = arm_volt_save;
			dvfs_uoc_val_list[i].vu_list[j].volt_log_new = dvfs_uoc_val_list[i].vu_list[j].volt_log;
			//DVFS_DBG("%s: Rarm=%lu,Varm=%lu,Vlog=%lu\n", __func__,
			//		dvfs_uoc_val_list[i].rate_arm, dvfs_uoc_val_list[i].volt_arm,
			//		dvfs_uoc_val_list[i].vu_list[j].volt_log);

			dvfs_uoc_val_list[i].vu_list[j].uoc_val = dvfs_get_uoc_val_init(
					&dvfs_uoc_val_list[i].vu_list[j].volt_arm_new, 
					&dvfs_uoc_val_list[i].vu_list[j].volt_log_new, 
					dvfs_uoc_val_list[i].rate_arm);
			DVFS_DBG("%s: Rarm=%lu,(Varm=%lu,Vlog=%lu)--->(Vn_arm=%lu,Vn_log=%lu), uoc=%d\n", __func__,
					dvfs_uoc_val_list[i].rate_arm, dvfs_uoc_val_list[i].volt_arm,
					dvfs_uoc_val_list[i].vu_list[j].volt_log,
					dvfs_uoc_val_list[i].vu_list[j].volt_arm_new, 
					dvfs_uoc_val_list[i].vu_list[j].volt_log_new, 
					dvfs_uoc_val_list[i].vu_list[j].uoc_val);
			mdelay(10);
		}
	}

	return 0;
}
arch_initcall(dvfs_with_uoc_init);

static unsigned long dvfs_get_cycle_by_rate(unsigned long rate_khz)
{
	int i = 0;
	for (i = 0; i < size_dvfs_arm_table - 1; i++) {
		if (rate_khz == rate_cycle[i].rate_khz)
			return rate_cycle[i].cycle_ns;
	}
	DVFS_ERR("%s, %d: can not find rate=%lu KHz in list\n", __func__, __LINE__, rate_khz);
	return -1;
}
#define UOC_NEED_INCREASE_ARM	0
#define UOC_NEED_INCREASE_LOG	1
static unsigned long get_uoc_delay(unsigned long hold, unsigned long uoc_val_xx)
{
	// hold - uoc_val_11; make sure not smaller than UOC_VAL_MIN
	return hold > uoc_val_xx ? (hold - uoc_val_xx) : UOC_VAL_MIN;
}
static unsigned long dvfs_recalc_volt(unsigned long *p_volt_arm_new, unsigned long *p_volt_log_new,
		unsigned long arm_perf, unsigned long log_perf,
		unsigned long hold, unsigned long setup, unsigned long flag)
{
	int i = 0;
	unsigned long volt_arm = *p_volt_arm_new, volt_log = *p_volt_log_new;
	unsigned long curr_delay = 0;
	unsigned long uoc_val_11 = dvfs_get_uoc_val_xx_by_volt(UOC_VAL_11, *p_volt_log_new);

	if (flag == UOC_NEED_INCREASE_LOG) {
		for (i = 0; i < ARRAY_SIZE(dvfs_volt_log_support_table); i++) {
			if (dvfs_volt_log_support_table[i] <= volt_log)
				continue;

			volt_log = dvfs_volt_log_support_table[i];
			log_perf = dvfs_get_perf_byvolt(volt_log);
			uoc_val_11 = dvfs_get_uoc_val_xx_by_volt(UOC_VAL_11, volt_log);
			curr_delay = dvfs_get_gate_delay(arm_perf, log_perf);
			DVFS_DBG("\t%s line:%d get volt=%lu; arm_perf=%lu, log_perf=%lu, curr_delay=%lu\n",
					__func__, __LINE__, dvfs_volt_log_support_table[i],
					arm_perf, log_perf, curr_delay);
			if (curr_delay > get_uoc_delay(hold, uoc_val_11)) {
				*p_volt_log_new = volt_log;
				break;
			}
		}
	} else if (flag == UOC_NEED_INCREASE_ARM) {
		for (i = 0; i < ARRAY_SIZE(dvfs_volt_arm_support_table); i++) {
			if (dvfs_volt_arm_support_table[i] <= volt_arm)
				continue;

			volt_arm = dvfs_volt_arm_support_table[i];
			arm_perf = dvfs_get_perf_byvolt(volt_arm);
			curr_delay = dvfs_get_gate_delay(arm_perf, log_perf);
			DVFS_DBG("\t%s line:%d get volt=%lu; arm_perf=%lu, log_perf=%lu, curr_delay=%lu\n",
					__func__, __LINE__, dvfs_volt_log_support_table[i],
					arm_perf, log_perf, curr_delay);
			if (curr_delay < setup) {
				*p_volt_arm_new = volt_arm;
				break;
			}
		}

	} else {
		DVFS_ERR("Oops, some bugs here, %s Unknown flag:%08lx\n", __func__, flag);
	}
	return curr_delay;
}

static int dvfs_get_uoc_val_init(unsigned long *p_volt_arm_new, unsigned long *p_volt_log_new, unsigned long rate_khz)
{
	int uoc_val = 0;
	unsigned long arm_perf = 0, log_perf = 0;
	unsigned long cycle = 0, hold = 0, setup = 0;
	unsigned long curr_delay = 0;	// arm slow than log
	//unsigned long volt_arm_new = *p_volt_arm_new;
	//unsigned long volt_log_new = *p_volt_log_new;
	unsigned long uoc_val_01 , uoc_val_11;
	//unsigned long rate_MHz;
	//DVFS_DBG("enter %s\n", __func__);
	arm_perf = dvfs_get_perf_byvolt(*p_volt_arm_new);
	log_perf = dvfs_get_perf_byvolt(*p_volt_log_new);
	uoc_val_01 = dvfs_get_uoc_val_xx_by_volt(UOC_VAL_01, *p_volt_log_new);
	uoc_val_11 = dvfs_get_uoc_val_xx_by_volt(UOC_VAL_11, *p_volt_log_new);
	DVFS_DBG("%s volt:arm(%lu), log(%lu);\tget perf arm(%lu), log(%lu)\n", __func__,
			*p_volt_arm_new, *p_volt_log_new, arm_perf, log_perf);

	// warning: this place may cause div 0 warning, DO NOT take place
	// rate_MHz with (rate / DVFS_MHZ)
	// rate_MHz = rate_khz / 1000;
	// cycle = (1000UL * VD_DELAY_ZOOM) / (rate_khz / 1000); // ns = 1 / rate(GHz), Magnified 1000 times
	cycle = dvfs_get_cycle_by_rate(rate_khz);

	hold = cycle * L2_HOLD / 100UL;
	setup = cycle * L2_SETUP / 100UL;

	curr_delay = dvfs_get_gate_delay(arm_perf, log_perf);
	DVFS_DBG("%s cycle=%lu, curr_delay=%lu, (hold=%lu, setup=%lu)\n",
			__func__, cycle, curr_delay, hold, setup);

	if (curr_delay <= get_uoc_delay(hold, uoc_val_11)) {
		DVFS_DBG("%s Need to increase log voltage\n", __func__);
		curr_delay = dvfs_recalc_volt(p_volt_arm_new, p_volt_log_new, arm_perf, log_perf,
				hold, setup, UOC_NEED_INCREASE_LOG);

		//log_perf = dvfs_get_perf_byvolt(*p_volt_log_new);
		uoc_val_01 = dvfs_get_uoc_val_xx_by_volt(UOC_VAL_01, *p_volt_log_new);
		uoc_val_11 = dvfs_get_uoc_val_xx_by_volt(UOC_VAL_11, *p_volt_log_new);

	} else if (curr_delay >= setup) {
		DVFS_DBG("%s Need to increase arm voltage\n", __func__);
		curr_delay = dvfs_recalc_volt(p_volt_arm_new, p_volt_log_new, arm_perf, log_perf,
				hold, setup, UOC_NEED_INCREASE_ARM);
		//arm_perf = dvfs_get_perf_byvolt(*p_volt_arm_new);
	}

	DVFS_DBG("TARGET VOLT:arm(%lu), log(%lu);\tget perf arm(%lu), log(%lu)\n",
			*p_volt_arm_new, *p_volt_log_new, 
			dvfs_get_perf_byvolt(*p_volt_arm_new), dvfs_get_perf_byvolt(*p_volt_log_new));
	// update uoc_val_01/11 with new volt
	DVFS_DBG("cycle=%lu, hold-val11=%lu, hold-val01=%lu, (hold=%lu, setup=%lu), curr_delay=%lu\n",
			cycle, get_uoc_delay(hold, uoc_val_11), get_uoc_delay(hold, uoc_val_01),
			hold, setup, curr_delay);
	if (curr_delay > hold && curr_delay < setup)
		uoc_val = 0;
	else if (curr_delay <= hold && curr_delay > get_uoc_delay(hold, uoc_val_01))
		uoc_val = 1;
	else if (curr_delay <= get_uoc_delay(hold, uoc_val_01) && curr_delay > get_uoc_delay(hold, uoc_val_11))
		uoc_val = 3;

	DVFS_DBG("%s curr_delay=%lu, uoc_val=%d\n", __func__, curr_delay, uoc_val);

	return uoc_val;
}
static int dvfs_get_uoc_val(unsigned long *p_volt_arm_new, unsigned long *p_volt_log_new, 
		unsigned long rate_khz)
{	
	int i = 0, j = 0;
	for (i = 0; i < size_dvfs_arm_table; i++) {
		if (dvfs_uoc_val_list[i].rate_arm != rate_khz)
			continue;
		for (j = 0; j < SIZE_SUPPORT_LOG_VOLT - 1; j++) {
			if (dvfs_uoc_val_list[i].vu_list[j].volt_log < *p_volt_log_new)
				continue;
			*p_volt_arm_new = dvfs_uoc_val_list[i].vu_list[j].volt_arm_new;
			*p_volt_log_new = dvfs_uoc_val_list[i].vu_list[j].volt_log_new;
			DVFS_DBG("%s: Varm_set=%lu, Vlog_set=%lu, uoc=%d\n", __func__,
					*p_volt_arm_new, *p_volt_log_new,
					dvfs_uoc_val_list[i].vu_list[j].uoc_val);
			return dvfs_uoc_val_list[i].vu_list[j].uoc_val;
		}
	}
	DVFS_ERR("%s: can not get uoc_val(Va=%lu, Vl=%lu, Ra=%lu)\n", __func__, 
			*p_volt_arm_new, *p_volt_log_new, rate_khz);
	return -1;
}
static int dvfs_set_uoc_val(int uoc_val)
{
	DVFS_DBG("%s set UOC = %d\n", __func__, uoc_val);
	writel_relaxed(
			((readl_relaxed(RK30_GRF_BASE + GRF_UOC3_CON0) | (3 << (12 + 16)))
			 & (~(3 << 12))) | (uoc_val << 12), RK30_GRF_BASE + GRF_UOC3_CON0);

	DVFS_DBG("read UOC=0x%08x\n", readl_relaxed(RK30_GRF_BASE + GRF_UOC3_CON0));
	return 0;
}

static int target_set_rate(struct clk_node *dvfs_clk, unsigned long rate_new)
{
	int ret = 0;

	if (dvfs_clk->clk_dvfs_target) {
		ret = dvfs_clk->clk_dvfs_target(dvfs_clk->clk, rate_new, clk_set_rate_locked);
	} else {
		ret = clk_set_rate_locked(dvfs_clk->clk, rate_new);
	}
	if (!ret)
		dvfs_clk->set_freq = rate_new / 1000;
	return ret;

}
static int dvfs_balance_volt(unsigned long volt_arm_old, unsigned long volt_log_old)
{
	int ret = 0;
	if (volt_arm_old > volt_log_old)
		ret = dvfs_scale_volt_direct(&vd_core, volt_arm_old);
	if (volt_arm_old < volt_log_old)
		ret = dvfs_scale_volt_direct(&vd_cpu, volt_log_old);
	if (ret)
		DVFS_ERR("%s error, volt_arm_old=%lu, volt_log_old=%lu\n", __func__, volt_arm_old, volt_log_old);
	return ret;
}
#if 0
// use 24M to switch uoc bits
static int uoc_pre = 0;
static int dvfs_scale_volt_rate_with_uoc(
		unsigned long volt_arm_new, unsigned long volt_log_new,
		unsigned long volt_arm_old, unsigned long volt_log_old,
		unsigned long rate_arm_new)
{
	int uoc_val = 0;
	unsigned int axi_div = 0x0;
	unsigned long flags, lpj_save;
	DVFS_DBG("Va_new=%lu uV, Vl_new=%lu uV;(was Va_old=%lu uV, Vl_old=%lu uV); Ra_new=%luHz\n",
			volt_arm_new, volt_log_new, volt_arm_old, volt_log_old,
			rate_arm_new);
	axi_div = readl_relaxed(RK30_CRU_BASE + CRU_CLKSELS_CON(1));
	uoc_val = dvfs_get_uoc_val(&volt_arm_new, &volt_log_new, rate_arm_new);
	if (uoc_val == uoc_pre) {
		dvfs_scale_volt_bystep(&vd_cpu, &vd_core, volt_arm_new, volt_log_new,
				100 * 1000, 100 * 1000,
				volt_arm_new > volt_log_new ? (volt_arm_new - volt_log_new) : 0,
				volt_log_new > volt_arm_new ? (volt_log_new - volt_arm_new) : 0);
	} else {

		//local_irq_save(flags);
		preempt_disable();
		u32 t[10];
		t[0] = readl_relaxed(RK30_TIMER1_BASE + 4);
		lpj_save = loops_per_jiffy;

		//arm slow mode
		writel_relaxed(PLL_MODE_SLOW(APLL_ID), RK30_CRU_BASE + CRU_MODE_CON);
		loops_per_jiffy = lpj_24m;
		smp_wmb();

		arm_pll_clk->rate = arm_pll_clk->recalc(arm_pll_clk);

		//cpu_axi parent to apll
		//writel_relaxed(0x00200000, RK30_CRU_BASE + CRU_CLKSELS_CON(0));
		clk_set_parent_nolock(clk_cpu_div, arm_pll_clk);

		//set axi/ahb/apb to 1:1:1
		writel_relaxed(axi_div & (~(0x3 << 0)) & (~(0x3 << 8)) & (~(0x3 << 12)), RK30_CRU_BASE + CRU_CLKSELS_CON(1));

		t[1] = readl_relaxed(RK30_TIMER1_BASE + 4);
		/*********************/
		//balance voltage before set UOC bits
		dvfs_balance_volt(volt_arm_old, volt_log_old);
		t[2] = readl_relaxed(RK30_TIMER1_BASE + 4);

		//set UOC bits
		dvfs_set_uoc_val(uoc_val);
		t[3] = readl_relaxed(RK30_TIMER1_BASE + 4);

		//voltage up
		dvfs_scale_volt_bystep(&vd_cpu, &vd_core, volt_arm_new, volt_log_new,
				100 * 1000, 100 * 1000,
				volt_arm_new > volt_log_new ? (volt_arm_new - volt_log_new) : 0,
				volt_log_new > volt_arm_new ? (volt_log_new - volt_arm_new) : 0);
		t[4] = readl_relaxed(RK30_TIMER1_BASE + 4);

		/*********************/
		//set axi/ahb/apb to default
		writel_relaxed(axi_div, RK30_CRU_BASE + CRU_CLKSELS_CON(1));

		//cpu_axi parent to gpll
		//writel_relaxed(0x00200020, RK30_CRU_BASE + CRU_CLKSELS_CON(0));
		clk_set_parent_nolock(clk_cpu_div, general_pll_clk);

		//arm normal mode
		writel_relaxed(PLL_MODE_NORM(APLL_ID), RK30_CRU_BASE + CRU_MODE_CON);
		loops_per_jiffy = lpj_save;
		smp_wmb();

		arm_pll_clk->rate = arm_pll_clk->recalc(arm_pll_clk);

		t[5] = readl_relaxed(RK30_TIMER1_BASE + 4);
		preempt_enable();
		//local_irq_restore(flags);
		DVFS_DBG(KERN_DEBUG "T %d %d %d %d %d\n", t[0] - t[1], t[1] - t[2], t[2] - t[3], t[3] - t[4], t[4] - t[5]);
		uoc_pre = uoc_val;
	}
	return 0;
}
#else
// use 312M to switch uoc bits
static int uoc_pre = 0;
static int dvfs_scale_volt_rate_with_uoc(
		unsigned long volt_arm_new, unsigned long volt_log_new,
		unsigned long volt_arm_old, unsigned long volt_log_old,
		unsigned long rate_arm_new)
{
	int uoc_val = 0;
	unsigned long arm_freq = 0;
	uoc_val = dvfs_get_uoc_val(&volt_arm_new, &volt_log_new, rate_arm_new);
	DVFS_DBG("Va_new=%lu uV, Vl_new=%lu uV;(was Va_old=%lu uV, Vl_old=%lu uV); Ra_new=%luHz, uoc=%d\n",
			volt_arm_new, volt_log_new, volt_arm_old, volt_log_old, rate_arm_new, uoc_val);
	if (uoc_val == uoc_pre) {
		dvfs_scale_volt_bystep(&vd_cpu, &vd_core, volt_arm_new, volt_log_new,
				100 * 1000, 100 * 1000,
				volt_arm_new > volt_log_new ? (volt_arm_new - volt_log_new) : 0,
				volt_log_new > volt_arm_new ? (volt_log_new - volt_arm_new) : 0);
	} else {
		//save arm freq
		arm_freq = clk_get_rate(clk_cpu);
		target_set_rate(dvfs_clk_cpu, 312 * MHZ);

		//cpu_axi parent to apll
		//writel_relaxed(0x00200000, RK30_CRU_BASE + CRU_CLKSELS_CON(0));
		clk_set_parent_nolock(clk_cpu_div, arm_pll_clk);

		/*********************/
		//balance voltage before set UOC bits
		dvfs_balance_volt(volt_arm_old, volt_log_old);

		//set UOC bits
		dvfs_set_uoc_val(uoc_val);

		//voltage up
		dvfs_scale_volt_bystep(&vd_cpu, &vd_core, volt_arm_new, volt_log_new,
				100 * 1000, 100 * 1000,
				volt_arm_new > volt_log_new ? (volt_arm_new - volt_log_new) : 0,
				volt_log_new > volt_arm_new ? (volt_log_new - volt_arm_new) : 0);

		/*********************/
		//cpu_axi parent to gpll
		//writel_relaxed(0x00200020, RK30_CRU_BASE + CRU_CLKSELS_CON(0));
		clk_set_parent_nolock(clk_cpu_div, general_pll_clk);

		//reset arm freq as normal freq
		target_set_rate(dvfs_clk_cpu, arm_freq);

		uoc_pre = uoc_val;
	}
	return 0;
}
#endif
int dvfs_target_cpu(struct clk *clk, unsigned long rate_hz)
{
	struct clk_node *dvfs_clk;
	int ret = 0;
	int volt_new = 0, volt_dep_new = 0, volt_old = 0, volt_dep_old = 0;
	struct cpufreq_frequency_table clk_fv;
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
	if (rate_hz < dvfs_clk->min_rate) {
		rate_hz = dvfs_clk->min_rate;
	} else if (rate_hz > dvfs_clk->max_rate) {
		rate_hz = dvfs_clk->max_rate;
	}

	/* need round rate */
	volt_old = vd_cpu.cur_volt;
	volt_dep_old = vd_core.cur_volt;

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
	dvfs_clk->set_volt = clk_fv.index;

	// target
	volt_new = dvfs_vd_get_newvolt_byclk(dvfs_clk);
	volt_dep_new = dvfs_vd_get_newvolt_bypd(&vd_core);

	if (volt_dep_new <= 0)
		goto fail_roll_back;

	if (rate_new < rate_old)
		target_set_rate(dvfs_clk, rate_new);

	dvfs_scale_volt_rate_with_uoc(volt_new, volt_dep_new, volt_old, volt_dep_old,
			rate_new / 1000);

	if (rate_new > rate_old)
		target_set_rate(dvfs_clk, rate_new);


	DVFS_DBG("UOC VOLT OK\n");

	return 0;
fail_roll_back:
	//dvfs_clk = clk_get_rate(dvfs_clk->clk);
	return -1;
}

int dvfs_target_core(struct clk *clk, unsigned long rate_hz)
{
	struct clk_node *dvfs_clk;
	int ret = 0;
	int volt_new = 0, volt_dep_new = 0, volt_old = 0, volt_dep_old = 0;
	struct cpufreq_frequency_table clk_fv;
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
	if (rate_hz < dvfs_clk->min_rate) {
		rate_hz = dvfs_clk->min_rate;
	} else if (rate_hz > dvfs_clk->max_rate) {
		rate_hz = dvfs_clk->max_rate;
	}

	/* need round rate */
	volt_old = vd_cpu.cur_volt;
	volt_dep_old = vd_core.cur_volt;

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
	dvfs_clk->set_volt = clk_fv.index;

	// target arm:volt_new/old, log:volt_dep_new/old
	volt_dep_new = dvfs_vd_get_newvolt_byclk(dvfs_clk);
	volt_new = dvfs_vd_get_newvolt_bypd(&vd_cpu);

	if (volt_dep_new <= 0)
		goto fail_roll_back;

	if (rate_new < rate_old)
		target_set_rate(dvfs_clk, rate_new);

	dvfs_scale_volt_rate_with_uoc(volt_new, volt_dep_new, volt_old, volt_dep_old,
			dvfs_clk_cpu->set_freq);

	if (rate_new > rate_old)
		target_set_rate(dvfs_clk, rate_new);

	DVFS_DBG("UOC VOLT OK\n");

	return 0;
fail_roll_back:
	//dvfs_clk = clk_get_rate(dvfs_clk->clk);
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
	.volt_set_flag		= DVFS_SET_VOLT_FAILURE,
	.vd_dvfs_target	= dvfs_target_cpu,
};

static struct vd_node vd_core = {
	.name 		= "vd_core",
	.regulator_name = "vdd_core",
	.volt_set_flag		= DVFS_SET_VOLT_FAILURE,
	.vd_dvfs_target	= dvfs_target_core,
};

static struct vd_node vd_rtc = {
	.name 		= "vd_rtc",
	.regulator_name	= "vdd_rtc",
	.volt_set_flag		= DVFS_SET_VOLT_FAILURE,
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
	RK_CLKS("aclk_periph", aclk_periph_pds, peri_aclk_dvfs_table, &rk_dvfs_clk_notifier),
};

#define RK_DEPPENDS(_clk_name, _pvd, _dep_table) \
{ \
	.clk_name	= _clk_name, \
	.dep_vd 	= _pvd,\
	.dep_table 	= _dep_table,	\
}

static struct depend_lookup rk30_depends[] = {
#ifndef CONFIG_ARCH_RK3066B
	RK_DEPPENDS("cpu", &vd_core, dep_cpu2core_table),
#endif
	//RK_DEPPENDS("gpu", &vd_cpu, NULL),
	//RK_DEPPENDS("gpu", &vd_cpu, NULL),
};
static struct avs_ctr_st rk30_avs_ctr;

int rk_dvfs_init(void)
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
	clk_cpu = clk_get(NULL, "cpu");
	if (IS_ERR_OR_NULL(clk_cpu)) {
		DVFS_ERR("%s get clk_cpu error\n", __func__);
		return -1;
	}

	clk_cpu_div = clk_get(NULL, "logic");
	if (IS_ERR_OR_NULL(clk_cpu_div)) {
		DVFS_ERR("%s get clk_cpu_div error\n", __func__);
		return -1;
	}

	arm_pll_clk = clk_get(NULL, "arm_pll");
	if (IS_ERR_OR_NULL(arm_pll_clk)) {
		DVFS_ERR("%s get arm_pll_clk error\n", __func__);
		return -1;
	}

	general_pll_clk = clk_get(NULL, "general_pll");
	if (IS_ERR_OR_NULL(general_pll_clk)) {
		DVFS_ERR("%s get general_pll_clk error\n", __func__);
		return -1;
	}

	avs_board_init(&rk30_avs_ctr);
	DVFS_DBG("rk30_dvfs_init\n");
	return 0;
}



/******************************rk30 avs**************************************************/

#ifdef CONFIG_ARCH_RK3066B

static void __iomem *rk30_nandc_base = NULL;

#define nandc_readl(offset)	readl_relaxed(rk30_nandc_base + offset)
#define nandc_writel(v, offset) do { writel_relaxed(v, rk30_nandc_base + offset); dsb(); } while (0)
static u8 rk30_get_avs_val(void)
{
	u32 nanc_save_reg[4];
	unsigned long flags;
	u32 paramet = 0;
	u32 count = 100;
	if(rk30_nandc_base == NULL)
		return 0;

	preempt_disable();
	local_irq_save(flags);

	nanc_save_reg[0] = nandc_readl(0);
	nanc_save_reg[1] = nandc_readl(0x130);
	nanc_save_reg[2] = nandc_readl(0x134);
	nanc_save_reg[3] = nandc_readl(0x158);

	nandc_writel(nanc_save_reg[0] | 0x1 << 14, 0);
	nandc_writel(0x5, 0x130);

	/* Just break lock status */
	nandc_writel(0x1, 0x158);
	nandc_writel(3, 0x158);
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

void rk30_avs_init(void)
{
	rk30_nandc_base = ioremap(RK30_NANDC_PHYS, RK30_NANDC_SIZE);
}
static struct avs_ctr_st rk30_avs_ctr = {
	.avs_init 		= rk30_avs_init,
	.avs_get_val	= rk30_get_avs_val,
};
#endif


