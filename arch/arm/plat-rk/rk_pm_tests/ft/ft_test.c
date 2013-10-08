/* arch/arm/mach-rk30/rk_pm_tests.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
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


/*************************************************************************/
/*	  COPYRIGHT (C)  ROCK-CHIPS FUZHOU . ALL RIGHTS RESERVED.*/
/*************************************************************************
FILE		:	  rk_pm_tests.c
DESC		:	  Power management in dynning state
AUTHOR		:	  chenxing
DATE		:	  2012-7-2
NOTES		:
$LOG: GPIO.C,V $
REVISION 0.01
#include <linux/clk.h>
#include <linux/kobject.h>
 ***************************************************************************/
#include <linux/string.h>
#include <linux/resume-trace.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/freezer.h>

#include <linux/dma-mapping.h>
#include <linux/regulator/machine.h>
#include <plat/dma-pl330.h>
#include <linux/mfd/wm831x/core.h>
#include <linux/sysfs.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/clk.h>
#include <linux/suspend.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include "ft_pwm.h"


#if 0
#define ft_printk(fmt, arg...) \
	printk(KERN_EMERG fmt, ##arg)
 //KERN_DEBUG
#define ft_printk_dbg(fmt, arg...) \
	printk(KERN_EMERG fmt, ##arg) 
	
 //KERN_WARNING
#define ft_printk_info(fmt, arg...) \
	printk(KERN_EMERG fmt, ##arg)
#else

#define ft_printk(fmt, arg...) \
	printk(KERN_EMERG fmt, ##arg)
 //KERN_DEBUG
#define ft_printk_dbg(fmt, arg...) {while(0);}

 //KERN_WARNING
#define ft_printk_info(fmt, arg...) {while(0);}
#endif

#define FT_PWM	0

int pwm_volt[5] = {900000, 1250000, 1350000, 0, 0};

#define MHZ (1000*1000)
#define TST_SETUPS (4)
#define FT_END_CNT (0x23)

#define ENABLE_FT_TEST_GPIO   // for ft seting 1.6G volt

//#define ENABLE_FT_SUSPEND_TST   // for ft seting 1.6G volt

static struct semaphore sem_step0 = __SEMAPHORE_INITIALIZER(sem_step0, 0);
static struct semaphore sem_step1 = __SEMAPHORE_INITIALIZER(sem_step1, 0);
static struct semaphore sem_step2 = __SEMAPHORE_INITIALIZER(sem_step2, 0);
static struct semaphore sem_step3 = __SEMAPHORE_INITIALIZER(sem_step3, 0);

static struct semaphore *sem_steps[TST_SETUPS]={&sem_step0,&sem_step1,&sem_step2,&sem_step3};

static int setups_flag[TST_SETUPS]={0,0,0,0};


#if defined(CONFIG_ARCH_RK3188)

#if 0  // ¡Ωµµ≤‚ ‘
const static unsigned long arm_setups_rate[TST_SETUPS]={816*MHZ,1608*MHZ*1,0,0};
const static unsigned long l1_tst_cnt[TST_SETUPS]={5*10*1,5*10*1,5*10*21,5*10*1};
const static unsigned long l2_cpy_cnt[TST_SETUPS]={5*1+2,5*2+4,5*2+4,5*2+4};

#else

//const static unsigned long arm_setups_rate[TST_SETUPS]={312*MHZ,1656*MHZ*1,1608*MHZ*1,312*MHZ*1};
const static unsigned long arm_setups_rate[TST_SETUPS]={816*MHZ,1656*MHZ*1,1608*MHZ*1,312*MHZ*1};
//const static unsigned long arm_setups_rate[TST_SETUPS]={816*MHZ,1608*MHZ*1,816*MHZ*1,1608*MHZ*1};
const static unsigned long l1_tst_cnt[TST_SETUPS]={5*10*2,5*10*2,5*10*2,5*10*2};
const static unsigned long l2_cpy_cnt[TST_SETUPS]={5*1+2,5*2+4,5*2+4,5*2+4};

#endif

#define FT_CLIENT_READY_PIN    RK30_PIN3_PB3
#define FT_CLIENT_IDLE_PIN     RK30_PIN0_PA3

#elif defined(CONFIG_SOC_RK3168)

const static unsigned long arm_setups_rate[4] = {552 * MHZ, 1200 * MHZ, 1608 * MHZ, 0};
//const static unsigned long arm_setups_rate[4]={552*MHZ,0,0,0};

const static unsigned long l1_tst_cnt[TST_SETUPS]={5*10,5*10,0,0};
const static unsigned long l2_cpy_cnt[TST_SETUPS]={5*3,5*4,0,0};


#define FT_CLIENT_READY_PIN    RK30_PIN3_PB3
#define FT_CLIENT_IDLE_PIN     RK30_PIN0_PA3

#elif defined(CONFIG_SOC_RK3028)

const static unsigned long arm_setups_rate[4]={552*MHZ,1200*MHZ*1,0,0};

const static unsigned long l1_tst_cnt[TST_SETUPS]={5*10,5*10,0,0};
const static unsigned long l2_cpy_cnt[TST_SETUPS]={5*3,5*4,0,0};

#define FT_CLIENT_READY_PIN    RK30_PIN1_PA2
#define FT_CLIENT_IDLE_PIN     RK30_PIN3_PD4

#elif defined(CONFIG_ARCH_RK3026)

const static unsigned long arm_setups_rate[4] = {312 * MHZ, 816 * MHZ, 1008 * MHZ, 0};
const static unsigned long l1_tst_cnt[TST_SETUPS]={5 * 10, 5 * 10, 0, 0};
const static unsigned long l2_cpy_cnt[TST_SETUPS]={5 * 3, 5 * 4, 0, 0};

#define FT_CLIENT_READY_PIN    RK30_PIN2_PA7
#define FT_CLIENT_IDLE_PIN     RK30_PIN0_PA3

#else

const static unsigned long arm_setups_rate[4]={552*MHZ,0,0,0};

const static unsigned long l1_tst_cnt[TST_SETUPS]={5*10,5*10,0,0};
const static unsigned long l2_cpy_cnt[TST_SETUPS]={5*3,5*4,0,0};

#define FT_CLIENT_READY_PIN    RK30_PIN3_PB3
#define FT_CLIENT_IDLE_PIN     RK30_PIN0_PA3
#endif


//0-7 :test setup1
#define CPU_TST_L1_STP0 (1<<0)
#define CPU_TST_L2_STP0 (1<<1)
#define CPU_TST_SETUP0_MSK (0xff)
//7-15 :test setup2
#define CPU_TST_L1_STP1 (1<<8)
#define CPU_TST_L2_STP1 (1<<9)
#define CPU_TST_SETUP1_MSK (0xff00)
//16-23:test stetup3
#define CPU_TST_L1_STP2 (1<<16)
#define CPU_TST_L2_STP2 (1<<17)
#define CPU_TST_SETUP2_MSK (0xff0000)

//24-31:test stetup4
#define CPU_TST_L1_STP3 (1<<24)
#define CPU_TST_L2_STP3 (1<<25)
#define CPU_TST_SETUP3_MSK (0xff000000)

const static unsigned int setup_l1_bits[TST_SETUPS]={CPU_TST_L1_STP0,CPU_TST_L1_STP1,CPU_TST_L1_STP2,CPU_TST_L1_STP3};
const static unsigned int setup_l2_bits[TST_SETUPS]={CPU_TST_L2_STP0,CPU_TST_L2_STP1,CPU_TST_L2_STP2,CPU_TST_L2_STP3};
const static unsigned int setup_bits_msk[TST_SETUPS]={CPU_TST_SETUP0_MSK,CPU_TST_SETUP1_MSK,CPU_TST_SETUP2_MSK,CPU_TST_SETUP3_MSK};


static DEFINE_PER_CPU(int, cpu_tst_flags)=(
			  CPU_TST_L1_STP0|CPU_TST_L2_STP0
			|CPU_TST_L1_STP1|CPU_TST_L2_STP1
			|CPU_TST_L1_STP2|CPU_TST_L2_STP2
			|CPU_TST_L1_STP3|CPU_TST_L2_STP3
			);

static struct clk *arm_clk;
static DEFINE_PER_CPU(wait_queue_head_t [TST_SETUPS], wait_setups);

unsigned long __init ft_test_init_arm_rate(void)
{
	return arm_setups_rate[0];

}

/************************************l1 tst***************************************/
void test_cpus_l1(u32 *data);
void test_cpus_l0(u32 *data);

void ft_cpu_l1_test(u32 cnt)
{
	u32 cpu = smp_processor_id();
	int test_array[100];
	int i;
	for(i=0;i<cnt;i++)
	{
		test_cpus_l1(&test_array[0]);
		barrier();
		
	}
	if(cpu==0)
		ft_printk(".");
	for(i=0;i<cnt;i++)
	{
		test_cpus_l0(&test_array[0]);
		barrier();
		
	}
	
}

/************************************l2 tst***************************************/

int test_cpus_l2(char *data_s,char *data_e,u32 data);

int test_cpus_mem_set(char *data_s,char *data_e,u32 data);
int test_cpus_mem_check(char *data_s,char *data_e,u32 data);
#define l1_DCACHE_SIZE (32*1024)
#define l1_DCACHE_SIZE_M (l1_DCACHE_SIZE*32)

#define l2_DCACHE_SIZE (512*1024)
#define BUF_SIZE (l2_DCACHE_SIZE*8)// tst buf size

#define BUF_SIZE_M (BUF_SIZE)// tst buf size


static char memtest_buf0[BUF_SIZE] __attribute__((aligned(4096)));
#if (NR_CPUS>=2)
static char memtest_buf1[BUF_SIZE] __attribute__((aligned(4096)));
#if (NR_CPUS>=4)
static char memtest_buf2[BUF_SIZE] __attribute__((aligned(4096)));
static char memtest_buf3[BUF_SIZE] __attribute__((aligned(4096)));
#if (NR_CPUS>=8)
static char memtest_buf4[BUF_SIZE] __attribute__((aligned(4096)));
static char memtest_buf5[BUF_SIZE] __attribute__((aligned(4096)));
static char memtest_buf6[BUF_SIZE] __attribute__((aligned(4096)));
static char memtest_buf7[BUF_SIZE] __attribute__((aligned(4096)));
#endif
#endif
#endif

static char *l2_test_buf[NR_CPUS]=
{
	memtest_buf0,
#if (NR_CPUS>=2)
	memtest_buf1,
#if (NR_CPUS>=4)
	memtest_buf2,
	memtest_buf3,
#if (NR_CPUS>=8)
	memtest_buf4,
	memtest_buf5,
	memtest_buf6,
	memtest_buf7,
#endif
#endif
#endif

};

static char *l2_test_mbuf[NR_CPUS]=
{
NULL,
};

#if 0
int ft_test_cpus_l2(char *data_s,u32 buf_size,u32 cnt)
{
	int i,j;
	int ret=0;
	int test_size=l1_DCACHE_SIZE;
	for(j=0;j<cnt;j++)	
	{	
		for(i=0;i<buf_size;)
		{
			
			ret=test_cpus_l2(data_s+i,data_s+i+test_size,0xffffffff);
			if(ret)
			{	
				return 0xff;
			}
			ret=test_cpus_l2(data_s+i,data_s+i+test_size,0);
			if(ret)
			{	
				return 0x1;
			}
			ret=test_cpus_l2(data_s+i,data_s+i+test_size,0xaaaaaaaa);
			if(ret)
			{	
				return 0xaa;
			}
			
			i+=test_size;	

			if((i+test_size)>buf_size)
				break;	
			
		}
	}

	return 0;
}

int test_cpus_l2_m(char *data_s,char *data_d,int size,char data)
{
	char *start;
	char *data_end=data_s+size;
	for(start=data_s;start<data_end;start++)
	{
		*start=data;
		barrier();
	}
	
	memcpy(data_d,data_s,size);
	
	data_end=data_d+size;
	for(start=data_d;start<data_end;start++)
	{
		if(*start!=data)
		{
			barrier();
			return -1;
		}
	}
	return 0;
}


int ft_test_cpus_l2_m(char *data_s,char *data_d,u32 buf_size,u32 cnt)
{
	int i,j;
	int ret=0;
	int test_size=l1_DCACHE_SIZE;
	for(j=0;j<cnt;j++)	
	{	
		for(i=0;i<buf_size;)
		{
			
			ret=test_cpus_l2_m(data_s+i,data_d+i,test_size,0xff);
			
			if(ret)
			{	
				return 0xff;
			}
			ret=test_cpus_l2_m(data_s+i,data_d+i,test_size,0);
			if(ret)
			{	
				return 0x1;
			}
			ret=test_cpus_l2_m(data_s+i,data_d+i,test_size,0xaa);
			if(ret)
			{	
				return 0xaa;
			}
			
			i+=test_size;	

			if((i+test_size)>buf_size)
				break;	
			
		}
	}

	return 0;
}
#endif

int ft_test_cpus_l2_memcpy(char *data_d,char *data_s,u32 buf_size,u32 cnt)
{
	
	u32 cpu = smp_processor_id();
	int j;
	int ret=0;
	int test_size;
	char *mem_start;
	char *mem_end;
	
	char *cpy_start;
	char *cpy_end;
	
	test_size=(buf_size/(2*l2_DCACHE_SIZE))*(2*l2_DCACHE_SIZE);
	
	for(j=0;j<cnt;j++)	
	{	
		mem_start=data_s;
		mem_end=data_s+test_size;

		if(data_d)
		{	
			cpy_start=data_d;
			cpy_end=data_d+test_size;
		}
		else
			cpy_start=NULL;

		for(;mem_start<=(mem_end-2*l2_DCACHE_SIZE);)
		{
			

			test_cpus_mem_set(mem_start,mem_start+l2_DCACHE_SIZE,0xaaaaaaaa);
			test_cpus_mem_set(mem_start+l2_DCACHE_SIZE,mem_start+l2_DCACHE_SIZE*2,0x55555555);

			
			//ret=test_cpus_mem_check(mem_start,mem_start+l2_DCACHE_SIZE,0xaaaaaaaa);
			//ft_printk("mem_start end=%x,%x\n",mem_start+l2_DCACHE_SIZE,ret);


			if(test_cpus_mem_check(mem_start,mem_start+l2_DCACHE_SIZE,0xaaaaaaaa))
			{	
				ret=0xaa;
				return ret;
			}
						
			if(test_cpus_mem_check(mem_start+l2_DCACHE_SIZE,mem_start+l2_DCACHE_SIZE*2,0x55555555))
			{	
				ret=0x55;
				return ret;
			}
#if 1
			if(cpy_start)
			{	
				memcpy(cpy_start,mem_start,l2_DCACHE_SIZE);
				memcpy(cpy_start+l2_DCACHE_SIZE,mem_start+l2_DCACHE_SIZE,l2_DCACHE_SIZE);

				if(test_cpus_mem_check(cpy_start,cpy_start+l2_DCACHE_SIZE,0xaaaaaaaa))
				{	
					ret=0x1aa;
					return ret;
				}
				
				if(test_cpus_mem_check(cpy_start+l2_DCACHE_SIZE,cpy_start+l2_DCACHE_SIZE*2,0x55555555))
				{	
					ret=0x155;
					return ret;
				}
			}

			
			test_cpus_mem_set(mem_start+l2_DCACHE_SIZE,mem_start+l2_DCACHE_SIZE*2,0xaaaaaaaa);
			test_cpus_mem_set(mem_start,mem_start+l2_DCACHE_SIZE,0x55555555);


			if(test_cpus_mem_check(mem_start+l2_DCACHE_SIZE,mem_start+l2_DCACHE_SIZE*2,0xaaaaaaaa))
			{	
				ret=0x2aa;
				return ret;
			}
						
			if(test_cpus_mem_check(mem_start,mem_start+l2_DCACHE_SIZE,0x55555555))
			{	
				ret=0x255;
				return ret;
			}

			//cpy_start is aa  mem_start is  55
			
			if(cpy_start)
			{				
				memcpy(cpy_start+l2_DCACHE_SIZE,mem_start+l2_DCACHE_SIZE,l2_DCACHE_SIZE);//aa
				memcpy(cpy_start,mem_start,l2_DCACHE_SIZE);//55

				if(test_cpus_mem_check(cpy_start+l2_DCACHE_SIZE,cpy_start+l2_DCACHE_SIZE*2,0xaaaaaaaa))
				{	
					ret=0x3aa;
					return ret;
				}
				
				if(test_cpus_mem_check(cpy_start,cpy_start+l2_DCACHE_SIZE,0x55555555))
				{	
					ret=0x355;
					return ret;
				}
			}
#endif
			mem_start+=2*l2_DCACHE_SIZE;
			if(cpy_start)
			{
				cpy_start+=2*l2_DCACHE_SIZE;
			}

			
		}
		if((j%20==0)&&(cpu==0))
			ft_printk(".");
	}

	return 0;
}

/*************************************816 tst case**************************************/

void ft_cpu_test_type0(int steps)
{
	u32 temp=-1;
	u32 cpu = smp_processor_id();
	
	ft_printk_dbg("test typ0 step%d cpu=%d start\n",steps,cpu);	
	//arm rate init
	ft_cpu_l1_test(l1_tst_cnt[steps]);
#if FT_PWM
	ft_pwm_set_voltage("arm", pwm_volt[0]);
	mdelay(5);
#endif
	per_cpu(cpu_tst_flags, cpu)&=~setup_l1_bits[steps];

	//temp=ft_test_cpus_l2(l2_test_buf[cpu],sizeof(char)*BUF_SIZE,20);
	temp=ft_test_cpus_l2_memcpy(NULL,l2_test_buf[cpu],sizeof(char)*BUF_SIZE,l2_cpy_cnt[steps]);
	
	if(!temp)
		per_cpu(cpu_tst_flags, cpu)&=~setup_l2_bits[steps];

	ft_printk_dbg("test typ0 step%d cpu=%d end\n",steps,cpu);	

}

int ft_cpu_test_type0_check(int steps,const char *str)
{

	int cpu, ret = 0;

	for (cpu = 0; cpu < NR_CPUS; cpu++)
		down(sem_steps[steps]);
	
	
	for (cpu = 0; cpu < NR_CPUS; cpu++)
	{
		ret|=per_cpu(cpu_tst_flags, cpu);
		ft_printk_dbg("setup%d,cpu%d flags=%x(%x)\n",
				steps, cpu, per_cpu(cpu_tst_flags, cpu) & setup_bits_msk[steps],
				per_cpu(cpu_tst_flags, cpu));
	}
	
	ret&=setup_bits_msk[steps];// test setup1
	ft_printk("setup%d end:ret=%x,arm=%lu,ddr=%lu\n",
			steps,ret,clk_get_rate(arm_clk)/MHZ,clk_get_rate(clk_get(NULL, "ddr"))/MHZ);
	if(ret)
	{
		ft_printk("#R01%s*\n",str);
		while(1);
	}
	else
		ft_printk("#R00%s*\n",str);
	
	return ret;
}	


/*************************************type1 rate tst case**************************************/

void ft_cpu_test_type1(int steps)
{
	u32 temp=-1;
	u32 cpu = smp_processor_id();
	//int i;
	
	ft_printk_dbg("test typ1 step%d cpu=%d start\n",steps,cpu);	

	//arm rate init
	ft_cpu_l1_test(l1_tst_cnt[steps]);
	
	per_cpu(cpu_tst_flags, cpu)&=~setup_l1_bits[steps];

	//ft_printk_dbg("ft test cpus l2 begin\n");
	
	temp=ft_test_cpus_l2_memcpy(l2_test_mbuf[cpu],l2_test_buf[cpu],sizeof(char)*BUF_SIZE_M,l2_cpy_cnt[steps]);
		
	if(!temp)
		per_cpu(cpu_tst_flags, cpu)&=~setup_l2_bits[steps];
	
	#if 0
	if(cpu==0)
		ft_printk(".");
	for(i=0;i<1200;i++)
	{	
		usleep_range(200, 200);//200
		if(( i%700 == 0)&&cpu==0)
			ft_printk(".");
	}
	#endif
	
	ft_printk_dbg("test typ1 step%d cpu=%d end\n",steps,cpu);	
	
}

int ft_cpu_test_type1_check(int steps,const char *str)
{
	int cpu, ret = 0;
	int rate;

	if(arm_setups_rate[steps])
	{	
		setups_flag[steps]=1;
		
		ft_printk("#S%s*\n",str);
		

		rate=clk_get_rate(arm_clk);

		if(arm_setups_rate[steps]<rate)
			clk_set_rate(arm_clk,arm_setups_rate[steps]);
		
#if FT_PWM
		ft_pwm_set_voltage("arm", pwm_volt[steps]);
		mdelay(5);
#else
#ifdef ENABLE_FT_TEST_GPIO
		// send msg to ctr board to up the volt
		gpio_request(FT_CLIENT_READY_PIN, "client ready");
		gpio_request(FT_CLIENT_IDLE_PIN, "client idle");

		gpio_direction_output(FT_CLIENT_READY_PIN, GPIO_HIGH);
		gpio_direction_input(FT_CLIENT_IDLE_PIN);

		// waiting for volt upping ok 
		if((steps%2))
			while( 0 == gpio_get_value(FT_CLIENT_IDLE_PIN));
		else
			while( 1 == gpio_get_value(FT_CLIENT_IDLE_PIN));

		gpio_set_value(FT_CLIENT_READY_PIN, GPIO_LOW);
#endif
#endif

		if(arm_setups_rate[steps]>=rate)
			clk_set_rate(arm_clk,arm_setups_rate[steps]);

		for (cpu = 0; cpu < NR_CPUS; cpu++)
			wake_up(&per_cpu(wait_setups, cpu)[steps]);


		for (cpu = 0; cpu < NR_CPUS; cpu++)
			down(sem_steps[steps]);
			
		ret=0;
		for (cpu = 0; cpu < NR_CPUS; cpu++)
		{
			ret|=per_cpu(cpu_tst_flags, cpu);
			ft_printk_dbg("setup%d,cpu%d flags=%x(%x)\n",
					steps, cpu, per_cpu(cpu_tst_flags, cpu) & setup_bits_msk[steps],
					per_cpu(cpu_tst_flags, cpu));
		}
	
		ret&=setup_bits_msk[steps];// test setup2
	
		ft_printk("setup%d end:ret=%x,arm=%lu,ddr=%lu\n",
				steps,ret,clk_get_rate(arm_clk)/MHZ,clk_get_rate(clk_get(NULL, "ddr"))/MHZ);
		if(ret)
		{
			ft_printk("#R01%s*\n",str);
			while(1);
		}
		else
			ft_printk("#R00%s*\n",str);
		
	}
	return ret;

}

// tst thread callback for per cpu
static int ft_cpu_test(void *data)
{	
	u32 cpu = smp_processor_id();
	
	ft_cpu_test_type0(0);
	up(sem_steps[0]);
	
	wait_event_freezable(per_cpu(wait_setups, cpu)[1],(setups_flag[1]==1)||kthread_should_stop());
	ft_cpu_test_type1(1);
	up(sem_steps[1]);
	
	wait_event_freezable(per_cpu(wait_setups, cpu)[2],(setups_flag[2]==1)||kthread_should_stop());
	ft_cpu_test_type1(2);
	up(sem_steps[2]);
	
	wait_event_freezable(per_cpu(wait_setups, cpu)[3],(setups_flag[3]==1)||kthread_should_stop());
	ft_cpu_test_type1(3);
	up(sem_steps[3]);

	return 0;
}


static int __init rk_ft_tests_init(void)
{
	int cpu, i,ret = 0;
	struct sched_param param = { .sched_priority = 0 };	
	char *buf;
	arm_clk=clk_get(NULL, "cpu");
	
	ft_pwm_init();
	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		l2_test_mbuf[cpu]=NULL;
		buf = kmalloc(BUF_SIZE_M, GFP_KERNEL);
		
		if (buf)
		{
			l2_test_mbuf[cpu]=buf;
			//printk("xdbg but=%x\n",(void*)buf);
		}	
	}

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		for(i=0;i<TST_SETUPS;i++)
		{	
			init_waitqueue_head(&per_cpu(wait_setups, cpu)[i]);
		}
		
	}

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		struct task_struct *task = kthread_create(ft_cpu_test, NULL, "ft_cpu_test%d", cpu);
		sched_setscheduler_nocheck(task, SCHED_RR, &param);
		get_task_struct(task);
		kthread_bind(task, cpu);
		wake_up_process(task);
	}

	return ret;
}
core_initcall(rk_ft_tests_init);

void ft_test_flag_seting(void);

static int rk_ft_tests_over(void)
{
	int ret = 0;

	ft_cpu_test_type0_check(0,"KERNEL");
	
	ft_cpu_test_type1_check(1,"HSPEED");
	
	ft_cpu_test_type1_check(2,"HSPEED2");
	
	ft_cpu_test_type1_check(3,"KERNEL2");

	ft_printk("#END%x*\n",FT_END_CNT);

	
	#ifdef ENABLE_FT_SUSPEND_TST
	{	
		ft_test_flag_seting();
		pm_suspend(PM_SUSPEND_MEM);
	}
	#endif

	while(1);

	return ret;
}


late_initcall_sync(rk_ft_tests_over);




