/*
 * amlogic_thermal.c - Samsung amlogic thermal (Thermal Management Unit)
 *
 *  Copyright (C) 2011 Samsung Electronics
 *  Donggeun Kim <dg77.kim@samsung.com>
 *  Amit Daniel Kachhap <amit.kachhap@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/thermal.h>
#include <linux/cpufreq.h>
#include <linux/cpu_cooling.h>
#include <linux/of.h>
#include <linux/amlogic/saradc.h>
#include <plat/cpu.h>
#include <linux/random.h>
#include <linux/gpu_cooling.h>
#include <linux/cpucore_cooling.h>
#include <linux/gpucore_cooling.h>

#ifdef CONFIG_AML_VIRTUAL_THERMAL
#define DBG_VIRTUAL         0
static int trim_flag = 0;
static int virtual_thermal_en = 0;

struct aml_virtual_thermal {
    unsigned int freq;
    unsigned int temp_time[4];
};

struct aml_virtual_thermal_device {
    int count;
    struct aml_virtual_thermal *thermal;
};

static struct aml_virtual_thermal_device cpu_virtual_thermal = {};
static struct aml_virtual_thermal_device gpu_virtual_thermal = {};
static unsigned int report_interval[4] = {};
static struct delayed_work freq_collect_work;
static int freq_sample_period = 30;
#endif  /* CONFIG_AML_VIRTUAL_THERMAL */

struct freq_trip_table {
	unsigned int freq_state;
};
struct temp_trip{
	unsigned int temperature;
	unsigned int cpu_upper_freq;
	unsigned int cpu_lower_freq;
	int cpu_upper_level;
	int cpu_lower_level;
	unsigned int gpu_upper_freq;
	unsigned int gpu_lower_freq;
	int gpu_upper_level;
	int gpu_lower_level;
	int cpu_core_num;
	int cpu_core_upper;
	int gpu_core_num;
	int gpu_core_upper;
};

struct amlogic_thermal_platform_data {
	const char *name;
	struct temp_trip *tmp_trip;
	unsigned int temp_trip_count;
	unsigned int critical_temp;
	unsigned int idle_interval;
	struct thermal_zone_device *therm_dev;
	struct thermal_cooling_device *cpu_cool_dev;
	struct thermal_cooling_device *gpu_cool_dev;
	struct thermal_cooling_device *cpucore_cool_dev;
	struct thermal_cooling_device *gpucore_cool_dev;
	enum thermal_device_mode mode;
	struct mutex lock;
};
struct temp_level{
	unsigned int temperature;
	int cpu_high_freq;
	int cpu_low_freq;
	int gpu_high_freq;
	int gpu_low_freq;
	int cpu_core_num;
	int gpu_core_num;
};

/* CPU Zone information */
#define PANIC_ZONE      4
#define WARN_ZONE       3
#define MONITOR_ZONE    2
#define SAFE_ZONE       1

#define GET_ZONE(trip) (trip + 2)
#define GET_TRIP(zone) (zone - 2)

static void amlogic_unregister_thermal(struct amlogic_thermal_platform_data *pdata);
static int amlogic_register_thermal(struct amlogic_thermal_platform_data *pdata);

/* Get mode callback functions for thermal zone */
static int amlogic_get_mode(struct thermal_zone_device *thermal,
			enum thermal_device_mode *mode)
{
	struct  amlogic_thermal_platform_data *pdata= thermal->devdata;
	
	if (pdata)
		*mode = pdata->mode;
	return 0;
}

/* Set mode callback functions for thermal zone */
static int amlogic_set_mode(struct thermal_zone_device *thermal,
			enum thermal_device_mode mode)
{
	struct  amlogic_thermal_platform_data *pdata= thermal->devdata;
	struct cpucore_cooling_device *cpucore_device =NULL;
	struct gpucore_cooling_device *gpucore_device = NULL;
	if(!pdata)
		return -EINVAL;
	
	//mutex_lock(&pdata->therm_dev->lock);
	
	if (mode == THERMAL_DEVICE_ENABLED){
		pdata->therm_dev->polling_delay = pdata->idle_interval;
		if(pdata->cpucore_cool_dev){
			cpucore_device=pdata->cpucore_cool_dev->devdata;
			cpucore_device->stop_flag=0;
		}
		if(pdata->gpucore_cool_dev){
			gpucore_device=pdata->gpucore_cool_dev->devdata;
			gpucore_device->stop_flag=0;
		}
	}
	else{
		pdata->therm_dev->polling_delay = 0;
		if(pdata->cpucore_cool_dev)
			pdata->cpucore_cool_dev->ops->set_cur_state(pdata->cpucore_cool_dev,(0|CPU_STOP));
		if(pdata->gpucore_cool_dev)
			pdata->gpucore_cool_dev->ops->set_cur_state(pdata->gpucore_cool_dev,(0|GPU_STOP));
	}

	//mutex_unlock(&pdata->therm_dev->lock);

	pdata->mode = mode;
	thermal_zone_device_update(pdata->therm_dev);
	pr_info("thermal polling set for duration=%d msec\n",
				pdata->therm_dev->polling_delay);
	return 0;
}


/* Get trip type callback functions for thermal zone */
static int amlogic_get_trip_type(struct thermal_zone_device *thermal, int trip,
				 enum thermal_trip_type *type)
{
	if(trip < thermal->trips-1)
		*type = THERMAL_TRIP_ACTIVE;
	else if(trip == thermal->trips-1)
		*type = THERMAL_TRIP_CRITICAL;
	else 
		return -EINVAL;
	return 0;
}

/* Get trip temperature callback functions for thermal zone */
static int amlogic_get_trip_temp(struct thermal_zone_device *thermal, int trip,
				unsigned long *temp)
{
	struct  amlogic_thermal_platform_data *pdata= thermal->devdata;
	
	if(trip > pdata->temp_trip_count ||trip<0)
		return  -EINVAL;
	mutex_lock(&pdata->lock);
	*temp =pdata->tmp_trip[trip].temperature;
	/* convert the temperature into millicelsius */
	mutex_unlock(&pdata->lock);

	return 0;
}

static int amlogic_set_trip_temp(struct thermal_zone_device *thermal, int trip,
				unsigned long temp)
{
	struct  amlogic_thermal_platform_data *pdata= thermal->devdata;
	
	if(trip > pdata->temp_trip_count ||trip<0)
		return  -EINVAL;
	mutex_lock(&pdata->lock);
	pdata->tmp_trip[trip].temperature=temp;
	/* convert the temperature into millicelsius */
	mutex_unlock(&pdata->lock);
	return 0;
}

/* Get critical temperature callback functions for thermal zone */
static int amlogic_get_crit_temp(struct thermal_zone_device *thermal,
				unsigned long *temp)
{
	int ret;
	/* Panic zone */
	ret =amlogic_get_trip_temp(thermal, thermal->trips-1, temp);
	
	return ret;
}


/* Bind callback functions for thermal zone */
static int amlogic_bind(struct thermal_zone_device *thermal,
			struct thermal_cooling_device *cdev)
{
	int ret = 0, i;
	struct  amlogic_thermal_platform_data *pdata= thermal->devdata;
	int id;
	char type[THERMAL_NAME_LENGTH];
	if (!sscanf(cdev->type, "thermal-%7s-%d", type,&id))
		return -EINVAL;
	if(!strcmp(type,"cpufreq")){
		/* Bind the thermal zone to the cpufreq cooling device */
		for (i = 0; i < pdata->temp_trip_count; i++) {
			if(pdata->tmp_trip[0].cpu_upper_level==THERMAL_CSTATE_INVALID)
			{
				printk("disable cpu cooling device by dtd\n");
				ret = -EINVAL;
				goto out;
			}
			if (thermal_zone_bind_cooling_device(thermal, i, cdev,
								pdata->tmp_trip[i].cpu_upper_level,
								pdata->tmp_trip[i].cpu_lower_level)) {
				pr_err("error binding cdev inst %d\n", i);
				ret = -EINVAL;
				goto out;
			}
		}
		pr_info("%s bind %s okay !\n",thermal->type,cdev->type);
	}
	
	if(!strcmp(type,"gpufreq")){
		struct gpufreq_cooling_device *gpufreq_dev=
			(struct gpufreq_cooling_device *)cdev->devdata;
		/* Bind the thermal zone to the cpufreq cooling device */
		for (i = 0; i < pdata->temp_trip_count; i++) {
			if(!gpufreq_dev->get_gpu_freq_level){
				ret = -EINVAL;
				pr_info("invalidate pointer %p\n",gpufreq_dev->get_gpu_freq_level);
				goto out;
			}
			pdata->tmp_trip[i].gpu_lower_level=gpufreq_dev->get_gpu_freq_level(pdata->tmp_trip[i].gpu_upper_freq);
			pdata->tmp_trip[i].gpu_upper_level=gpufreq_dev->get_gpu_freq_level(pdata->tmp_trip[i].gpu_lower_freq);
			printk("pdata->tmp_trip[%d].gpu_lower_level=%d\n",i,pdata->tmp_trip[i].gpu_lower_level);
			printk("pdata->tmp_trip[%d].gpu_upper_level=%d\n",i,pdata->tmp_trip[i].gpu_upper_level);
			if(pdata->tmp_trip[0].gpu_lower_level==THERMAL_CSTATE_INVALID)
			{
				printk("disable gpu cooling device by dtd\n");
				ret = -EINVAL;
				goto out;
			}
			if (thermal_zone_bind_cooling_device(thermal, i, cdev,
								pdata->tmp_trip[i].gpu_upper_level,
								pdata->tmp_trip[i].gpu_lower_level)) {
				pr_err("error binding cdev inst %d\n", i);
				ret = -EINVAL;
				goto out;
			}
		}
		pdata->gpu_cool_dev=cdev;
		pr_info("%s bind %s okay !\n",thermal->type,cdev->type);
	}

	if(!strcmp(type,"cpucore")){
		/* Bind the thermal zone to the cpufreq cooling device */
		struct cpucore_cooling_device *cpucore_dev=
			(struct cpucore_cooling_device *)cdev->devdata;
		for (i = 0; i < pdata->temp_trip_count; i++) {
			if(pdata->tmp_trip[0].cpu_core_num==THERMAL_CSTATE_INVALID)
			{
				printk("disable cpu cooling device by dtd\n");
				ret = -EINVAL;
				goto out;
			}
			if(pdata->tmp_trip[i].cpu_core_num !=-1)
				pdata->tmp_trip[i].cpu_core_upper=cpucore_dev->max_cpu_core_num-pdata->tmp_trip[i].cpu_core_num;
			else
				pdata->tmp_trip[i].cpu_core_upper=pdata->tmp_trip[i].cpu_core_num;
			printk("tmp_trip[%d].cpu_core_upper=%d\n",i,pdata->tmp_trip[i].cpu_core_upper);
			if (thermal_zone_bind_cooling_device(thermal, i, cdev,
								pdata->tmp_trip[i].cpu_core_upper,
								pdata->tmp_trip[i].cpu_core_upper)) {
				pr_err("error binding cdev inst %d\n", i);
				ret = -EINVAL;
				goto out;
			}
		}
		pr_info("%s bind %s okay !\n",thermal->type,cdev->type);
	}

	if(!strcmp(type,"gpucore")){
		/* Bind the thermal zone to the cpufreq cooling device */
		struct gpucore_cooling_device *gpucore_dev=
			(struct gpucore_cooling_device *)cdev->devdata;
		for (i = 0; i < pdata->temp_trip_count; i++) {
			if(pdata->tmp_trip[0].cpu_core_num==THERMAL_CSTATE_INVALID)
			{
				printk("disable cpu cooling device by dtd\n");
				ret = -EINVAL;
				goto out;
			}
			if(pdata->tmp_trip[i].gpu_core_num != -1)
				pdata->tmp_trip[i].gpu_core_upper=gpucore_dev->max_gpu_core_num-pdata->tmp_trip[i].gpu_core_num;
			else
				pdata->tmp_trip[i].gpu_core_upper=pdata->tmp_trip[i].gpu_core_num;
			
			printk("tmp_trip[%d].gpu_core_upper=%d\n",i,pdata->tmp_trip[i].gpu_core_upper);
			if (thermal_zone_bind_cooling_device(thermal, i, cdev,
								pdata->tmp_trip[i].gpu_core_upper,
								pdata->tmp_trip[i].gpu_core_upper)) {
				pr_err("error binding cdev inst %d\n", i);
				ret = -EINVAL;
				goto out;
			}
		}
		pdata->gpucore_cool_dev=cdev;
		pr_info("%s bind %s okay !\n",thermal->type,cdev->type);
	}
	return ret;
out:
	return ret;
}

/* Unbind callback functions for thermal zone */
static int amlogic_unbind(struct thermal_zone_device *thermal,
			struct thermal_cooling_device *cdev)
{
	int i;
	if(thermal && cdev){
		struct  amlogic_thermal_platform_data *pdata= thermal->devdata;
		for (i = 0; i < pdata->temp_trip_count; i++) {
			pr_info("\n%s unbinding %s ",thermal->type,cdev->type);
			if (thermal_zone_unbind_cooling_device(thermal, i, cdev)) {
				pr_err(" error  %d \n", i);
				return -EINVAL;
			}
			pr_info(" okay\n");
			return 0;
		}
	}else{
		return -EINVAL;
	}
	return -EINVAL;
}
#ifdef CONFIG_AML_VIRTUAL_THERMAL
#define ABS(a) ((a) > 0 ? (a) : -(a))

static unsigned int (*gpu_freq_callback)(void) = NULL;
int register_gpu_freq_info(unsigned int (*fun)(void))
{
    if (fun) {
        gpu_freq_callback = fun;
    } 
    return 0;
}
EXPORT_SYMBOL(register_gpu_freq_info);

static atomic_t last_gpu_avg_freq;
static atomic_t last_cpu_avg_freq;
static atomic_t freq_update_flag; 

static void collect_freq_work(struct work_struct *work)
{
    static unsigned int total_cpu_freq = 0; 
    static unsigned int total_gpu_freq = 0; 
    static unsigned int count = 0; 
    struct cpufreq_policy *policy = cpufreq_cpu_get(0);

    if (policy) {
        total_cpu_freq += policy->cur;
        count++;
    } else {
        total_cpu_freq += 0;    
    }

    if (gpu_freq_callback) {
        total_gpu_freq += gpu_freq_callback();
    } else {
        total_gpu_freq += 0; 
    }
    if (count >= freq_sample_period * 10) {
        atomic_set(&last_cpu_avg_freq, total_cpu_freq / count);
        atomic_set(&last_gpu_avg_freq, total_gpu_freq / count);
        total_cpu_freq = 0;
        total_gpu_freq = 0;
        count = 0;
        atomic_set(&freq_update_flag, 1);
    }
    schedule_delayed_work(&freq_collect_work, msecs_to_jiffies(100));
}

static int aml_virtaul_thermal_probe(struct platform_device *pdev)
{
    int ret, len, cells; 
    struct property *prop;
    void *buf;

    if (!of_property_read_bool(pdev->dev.of_node, "use_virtual_thermal")) {
        printk("%s, virtual thermal is not enabled\n", __func__);
        virtual_thermal_en = 0;
        return 0;
    } else {
        printk("%s, virtual thermal enabled\n", __func__);
    }

    ret = of_property_read_u32(pdev->dev.of_node,
                               "freq_sample_period",
                               &freq_sample_period);
    if (ret) {
        printk("%s, get freq_sample_period failed, us 30 as default\n", __func__);
        freq_sample_period = 30;
    } else {
        printk("%s, get freq_sample_period with value:%d\n", __func__, freq_sample_period);    
    }
    ret = of_property_read_u32_array(pdev->dev.of_node, 
                                     "report_time", 
                                     report_interval, sizeof(report_interval) / sizeof(u32));
    if (ret) {
        printk("%s, get report_time failed\n", __func__);    
        goto error;
    } else {
        printk("[virtual_thermal] report interval:%4d, %4d, %4d, %4d\n",
               report_interval[0], report_interval[1], report_interval[2], report_interval[3]);    
    }
    /*
     * read cpu_virtal
     */
    prop = of_find_property(pdev->dev.of_node, "cpu_virtual", &len);
    if (!prop) {
        printk("%s, cpu virtual not found\n", __func__);
        goto error;
    }
    cells = len / sizeof(struct aml_virtual_thermal);
    buf = kzalloc(len, GFP_KERNEL);
    if (!buf) {
        printk("%s, no memory\n", __func__);
        return -ENOMEM;
    }
    ret = of_property_read_u32_array(pdev->dev.of_node, 
                                     "cpu_virtual", 
                                     buf, len/sizeof(u32)); 
    if (ret) {
        printk("%s, read cpu_virtual failed\n", __func__);
        kfree(buf);
        goto error;
    }
    cpu_virtual_thermal.count   = cells;
    cpu_virtual_thermal.thermal = buf;

    /*
     * read gpu_virtal
     */
    prop = of_find_property(pdev->dev.of_node, "gpu_virtual", &len);
    if (!prop) {
        printk("%s, gpu virtual not found\n", __func__);
        goto error;
    }
    cells = len / sizeof(struct aml_virtual_thermal);
    buf = kzalloc(len, GFP_KERNEL);
    if (!buf) {
        printk("%s, no memory\n", __func__);
        return -ENOMEM;
    }
    ret = of_property_read_u32_array(pdev->dev.of_node, 
                                     "gpu_virtual", 
                                     buf, len/sizeof(u32)); 
    if (ret) {
        printk("%s, read gpu_virtual failed\n", __func__);
        kfree(buf);
        goto error;
    }
    gpu_virtual_thermal.count   = cells;
    gpu_virtual_thermal.thermal = buf;

#if DBG_VIRTUAL
    printk("cpu_virtal cells:%d, table:\n", cpu_virtual_thermal.count);
    for (len = 0; len < cpu_virtual_thermal.count; len++) {
        printk("%2d, %8d, %4d, %4d, %4d, %4d\n",
               len, 
               cpu_virtual_thermal.thermal[len].freq,
               cpu_virtual_thermal.thermal[len].temp_time[0],
               cpu_virtual_thermal.thermal[len].temp_time[1],
               cpu_virtual_thermal.thermal[len].temp_time[2],
               cpu_virtual_thermal.thermal[len].temp_time[3]);
    }
    printk("gpu_virtal cells:%d, table:\n", gpu_virtual_thermal.count);
    for (len = 0; len < gpu_virtual_thermal.count; len++) {
        printk("%2d, %8d, %4d, %4d, %4d, %4d\n",
               len, 
               gpu_virtual_thermal.thermal[len].freq,
               gpu_virtual_thermal.thermal[len].temp_time[0],
               gpu_virtual_thermal.thermal[len].temp_time[1],
               gpu_virtual_thermal.thermal[len].temp_time[2],
               gpu_virtual_thermal.thermal[len].temp_time[3]);
    }
#endif

    virtual_thermal_en = 1;    
    return 0;

error: 
    virtual_thermal_en = 0;
    return -1;
}

static void aml_virtual_thermal_remove(void)
{
    kfree(cpu_virtual_thermal.thermal);    
    kfree(gpu_virtual_thermal.thermal);    
    virtual_thermal_en = 0;
}

static int check_freq_level(struct aml_virtual_thermal_device *dev, unsigned int freq)
{
    int i = 0;

    if (freq >= dev->thermal[dev->count-1].freq) {
        return dev->count - 1;
    }
    for (i = 0; i < dev->count - 1; i++) {
        if (freq > dev->thermal[i].freq && freq <= dev->thermal[i + 1].freq) {
            return i + 1;
        }
    }
    return 0; 
}

static int check_freq_level_cnt(unsigned int cnt) 
{
    int i;

    if (cnt >= report_interval[3]) {
        return  3; 
    } 
    for (i = 0; i < 3; i++) {
        if (cnt >= report_interval[i] && cnt < report_interval[i + 1]) {
            return i;
        }
    }
    return 0;
}

static unsigned long aml_cal_virtual_temp(void)
{
    static unsigned int cpu_freq_level_cnt  = 0, gpu_freq_level_cnt  = 0;
    static unsigned int last_cpu_freq_level = 0, last_gpu_freq_level = 0;
    static unsigned int cpu_temp = 40, gpu_temp = 40;                   // default set to 40 when at homescreen
    unsigned int curr_cpu_avg_freq,   curr_gpu_avg_freq;
    int curr_cpu_freq_level, curr_gpu_freq_level;
    int cnt_level, level_diff; 
    int temp_update = 0, final_temp;
    
    /*
     * CPU temp 
     */
    if (atomic_read(&freq_update_flag)) {
        curr_cpu_avg_freq = atomic_read(&last_cpu_avg_freq);
        curr_cpu_freq_level = check_freq_level(&cpu_virtual_thermal, curr_cpu_avg_freq); 
        level_diff = curr_cpu_freq_level - last_cpu_freq_level;
        if (ABS(level_diff) <= 1) {  // freq change is not large 
            cpu_freq_level_cnt++;
            cnt_level = check_freq_level_cnt(cpu_freq_level_cnt);
            cpu_temp  = cpu_virtual_thermal.thermal[curr_cpu_freq_level].temp_time[cnt_level];
        #if DBG_VIRTUAL
            printk("%s, cur_freq:%7d, freq_level:%d, cnt_level:%d, cnt:%d, cpu_temp:%d\n",
                   __func__, curr_cpu_avg_freq, curr_cpu_freq_level, cnt_level, cpu_freq_level_cnt, cpu_temp);
        #endif
        } else {                                                // level not match
            cpu_temp = cpu_virtual_thermal.thermal[curr_cpu_freq_level].temp_time[0]; 
        #if DBG_VIRTUAL
            printk("%s, cur_freq:%7d, cur_level:%d, last_level:%d, last_cnt_level:%d, cpu_temp:%d\n",
                   __func__, curr_cpu_avg_freq, curr_cpu_freq_level, last_cpu_freq_level, cpu_freq_level_cnt, cpu_temp);
        #endif
            cpu_freq_level_cnt = 0;
        }
        last_cpu_freq_level = curr_cpu_freq_level;

        curr_gpu_avg_freq = atomic_read(&last_gpu_avg_freq);
        curr_gpu_freq_level = check_freq_level(&gpu_virtual_thermal, curr_gpu_avg_freq); 
        level_diff = curr_gpu_freq_level - last_gpu_freq_level;
        if (ABS(level_diff) <= 1) {  // freq change is not large 
            gpu_freq_level_cnt++;
            cnt_level = check_freq_level_cnt(gpu_freq_level_cnt);
            gpu_temp  = gpu_virtual_thermal.thermal[curr_gpu_freq_level].temp_time[cnt_level];
        #if DBG_VIRTUAL
            printk("%s, cur_freq:%7d, freq_level:%d, cnt_level:%d, cnt:%d, gpu_temp:%d\n",
                   __func__, curr_gpu_avg_freq, curr_gpu_freq_level, cnt_level, gpu_freq_level_cnt, gpu_temp);
        #endif
        } else {                                                // level not match
            gpu_temp = gpu_virtual_thermal.thermal[curr_gpu_freq_level].temp_time[0]; 
            gpu_freq_level_cnt = 0;
        #if DBG_VIRTUAL
            printk("%s, cur_freq:%7d, cur_level:%d, last_level:%d, gpu_temp:%d\n",
                   __func__, curr_gpu_avg_freq, curr_gpu_freq_level, last_gpu_freq_level, gpu_temp);
        #endif
        }
        last_gpu_freq_level = curr_gpu_freq_level;

        atomic_set(&freq_update_flag, 0);
        temp_update = 1; 
    }

    if (cpu_temp <= 0 && gpu_temp <= 0) {
        printk("%s, Bug here, cpu & gpu temp can't be 0, cpu_temp:%d, gpu_temp:%d\n", __func__, cpu_temp, gpu_temp);
        final_temp = 40;    
    }
    final_temp = (cpu_temp >= gpu_temp ? cpu_temp : gpu_temp);
    if (temp_update) {
    #if DBG_VIRTUAL
        printk("final temp:%d\n", final_temp);    
    #endif
    }
    return final_temp;
}
#endif

/* Get temperature callback functions for thermal zone */
int aa=50;
int trend=1;
static int amlogic_get_temp(struct thermal_zone_device *thermal,
			unsigned long *temp)
{
#if 0
	if(aa>=100)
		trend=1;
	else if (aa<=40)
		trend=0;
	
	if(trend)
		aa=aa-5;
	else
		aa=aa+5;
	//get_random_bytes(&aa,4);
	printk("========  temp=%d\n",aa);
	*temp=aa;
#else
#ifdef CONFIG_AML_VIRTUAL_THERMAL
    if (trim_flag) { 
	    *temp = (unsigned long)get_cpu_temp();
    } else if (virtual_thermal_en) {
	    *temp = aml_cal_virtual_temp(); 
    } else {
        *temp = 45;                     // fix cpu temperature to 45 if not trimed && disable virtual thermal    
    }
#else
	*temp = (unsigned long)get_cpu_temp();
	pr_debug( "========  temp=%ld\n",*temp);
#endif
#endif
	return 0;
}

/* Get the temperature trend */
static int amlogic_get_trend(struct thermal_zone_device *thermal,
			int trip, enum thermal_trend *trend)
{
	return 1;
}
/* Operation callback functions for thermal zone */
static struct thermal_zone_device_ops const amlogic_dev_ops = {
	.bind = amlogic_bind,
	.unbind = amlogic_unbind,
	.get_temp = amlogic_get_temp,
	.get_trend = amlogic_get_trend,
	.get_mode = amlogic_get_mode,
	.set_mode = amlogic_set_mode,
	.get_trip_type = amlogic_get_trip_type,
	.get_trip_temp = amlogic_get_trip_temp,
	.set_trip_temp = amlogic_set_trip_temp,
	.get_crit_temp = amlogic_get_crit_temp,
};



/* Register with the in-kernel thermal management */
static int amlogic_register_thermal(struct amlogic_thermal_platform_data *pdata)
{
	int ret=0;
	struct cpumask mask_val;
	memset(&mask_val,0,sizeof(struct cpumask));
	cpumask_set_cpu(0, &mask_val);
	pdata->cpu_cool_dev= cpufreq_cooling_register(&mask_val);
	if (IS_ERR(pdata->cpu_cool_dev)) {
		pr_err("Failed to register cpufreq cooling device\n");
		ret = -EINVAL;
		goto err_unregister;
	}
	pdata->cpucore_cool_dev = cpucore_cooling_register();
	if (IS_ERR(pdata->cpucore_cool_dev)) {
		pr_err("Failed to register cpufreq cooling device\n");
		ret = -EINVAL;
		goto err_unregister;
	}

	pdata->therm_dev = thermal_zone_device_register(pdata->name,
			pdata->temp_trip_count, 7, pdata, &amlogic_dev_ops, NULL, 0,
			pdata->idle_interval);

	if (IS_ERR(pdata->therm_dev)) {
		pr_err("Failed to register thermal zone device\n");
		ret = -EINVAL;
		goto err_unregister;
	}

	pr_info("amlogic: Kernel Thermal management registered\n");

	return 0;

err_unregister:
	amlogic_unregister_thermal(pdata);
	return ret;
}

/* Un-Register with the in-kernel thermal management */
static void amlogic_unregister_thermal(struct amlogic_thermal_platform_data *pdata)
{
	if (pdata->therm_dev)
		thermal_zone_device_unregister(pdata->therm_dev);
	if (pdata->cpu_cool_dev)
		cpufreq_cooling_unregister(pdata->cpu_cool_dev);

	pr_info("amlogic: Kernel Thermal management unregistered\n");
}
/*
struct amlogic_thermal_platform_data Pdata={
	.name="amlogic, theraml",
	.temp_trip_count=3,
	.idle_interval=1000,
	.therm_dev=NULL,
	.cpu_cool_dev=NULL,
};

static  struct  amlogic_thermal_platform_data *amlogic_get_driver_data(
			struct platform_device *pdev)
{
	struct amlogic_thermal_platform_data *pdata=&Pdata;
	return pdata;
}
*/
int get_desend(void)
{
	int i;
	unsigned int freq = CPUFREQ_ENTRY_INVALID;
	int descend = -1;
	struct cpufreq_frequency_table *table =
					cpufreq_frequency_get_table(0);

	if (!table)
		return -EINVAL;

	for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
		/* ignore invalid entries */
		if (table[i].frequency == CPUFREQ_ENTRY_INVALID)
			continue;

		/* ignore duplicate entry */
		if (freq == table[i].frequency)
			continue;

		/* get the frequency order */
		if (freq != CPUFREQ_ENTRY_INVALID && descend == -1){
			descend = !!(freq > table[i].frequency);
			break;
		}

		freq = table[i].frequency;
	}
	return descend;
}
int fix_to_freq(int freqold,int descend)
{
	int i;
	unsigned int freq = CPUFREQ_ENTRY_INVALID;
	struct cpufreq_frequency_table *table =
					cpufreq_frequency_get_table(0);

	if (!table)
		return -EINVAL;

	for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
		/* ignore invalid entry */
		if (table[i].frequency == CPUFREQ_ENTRY_INVALID)
			continue;

		/* ignore duplicate entry */
		if (freq == table[i].frequency)
			continue;
		freq = table[i].frequency;
		if(descend){
			if(freqold>=table[i+1].frequency && freqold<=table[i].frequency)
				return table[i+1].frequency;
		}
		else{
			if(freqold>=table[i].frequency && freqold<=table[i+1].frequency)
				return table[i].frequency;
		}
	}
	return -EINVAL;
}
static struct amlogic_thermal_platform_data * amlogic_thermal_init_from_dts(struct platform_device *pdev)
{
	int i=0,ret=-1,val=0,cells,descend;
	struct property *prop;
	struct temp_level *tmp_level=NULL;
	struct amlogic_thermal_platform_data *pdata=NULL;
	if(!of_property_read_u32(pdev->dev.of_node, "trip_point", &val)){
		//INIT FROM DTS
		pdata=kzalloc(sizeof(*pdata),GFP_KERNEL);
		if(!pdata){
			goto err;
		}
		memset((void* )pdata,0,sizeof(*pdata));
		ret=of_property_read_u32(pdev->dev.of_node, "#thermal-cells", &val);
		if(ret){
			dev_err(&pdev->dev, "dt probe #thermal-cells failed: %d\n", ret);
			goto err;
		}
		printk("#thermal-cells=%d\n",val);
		cells=val;
		prop = of_find_property(pdev->dev.of_node, "trip_point", &val);
		if (!prop){
			dev_err(&pdev->dev, "read %s length error\n","trip_point");
			goto err;
		}
		pdata->temp_trip_count=val/cells/sizeof(u32);
		printk("pdata->temp_trip_count=%d\n",pdata->temp_trip_count);
		tmp_level=kzalloc(sizeof(*tmp_level)*pdata->temp_trip_count,GFP_KERNEL);
		pdata->tmp_trip=kzalloc(sizeof(struct temp_trip)*pdata->temp_trip_count,GFP_KERNEL);
		if(!tmp_level){
			goto err;
		}
		ret=of_property_read_u32_array(pdev->dev.of_node,"trip_point",(u32 *)tmp_level,val/sizeof(u32));
		if (ret){
			dev_err(&pdev->dev, "read %s data error\n","trip_point");
			goto err;
		}
		descend=get_desend();
		for (i = 0; i < pdata->temp_trip_count; i++) {
			printk("temperature=%d on trip point=%d\n",tmp_level[i].temperature,i);
			pdata->tmp_trip[i].temperature=tmp_level[i].temperature;
			printk("fixing high_freq=%d to ",tmp_level[i].cpu_high_freq);
			tmp_level[i].cpu_high_freq=fix_to_freq(tmp_level[i].cpu_high_freq,descend);
			pdata->tmp_trip[i].cpu_lower_level=cpufreq_cooling_get_level(0,tmp_level[i].cpu_high_freq);
			printk("%d at trip point %d,level=%d\n",tmp_level[i].cpu_high_freq,i,pdata->tmp_trip[i].cpu_lower_level);	
			
			printk("fixing low_freq=%d to ",tmp_level[i].cpu_low_freq);
			tmp_level[i].cpu_low_freq=fix_to_freq(tmp_level[i].cpu_low_freq,descend);
			pdata->tmp_trip[i].cpu_upper_level=cpufreq_cooling_get_level(0,tmp_level[i].cpu_low_freq);
			printk("%d at trip point %d,level=%d\n",tmp_level[i].cpu_low_freq,i,pdata->tmp_trip[i].cpu_upper_level);
			pdata->tmp_trip[i].gpu_lower_freq=tmp_level[i].gpu_low_freq;
			pdata->tmp_trip[i].gpu_upper_freq=tmp_level[i].gpu_high_freq;
			printk("gpu[%d].gpu_high_freq=%d,tmp_level[%d].gpu_high_freq=%d\n",i,tmp_level[i].gpu_high_freq,i,tmp_level[i].gpu_low_freq);

			pdata->tmp_trip[i].cpu_core_num=tmp_level[i].cpu_core_num;
			printk("cpu[%d] core num==%d\n",i,pdata->tmp_trip[i].cpu_core_num);
			pdata->tmp_trip[i].gpu_core_num=tmp_level[i].gpu_core_num;
			printk("gpu[%d] core num==%d\n",i,pdata->tmp_trip[i].gpu_core_num);
		}
		
		ret= of_property_read_u32(pdev->dev.of_node, "idle_interval", &val);
		if (ret){
			dev_err(&pdev->dev, "read %s  error\n","idle_interval");
			goto err;
		}
		pdata->idle_interval=val;
		printk("idle interval=%d\n",pdata->idle_interval);
		ret=of_property_read_string(pdev->dev.of_node,"dev_name",&pdata->name);
		if (ret){
			dev_err(&pdev->dev, "read %s  error\n","dev_name");
			goto err;
		}
		printk("pdata->name:%s\n",pdata->name);
		pdata->mode=THERMAL_DEVICE_ENABLED;
		if(tmp_level)
			kfree(tmp_level);
		return pdata;
	}	
err:
	if(tmp_level)
		kfree(tmp_level);
	if(pdata)
		kfree(pdata);
	pdata= NULL;
	return pdata;
}
static struct amlogic_thermal_platform_data * amlogic_thermal_initialize(struct platform_device *pdev)
{
	struct amlogic_thermal_platform_data *pdata=NULL;
	pdata=amlogic_thermal_init_from_dts(pdev);
	return pdata;
}

static const struct of_device_id amlogic_thermal_match[] = {
	{
		.compatible = "amlogic-thermal",
	},
};
static int amlogic_thermal_probe(struct platform_device *pdev)
{
	int ret;
	struct amlogic_thermal_platform_data *pdata=NULL;
	//pdata = amlogic_get_driver_data(pdev);
#ifdef CONFIG_AML_VIRTUAL_THERMAL
	ret=thermal_firmware_init();
	if(ret<0){
		printk("%s, this chip is not trimmed, use virtual thermal\n", __func__);
		trim_flag = 0;
	}else{
		printk("%s, this chip is trimmed, use thermal\n", __func__);
		trim_flag = 1;
	}
	if(!trim_flag){
		aml_virtaul_thermal_probe(pdev);
		INIT_DELAYED_WORK(&freq_collect_work, collect_freq_work);
		schedule_delayed_work(&freq_collect_work, msecs_to_jiffies(100));
		atomic_set(&freq_update_flag, 0);
	}
#else
	ret=thermal_firmware_init();
	if(ret<0)
		return ret;
#endif
	dev_info(&pdev->dev, "amlogic thermal probe start\n");
	pdata = amlogic_thermal_initialize(pdev);
	if (!pdata) {
		dev_err(&pdev->dev, "Failed to initialize thermal\n");
		goto err;
	}
	mutex_init(&pdata->lock);
	pdev->dev.platform_data=pdata;
	platform_set_drvdata(pdev, pdata);
	ret = amlogic_register_thermal(pdata);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register thermal interface\n");
		goto err;
	}
	dev_info(&pdev->dev, "amlogic thermal probe done\n");
	return 0;
err:
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static int amlogic_thermal_remove(struct platform_device *pdev)
{
	struct amlogic_thermal_platform_data *pdata = platform_get_drvdata(pdev);

#ifdef CONFIG_AML_VIRTUAL_THERMAL
    aml_virtual_thermal_remove();
#endif

	amlogic_unregister_thermal(pdata);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int amlogic_thermal_suspend(struct device *dev)
{
	return 0;
}

static int amlogic_thermal_resume(struct device *dev)
{
	return 0;
}

static SIMPLE_DEV_PM_OPS(amlogic_thermal_pm,
			 amlogic_thermal_suspend, amlogic_thermal_resume);
#define amlogic_thermal_PM	(&amlogic_thermal_pm)
#else
#define amlogic_thermal_PM	NULL
#endif

static struct platform_driver amlogic_thermal_driver = {
	.driver = {
		.name   = "amlogic-thermal",
		.owner  = THIS_MODULE,
		.pm     = amlogic_thermal_PM,
		.of_match_table = of_match_ptr(amlogic_thermal_match),
	},
	.probe = amlogic_thermal_probe,
	.remove	= amlogic_thermal_remove,
};
static int __init amlogic_thermal_driver_init(void) 
{ 
	return platform_driver_register(&(amlogic_thermal_driver)); 
} 
late_initcall(amlogic_thermal_driver_init); 
static void __exit amlogic_thermal_driver_exit(void) 
{ 
	platform_driver_unregister(&(amlogic_thermal_driver) ); 
} 
module_exit(amlogic_thermal_driver_exit);

MODULE_DESCRIPTION("amlogic thermal Driver");
MODULE_AUTHOR("Amlogic SH platform team");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:amlogic-thermal");

