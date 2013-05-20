#if WMT_PLAT_ALPS

#include <linux/thermal.h>
#include <linux/xlog.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include "wmt_tm.h"
#include <mach/mtk_thermal_monitor.h>

#include <linux/timer.h>
#include <linux/pid.h>
/* For using net dev + */
#include <linux/netdevice.h>
/* For using net dev - */

#if  defined(CONFIG_THERMAL) &&  defined(CONFIG_THERMAL_OPEN)

static int wmt_tm_debug_log = 0;
#define wmt_tm_dprintk(fmt, args...)   \
do {                                    \
    if (wmt_tm_debug_log) {                \
        xlog_printk(ANDROID_LOG_DEBUG, "Power/WMT_Thermal", fmt, ##args); \
    }                                   \
} while(0)

#define wmt_tm_printk(fmt, args...)   \
do {                                    \
    xlog_printk(ANDROID_LOG_DEBUG, "Power/WMT_Thermal", fmt, ##args); \
} while(0)

#define wmt_tm_info(fmt, args...)   \
do {                                    \
    xlog_printk(ANDROID_LOG_INFO, "Power/WMT_Thermal", fmt, ##args); \
} while(0)

struct linux_thermal_ctrl_if {
    int kernel_mode;
    int interval;
    struct thermal_zone_device *thz_dev;
    struct thermal_cooling_device *cl_dev;
    struct thermal_cooling_device *cl_pa1_dev;
    struct thermal_cooling_device *cl_pa2_dev;
    struct thermal_cooling_device *cl_pa3_dev;
};

struct wmt_thermal_ctrl_if {
    struct wmt_thermal_ctrl_ops ops;
};

typedef struct wmt_tm {
   struct linux_thermal_ctrl_if linux_if;
   struct wmt_thermal_ctrl_if   wmt_if;
}wmt_tm_t;

struct wmt_stats {
   unsigned long pre_time;
   unsigned long pre_tx_bytes;
};

static struct timer_list wmt_stats_timer;
static struct wmt_stats wmt_stats_info;
static unsigned long pre_time;
static unsigned long tx_throughput;

/*New Wifi throttling Algo+*/
//over_up_time * polling interval > up_duration --> throttling
static unsigned int over_up_time = 0; //polling time
static unsigned int up_duration = 30; //sec
static unsigned int up_denominator = 2;
static unsigned int up_numerator = 1;

//below_low_time * polling interval > low_duration --> throttling
static unsigned int below_low_time = 0; //polling time
static unsigned int low_duration = 10; //sec
static unsigned int low_denominator = 2;
static unsigned int low_numerator = 3;

static unsigned int low_rst_time = 0;
static unsigned int low_rst_max = 3;
/*New Wifi throttling Algo-*/

#define MAX_LEN	256
#define COOLER_THRO_NUM 3
#define COOLER_NUM 10
#define ONE_MBITS_PER_SEC 1000

static unsigned int tm_pid = 0;
static unsigned int tm_input_pid = 0;
static unsigned int tm_wfd_stat = 0;
static struct task_struct g_task;
static struct task_struct *pg_task = &g_task;

/* +Cooler info+ */
static int g_num_trip = COOLER_THRO_NUM + 1;
static char g_bind0[20]="mtktswmt-pa1";
static char g_bind1[20]="mtktswmt-pa2";
static char g_bind2[20]="mtktswmt-pa3";
static char g_bind3[20]="mtktswmt-sysrst";
static char g_bind4[20]={0};
static char g_bind5[20]={0};
static char g_bind6[20]={0};
static char g_bind7[20]={0};
static char g_bind8[20]={0};
static char g_bind9[20]={0};


static unsigned int cl_dev_state =0;
static unsigned int cl_pa1_dev_state =0;
static unsigned int cl_pa2_dev_state =0;
/*static unsigned int cl_pa3_dev_state =0;*/
static unsigned int g_trip_temp[COOLER_NUM] = {85000,85000,85000,85000,0,0,0,0,0,0};
/*static int g_thro[COOLER_THRO_NUM] = {10 * ONE_MBITS_PER_SEC, 5 * ONE_MBITS_PER_SEC, 1 * ONE_MBITS_PER_SEC};*/
static int g_thermal_trip[COOLER_NUM] = {0,0,0,0,0,0,0,0,0,0};
/* -Cooler info- */

wmt_tm_t g_wmt_tm;
wmt_tm_t *pg_wmt_tm = &g_wmt_tm;

static int wmt_thz_bind(struct thermal_zone_device *,
		     struct thermal_cooling_device *);
static int wmt_thz_unbind(struct thermal_zone_device *,
		     struct thermal_cooling_device *);
static int wmt_thz_get_temp(struct thermal_zone_device *,
             unsigned long *);
static int wmt_thz_get_mode(struct thermal_zone_device *,
			 enum thermal_device_mode *);
static int wmt_thz_set_mode(struct thermal_zone_device *,
		     enum thermal_device_mode);
static int wmt_thz_get_trip_type(struct thermal_zone_device *, int,
		     enum thermal_trip_type *);
static int wmt_thz_get_trip_temp(struct thermal_zone_device *, int,
			 unsigned long *);
static int wmt_thz_get_crit_temp(struct thermal_zone_device *,
             unsigned long *);
static int wmt_cl_get_max_state(struct thermal_cooling_device *,
             unsigned long *);
static int wmt_cl_get_cur_state(struct thermal_cooling_device *,
             unsigned long *);
static int wmt_cl_set_cur_state(struct thermal_cooling_device *,
             unsigned long);

static int wmt_cl_pa1_get_max_state(struct thermal_cooling_device *,
             unsigned long *);
static int wmt_cl_pa1_get_cur_state(struct thermal_cooling_device *,
             unsigned long *);
static int wmt_cl_pa1_set_cur_state(struct thermal_cooling_device *,
             unsigned long);

static int wmt_cl_pa2_get_max_state(struct thermal_cooling_device *,
             unsigned long *);
static int wmt_cl_pa2_get_cur_state(struct thermal_cooling_device *,
             unsigned long *);
static int wmt_cl_pa2_set_cur_state(struct thermal_cooling_device *,
             unsigned long);

#ifdef NEVER
static int wmt_cl_pa3_get_max_state(struct thermal_cooling_device *,
             unsigned long *);
static int wmt_cl_pa3_get_cur_state(struct thermal_cooling_device *,
             unsigned long *);
static int wmt_cl_pa3_set_cur_state(struct thermal_cooling_device *,
             unsigned long);
#endif /* NEVER */

static struct thermal_zone_device_ops wmt_thz_dev_ops = {
	.bind = wmt_thz_bind,
	.unbind = wmt_thz_unbind,
	.get_temp = wmt_thz_get_temp,
	.get_mode = wmt_thz_get_mode,
	.set_mode = wmt_thz_set_mode,
	.get_trip_type = wmt_thz_get_trip_type,
	.get_trip_temp = wmt_thz_get_trip_temp,
	.get_crit_temp = wmt_thz_get_crit_temp,
};

static struct thermal_cooling_device_ops mtktspa_cooling_sysrst_ops = {
	.get_max_state = wmt_cl_get_max_state,
	.get_cur_state = wmt_cl_get_cur_state,
	.set_cur_state = wmt_cl_set_cur_state,
};

static struct thermal_cooling_device_ops mtktspa_cooling_pa1_ops = {
	.get_max_state = wmt_cl_pa1_get_max_state,
	.get_cur_state = wmt_cl_pa1_get_cur_state,
	.set_cur_state = wmt_cl_pa1_set_cur_state,
};

static struct thermal_cooling_device_ops mtktspa_cooling_pa2_ops = {
	.get_max_state = wmt_cl_pa2_get_max_state,
	.get_cur_state = wmt_cl_pa2_get_cur_state,
	.set_cur_state = wmt_cl_pa2_set_cur_state,
};

#ifdef NEVER
static struct thermal_cooling_device_ops mtktspa_cooling_pa3_ops = {
	.get_max_state = wmt_cl_pa3_get_max_state,
	.get_cur_state = wmt_cl_pa3_get_cur_state,
	.set_cur_state = wmt_cl_pa3_set_cur_state,
};
#endif /* NEVER */

static unsigned long get_tx_bytes(void)
{
	struct net_device *dev;
	struct net *net;
	unsigned long tx_bytes = 0;

	read_lock(&dev_base_lock);
	for_each_net(net) {
		for_each_netdev(net, dev) {
			if(!strncmp(dev->name, "wlan", 4) || !strncmp(dev->name, "ap", 2) || !strncmp(dev->name, "p2p", 3)) {
				struct rtnl_link_stats64 temp;
				const struct rtnl_link_stats64 *stats = dev_get_stats(dev, &temp);
				tx_bytes = tx_bytes + stats->tx_bytes;
			}
		}
	}
	read_unlock(&dev_base_lock);
	return tx_bytes;
}

static int wmt_cal_stats(unsigned long data)
{
	struct wmt_stats *stats_info = (struct wmt_stats*) data;
	struct timeval cur_time;

	wmt_tm_dprintk("[%s] pre_time=%lu, pre_data=%lu\n", __func__, pre_time, stats_info->pre_tx_bytes);

	do_gettimeofday(&cur_time);

	if (pre_time != 0 && cur_time.tv_sec > pre_time) {
		unsigned long tx_bytes = get_tx_bytes();
		if (tx_bytes > stats_info->pre_tx_bytes) {

			tx_throughput = ((tx_bytes - stats_info->pre_tx_bytes) / (cur_time.tv_sec - pre_time)) >> 7;

			wmt_tm_dprintk("[%s] cur_time=%lu, cur_data=%lu, tx_throughput=%luKb/s\n", __func__, cur_time.tv_sec, tx_bytes, tx_throughput );

			stats_info->pre_tx_bytes = tx_bytes;
		} else if (tx_bytes < stats_info->pre_tx_bytes) {
			/* Overflow */
			tx_throughput = ((0xffffffff - stats_info->pre_tx_bytes + tx_bytes) / (cur_time.tv_sec - pre_time)) >> 7;;
			stats_info->pre_tx_bytes = tx_bytes;
			wmt_tm_dprintk("[%s] cur_tx(%lu) < pre_tx\n", __func__, tx_bytes);
		} else {
			/* No traffic */
			tx_throughput = 0;
			wmt_tm_dprintk("[%s] cur_tx(%lu) = pre_tx\n", __func__, tx_bytes);
		}
	} else {
		/* Overflow possible ??*/
		tx_throughput = 0;
		wmt_tm_printk("[%s] cur_time(%lu) < pre_time\n", __func__, cur_time.tv_sec);
	}

	pre_time = cur_time.tv_sec;
	wmt_tm_dprintk("[%s] pre_time=%lu, tv_sec=%lu\n",__func__, pre_time, cur_time.tv_sec);

	wmt_stats_timer.expires = jiffies + 1 * HZ;
	add_timer(&wmt_stats_timer);
	return 0;
}

static int wmt_thz_bind(struct thermal_zone_device *thz_dev,
		     struct thermal_cooling_device *cool_dev)
{
    struct linux_thermal_ctrl_if *p_linux_if = 0;
    int    table_val = 0;

    wmt_tm_dprintk("[%s]\n", __func__);

    if (pg_wmt_tm) {
       p_linux_if = &pg_wmt_tm->linux_if;
    } else {
       return -EINVAL;
    }
    #ifdef NEVER
    /* cooling devices */
    if (cool_dev != p_linux_if->cl_dev)
    {
        return 0;
    }
    #endif

    if(!strcmp(cool_dev->type, g_bind0)) {
        table_val = 0;
        wmt_tm_printk("[%s] %s\n", __func__, cool_dev->type);
    } else if(!strcmp(cool_dev->type, g_bind1)) {
        table_val = 1;
        wmt_tm_printk("[%s] %s\n", __func__, cool_dev->type);
    } else if(!strcmp(cool_dev->type, g_bind2)) {
        table_val = 2;
        wmt_tm_printk("[%s]] %s\n", __func__, cool_dev->type);
    } else if(!strcmp(cool_dev->type, g_bind3)) {
        table_val = 3;
        wmt_tm_printk("[%s]] %s\n", __func__, cool_dev->type);
    } else
        return 0;

    if (mtk_thermal_zone_bind_cooling_device(thz_dev, table_val, cool_dev)) {
        wmt_tm_info("[%s] binding fail\n", __func__);
        return -EINVAL;
    } else {
        wmt_tm_printk("[%s]] binding OK\n", __func__);
    }

    return 0;

}
static int wmt_thz_unbind(struct thermal_zone_device *thz_dev,
		     struct thermal_cooling_device *cool_dev)
{
    struct linux_thermal_ctrl_if *p_linux_if = 0;
    int    table_val = 0;

    wmt_tm_dprintk("[wmt_thz_unbind] \n");

    if (pg_wmt_tm) {
       p_linux_if = &pg_wmt_tm->linux_if;
    } else {
       return -EINVAL;
    }
#if 0
    /* cooling devices */
    if (cool_dev == p_linux_if->cl_dev)
    {
        table_val= 0;
    }
    else
    {
        wmt_tm_dprintk("[wmt_thz_unbind] unbind device fail..!\n");
        return -EINVAL;
    }
#endif

    if(!strcmp(cool_dev->type, g_bind0)) {
        table_val = 0;
        wmt_tm_printk("[wmt_thz_unbind] %s\n", cool_dev->type);
    } else if(!strcmp(cool_dev->type, g_bind1)) {
        table_val = 1;
        wmt_tm_printk("[wmt_thz_unbind] %s\n", cool_dev->type);
    } else if(!strcmp(cool_dev->type, g_bind2)) {
        table_val = 2;
        wmt_tm_printk("[wmt_thz_unbind] %s\n", cool_dev->type);
    } else if(!strcmp(cool_dev->type, g_bind3)) {
        table_val = 3;
        wmt_tm_printk("[wmt_thz_unbind] %s\n", cool_dev->type);
    } else
        return 0;

    if (thermal_zone_unbind_cooling_device(thz_dev, table_val, cool_dev)) {
	    wmt_tm_info("[wmt_thz_unbind] error unbinding cooling dev\n");
		return -EINVAL;
	} else {
	    wmt_tm_printk("[wmt_thz_unbind] unbinding OK\n");
    }

	return 0;
}

static int wmt_thz_get_temp(struct thermal_zone_device *thz_dev,
             unsigned long *pv)
{
    struct wmt_thermal_ctrl_ops *p_des;
    int temp = 0;

    *pv = 0;
    if(pg_wmt_tm) {
        p_des = &pg_wmt_tm->wmt_if.ops;
        temp = p_des->query_temp();

        //temp = ((temp & 0x80) == 0x0)?temp:(-1)*temp ;
         temp = ((temp & 0x80) == 0x0)?temp:(-1)*(temp & 0x7f);
        *pv =  temp*1000;

        wmt_tm_dprintk("[wmt_thz_get_temp] temp = %d\n", temp);

        if(temp > 100 || temp < 5)
            wmt_tm_info("[wmt_thz_get_temp] temp = %d\n", temp);
    }

    return 0;
}

static int wmt_thz_get_mode(struct thermal_zone_device *thz_dev,
			 enum thermal_device_mode *mode)
{
    struct linux_thermal_ctrl_if *p_linux_if = 0;
//    int    kernel_mode = 0;

    wmt_tm_dprintk("[%s]\n", __func__);

    if (pg_wmt_tm) {
       p_linux_if = &pg_wmt_tm->linux_if;
    } else {
       wmt_tm_dprintk("[%s] fail! \n", __func__);
       return -EINVAL;
    }

    wmt_tm_dprintk("[%s] %d\n", __func__, p_linux_if->kernel_mode);

    *mode = (p_linux_if->kernel_mode) ? THERMAL_DEVICE_ENABLED
			     : THERMAL_DEVICE_DISABLED;

	return 0;
}

static int wmt_thz_set_mode(struct thermal_zone_device *thz_dev,
		     enum thermal_device_mode mode)
{
    struct linux_thermal_ctrl_if *p_linux_if = 0;

    wmt_tm_dprintk("[%s]\n", __func__);


    if(pg_wmt_tm) {
       p_linux_if = &pg_wmt_tm->linux_if;
    } else {
       wmt_tm_dprintk("[%s] fail! \n", __func__);
       return -EINVAL;
    }

    wmt_tm_dprintk("[%s] %d\n", __func__, mode);

    p_linux_if->kernel_mode = mode;

	return 0;

}

static int wmt_thz_get_trip_type(struct thermal_zone_device *thz_dev, int trip,
		     enum thermal_trip_type *type)
{
    wmt_tm_dprintk("[mtktspa_get_trip_type] %d\n", trip);
    *type = g_thermal_trip[trip];
    return 0;
}

static int wmt_thz_get_trip_temp(struct thermal_zone_device *thz_dev, int trip,
			 unsigned long *pv)
{
    wmt_tm_dprintk("[mtktspa_get_trip_temp] %d\n", trip);
    *pv = g_trip_temp[trip];
    return 0;
}

static int wmt_thz_get_crit_temp(struct thermal_zone_device *thz_dev,
             unsigned long *pv)
{
    wmt_tm_dprintk("[%s]\n", __func__);
#define WMT_TM_TEMP_CRIT 85000 /* 85.000 degree Celsius */
    *pv = WMT_TM_TEMP_CRIT;

    return 0;
}

/* +mtktspa_cooling_sysrst_ops+ */
static int wmt_cl_get_max_state(struct thermal_cooling_device *cool_dev,
             unsigned long *pv)
{
    *pv = 1;
    wmt_tm_dprintk("[%s] %d\n", __func__, *pv);
	return 0;
}

static int wmt_cl_get_cur_state(struct thermal_cooling_device *cool_dev,
             unsigned long *pv)
{
    *pv = cl_dev_state;
    wmt_tm_dprintk("[%s] %d\n", __func__, *pv);
	return 0;
}

static int wmt_cl_set_cur_state(struct thermal_cooling_device *cool_dev,
             unsigned long v)
{
    wmt_tm_dprintk("[%s] %d\n", __func__, v);
    cl_dev_state = v;

    if (cl_dev_state == 1) {
        //the temperature is over than the critical, system reboot.
        BUG();
    }

    return 0;
}
/* -mtktspa_cooling_sysrst_ops- */

static int wmt_send_signal(int level)
{
	int ret = 0;
	int thro = level;

	if (tm_input_pid == 0) {
		wmt_tm_dprintk("[%s] pid is empty\n", __func__);
		ret = -1;
	}

	wmt_tm_printk("[%s] pid is %d, %d, %d\n", __func__, tm_pid, tm_input_pid, thro);

	if (ret == 0 && tm_input_pid != tm_pid) {
		tm_pid = tm_input_pid;
		pg_task = get_pid_task(find_vpid(tm_pid), PIDTYPE_PID);
	}

	if (ret == 0 && pg_task) {
		siginfo_t info;
		info.si_signo = SIGIO;
		info.si_errno = 0;
		info.si_code = thro;
		info.si_addr = NULL;
		ret = send_sig_info(SIGIO, &info, pg_task);
	}

	if (ret != 0) wmt_tm_info("[%s] ret=%d\n", __func__, ret);

	return ret;
}

#define UNK_STAT -1
#define LOW_STAT 0
#define MID_STAT 1
#define HIGH_STAT 2
#define WFD_STAT 3

static inline unsigned long thro(unsigned long a, unsigned int b, unsigned int c) {

	unsigned long tmp;

	tmp = (a << 10) * b / c;

	return tmp >> 10;
}

static int wmt_judge_throttling(int index, int is_on, int interval)
{
	/*
	 *     throttling_stat
	 *        2 ( pa1=1,pa2=1 )
	 * UPPER ----
	 *        1 ( pa1=1,pa2=0 )
	 * LOWER ----
	 *        0 ( pa1=0,pa2=0 )
	 */
	static unsigned int throttling_pre_stat = 0;
	static int mail_box[2] = {-1,-1};

	static bool is_reset = false;

	unsigned long cur_thro = tx_throughput;
	static unsigned long thro_constraint = 99 * 1000;

	int cur_wifi_stat = 0;

	wmt_tm_dprintk("[%s]+ [0]=%d, [1]=%d || [%d] is %s\n", __func__, mail_box[0], mail_box[1],
											index, (is_on==1?"ON":"OFF"));
	mail_box[index] = is_on;

	if (mail_box[0] >= 0 && mail_box[1] >= 0) {
		cur_wifi_stat = mail_box[0] + mail_box[1];

		/*
		 * If Wifi-display is on, go to WFD_STAT state, and reset the throttling.
		 */
		if (tm_wfd_stat == 2)
			cur_wifi_stat = WFD_STAT;

		switch(cur_wifi_stat) {
			case WFD_STAT:
				if (throttling_pre_stat != WFD_STAT) {
					/*
					 * Enter Wifi-Display status, reset all throttling. Dont affect the performance of Wifi-Display.
					 */
					wmt_send_signal(-1);
					below_low_time = 0;
					over_up_time = 0;
					throttling_pre_stat = WFD_STAT;
					wmt_tm_printk("WFD is on, reset everything!");
				}
			break;

			case HIGH_STAT:
				if (throttling_pre_stat < HIGH_STAT || throttling_pre_stat == WFD_STAT) {
					if (cur_thro > 0) /*Wifi is working!!*/
						thro_constraint = thro(cur_thro, up_numerator, up_denominator);
					else /*At this moment, current throughput is none. Use the previous constraint.*/
						thro_constraint = thro(thro_constraint, up_numerator, up_denominator);

					wmt_tm_printk("LOW/MID-->HIGH:%lu <- (%d / %d) %lu", thro_constraint, up_numerator, up_denominator, cur_thro);

					wmt_send_signal( thro_constraint/1000 );
					throttling_pre_stat = HIGH_STAT;
					over_up_time = 0;
				} else if (throttling_pre_stat == HIGH_STAT) {
					over_up_time++;
					if ( (over_up_time * interval) >= up_duration) {
						if (cur_thro < thro_constraint) /*real throughput may have huge variant*/
							thro_constraint = thro(cur_thro, up_numerator, up_denominator);
						else /* current throughput is large than constraint. WHAT!!!*/
							thro_constraint = thro(thro_constraint, up_numerator, up_denominator);

						wmt_tm_printk("HIGH-->HIGH:%lu <- (%d / %d) %lu", thro_constraint, up_numerator, up_denominator, cur_thro);

						wmt_send_signal( thro_constraint/1000 );
						over_up_time = 0;
					}
				} else {
					wmt_tm_info("[%s] Error state1!!\n", __func__, throttling_pre_stat);
				}
				wmt_tm_printk("case2 time=%d\n", over_up_time);
			break;

			case MID_STAT:
				if (throttling_pre_stat == LOW_STAT) {
					below_low_time = 0;
					throttling_pre_stat = MID_STAT;
					wmt_tm_printk("[%s] Go up!!\n", __func__);
				} else if (throttling_pre_stat == HIGH_STAT) {
					over_up_time = 0;
					throttling_pre_stat = MID_STAT;
					wmt_tm_printk("[%s] Go down!!\n", __func__);
				} else {
					throttling_pre_stat = MID_STAT;
					wmt_tm_dprintk("[%s] pre_stat=%d!!\n", __func__, throttling_pre_stat);
				}
			break;

			case LOW_STAT:
				if (throttling_pre_stat == WFD_STAT) {
					throttling_pre_stat = LOW_STAT;
					wmt_tm_dprintk("[%s] pre_stat=%d!!\n", __func__, throttling_pre_stat);
				} else if (throttling_pre_stat > LOW_STAT) {
					if (cur_thro < 5000 && cur_thro > 0) {
						thro_constraint = cur_thro * 3;
					} else if (cur_thro >= 5000) {
						thro_constraint = thro(cur_thro, low_numerator, low_denominator);
					} else {
						thro_constraint = thro(thro_constraint, low_numerator, low_denominator);
					}

					wmt_tm_printk("MID/HIGH-->LOW:%lu <- (%d / %d) %lu", thro_constraint, low_numerator, low_denominator, cur_thro);
					wmt_send_signal( thro_constraint/1000 );
					throttling_pre_stat = LOW_STAT;
					below_low_time = 0;
					low_rst_time = 0;
					is_reset = false;
				} else if (throttling_pre_stat == LOW_STAT) {
					below_low_time++;
					if ( (below_low_time*interval) >= low_duration) {
						if (low_rst_time >= low_rst_max && !is_reset) {
							wmt_tm_printk("over rst time=%d", low_rst_time);

							wmt_send_signal(-1); //reset
							low_rst_time = low_rst_max;
							is_reset = true;
						} else if(!is_reset) {
							if (cur_thro < 5000 && cur_thro > 0) {
								thro_constraint = cur_thro * 3;
							} else if (cur_thro >= 5000) {
								thro_constraint = thro(cur_thro, low_numerator, low_denominator);
								low_rst_time++;
							} else {
								thro_constraint = thro(thro_constraint, low_numerator, low_denominator);
								low_rst_time++;
							}

							wmt_tm_printk("LOW-->LOW:%lu <-(%d / %d) %lu", thro_constraint, low_numerator, low_denominator, cur_thro);

							wmt_send_signal( thro_constraint/1000 );
							below_low_time = 0;
						} else {
							wmt_tm_dprintk("Have reset, no control!!");
						}
					}
				} else {
					wmt_tm_info("[%s] Error state3 %d!!\n", __func__, throttling_pre_stat);
				}
				wmt_tm_dprintk("case0 time=%d, rst=%d %d\n", below_low_time, low_rst_time, is_reset);
			break;

			default:
				wmt_tm_info("[%s] Error cur_wifi_stat=%d!!\n", __func__, cur_wifi_stat);
			break;
		}

		mail_box[0] = UNK_STAT;
		mail_box[1] = UNK_STAT;
	} else {
		wmt_tm_dprintk("[%s] dont get all info!!\n", __func__);
	}
	return 0;
}

/* +mtktspa_cooling_pa1_ops+ */
static int wmt_cl_pa1_get_max_state(struct thermal_cooling_device *cool_dev,
             unsigned long *pv)
{
    *pv = 1;
    wmt_tm_dprintk("[%s] %d\n", __func__, *pv);
    return 0;
}

static int wmt_cl_pa1_get_cur_state(struct thermal_cooling_device *cool_dev,
             unsigned long *pv)
{
    *pv = cl_pa1_dev_state;
    wmt_tm_dprintk("[%s] %d\n", __func__, *pv);
    return 0;
}

static int wmt_cl_pa1_set_cur_state(struct thermal_cooling_device *cool_dev,
             unsigned long v)
{
	struct linux_thermal_ctrl_if *p_linux_if = 0;
	int ret = 0;

	wmt_tm_dprintk("[%s] %d\n", __func__, v);

	if (pg_wmt_tm) {
		p_linux_if = &pg_wmt_tm->linux_if;
	} else {
		ret = -1;
	}

	cl_pa1_dev_state = (unsigned int)v;

	if (cl_pa1_dev_state == 1) {
		ret = wmt_judge_throttling(0, 1, p_linux_if->interval/1000);
	} else {
		ret = wmt_judge_throttling(0, 0, p_linux_if->interval/1000);
	}
	if (ret != 0) wmt_tm_info("[%s] ret=%d\n", __func__, ret);
    return ret;
}
/* -mtktspa_cooling_pa1_ops- */

/* +mtktspa_cooling_pa2_ops+ */
static int wmt_cl_pa2_get_max_state(struct thermal_cooling_device *cool_dev,
             unsigned long *pv)
{
    *pv = 1;
    wmt_tm_dprintk("[%s] %d\n", __func__, *pv);
    return 0;
}

static int wmt_cl_pa2_get_cur_state(struct thermal_cooling_device *cool_dev,
             unsigned long *pv)
{
    *pv = cl_pa2_dev_state;
    wmt_tm_dprintk("[%s] %d\n", __func__, *pv);
    return 0;
}

static int wmt_cl_pa2_set_cur_state(struct thermal_cooling_device *cool_dev,
             unsigned long v)
{
	struct linux_thermal_ctrl_if *p_linux_if = 0;
	int ret = 0;

	wmt_tm_dprintk("[%s] %d\n", __func__, v);

	if (pg_wmt_tm) {
		p_linux_if = &pg_wmt_tm->linux_if;
	} else {
		ret = -1;
	}

	cl_pa2_dev_state = (unsigned int)v;

	if (cl_pa2_dev_state == 1) {
		ret = wmt_judge_throttling(1, 1, p_linux_if->interval/1000);
	} else {
		ret = wmt_judge_throttling(1, 0, p_linux_if->interval/1000);
	}
	if (ret != 0) wmt_tm_info("[%s] ret=%d\n", __func__, ret);
	return ret;
}
/* -mtktspa_cooling_pa2_ops- */

#ifdef NEVER
/* +mtktspa_cooling_pa3_ops+ */
static int wmt_cl_pa3_get_max_state(struct thermal_cooling_device *cool_dev,
             unsigned long *pv)
{
    *pv = 1;
    wmt_tm_dprintk("[%s] %d\n", __func__, *pv);
	return 0;
}

static int wmt_cl_pa3_get_cur_state(struct thermal_cooling_device *cool_dev,
             unsigned long *pv)
{
    *pv = cl_pa3_dev_state;
    wmt_tm_dprintk("[%s] %d\n", __func__, *pv);
	return 0;
}

static int wmt_cl_pa3_set_cur_state(struct thermal_cooling_device *cool_dev,
             unsigned long v)
{
	struct linux_thermal_ctrl_if *p_linux_if = 0;
	int ret = 0;

	wmt_tm_dprintk("[%s] %d\n", __func__, v);

	if (pg_wmt_tm) {
		p_linux_if = &pg_wmt_tm->linux_if;
	} else {
		ret = -1;
	}

	cl_pa3_dev_state = (unsigned int)v;

	if (cl_pa3_dev_state == 1) {
		//ret = wmt_arbitrate_thro(2,3);
	} else {
		//ret = wmt_arbitrate_thro(2,0);
 	}
	if (ret != 0) wmt_tm_printk("[%s] ret=%d\n", __func__, ret);
	return ret;
}
/* -mtktspa_cooling_pa3_ops- */
#endif /* NEVER */

int wmt_wifi_tx_thro_read( char *buf, char **start, off_t offset, int count, int *eof, void *data )
{
	count = sprintf(buf, "%lu\n", tx_throughput);

	wmt_tm_dprintk("[%s] tx=%lu\n", __func__, tx_throughput);

	return count;
}

/*New Wifi throttling Algo+*/
ssize_t wmt_wifi_algo_write( struct file *filp, const char __user *buf, unsigned long len, void *data )
{
	char desc[MAX_LEN] = {0};

	unsigned int tmp_up_dur = 30;
	unsigned int tmp_up_den = 2;
	unsigned int tmp_up_num = 1;

	unsigned int tmp_low_dur = 3;
	unsigned int tmp_low_den = 2;
	unsigned int tmp_low_num = 3;

	unsigned int tmp_low_rst_max = 3;

	unsigned int tmp_log = 0;

	len = (len < (sizeof(desc) - 1)) ? len : (sizeof(desc) - 1);

	/* write data to the buffer */
	if (copy_from_user(desc, buf, len)) {
		return -EFAULT;
	}

	if (sscanf(desc, "%d %d/%d, %d %d/%d, %d", &tmp_up_dur, &tmp_up_num, &tmp_up_den, &tmp_low_dur, \
								&tmp_low_num, &tmp_low_den, &tmp_low_rst_max) == 7) {

		up_duration = tmp_up_dur;
		up_denominator = tmp_up_den;
		up_numerator = tmp_up_num;

		low_duration = tmp_low_dur;
		low_denominator = tmp_low_den;
		low_numerator = tmp_low_num;

		low_rst_max = tmp_low_rst_max;

		over_up_time = 0;
		below_low_time = 0;
		low_rst_time = 0;

		wmt_tm_printk("[%s] %s [up]%d %d/%d, [low]%d %d/%d, rst=%d\n", __func__, desc, up_duration, \
			up_numerator, up_denominator, low_duration, low_numerator, low_denominator, low_rst_max);

		return len;
	} else if (sscanf(desc, "log=%d", &tmp_log) == 1) {
		if (tmp_log == 1)
			wmt_tm_debug_log = 1;
		else
			wmt_tm_debug_log = 0;

		return len;
	} else {
		wmt_tm_printk("[%s] bad argument = %s\n", __func__, desc);
	}
    return -EINVAL;
}

int wmt_wifi_algo_read( char *buf, char **start, off_t offset, int count, int *eof, void *data )
{
	int ret;
	char tmp[MAX_LEN] = {0};

	sprintf(tmp, "[up]\t%3d(sec)\t%2d/%2d\n[low]\t%3d(sec)\t%2d/%2d\nrst=%2d\n", up_duration, up_numerator, up_denominator, \
								low_duration, low_numerator, low_denominator, low_rst_max);
	ret = strlen(tmp);

	memcpy(buf, tmp, ret*sizeof(char));

	wmt_tm_printk("[%s] [up]%d %d/%d, [low]%d %d/%d, rst=%d\n", __func__, up_duration, \
		up_numerator, up_denominator, low_duration, low_numerator, low_denominator, low_rst_max);

	return ret;
}
/*New Wifi throttling Algo-*/

ssize_t wmt_tm_wfd_write( struct file *filp, const char __user *buf, unsigned long len, void *data )
{
	int ret = 0;
	char tmp[MAX_LEN] = {0};

	/* write data to the buffer */
	if (copy_from_user(tmp, buf, len)) {
		return -EFAULT;
    }

	ret = sscanf(tmp, "%d", &tm_wfd_stat);

	wmt_tm_printk("[%s] %s = %d, len=%d, ret=%d\n", __func__, tmp, tm_wfd_stat, len, ret);

		return len;
}

int wmt_tm_wfd_read( char *buf, char **start, off_t offset , int count, int *eof, void *data )
{
	int len;
	int ret = 0;
	char tmp[MAX_LEN] = {0};

	ret = sprintf(tmp, "%d", tm_wfd_stat);
	len = strlen(tmp);

	memcpy(buf, tmp, ret*sizeof(char));

	wmt_tm_printk("[%s] %s = %d, len=%d, ret=%d\n", __func__, tmp, tm_wfd_stat, len, ret);

	return ret;
}

ssize_t wmt_tm_pid_write( struct file *filp, const char __user *buf, unsigned long len, void *data )
{
	int ret = 0;
	char tmp[MAX_LEN] = {0};

	/* write data to the buffer */
	if ( copy_from_user(tmp, buf, len) ) {
		return -EFAULT;
	}

	ret = kstrtouint(tmp, 10, &tm_input_pid);
	if (ret)
		WARN_ON(1);

	wmt_tm_printk("[%s] %s = %d\n", __func__, tmp, tm_input_pid);

	return len;
}

int wmt_tm_pid_read( char *buf, char **start, off_t offset , int count, int *eof, void *data )
{
	int ret;
	char tmp[MAX_LEN] = {0};

	sprintf(tmp, "%d", tm_input_pid);
	ret = strlen(tmp);

	memcpy(buf, tmp, ret*sizeof(char));

	wmt_tm_printk("[%s] %s = %d\n", __func__, buf, tm_input_pid);

	return ret;
}

#define check_str(x) (x[0]=='\0'?"none\t":x)

static int wmt_tm_read(char *buf, char **start, off_t off, int count, int *eof, void *data)
{
    int len = 0;
    char *p = buf;
    struct linux_thermal_ctrl_if *p_linux_if = 0;

    wmt_tm_printk("[%s]\n", __func__);

    //sanity
    if(pg_wmt_tm) {
       p_linux_if = &pg_wmt_tm->linux_if;
    } else {
       wmt_tm_info("[wmt_tm_read] fail! \n");
       return -EINVAL;
    }

    p += sprintf(p, "[wmt_tm_read]"\
                    "\n \tcooler\t\ttrip_temp\ttrip_type" \
                    "\n [0] %s\t%d\t\t%d" \
                    "\n [1] %s\t%d\t\t%d" \
                    "\n [2] %s\t%d\t\t%d" \
                    "\n [3] %s\t%d\t\t%d" \
                    "\n [4] %s\t%d\t\t%d" \
                    "\n [5] %s\t%d\t\t%d" \
                    "\n [6] %s\t%d\t\t%d" \
                    "\n [7] %s\t%d\t\t%d" \
                    "\n [8] %s\t%d\t\t%d" \
                    "\n [9] %s\t%d\t\t%d" \
                    "\ntime_ms=%d\n", \
                    check_str(g_bind0),g_trip_temp[0],g_thermal_trip[0], check_str(g_bind1),g_trip_temp[1],g_thermal_trip[1],\
                    check_str(g_bind2),g_trip_temp[2],g_thermal_trip[2], check_str(g_bind3),g_trip_temp[3],g_thermal_trip[3],\
                    check_str(g_bind4),g_trip_temp[4],g_thermal_trip[4], check_str(g_bind5),g_trip_temp[5],g_thermal_trip[5],\
                    check_str(g_bind6),g_trip_temp[6],g_thermal_trip[6], check_str(g_bind7),g_trip_temp[7],g_thermal_trip[7],\
                    check_str(g_bind8),g_trip_temp[8],g_thermal_trip[8], check_str(g_bind9),g_trip_temp[9],g_thermal_trip[9],\
                    p_linux_if->interval);

    *start = buf + off;

    len = p - buf;
    if (len > off)
        len -= off;
    else
        len = 0;

    return len < count ? len  : count;
}

static ssize_t wmt_tm_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
    int i = 0;
    int len=0,time_msec=0;
    int trip_temp[COOLER_NUM] = {0};
    int thermal_trip[COOLER_NUM] = {0};

    char desc[512];
    char bind0[20],bind1[20],bind2[20],bind3[20],bind4[20];
    char bind5[20],bind6[20],bind7[20],bind8[20],bind9[20];

    struct linux_thermal_ctrl_if *p_linux_if = 0;

    wmt_tm_printk("[%s]\n", __func__);

    //sanity
    if(pg_wmt_tm) {
       p_linux_if = &pg_wmt_tm->linux_if;
    } else {
       wmt_tm_info("[wmt_thz_write] fail! \n");
       return -EINVAL;
    }

    len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);

    if (copy_from_user(desc, buffer, len)) {
        return 0;
    }

    desc[len] = '\0';

    if (sscanf(desc, "%d %d %d %s %d %d %s %d %d %s %d %d %s %d %d %s %d %d %s %d %d %s %d %d %s %d %d %s %d %d %s %d",
                      &g_num_trip, &trip_temp[0], &thermal_trip[0], bind0,
                      &trip_temp[1], &thermal_trip[1], bind1,
                      &trip_temp[2], &thermal_trip[2], bind2,
                      &trip_temp[3], &thermal_trip[3], bind3,
                      &trip_temp[4], &thermal_trip[4], bind4,
                      &trip_temp[5], &thermal_trip[5], bind5,
                      &trip_temp[6], &thermal_trip[6], bind6,
                      &trip_temp[7], &thermal_trip[7], bind7,
                      &trip_temp[8], &thermal_trip[8], bind8,
                      &trip_temp[9], &thermal_trip[9], bind9,
                      &time_msec) == 32)
    {
        // unregister
        if (p_linux_if->thz_dev) {
    		mtk_thermal_zone_device_unregister(p_linux_if->thz_dev);
    		p_linux_if->thz_dev = NULL;
    	}

		for ( i = 0; i < g_num_trip; i++) {
			g_thermal_trip[i] = thermal_trip[i];
		}

		g_bind0[0]=g_bind1[0]=g_bind2[0]=g_bind3[0]=g_bind4[0]='\0';
		g_bind5[0]=g_bind6[0]=g_bind7[0]=g_bind8[0]=g_bind9[0]='\0';

		for ( i = 0; i < 20; i++ ) {
			g_bind0[i]=bind0[i];
			g_bind1[i]=bind1[i];
			g_bind2[i]=bind2[i];
			g_bind3[i]=bind3[i];
			g_bind4[i]=bind4[i];
			g_bind5[i]=bind5[i];
			g_bind6[i]=bind6[i];
			g_bind7[i]=bind7[i];
			g_bind8[i]=bind8[i];
			g_bind9[i]=bind9[i];
		}

		for ( i = 0; i < g_num_trip; i++) {
			g_trip_temp[i] = trip_temp[i];
		}

        p_linux_if->interval = time_msec;

        wmt_tm_printk("[wmt_tm_write] g_trip_temp [0]=%d, [1]=%d, [2]=%d, [3]=%d, [4]=%d\n",
                       g_thermal_trip[0], g_thermal_trip[1], g_thermal_trip[2],
                       g_thermal_trip[3], g_thermal_trip[4]);

        wmt_tm_printk("[wmt_tm_write] g_trip_temp [5]=%d, [6]=%d, [7]=%d, [8]=%d, [9]=%d\n",
                       g_thermal_trip[5], g_thermal_trip[6], g_thermal_trip[7],
                       g_thermal_trip[8], g_thermal_trip[9]);

        wmt_tm_printk("[wmt_tm_write] cooldev [0]=%s, [1]=%s, [2]=%s, [3]=%s, [4]=%s,\n",
                       g_bind0, g_bind1, g_bind2, g_bind3, g_bind4);

        wmt_tm_printk("[wmt_tm_write] cooldev [5]=%s, [6]=%s, [7]=%s, [8]=%s, [9]=%s,\n",
                       g_bind5, g_bind6, g_bind7, g_bind8, g_bind9);

        wmt_tm_printk("[wmt_tm_write] trip_temp [0]=%d, [1]=%d, [2]=%d, [3]=%d, [4]=%d\n",
                       trip_temp[0], trip_temp[1], trip_temp[2], trip_temp[3], trip_temp[4]);

        wmt_tm_printk("[wmt_tm_write] trip_temp [5]=%d, [6]=%d, [7]=%d, [8]=%d, [9]=%d\n",
                       trip_temp[5], trip_temp[6], trip_temp[7], trip_temp[8], trip_temp[9]);

        wmt_tm_printk("[wmt_tm_write] polling time=%d\n", p_linux_if->interval);

        //p_linux_if->thz_dev->polling_delay = p_linux_if->interval*1000;

        //thermal_zone_device_update(p_linux_if->thz_dev);

        // register
        p_linux_if->thz_dev = mtk_thermal_zone_device_register("mtktswmt", g_num_trip, NULL,
                                &wmt_thz_dev_ops, 0, 0, 0, p_linux_if->interval);

        wmt_tm_printk("[wmt_tm_write] time_ms=%d\n", p_linux_if->interval);

        return count;
    } else {
        wmt_tm_info("[%s] bad argument = %s\n", __func__, desc);
    }

    return -EINVAL;
}

static int wmt_tm_proc_register(void)
{
    struct proc_dir_entry *entry = NULL;
    struct proc_dir_entry *wmt_tm_proc_dir = NULL;

    wmt_tm_printk("[%s]\n", __func__);

    wmt_tm_proc_dir = proc_mkdir("wmt_tm", NULL);
    if (!wmt_tm_proc_dir) {
        wmt_tm_printk("[wmt_tm_proc_register]: mkdir /proc/wmt_tm failed\n");
    } else {
        entry = create_proc_entry("wmt_tm", S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, wmt_tm_proc_dir);
        if (entry) {
            entry->read_proc = wmt_tm_read;
            entry->write_proc = wmt_tm_write;
        }

        entry = create_proc_entry("tm_pid", S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, wmt_tm_proc_dir);
        if (entry) {
            entry->read_proc = wmt_tm_pid_read;
            entry->write_proc = wmt_tm_pid_write;
        }

        entry = create_proc_entry("wmt_val", S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, wmt_tm_proc_dir);
        if (entry) {
            entry->read_proc = wmt_wifi_algo_read;
            entry->write_proc = wmt_wifi_algo_write;
        }

        entry = create_proc_entry("tx_thro", S_IRUGO | S_IWUSR, wmt_tm_proc_dir);
        if (entry) {
            entry->read_proc = wmt_wifi_tx_thro_read;
        }

        entry = create_proc_entry("wfd_stat", S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, wmt_tm_proc_dir);
        if (entry) {
            entry->read_proc = wmt_tm_wfd_read;
            entry->write_proc = wmt_tm_wfd_write;
        }
    }
    return 0;
}

static int wmt_tm_proc_unregister(void)
{
    wmt_tm_printk("[%s]\n", __func__);
    //remove_proc_entry("wmt_tm", proc_entry);
    return 0;
}

static int wmt_tm_thz_cl_register(void)
{
    #define DEFAULT_POLL_TIME 0 /*Default disable, turn on by thermal policy*/

    struct linux_thermal_ctrl_if *p_linux_if = 0;

	wmt_tm_printk("[%s]\n", __func__);

    if(pg_wmt_tm) {
       p_linux_if = &pg_wmt_tm->linux_if;
    } else {
       return -1;
    }

    /* cooling devices */
    p_linux_if->cl_dev = mtk_thermal_cooling_device_register("mtktswmt-sysrst", NULL,
			    &mtktspa_cooling_sysrst_ops);

    p_linux_if->cl_pa1_dev = mtk_thermal_cooling_device_register("mtktswmt-pa1", NULL,
			    &mtktspa_cooling_pa1_ops);

    p_linux_if->cl_pa2_dev = mtk_thermal_cooling_device_register("mtktswmt-pa2", NULL,
			    &mtktspa_cooling_pa2_ops);

#ifdef NEVER
    p_linux_if->cl_pa3_dev = mtk_thermal_cooling_device_register("mtktswmt-pa3", NULL,
			    &mtktspa_cooling_pa3_ops);
#endif /* NEVER */

	p_linux_if->interval = DEFAULT_POLL_TIME;

    /* trips */
    p_linux_if->thz_dev = mtk_thermal_zone_device_register("mtktswmt", g_num_trip, NULL,
			    &wmt_thz_dev_ops, 0, 0, 0, p_linux_if->interval);

    return 0;
}

static int wmt_tm_thz_cl_unregister(void)
{
    struct linux_thermal_ctrl_if *p_linux_if = 0;

    wmt_tm_printk("[%s]\n", __func__);

    if(pg_wmt_tm) {
       p_linux_if = &pg_wmt_tm->linux_if;
    } else {
       return -1;
    }

    if (p_linux_if->cl_dev) {
        mtk_thermal_cooling_device_unregister(p_linux_if->cl_dev);
        p_linux_if->cl_dev = NULL;
    }

    if (p_linux_if->cl_pa1_dev) {
        mtk_thermal_cooling_device_unregister(p_linux_if->cl_pa1_dev);
        p_linux_if->cl_pa1_dev = NULL;
    }

    if (p_linux_if->cl_pa2_dev) {
        mtk_thermal_cooling_device_unregister(p_linux_if->cl_pa2_dev);
        p_linux_if->cl_pa2_dev = NULL;
    }

#ifdef NEVER
    if (p_linux_if->cl_pa3_dev) {
        mtk_thermal_cooling_device_unregister(p_linux_if->cl_pa3_dev);
        p_linux_if->cl_pa3_dev = NULL;
    }
#endif /* NEVER */

    if (p_linux_if->thz_dev) {
        mtk_thermal_zone_device_unregister(p_linux_if->thz_dev);
        p_linux_if->thz_dev = NULL;
    }

    return 0;
}

static int wmt_tm_ops_register(struct wmt_thermal_ctrl_ops *ops)
{
    struct wmt_thermal_ctrl_ops *p_des;

   wmt_tm_printk("[%s]\n", __func__);

    if (pg_wmt_tm) {
#if 1
        p_des = &pg_wmt_tm->wmt_if.ops;
        if (ops!=NULL) {
            wmt_tm_printk("[wmt_tm_ops_register] reg start ...\n");
            p_des->query_temp = ops->query_temp;
            p_des->set_temp = ops->set_temp;
            wmt_tm_printk("[wmt_tm_ops_register] reg end ...\n");
        } else {
            p_des->query_temp =  0;
            p_des->set_temp = 0;
        }
#endif
        return 0;
    } else {
        return -1;
    }
}

static int wmt_tm_ops_unregister(void)
{
    struct wmt_thermal_ctrl_ops *p_des;

   wmt_tm_printk("[%s]\n", __func__);

    if (pg_wmt_tm) {
        p_des = &pg_wmt_tm->wmt_if.ops;
        p_des->query_temp = 0;
        p_des->set_temp = 0;

        return 0;
    } else {
        return -1;
    }
}

int wmt_tm_init(struct wmt_thermal_ctrl_ops *ops)
{
    int err = 0;

    wmt_tm_printk("[wmt_tm_init] start -->\n");

    err = wmt_tm_ops_register(ops);
    if(err)
        return err;

    err = wmt_tm_proc_register();
    if(err)
        return err;

    /* init a timer for stats tx bytes */
    wmt_stats_info.pre_time = 0;
    wmt_stats_info.pre_tx_bytes = 0;

    init_timer(&wmt_stats_timer);
    wmt_stats_timer.function = (void *)&wmt_cal_stats;
    wmt_stats_timer.data = (unsigned long) &wmt_stats_info;
    wmt_stats_timer.expires = jiffies + 1 * HZ;
    add_timer(&wmt_stats_timer);

#if 0
    err = wmt_tm_thz_cl_register();
    if(err)
        return err;
#endif
    wmt_tm_printk("[wmt_tm_init] end <--\n");

    return 0;
}

int wmt_tm_init_rt()
{
    int err = 0;

    wmt_tm_printk("[wmt_tm_init_rt] start -->\n");

    err = wmt_tm_thz_cl_register();
    if(err)
        return err;

    wmt_tm_printk("[wmt_tm_init_rt] end <--\n");

    return 0;
}

int wmt_tm_deinit_rt()
{
    int err = 0;

    wmt_tm_printk("[wmt_tm_deinit_rt] start -->\n");

    err = wmt_tm_thz_cl_unregister();
    if(err)
        return err;

    wmt_tm_printk("[wmt_tm_deinit_rt] end <--\n");

    return 0;
}

int wmt_tm_deinit()
{
    int err = 0;

    wmt_tm_printk("[%s]\n", __func__);
#if 0
    err = wmt_tm_thz_cl_unregister();
    if(err)
        return err;
#endif
    err = wmt_tm_proc_unregister();
    if(err)
        return err;

    err = wmt_tm_ops_unregister();
    if(err)
        return err;

    del_timer(&wmt_stats_timer);

    return 0;
}
#endif
#endif

