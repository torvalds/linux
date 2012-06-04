/*
 * drivers/cpufreq/cpufreq_fantasy.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin Zhang <kevin@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/jiffies.h>
#include <linux/kernel_stat.h>
#include <linux/mutex.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>
#include <linux/ktime.h>
#include <linux/sched.h>

/* idle rate coarse adjust for cpu frequency down */
#define FANTASY_CPUFREQ_IDLE_MAX_RATE(freq)         \
    (freq<100000? 65 : (freq<200000? 60 : (freq<600000? 55 : (freq<900000? 35 : 20))))

/*
 * minimum rate for idle task, if idle rate is less than this
 * value, cpu frequency should be adjusted to the mauximum value
*/
#define FANTASY_CPUFREQ_IDLE_MIN_RATE(freq)         \
    (freq<100000? 35 : (freq<200000? 30 : (freq<600000? 20 : (freq<900000? 15 : 5))))

#define LATENCY_MULTIPLIER          (1000)                      /* latency multiplier */
#define TRANSITION_LATENCY_LIMIT    (1 * 1000 * 1000 * 1000)    /* latency limitation, should be larger than 1 second */
#define IOWAIT_IS_BUSY              (1)                         /* io wait time should be counted in idle time */

#define DECRASE_FREQ_STEP_LIMIT1    (300000)   /* decrase frequency limited to 300Mhz when frequency is [900Mhz, 1008Mhz] */
#define DECRASE_FREQ_STEP_LIMIT2    (200000)   /* decrase frequency limited to 200Mhz when frequency is [600Mhz,  900Mhz) */
#define DECRASE_FREQ_STEP_LIMIT3    (100000)   /* decrase frequency limited to 100Mhz when frequency is [200Mhz,  600Mhz) */
#define DECRASE_FREQ_STEP_LIMIT4    (20000)    /* decrase frequency limited to  20Mhz when frequency is [60Mhz,   200Mhz) */
#define IOWAIT_FREQ_STEP_LIMIT1     (300000)   /* frequency limited to  300Mhz when iowait is [10, 20)  */
#define IOWAIT_FREQ_STEP_LIMIT2     (600000)   /* frequency limited to  600Mhz when iowait is [20, 30)  */
#define IOWAIT_FREQ_STEP_LIMIT3     (816000)   /* frequency limited to  816Mhz when iowait is [30, 40)  */
#define IOWAIT_FREQ_STEP_LIMIT4     (1008000)  /* frequency limited to 1008Mhz when iowait is [40, 100) */


enum cpufreq_fantasy_step {
    CPUFREQ_FANTASY_STEP1,      /* step1 for fantasy policy, adjust cpu frequency to the maximum value      */
    CPUFREQ_FANTASY_STEP2,      /* step2 for fantasy policy, adjust cpu frequency to the 2nd maximum value  */
    CPUFREQ_FANTASY_STEP3,      /* step3 for fantasy policy, adjust cpu frequency to the value which keep
                                   CPU idle rate upto FANTASY_CPUFREQ_IDLE_MAX_RATE  */
    CPUFREQ_FANTASY_STEP4,      /* step4 for fantasy policy, adjust cpu frequency down, step by step, the
                                   idle rate will be closed to FANTASY_CPUFREQ_IDLE_MIN_RATE    */
};
/*
state machine for the 4 step is following:
    step1 -----> step2 -----> step3 -----> step4 ---------------------|
      ^            |            |            ^                        |
      |            |            |            | close to min idle rate |
      -------------|            |            -------------------------|
      |                         |                                     |
      --------------------------|                                     |
      |             user event or cpu load heavy                      |
      ----------------------------------------------------------------|
*/


static void do_dbs_timer(struct work_struct *work);
static int cpufreq_governor_dbs(struct cpufreq_policy *policy, unsigned int event);

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_FANTASY
static
#endif
struct cpufreq_governor cpufreq_gov_fantasy = {
       .name                   = "fantasy",
       .governor               = cpufreq_governor_dbs,
       .max_transition_latency = TRANSITION_LATENCY_LIMIT,
       .owner                  = THIS_MODULE,
};


static struct cpu_dbs_info_s {
    cputime64_t prev_cpu_idle;                  /* cpu idle time accumulative total */
    cputime64_t prev_cpu_iowait;                /* io wait time accumulative total  */
    cputime64_t prev_cpu_wall;                  /* cpu run time accumulative total  */
    struct cpufreq_policy *cur_policy;          /* current policy                   */
    struct cpufreq_frequency_table *freq_table; /* cpu frequency table              */
    struct mutex timer_mutex;                   /* mutex for timer operation        */
    enum cpufreq_fantasy_step step;             /* policy state machine             */
    struct delayed_work work;                   /* timer proc for workqueue         */
} fantasy_dbs_info;


static struct dbs_tuners {
    unsigned int sampling_rate; /* cpu loading statistic frequency                                  */
    unsigned int io_is_busy;    /* flag to mark that if io wait time should be count in idle time   */
} dbs_tuners_ins = {
    .sampling_rate = TRANSITION_LATENCY_LIMIT,      /* default sample rate is                       */
};

static struct workqueue_struct    *kfantasy_wq;     /* work queue for process cpu dynamic frequency */

static DEFINE_MUTEX(dbs_mutex); /* mutex for protect dbs start/stop                                 */

#undef FANTASY_DBG
#undef FANTASY_ERR
#if (0)
    #define FANTASY_DBG(format,args...)   printk("[fantasy]"format,##args)
    #define FANTASY_ERR(format,args...)   printk("[fantasy]"format,##args)
#else
    #define FANTASY_DBG(format,args...)   do{}while(0)
    #define FANTASY_ERR(format,args...)   do{}while(0)
#endif


#undef FANTASY_INF
#if (0)
    #define FANTASY_INF(format,args...)   printk(format,##args)
#else
    #define FANTASY_INF(format,args...)   do{}while(0)
#endif


/*
*********************************************************************************************************
*                           __ulldiv
*
*Description: unsigned long long int division.
*
*Arguments  : dividend  64bits dividend;
*             divisior  64bits divisior;
*
*Return     : 64bits quotient;
*
*Notes      :
*
*********************************************************************************************************
*/
static inline __u64 __ulldiv(__u64 dividend, __u64 divisior)
{
    __u64   tmpDiv = divisior;
    __u64   tmpQuot = 0;
    __s32   shift = 0;

    if(!divisior)
    {
        /* divide 0 error abort */
        return 0;
    }

    while(!(tmpDiv & ((__u64)1<<63)))
    {
        tmpDiv <<= 1;
        shift ++;
    }

    do
    {
        if(dividend >= tmpDiv)
        {
            dividend -= tmpDiv;
            tmpQuot = (tmpQuot << 1) | 1;
        }
        else
        {
            tmpQuot = (tmpQuot << 1) | 0;
        }
        tmpDiv >>= 1;
        shift --;
    } while(shift >= 0);

    return tmpQuot;
}



/*
*********************************************************************************************************
*                           get_cpu_idle_time_jiffy
*
*Description: get cpu idle time by jiffies, update cpu run total time;
*
*Arguments  : wall  pointer for store total run-time;
*
*Return     : cpu idle time, based on us.
*
*Notes      :
*
*********************************************************************************************************
*/
static inline cputime64_t get_cpu_idle_time_jiffy(unsigned int cpu, cputime64_t *wall)
{
    cputime64_t idle_time;
    cputime64_t cur_wall_time;
    cputime64_t busy_time;

    cur_wall_time = jiffies64_to_cputime64(get_jiffies_64());

    busy_time = cputime64_add(kstat_cpu(cpu).cpustat.user,
            kstat_cpu(cpu).cpustat.system);

    busy_time = cputime64_add(busy_time, kstat_cpu(cpu).cpustat.irq);
    busy_time = cputime64_add(busy_time, kstat_cpu(cpu).cpustat.softirq);
    busy_time = cputime64_add(busy_time, kstat_cpu(cpu).cpustat.steal);
    busy_time = cputime64_add(busy_time, kstat_cpu(cpu).cpustat.nice);

    idle_time = cputime64_sub(cur_wall_time, busy_time);
    if (wall)
        *wall = (cputime64_t)jiffies_to_usecs(cur_wall_time);

    return (cputime64_t)jiffies_to_usecs(idle_time);
}


/*
*********************************************************************************************************
*                           get_cpu_idle_time
*
*Description: get cpu idle time, try to get cpu idle time with clock source first, if clock source
*             is invlalid, try to get idle time with jiffies again.
*
*Arguments  : wall  pointer for store cpu run total time.
*
*Return     : idle time;
*
*Notes      :
*
*********************************************************************************************************
*/
static inline cputime64_t get_cpu_idle_time(cputime64_t *wall)
{
    u64 idle_time = get_cpu_idle_time_us(fantasy_dbs_info.cur_policy->cpu, wall);

    if (idle_time == -1ULL)
        return get_cpu_idle_time_jiffy(fantasy_dbs_info.cur_policy->cpu, wall);

    return idle_time;
}


/*
*********************************************************************************************************
*                           get_cpu_iowait_time_jiffy
*
*Description: get cpu iowait time by jiffies;
*
*Arguments  : cpu  cpu number;
*
*Return     : cpu iowait time, based on us.
*
*Notes      :
*
*********************************************************************************************************
*/
static inline cputime64_t get_cpu_iowait_time_jiffy(unsigned int cpu)
{
    cputime64_t iowait_time;
    iowait_time = kstat_cpu(cpu).cpustat.iowait;

    return (cputime64_t)jiffies_to_usecs(iowait_time);
}


/*
*********************************************************************************************************
*                           get_cpu_iowait_time
*
*Description: get io wait time.
*
*Arguments  : wall  pointer for store cpu run total time.
*
*Return     : io wait time.
*
*Notes      :
*
*********************************************************************************************************
*/
static inline cputime64_t get_cpu_iowait_time(cputime64_t *wall)
{
    u64 iowait_time = get_cpu_iowait_time_us(fantasy_dbs_info.cur_policy->cpu, wall);

    if (iowait_time == -1ULL)
        return get_cpu_iowait_time_jiffy(fantasy_dbs_info.cur_policy->cpu);

    return iowait_time;
}


/*
*********************************************************************************************************
*                           do_dbs_timer
*
*Description: cpu frequency timer process handler.
*
*Arguments  :
*
*Return     :
*
*Notes      :
*
*********************************************************************************************************
*/
static void do_dbs_timer(struct work_struct *work)
{
    /* We want all CPUs to do sampling nearly on same jiffy */
    int delay = usecs_to_jiffies(dbs_tuners_ins.sampling_rate);
    unsigned int freq_cur;

    mutex_lock(&fantasy_dbs_info.timer_mutex);

    /* get current frequency */
    freq_cur = cpufreq_quick_get(fantasy_dbs_info.cur_policy->cpu);
    FANTASY_DBG("current cpu frequency is:%d\n", freq_cur);

    switch (fantasy_dbs_info.step) {

        case CPUFREQ_FANTASY_STEP1: {
            FANTASY_DBG("step1 : set cpu frequency to max value (%d)\n", fantasy_dbs_info.cur_policy->max);
            if(freq_cur != fantasy_dbs_info.cur_policy->max) {
                /* adjust cpu frequncy to the maximum value */
                __cpufreq_driver_target(fantasy_dbs_info.cur_policy, fantasy_dbs_info.cur_policy->max, CPUFREQ_RELATION_H);
            }
            fantasy_dbs_info.step = CPUFREQ_FANTASY_STEP2;
            break;
        }

        case CPUFREQ_FANTASY_STEP2: {
            /* adjust cpu frequncy to the maximum value */
            FANTASY_DBG("step2 : set cpu frequency to second max value\n");
            __cpufreq_driver_target(fantasy_dbs_info.cur_policy, freq_cur-1000, CPUFREQ_RELATION_L);
            fantasy_dbs_info.step = CPUFREQ_FANTASY_STEP3;
            break;
        }

        case CPUFREQ_FANTASY_STEP3: {
            cputime64_t cur_wall_time, cur_idle_time, cur_iowait_time;
            unsigned int idle_time, wall_time, iowait_time;
            unsigned int freq_target;
            unsigned int idle_rate, iowait_rate;

            FANTASY_DBG("step3 : set cpu frequency\n");

            /* get idle time, io-wait time, and cpu run total time */
            cur_idle_time = get_cpu_idle_time(&cur_wall_time);
            cur_iowait_time = get_cpu_iowait_time(&cur_wall_time);

            /* calculate idle/io-wait/total run time in last statistic cycle */
            wall_time = (unsigned int) cputime64_sub(cur_wall_time, fantasy_dbs_info.prev_cpu_wall);
            idle_time = (unsigned int) cputime64_sub(cur_idle_time, fantasy_dbs_info.prev_cpu_idle);
            iowait_time = (unsigned int) cputime64_sub(cur_iowait_time, fantasy_dbs_info.prev_cpu_iowait);

            if(dbs_tuners_ins.io_is_busy) {
                idle_time -= iowait_time;
                if(idle_time < 0) {
                    idle_time = 0;
                }
            }

            /* update parameters */
            fantasy_dbs_info.prev_cpu_wall = cur_wall_time;
            fantasy_dbs_info.prev_cpu_idle = cur_idle_time;
            fantasy_dbs_info.prev_cpu_iowait = cur_iowait_time;

            idle_rate = idle_time*100/wall_time;
            iowait_rate = iowait_time*100/wall_time;

            FANTASY_INF("%d,", freq_cur/1000);  /*cpu current frequency*/
            FANTASY_INF("%d,", idle_rate);      /*cpu idle rate*/
            FANTASY_INF("%d\n", iowait_rate);   /*cpu iowait rate*/

            /* check idle rate */
            if(idle_rate > FANTASY_CPUFREQ_IDLE_MAX_RATE(freq_cur)) {
                /* idle rate is higher than the max idle rate, so, try to decrase the cpu frequency */
              	freq_target = __ulldiv((u64)freq_cur*(wall_time-idle_time)*100, wall_time);
			   	freq_target = freq_target / ((100-FANTASY_CPUFREQ_IDLE_MAX_RATE(freq_target)));

			   	FANTASY_DBG("current max idle rate is:%d\n", FANTASY_CPUFREQ_IDLE_MAX_RATE(freq_cur));

                if(freq_cur >= 900000){
                    if(freq_cur - freq_target > DECRASE_FREQ_STEP_LIMIT1){
                        freq_target = freq_cur - DECRASE_FREQ_STEP_LIMIT1;
                    }
                }
                else if(freq_cur >= 600000){
                    if(freq_cur - freq_target > DECRASE_FREQ_STEP_LIMIT2){
                            freq_target = freq_cur - DECRASE_FREQ_STEP_LIMIT2;
                    }
                }
                else if(freq_cur >= 200000){
                    if(freq_cur - freq_target > DECRASE_FREQ_STEP_LIMIT3){
                        freq_target = freq_cur - DECRASE_FREQ_STEP_LIMIT3;
                    }
                }
                else if(freq_cur >= 60000){
                    if(freq_cur - freq_target > DECRASE_FREQ_STEP_LIMIT4){
                        freq_target = freq_cur - DECRASE_FREQ_STEP_LIMIT4;
                    }
                }

                if(iowait_rate >= 40){
                    if(freq_target < IOWAIT_FREQ_STEP_LIMIT4){
                        freq_target = IOWAIT_FREQ_STEP_LIMIT4;
                    }
                }
                else if(iowait_rate >= 30){
                    if(freq_target < IOWAIT_FREQ_STEP_LIMIT3){
                        freq_target = IOWAIT_FREQ_STEP_LIMIT3;
                    }
                }
                else if(iowait_rate >= 20){
                    if(freq_target < IOWAIT_FREQ_STEP_LIMIT2){
                        freq_target = IOWAIT_FREQ_STEP_LIMIT2;
                    }
                }
                else if(iowait_rate >= 10){
                    if(freq_target < IOWAIT_FREQ_STEP_LIMIT1){
                        freq_target = IOWAIT_FREQ_STEP_LIMIT1;
                    }
                }

                if (fantasy_dbs_info.cur_policy->cur == fantasy_dbs_info.cur_policy->min){
                    fantasy_dbs_info.step = CPUFREQ_FANTASY_STEP3;
                    break;
                }

                /* set target frequency */
                __cpufreq_driver_target(fantasy_dbs_info.cur_policy, freq_target, CPUFREQ_RELATION_L);
                FANTASY_DBG("set cpu frequency to %d\n", freq_target);
            }
            else if(idle_rate < FANTASY_CPUFREQ_IDLE_MIN_RATE(freq_cur)) {
			   	FANTASY_DBG("min idle rate is:%d\n", FANTASY_CPUFREQ_IDLE_MIN_RATE(freq_cur));

                /* adjust cpu frequncy to the maximum value */
                __cpufreq_driver_target(fantasy_dbs_info.cur_policy, fantasy_dbs_info.cur_policy->max, CPUFREQ_RELATION_H);
                FANTASY_DBG("set cpu frequency to %d\n", fantasy_dbs_info.cur_policy->max);
                fantasy_dbs_info.step = CPUFREQ_FANTASY_STEP2;
                break;
            }
            else {
                if(iowait_rate >= 40){
                    if(freq_cur < IOWAIT_FREQ_STEP_LIMIT4){
                        freq_target = IOWAIT_FREQ_STEP_LIMIT4;
                    }
                    else {
                        break;
                    }
                }
                else if(iowait_rate >= 30){
                    if(freq_cur < IOWAIT_FREQ_STEP_LIMIT3){
                        freq_target = IOWAIT_FREQ_STEP_LIMIT3;
                    }
                    else {
                        break;
                    }
                }
                else if(iowait_rate >= 20){
                    if(freq_cur < IOWAIT_FREQ_STEP_LIMIT2){
                        freq_target = IOWAIT_FREQ_STEP_LIMIT2;
                    }
                    else {
                        break;
                    }
                }
                else if(iowait_rate >= 10){
                    if(freq_cur < IOWAIT_FREQ_STEP_LIMIT1){
                        freq_target = IOWAIT_FREQ_STEP_LIMIT1;
                    }
                    else {
                        break;
                    }
                }else {
                    /* cpu frequency is in the valid threshold, do nothing */
                    FANTASY_DBG("do nothing for cpu frequency change\n");
                    break;
                }

                /* set target frequency */
                __cpufreq_driver_target(fantasy_dbs_info.cur_policy, freq_target, CPUFREQ_RELATION_L);
                FANTASY_DBG("set cpu frequency to %d\n", freq_target);
            }

            fantasy_dbs_info.step = CPUFREQ_FANTASY_STEP3;
            break;
        }

        case CPUFREQ_FANTASY_STEP4: {
            /* don't process cpu frequency nice ajust now */
            break;
        }
    }

    queue_delayed_work(kfantasy_wq, &fantasy_dbs_info.work, delay);
    mutex_unlock(&fantasy_dbs_info.timer_mutex);
}


/*
*********************************************************************************************************
*                           dbs_timer_init
*
*Description: initialise timer for process cpu frquency.
*
*Arguments  : dbs_info  cpu frequency policy informatioin.
*
*Return     : none
*
*Notes      :
*
*********************************************************************************************************
*/
static void dbs_timer_init(struct cpu_dbs_info_s *dbs_info)
{
    /* calcualte timer cycle time by sampling rate */
    int delay = usecs_to_jiffies(dbs_tuners_ins.sampling_rate);
    /* init workqueue for process cpu frequency */
    INIT_DELAYED_WORK_DEFERRABLE(&dbs_info->work, do_dbs_timer);
    queue_delayed_work(kfantasy_wq, &dbs_info->work, delay);
}


/*
*********************************************************************************************************
*                           dbs_timer_exit
*
*Description:
*
*Arguments  :
*
*Return     :
*
*Notes      :
*
*********************************************************************************************************
*/
static void dbs_timer_exit(struct cpu_dbs_info_s *dbs_info)
{
    cancel_delayed_work_sync(&dbs_info->work);
}


/*
*********************************************************************************************************
*                           cpufreq_governor_dbs
*
*Description: cpu frequency governor process handle, call-back by cpu-freq core.
*
*Arguments  : policy    cpu frequency policy crrent using.
*             event     command from cpu-freq core.
*
*Return     : result,
*
*Notes      :
*
*********************************************************************************************************
*/
static int cpufreq_governor_dbs(struct cpufreq_policy *policy, unsigned int event)
{
    unsigned int    cpu = policy->cpu;
    struct cpu_dbs_info_s *this_dbs_info = &fantasy_dbs_info;
    unsigned int    latency;

    switch (event){
        case CPUFREQ_GOV_START: {
            mutex_lock(&dbs_mutex);

            /* set cpu policy */
            this_dbs_info->cur_policy = policy;

            /* initialise cpu idle time */
            this_dbs_info->prev_cpu_idle = get_cpu_idle_time(&this_dbs_info->prev_cpu_wall);

            /* initialise cpu iowait time */
            this_dbs_info->prev_cpu_iowait = get_cpu_iowait_time(&this_dbs_info->prev_cpu_wall);

            /* initialise cpu frequency table */
            this_dbs_info->freq_table = cpufreq_frequency_get_table(cpu);

            /* policy latency is in nS. Convert it to uS first */
            latency = policy->cpuinfo.transition_latency / 1000;
            latency = latency ? latency : 1;
            dbs_tuners_ins.sampling_rate = latency * LATENCY_MULTIPLIER;

            /* set if io wait should be counted in cpu idle */
            dbs_tuners_ins.io_is_busy = IOWAIT_IS_BUSY;
            mutex_unlock(&dbs_mutex);

            /* init mutex for protecting timer process */
            mutex_init(&this_dbs_info->timer_mutex);

            /* init tuners state machine */
            fantasy_dbs_info.step = CPUFREQ_FANTASY_STEP1;

            /* init timer for prccess cpu-frequencyh */
            dbs_timer_init(this_dbs_info);
            break;
        }

        case CPUFREQ_GOV_STOP: {
            /* delete timer */
            dbs_timer_exit(this_dbs_info);
            mutex_lock(&dbs_mutex);
            /* destroy timer mutex */
            mutex_destroy(&this_dbs_info->timer_mutex);
            mutex_unlock(&dbs_mutex);
            break;
        }

        case CPUFREQ_GOV_LIMITS: {
            /* cpu frequency limitation has changed, adjust current frequency */
            mutex_lock(&this_dbs_info->timer_mutex);
            /* set cpu frequency to the max value, and reset state machine */
            __cpufreq_driver_target(fantasy_dbs_info.cur_policy, fantasy_dbs_info.cur_policy->max, CPUFREQ_RELATION_H);
            /* reset tuners state machine */
            fantasy_dbs_info.step = CPUFREQ_FANTASY_STEP1;
            mutex_unlock(&this_dbs_info->timer_mutex);
            break;
        }

        #ifdef CONFIG_CPU_FREQ_USR_EVNT_NOTIFY
        case CPUFREQ_GOV_USRENET: {
            /* cpu frequency limitation has changed, adjust current frequency */
            if(!mutex_trylock(&this_dbs_info->timer_mutex)) {
                FANTASY_DBG("CPUFREQ_GOV_USRENET try to lock mutex failed!\n");
                /* reset tuners state machine */
                fantasy_dbs_info.step = CPUFREQ_FANTASY_STEP1;
                break;
            }
            /* set cpu frequenc to the max value, and reset state machine */
            __cpufreq_driver_target(fantasy_dbs_info.cur_policy, fantasy_dbs_info.cur_policy->max, CPUFREQ_RELATION_H);
            /* reset tuners state machine */
            fantasy_dbs_info.step = CPUFREQ_FANTASY_STEP1;
            mutex_unlock(&this_dbs_info->timer_mutex);
            break;
        }
        #endif
    }
   return 0;
}


/*
*********************************************************************************************************
*                           cpufreq_gov_dbs_init
*
*Description: fantasy cpu-freq governor initialise.
*
*Arguments  : none
*
*Return     : result
*
*Notes      :
*
*********************************************************************************************************
*/
static int __init cpufreq_gov_dbs_init(void)
{
    int     err;

    /* create work queue for process cpu frequency policy */
    kfantasy_wq = create_workqueue("kfantasy");
    if (!kfantasy_wq) {
        printk(KERN_ERR "Creation of kfantasy failed\n");
        return -EFAULT;
    }
    /* register cpu frequency governor into cpu-freq core */
    err = cpufreq_register_governor(&cpufreq_gov_fantasy);
    if (err) {
        destroy_workqueue(kfantasy_wq);
    }

    return err;
}


/*
*********************************************************************************************************
*                           cpufreq_gov_dbs_exit
*
*Description: fantasy cpu-freq governor exit.
*
*Arguments  : none
*
*Return     : none
*
*Notes      :
*
*********************************************************************************************************
*/
static void __exit cpufreq_gov_dbs_exit(void)
{
    /* unregister cpu frequency governor */
    cpufreq_unregister_governor(&cpufreq_gov_fantasy);
    /* destroy work queue */
    destroy_workqueue(kfantasy_wq);
}



MODULE_AUTHOR("kevin.z.m <kevin@allwinnertech.com>");
MODULE_DESCRIPTION("'cpufreq_fantasy' - A good cpu frequency policy");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_FANTASY
fs_initcall(cpufreq_gov_dbs_init);
#else
module_init(cpufreq_gov_dbs_init);
#endif
module_exit(cpufreq_gov_dbs_exit);
