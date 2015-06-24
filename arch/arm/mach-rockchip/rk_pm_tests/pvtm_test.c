/* arch/arm/mach-rockchip/rk_pm_tests/pvtm_test.c
 *
 * Copyright (C) 2014 ROCKCHIP, Inc.
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
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/regulator/consumer.h>
#include <asm/cacheflush.h>

#define PVTM_TEST 0

extern int rockchip_tsadc_get_temp(int chn);
extern void rk29_wdt_keepalive(void);
extern u32 pvtm_get_value(u32 ch, u32 time_us);

#if PVTM_TEST
char *pvtm_buf;
static const char pi_result[] = "3141592653589793238462643383279528841971693993751058209749445923078164062862089986280348253421170679821480865132823664709384469555822317253594081284811174502841270193852115559644622948954930381964428810975665933446128475648233786783165271201991456485669234634861045432664821339360726024914127372458706606315588174881520920962829254917153643678925903611330530548820466521384146951941511609433057273657595919530921861173819326117931051185480744623799627495673518857527248912279381830119491298336733624406566438602139494639522473719070217986943702770539217176293176752384674818467669451320005681271452635608277857713427577896091736371787214684409012249534301465495853710579227968925892354201995611212902196864344181598136297747713099605187072113499999983729780499510597317328160963185";
int calc_pi(void)
{
	int bit = 0, i = 0;
	long a = 10000, b = 0, c = 2800, d = 0, e = 0, g = 0;
	int *result;
	long *f;
	int len = 0;
	char *pi_calc, *pi_tmp;
	char *pi_just = (char *)&pi_result[0];
	size_t pi_just_size = sizeof(pi_result);

	 result = vmalloc(10000*sizeof(int));
	 if (result == NULL)
		return -ENOMEM;

	 f = vmalloc(2801*sizeof(long));
	  if (f == NULL)
		return -ENOMEM;

	 pi_calc = vmalloc(1000*sizeof(char));
	  if (pi_calc == NULL)
		return -ENOMEM;

	for (; b-c; )
		f[b++] = a/5;
	for (; d = 0, g = c*2; c -= 14, result[bit++] = e+d/a, e = d%a)
		for (b = c; d += f[b]*a, f[b] = d%--g, d /= g--, --b; d *= b)
			;

	pi_tmp = pi_calc;
	for (i = 0; i < bit; i++)
		len += sprintf(pi_tmp+len, "%d", result[i]);

	if (strncmp(pi_just, pi_calc, pi_just_size) == 0) {
		vfree(result);
		vfree(f);
		vfree(pi_calc);
		return 0;
	} else {
		vfree(result);
		vfree(f);
		vfree(pi_calc);

		while (1)
			pr_info("calc_pi error\n");
	}
}

void pvtm_repeat_test(void)
{
	struct regulator *regulator_arm;
	struct clk *clk_core;
	u32 pvtm, delta, old_pvtm;
	u32 min_pvtm = -1, max_pvtm = 0;
	u32 average = 0, sum = 0;
	int i, n;

	regulator_arm = regulator_get(NULL, "vdd_arm");
	if (IS_ERR_OR_NULL(regulator_arm)) {
		pr_err("get regulator err\n");
		return;
	}

	clk_core = clk_get(NULL, "clk_core");
	if (IS_ERR_OR_NULL(clk_core)) {
		pr_err("get clk err\n");
		return;
	}

	n = 1000;
	for (i = 0; i < n; i++) {
		rk29_wdt_keepalive();
		pvtm = pvtm_get_value(0, 1000);

		sum += pvtm;
		if (!old_pvtm)
			old_pvtm = pvtm;

		if (pvtm > max_pvtm)
			max_pvtm = pvtm;

		if (pvtm < min_pvtm)
			min_pvtm = pvtm;
	}

	average = sum/n;
	delta = max_pvtm - min_pvtm;
	pr_info("rate %lu volt %d max %u min %u average %u delta %u\n",
		clk_get_rate(clk_core), regulator_get_voltage(regulator_arm),
		max_pvtm, min_pvtm, average, delta);
}

void pvtm_samp_interval_test(void)
{
	struct regulator *regulator_arm;
	struct clk *clk_core;
	u32 pvtm, old_pvtm = 0, samp_interval, times;
	int i, n;

	regulator_arm = regulator_get(NULL, "vdd_arm");
	if (IS_ERR_OR_NULL(regulator_arm)) {
		pr_err("get regulator err\n");
		return;
	}

	clk_core = clk_get(NULL, "clk_core");
	if (IS_ERR_OR_NULL(clk_core)) {
		pr_err("get clk err\n");
		return;
	}


	n = 10;
	for (i = 0; i < n; i++) {
		rk29_wdt_keepalive();
		samp_interval = 10 * (1 << i);
		pvtm = pvtm_get_value(0, samp_interval);

		if (!old_pvtm)
			old_pvtm = pvtm;

		times = (1000*pvtm)/old_pvtm;

		old_pvtm = pvtm;

		pr_info("rate %lu volt %d samp_interval %d pvtm %d times %d\n",
			clk_get_rate(clk_core),
			regulator_get_voltage(regulator_arm),
			samp_interval, pvtm, times);
	}
}

void pvtm_temp_test(void)
{
	struct regulator *regulator_arm;
	struct clk *clk_core;
	int temp, old_temp = 0;
	int volt;
	u32 rate;

	regulator_arm = regulator_get(NULL, "vdd_arm");
	if (IS_ERR_OR_NULL(regulator_arm)) {
		pr_err("get regulator err\n");
		return;
	}

	clk_core = clk_get(NULL, "clk_core");
	if (IS_ERR_OR_NULL(clk_core)) {
		pr_err("get clk err\n");
		return;
	}

	volt = 1100000;
	rate = 312000000;
	regulator_set_voltage(regulator_arm, volt, volt);
	clk_set_rate(clk_core, rate);

	do {
		rk29_wdt_keepalive();
		temp = rockchip_tsadc_get_temp(1);
		if (!old_temp)
			old_temp = temp;

		if (temp-old_temp >= 2)
			pr_info("rate %lu volt %d temp %d pvtm %u\n",
				clk_get_rate(clk_core),
				regulator_get_voltage(regulator_arm),
				temp, pvtm_get_value(0, 1000));

		old_temp = temp;
	} while (1);
}


#define ALL_DONE_FLAG	'a'
#define ONE_DONE_FLAG	'o'
#define ALL_BUF_SIZE	(PAGE_SIZE << 8)
#define ONE_BUF_SIZE	PAGE_SIZE
#define VOLT_STEP	(12500)		/*mv*/
#define VOLT_START	(1300000)	/*mv*/
#define VOLT_END	(1000000)	/*mv*/
#define RATE_STEP	(48000000)	/*hz*/
#define RATE_START	(816000000)	/*hz*/
#define RATE_END	(1200000000)	/*hz*/

void scale_min_pvtm_fix_volt(void)
{
	struct regulator *regulator_arm;
	struct clk *clk_core;
	unsigned long rate;
	u32 pvtm, old_pvtm = 0;
	int volt, i, ret = 0;

	regulator_arm = regulator_get(NULL, "vdd_arm");
	if (IS_ERR_OR_NULL(regulator_arm)) {
		pr_info("get regulator err\n");
		return;
	}

	clk_core = clk_get(NULL, "clk_core");
	if (IS_ERR_OR_NULL(clk_core)) {
		pr_info("get clk err\n");
		return;
	}

	volt = VOLT_START;
	rate = RATE_START;
	do {
		for (i = 0; i < ALL_BUF_SIZE; i += ONE_BUF_SIZE) {
			if (pvtm_buf[i] == ALL_DONE_FLAG) {
				pr_info("=============test done!========\n");
				do {
					if (i) {
						i -= ONE_BUF_SIZE;
						pr_info("%s", pvtm_buf+i+1);
					} else {
						pr_info("no item!!!!\n");
					}
				} while (i);

				do {
					rk29_wdt_keepalive();
					msleep(1000);
				} while (1);
			}
			if (pvtm_buf[i] != ONE_DONE_FLAG)
				break;
			volt -= VOLT_STEP;
		}

		if (volt < VOLT_END) {
			pvtm_buf[i] = ALL_DONE_FLAG;
			continue;
		}

		pvtm_buf[i] = ONE_DONE_FLAG;

		ret = regulator_set_voltage(regulator_arm, volt, volt);
		if (ret) {
			pr_err("set volt(%d) err:%d\n", volt, ret);

			do {
				rk29_wdt_keepalive();
				msleep(1000);
			} while (1);
		}

		do {
			rk29_wdt_keepalive();

			flush_cache_all();
			outer_flush_all();

			clk_set_rate(clk_core, rate);

			calc_pi();
			/*fft_test();*/
			mdelay(500);
			rk29_wdt_keepalive();

			pvtm = pvtm_get_value(0, 1000);
			if (!old_pvtm)
				old_pvtm = pvtm;
			sprintf(pvtm_buf+i+1, "%d %lu %d %d %d\n",
				volt, clk_get_rate(clk_core), pvtm,
				rockchip_tsadc_get_temp(1), old_pvtm-pvtm);

			pr_info("%s", pvtm_buf+i+1);

			old_pvtm = pvtm;
			rate += RATE_STEP;
		} while (1);
	} while (1);
}

void scale_min_pvtm_fix_rate(void)
{
	struct regulator *regulator_arm;
	struct clk *clk_core;
	unsigned long rate;
	u32 pvtm, old_pvtm = 0;
	int volt, i, ret = 0;

	regulator_arm = regulator_get(NULL, "vdd_arm");
	if (IS_ERR_OR_NULL(regulator_arm)) {
		pr_info("get regulator err\n");
		return;
	}

	clk_core = clk_get(NULL, "clk_core");
	if (IS_ERR_OR_NULL(clk_core)) {
		pr_info("get clk err\n");
		return;
	}

	volt = VOLT_START;
	rate = RATE_START;
	do {
		for (i = 0; i < ALL_BUF_SIZE; i += ONE_BUF_SIZE) {
			if (pvtm_buf[i] == ALL_DONE_FLAG) {
				pr_info("=============test done!========\n");
				do {
					if (i) {
						i -= ONE_BUF_SIZE;
						pr_info("%s", pvtm_buf+i+1);
					} else {
						pr_info("no item!!!!\n");
					}
				} while (i);

				do {
					rk29_wdt_keepalive();
					msleep(1000);
				} while (1);
			}
			if (pvtm_buf[i] != ONE_DONE_FLAG)
				break;

			rate += RATE_STEP;
		}

		if (rate > RATE_END) {
			pvtm_buf[i] = ALL_DONE_FLAG;
			continue;
		}

		pvtm_buf[i] = ONE_DONE_FLAG;

		ret = regulator_set_voltage(regulator_arm, volt, volt);
		if (ret) {
			pr_err("set volt(%d) err:%d\n", volt, ret);

			do {
				rk29_wdt_keepalive();
				msleep(1000);
			} while (1);
		}

		ret = clk_set_rate(clk_core, rate);
		do {
			rk29_wdt_keepalive();

			flush_cache_all();
			outer_flush_all();
			regulator_set_voltage(regulator_arm, volt, volt);

			calc_pi();
			mdelay(500);
			rk29_wdt_keepalive();

			pvtm = pvtm_get_value(0, 1000);
			if (!old_pvtm)
				old_pvtm = pvtm;

			sprintf(pvtm_buf+i+1, "%d %lu %d %d %d\n",
				volt, clk_get_rate(clk_core), pvtm,
				rockchip_tsadc_get_temp(1), old_pvtm-pvtm);

			pr_info("%s", pvtm_buf+i+1);
			old_pvtm = pvtm;
			volt -= VOLT_STEP;
		} while (1);
	} while (1);
}

#endif

void scale_pvtm_fix_freq(void)
{
	struct regulator *regulator_arm;
	struct clk *clk_core_b, *clk_core_l;
	unsigned long rate;
	u32 pvtm, old_pvtm = 0;
	int volt, ret = 0;

	regulator_arm = regulator_get(NULL, "vdd_arm");
	if (IS_ERR_OR_NULL(regulator_arm)) {
		pr_info("get regulator err\n");
		return;
	}

	clk_core_b = clk_get(NULL, "clk_core_b");
	if (IS_ERR_OR_NULL(clk_core_b)) {
		pr_info("get clk err\n");
		return;
	}
	clk_core_l = clk_get(NULL, "clk_core_l");
	if (IS_ERR_OR_NULL(clk_core_l)) {
		pr_info("get clk err\n");
		return;
	}

	volt = 1300000;
	rate = 216000000;
	ret = regulator_set_voltage(regulator_arm, volt, volt);
	if (ret)
		pr_err("set volt(%d) err:%d\n", volt, ret);

	do {
		clk_set_rate(clk_core_b, rate);
		clk_set_rate(clk_core_l, rate);
		mdelay(500);
		pvtm = pvtm_get_value(0, 1000);
		if (!old_pvtm)
			old_pvtm = pvtm;
		pr_info("%d %lu %d %d\n",
			volt, clk_get_rate(clk_core_b), pvtm, old_pvtm-pvtm);

		old_pvtm = pvtm;
		rate += 48000000;
		if (rate > 1200000000)
			break;
	} while (1);
}

void scale_pvtm_fix_volt(void)
{
	struct regulator *regulator_arm;
	struct clk *clk_core_b, *clk_core_l;
	unsigned long rate;
	u32 pvtm, old_pvtm = 0;
	int volt, ret = 0;

	regulator_arm = regulator_get(NULL, "vdd_arm");
	if (IS_ERR_OR_NULL(regulator_arm)) {
		pr_info("get regulator err\n");
		return;
	}

	clk_core_b = clk_get(NULL, "clk_core_b");
	if (IS_ERR_OR_NULL(clk_core_b)) {
		pr_info("get clk err\n");
		return;
	}

	clk_core_l = clk_get(NULL, "clk_core_b");
	if (IS_ERR_OR_NULL(clk_core_l)) {
		pr_info("get clk err\n");
		return;
	}

	volt = 1300000;
	rate = 816000000;

	ret = regulator_set_voltage(regulator_arm, volt, volt);
	if (ret)
		pr_err("set volt(%d) err:%d\n", volt, ret);

	ret = clk_set_rate(clk_core_b, rate);
	ret = clk_set_rate(clk_core_l, rate);
	do {
		regulator_set_voltage(regulator_arm, volt, volt);
		mdelay(500);
		pvtm = pvtm_get_value(0, 1000);
		if (!old_pvtm)
			old_pvtm = pvtm;

		pr_info("%d %lu %d %d\n",
			volt, clk_get_rate(clk_core_b), pvtm, old_pvtm-pvtm);
		old_pvtm = pvtm;
		volt -= 12500;
		if (volt < 950000) {
			regulator_set_voltage(regulator_arm, 1300000, 1300000);
			break;
		}
	} while (1);
}

ssize_t pvtm_show(struct kobject *kobj, struct kobj_attribute *attr,
		  char *buf)
{
	char *str = buf;

	str += sprintf(str, "core:%d\ngpu:%d\n",
		pvtm_get_value(0, 1000),
		pvtm_get_value(1, 1000));
	return (str - buf);
}

ssize_t pvtm_store(struct kobject *kobj, struct kobj_attribute *attr,
		   const char *buf, size_t n)
{
	return n;
}

int pvtm_buf_init(void)
{
#if PVTM_TEST
	pvtm_buf = (char *)__get_free_pages(GFP_KERNEL, 8);

#endif
	return 0;
}
fs_initcall(pvtm_buf_init);
