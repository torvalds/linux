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
FILE		:	 ft_test.c
DESC	:	 ft test
AUTHOR	:	  xie xiu xin
DATE		:	  2013-7-2
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

//#include <linux/dma-mapping.h>
//#include <linux/regulator/machine.h>
//#include <plat/dma-pl330.h>
//#include <linux/mfd/wm831x/core.h>
#include <linux/sysfs.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/clk.h>
#include <linux/suspend.h>

#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/lzo.h>
#include <linux/vmalloc.h>


#if 1
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
       printk(fmt, ##arg)
	//printk(KERN_EMERG fmt, ##arg)
 //KERN_DEBUG
#define ft_printk_dbg(fmt, arg...) {while(0);}

 //KERN_WARNING
#define ft_printk_info(fmt, arg...) {while(0);}
#endif


#define MHZ (1000*1000)
#define TST_SETUPS (4)

//#define ENABLE_FT_SUSPEND_TST   // for ft seting 1.6G volt

#define l1_DCACHE_SIZE (32*1024)
#define l2_DCACHE_SIZE (512*1024)
#define TST_TASK_NUM (10*10*3)


static unsigned long arm_setups_rate[TST_SETUPS]={816*MHZ,1656*MHZ*0,1608*MHZ*0,312*MHZ*0};
const static unsigned long ddr_setups_rate[TST_SETUPS]={600*0,800*1,800*1,600*1};

#define L1_JUST (0-10)
const static unsigned int l1_tst_cnt[TST_SETUPS]={5*10*2+L1_JUST,5*10*2+L1_JUST,5*10*2+L1_JUST,80+L1_JUST};
const static unsigned int l2_cpy_cnt[TST_SETUPS]={5,5,4,4};
const static unsigned int ftt_tst_cnt [TST_SETUPS]={1,1,1,0};
const static unsigned int pi_tst_cnt[TST_SETUPS]={12,12,10,8};
const static unsigned int stp_task_num[TST_SETUPS]={300,250,250,80};

static u32 ft_end_cnt=0x20;

//
#define get_setup_tasks(a) ft_get_min(a,TST_TASK_NUM)

//0-7 :test setup1
#define CPU_TST_L1_STP0 (1<<0)
#define CPU_TST_L2_STP0 (1<<1)

static DEFINE_PER_CPU(int [TST_SETUPS], cpu_tst_flags)={0};
static DEFINE_PER_CPU(wait_queue_head_t [TST_SETUPS][TST_TASK_NUM], wait_setups);
static DEFINE_PER_CPU(struct mutex[TST_SETUPS], lzo_lock);
static DEFINE_PER_CPU(struct mutex[TST_SETUPS], l2_lock);
static DEFINE_PER_CPU(u32, sem_setups_cnt);
static DEFINE_PER_CPU(unsigned char *, lzo_wrk);

struct tst_task_struct {
	struct task_struct *task;
	u32 thread_id;
	int cpu;
	int tst_step;
};

static struct tst_task_struct tst_task_date[TST_TASK_NUM][NR_CPUS];
static struct semaphore sem_setups[TST_SETUPS];
static int setups_flag[TST_SETUPS]={0,0,0,0};

static struct clk *arm_clk;

#ifdef FT_DDR_FREQ
static struct clk *ddr_clk;
#endif

void ft_test_flag_seting(void);

int ftt_test(void);
int test_cpus_l2(char *data_s,char *data_e,u32 data);
int Test_mem(void *aligned, unsigned long b_size) ;

int test_cpus_mem_set(char *data_s,char *data_e,u32 data);
int test_cpus_mem_check(char *data_s,char *data_e,u32 data);


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

// for board init arm rate
unsigned long __init ft_test_init_arm_rate(void)
{
	return arm_setups_rate[0];
}
static int ft_get_min(int a, int b)
{
	if(a<b)
	return a;
	else
	return b;

}
void printk_dbg_data(char *buf,int len,char *info)
{
	int i;
	char *buf_s=buf;
	
	printk("%s\n",info);
	for(i=0;i<len;i++)
	{	
		printk("%x ",*(buf_s));
		buf_s++;
		if(i%40==0&&i!=0)
		printk("\n");	
	}
	printk("\n");	

}	

/*******************************************************************************************/
static const char pi_result[]="3141592653589793238462643383279528841971693993751058209749445923078164062862089986280348253421170679821480865132823664709384469555822317253594081284811174502841270193852115559644622948954930381964428810975665933446128475648233786783165271201991456485669234634861045432664821339360726024914127372458706606315588174881520920962829254917153643678925903611330530548820466521384146951941511609433057273657595919530921861173819326117931051185480744623799627495673518857527248912279381830119491298336733624406566438602139494639522473719070217986943702770539217176293176752384674818467669451320005681271452635608277857713427577896091736371787214684409012249534301465495853710579227968925892354201995611212902196864344181598136297747713099605187072113499999983729780499510597317328160963185";

int calc_pi(void)
{  
	int bit = 0, i=0;
	long a=10000,b=0,c=2800,d=0,e=0,g=0;
	int *result;
	long *f;
	int len=0;
	
	char *pi_calc,*pi_tmp;
	char *pi_just=(char *)&pi_result[0];
	size_t pi_just_size=sizeof(pi_result);

	 result = vmalloc(10000*sizeof(int));
	 if (result == NULL)
	 	return -ENOMEM;
	 
	 f = vmalloc(2801*sizeof(long));
	  if (f == NULL)
		 return -ENOMEM;
	  
	 pi_calc= vmalloc(1000*sizeof(char));
	  if (pi_calc == NULL)
		 return -ENOMEM;

        for(;b-c;)
                f[b++]=a/5;
        for(;d=0,g=c*2;c-=14,result[bit++] = e+d/a,e=d%a)
                for(b=c;d+=f[b]*a,f[b]=d%--g,d/=g--,--b;d*=b);

	//ft_printk("lzo1 calc_pi end\n");

	pi_tmp = pi_calc;
        for(i = 0; i < bit; i++) {
		len += sprintf(pi_tmp+len, "%d", result[i]);
		//ft_printk("%c", pi_calc[i]);
		//ft_printk("lzo1 calc_pi end\n");
        }

	for(i = 0; i < bit; i++) {
	 //  ft_printk("%c,%d", pi_calc[i],result[i]);
	   }
	
	//ft_printk("lzo1 calc_pi end,i=%d,len=%d\n",i,len);
	
	if (strncmp(pi_just, pi_calc, pi_just_size) == 0){	
		vfree(result);
		vfree(f );
		vfree(pi_calc);
		//ft_printk("calc_pi ok\n");
		return 0;
	} else {
			vfree(result);
			vfree(f );
			vfree(pi_calc);
			ft_printk("calc_pi error\n");
			return -EPERM;
	}

}

/************************************l1 tst***************************************/
void test_cpus_l1_0(u32 *data);
void test_cpus_l1_1(u32 *data);

void ft_cpu_l1_test(u32 cnt0,u32 cnt1)
{
	//u32 cpu = smp_processor_id();
	int test_array[100];
	int i;
	
	for(i=0;i<cnt0;i++)
	{
		test_cpus_l1_1(&test_array[0]);
		barrier();
		
	}
	//if(cpu==0&&cnt0>40&&cnt1>40)
	//ft_printk(".");
	for(i=0;i<cnt1;i++)
	{
		test_cpus_l1_0(&test_array[0]);
		barrier();
		
	}
}

/********************lzo***************************************/

#define SIZE_M (1024*1024)
#define lzo_dcmp_size (l2_DCACHE_SIZE*2)

int  lzo_cmp_decmp_tst(unsigned char *cmp_in,size_t in_size,unsigned char *cmp_out,size_t out_size,void *wrkmem)
{
	
	//u32 cpu = smp_processor_id();
	int ret;		
	size_t cmp_len,decmp_len;
	//unsigned char *cmp_sr;
	
	cmp_len=out_size;
	ret=lzo1x_1_compress(cmp_in,in_size,cmp_out,&cmp_len,wrkmem);
	
	if(ret != LZO_E_OK)
	{	
		ft_printk("lzo1 comp error\n");
		return ret; 
	}
	else
		ft_printk_dbg("cmplen=%d\n",cmp_len);


	decmp_len=in_size;
	
	ret=lzo1x_decompress_safe(cmp_out,cmp_len,cmp_in,&decmp_len);

	if(ret != LZO_E_OK)
		ft_printk("lzo1 decomp error=%d\n",ret);
	
	//ft_printk("lzo cmp (%x,%x)\n",decmp_len,cmp_len);
	return ret;	
}

/************************************l2 tst***************************************/

static char *test_mal_buf[NR_CPUS]=
{
	NULL,
};
int ft_test_cpus_l2_memcpy(char *data_d,char *data_s,u32 buf_size,u32 cnt)
{
	
	//u32 cpu = smp_processor_id();
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

			//if((j%20==0)&&(cpu==0))
			//ft_printk(".");
		}
		
	}

	return 0;
}


static int ft_test_mem(void *aligned, unsigned long b_size,int cnt) 
{
	int ret=0,i;
	
	for(i=0;i<cnt;i++)
	{
		ret=Test_mem(aligned,b_size);
		if(ret)
		return -1;	
	}

	return 0;	
}

/*************************************type0 tst case**************************************/
void ft_cpu_test_type0(int steps,struct tst_task_struct *task_data)
{
	u32 temp=-1;
	u32 cpu = smp_processor_id();
	u32 id_start;
	
	if(task_data->thread_id==0)
	{	

		if(steps<2)
		{
			mutex_lock(&per_cpu(lzo_lock, cpu)[steps]);

			temp=lzo_cmp_decmp_tst((unsigned char *) test_cpus_l1_0,lzo_dcmp_size,
			test_mal_buf[cpu],sizeof(char)*BUF_SIZE,per_cpu(lzo_wrk, cpu));
			if(temp)
			per_cpu(cpu_tst_flags, cpu)[steps]|=CPU_TST_L1_STP0;

			temp=lzo_cmp_decmp_tst((unsigned char *) test_cpus_l1_1,lzo_dcmp_size,
			test_mal_buf[cpu],sizeof(char)*BUF_SIZE,per_cpu(lzo_wrk, cpu));
			if(temp)
			per_cpu(cpu_tst_flags, cpu)[steps]|=CPU_TST_L1_STP0;

			mutex_unlock(&per_cpu(lzo_lock, cpu)[steps]);	

			
			if(ftt_test())
			{	
				ft_printk("ftt_test  err(%d,%d)\n",task_data->thread_id,cpu);
				while(1);
			}
			if(calc_pi()==(-EPERM))
			{	
				ft_printk("calc_pi	err(%d,%d)\n",task_data->thread_id,cpu);
				while(1);
			}
			
		}
		msleep(50);

		ft_cpu_l1_test(l1_tst_cnt[steps]/2,l1_tst_cnt[steps]/2);
		
		ft_printk(".");

		mutex_lock(&per_cpu(l2_lock, cpu)[steps]);
		if(steps==0)
		{	
			temp=ft_test_cpus_l2_memcpy(NULL,l2_test_buf[cpu],sizeof(char)*BUF_SIZE,l2_cpy_cnt[steps]);
		}
		else
			temp=ft_test_cpus_l2_memcpy(test_mal_buf[cpu],l2_test_buf[cpu],sizeof(char)*BUF_SIZE_M,l2_cpy_cnt[steps]);
			
		if(temp)
		per_cpu(cpu_tst_flags, cpu)[steps]|=CPU_TST_L2_STP0;
		
		msleep(50);
		#if 1
		// max rate test //1658 
		if(steps==1)
		{
		
			//ft_printk("ft_test_mem l2 end (%d,%d)",task_data->thread_id,cpu);
			temp=ft_test_mem((void *)l2_test_buf[cpu], l2_DCACHE_SIZE+l2_DCACHE_SIZE/20,1) ;//l2_DCACHE_SIZE
			//if(cpu==0)
			//ft_printk(".");

			if(temp)
			per_cpu(cpu_tst_flags, cpu)[steps]|=CPU_TST_L2_STP0;
		}
		#endif

		//ft_printk("test_type0 l2 end (%d,%d)",task_data->thread_id,cpu);
		mutex_unlock(&per_cpu(l2_lock, cpu)[steps]);


	}	

	if((task_data->thread_id<l1_tst_cnt[steps]))
	{	
		if(task_data->thread_id%2)
			ft_cpu_l1_test(1,0);
		else
			ft_cpu_l1_test(0,1);
		
	}
	
	if(steps<2)
	{	
		if(get_setup_tasks(stp_task_num[steps])>pi_tst_cnt[steps])
			id_start=get_setup_tasks(stp_task_num[steps])-pi_tst_cnt[steps];
		else
			id_start=1;

		if(task_data->thread_id>=(id_start))
		{
			//ft_printk("calc_pi st=%d(%d,%d)\n",id_start,task_data->thread_id,cpu);
			if(calc_pi()==(-EPERM))
			{	
				ft_printk("calc_pi  err(%d,%d)\n",task_data->thread_id,cpu);
				while(1);
			}

		}
	}


}


int ft_cpu_test_type0_check(int steps,const char *str)
{
	int cpu, ret = 0,i;


	for (cpu = 0; cpu < NR_CPUS; cpu++)
		for(i=0;i<get_setup_tasks(stp_task_num[0]);i++)	
			down(&sem_setups[0]);
		
	ret=0;
	for (cpu = 0; cpu < NR_CPUS; cpu++)
	{
		ret|=per_cpu(cpu_tst_flags, cpu)[steps];
	
	}
	ft_printk("setup%d end:ret=%x,arm=%lu\n",
			steps,ret,clk_get_rate(arm_clk)/MHZ);
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

static u32 ft_client_ready_pin =0;
static u32 ft_client_idle_pin =0;

#include "rk_ft_io.c"

//#define FT_CLIENT_READY_PIN    (0x0c20)
//#define FT_CLIENT_IDLE_PIN     (0x0a00)

int ft_cpu_test_type1_check(int steps,const char *str)
{
	int cpu, ret = 0,i;
	int rate;
        u8 idle_gpio_level;

	if(arm_setups_rate[steps])
	{	
		
		ft_printk("#S%s*\n",str);		
		ft_printk("start test arm=%lu\n",arm_setups_rate[steps]/MHZ);

		rate=clk_get_rate(arm_clk);

		if(arm_setups_rate[steps]<rate)
		{	
			clk_set_rate(arm_clk,arm_setups_rate[steps]);
                        #ifdef FT_DDR_FREQ
			if(ddr_setups_rate[steps])
			{	
				ddr_change_freq(ddr_setups_rate[steps]);
				clk_set_rate(ddr_clk,0);
			}	
                        #endif    
		}
		
                if(ft_client_ready_pin&&ft_client_ready_pin)
                {

                    //ft_printk("S%s,will set gpio\n",str);
                    idle_gpio_level=gpio_get_input_level(ft_client_idle_pin);
                    // send msg to ctr board to up the volt
                    gpio_set_output_level(ft_client_ready_pin,RKPM_GPIO_OUT_H);  

                    // waiting for volt upping ok 
                    while( idle_gpio_level== gpio_get_input_level(ft_client_idle_pin));
                    gpio_set_output_level(ft_client_ready_pin,RKPM_GPIO_OUT_L);  
                    //ft_printk("S%s,set gpio end\n",str);
                    
                }
		if(arm_setups_rate[steps]>=rate)
		{	
			clk_set_rate(arm_clk,arm_setups_rate[steps]);
            #ifdef FT_DDR_FREQ
			if(ddr_setups_rate[steps])
			{	
				ddr_change_freq(ddr_setups_rate[steps]);
				clk_set_rate(ddr_clk,0);
			}	
            #endif
				
		}
		
		setups_flag[steps-1]=1;
		for (cpu = 0; cpu < NR_CPUS; cpu++)
		{	
			wake_up(&per_cpu(wait_setups, cpu)[steps-1][0]);
		}
		
		
		for (cpu = 0; cpu < NR_CPUS; cpu++)
		for(i=0;i<get_setup_tasks(stp_task_num[steps]);i++)
			down(&sem_setups[steps]);
			
		ret=0;
		for (cpu = 0; cpu < NR_CPUS; cpu++)
		{
			ret|=per_cpu(cpu_tst_flags, cpu)[steps];
			//ft_printk("setup%d,cpu%d flags=%x\n",steps,cpu,
			//per_cpu(cpu_tst_flags, cpu)[steps]);
		}	
		ft_printk("setup%d end:ret=%x,arm=%lu\n",
				steps,ret,clk_get_rate(arm_clk)/MHZ);
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

int rk_ft_tests_cpus_init(int cpu);
#define   FT_TIME_PRT (80)
// tst thread callback for per cpu
static int ft_cpu_test(void *data)
{	
	u32 cpu = smp_processor_id();
	struct tst_task_struct *task_data=(struct tst_task_struct *)data;
	int i;	

	if(task_data->thread_id==0)
	{
		per_cpu(sem_setups_cnt, cpu)=0;
		rk_ft_tests_cpus_init(cpu);
		
		for(i=1;i<get_setup_tasks(stp_task_num[0]);i++)
		{	
			wake_up_process(tst_task_date[i][cpu].task);
		}
		//ft_printk("cpu %d wake up\n",cpu);
	}
	
	//steps 0
	if(task_data->thread_id<stp_task_num[0])
	{
		ft_cpu_test_type0(0,task_data);

		per_cpu(sem_setups_cnt, cpu)+=1;

		if((per_cpu(sem_setups_cnt, cpu)%FT_TIME_PRT==0||per_cpu(sem_setups_cnt, cpu)==get_setup_tasks(stp_task_num[0]))
			&&cpu==0
			)
		ft_printk(".\n");

		//if(per_cpu(sem_setups_cnt, cpu)==get_setup_tasks(stp_task_num[0]))
		//	ft_printk("setup=%d cpu=%d,totall=%d,id=%d\n",0,cpu,per_cpu(sem_setups_cnt, cpu),task_data->thread_id);
		up(&sem_setups[0]);
	}

	// steps 1
	if(task_data->thread_id<stp_task_num[1])
	{		
		wait_event_freezable(per_cpu(wait_setups, cpu)[0][task_data->thread_id],(setups_flag[0]==1)||kthread_should_stop());


		if(task_data->thread_id==0)
		{		
		
			per_cpu(sem_setups_cnt, cpu)=0;
			for(i=1;i<get_setup_tasks(stp_task_num[1]);i++)
			{	
				wake_up_process(tst_task_date[i][cpu].task);
			}
			//ft_printk("setup 1 cpu %d wake up\n",cpu);
		}

		ft_cpu_test_type0(1,task_data);


		per_cpu(sem_setups_cnt, cpu)+=1;
		
		if((per_cpu(sem_setups_cnt, cpu)%FT_TIME_PRT==0||per_cpu(sem_setups_cnt, cpu)==get_setup_tasks(stp_task_num[1]))
			&&cpu==0
			)
		ft_printk(".\n");

		//if(per_cpu(sem_setups_cnt, cpu)==get_setup_tasks(stp_task_num[1]))
		//	ft_printk("setup=%d cpu=%d,totall=%d,id=%d\n",1,cpu,per_cpu(sem_setups_cnt, cpu),task_data->thread_id);
		up(&sem_setups[1]);
	}

	
	if(task_data->thread_id<stp_task_num[2])
	{	
		wait_event_freezable(per_cpu(wait_setups, cpu)[1][task_data->thread_id],(setups_flag[1]==1)||kthread_should_stop());

		if(task_data->thread_id==0)
		{	
			per_cpu(sem_setups_cnt, cpu)=0;
			for(i=1;i<get_setup_tasks(stp_task_num[2]);i++)
			{	
				wake_up_process(tst_task_date[i][cpu].task);
			}
			//ft_printk("cpu %d wake up\n",cpu);
		}
		
		ft_cpu_test_type0(2,task_data);
		per_cpu(sem_setups_cnt, cpu)+=1;

		if((per_cpu(sem_setups_cnt, cpu)%FT_TIME_PRT==0||per_cpu(sem_setups_cnt, cpu)==get_setup_tasks(stp_task_num[2]))
			&&cpu==0
			)
		ft_printk(".\n");
		
		//if(per_cpu(sem_setups_cnt, cpu)==get_setup_tasks(stp_task_num[2]))
		//	ft_printk("setup=%d cpu=%d,totall=%d,id=%d\n",2,cpu,per_cpu(sem_setups_cnt, cpu),task_data->thread_id);
		up(&sem_setups[2]);
	}

	
	if(task_data->thread_id<stp_task_num[3])
	{
		wait_event_freezable(per_cpu(wait_setups, cpu)[2][task_data->thread_id],(setups_flag[2]==1)||kthread_should_stop());


		if(task_data->thread_id==0)
		{
		
			per_cpu(sem_setups_cnt, cpu)=0;
			
			for(i=1;i<get_setup_tasks(stp_task_num[3]);i++)
			{	
				wake_up_process(tst_task_date[i][cpu].task);
			}
			//ft_printk("cpu %d wake up\n",cpu);
		}
		
		ft_cpu_test_type0(3,task_data);
		per_cpu(sem_setups_cnt, cpu)+=1;

		
		if((per_cpu(sem_setups_cnt, cpu)%FT_TIME_PRT==0||per_cpu(sem_setups_cnt, cpu)==get_setup_tasks(stp_task_num[3]))
			&&cpu==0
			)
		ft_printk(".\n");
		
		//if(per_cpu(sem_setups_cnt, cpu)==get_setup_tasks(stp_task_num[3]))
		//	ft_printk("setup=%d cpu=%d,totall=%d,id=%d\n",3,cpu,per_cpu(sem_setups_cnt, cpu),task_data->thread_id);
		up(&sem_setups[3]);
	}
	return 0;
}

int rk_ft_tests_cpus_init(int cpu)
{
	int i,ret = 0,j;
	struct sched_param param = { .sched_priority = 0 }; 
	char *buf;
	unsigned char *wrk;
	struct task_struct *task;

	//arm_clk=clk_get(NULL, "cpu");
	
	// malloc buf
	test_mal_buf[cpu]=NULL;
	buf = kmalloc(BUF_SIZE_M, GFP_KERNEL);
	if (buf)
	{
		test_mal_buf[cpu]=buf;
		//printk("xdbg but=%x\n",(void*)buf);
	}
	
	for(i=0;i<TST_SETUPS;i++)
	{	
		for(j=0;j<TST_TASK_NUM;j++)
		init_waitqueue_head(&per_cpu(wait_setups, cpu)[i][j]);
		
	}	

	for(i=0;i<TST_SETUPS;i++)
	{	
		mutex_init(&per_cpu(lzo_lock, cpu)[i]);
		mutex_init(&per_cpu(l2_lock, cpu)[i]);
	}
		
		
	wrk = vmalloc(LZO1X_MEM_COMPRESS);
	if (wrk==NULL) {
		printk("xxx  Failed to allocate LZO workspace\n");
		//return -1;
	}
	else
		per_cpu(lzo_wrk, cpu)=wrk;		
	
	for(i=1;i<TST_TASK_NUM;i++)
	{	
		task= kthread_create(ft_cpu_test,(void *)&tst_task_date[i][cpu],"ft_test_task%d_cpu%d",i,cpu);
		tst_task_date[i][cpu].task=task;
		tst_task_date[i][cpu].thread_id=i;
		tst_task_date[i][cpu].cpu=cpu;
		
		tst_task_date[i][cpu].tst_step=0;
		if((i%3)==0)
			sched_setscheduler_nocheck(task, /*task_policy[i%4]*/SCHED_RR, &param);
		else if((i%3)==1)
			sched_setscheduler_nocheck(task, /*task_policy[i%4]*/SCHED_FIFO, &param); 
		else
			sched_setscheduler_nocheck(task, /*task_policy[i%4]*/SCHED_NORMAL, &param); 
		
		get_task_struct(task);
		kthread_bind(task, cpu);
	}
	
	return ret;
}

static int __init rk_ft_tests_init(void)
{
	int cpu, i,ret = 0;
	struct sched_param param = { .sched_priority =0 };	    
        struct device_node *parent;
        u32 temp_gpios[2];

        ft_printk_info("%s\n",__FUNCTION__);

        
        arm_clk=clk_get(NULL, "clk_core");       

        parent = of_find_node_by_name(NULL, "rockchip_ft_test");    
        
        if (IS_ERR_OR_NULL(parent)) {
             printk("%s dev node err\n", __func__);
             return -1;
         }
              
         if(of_property_read_u32_array(parent,"rockchip,arm_rate",(u32 *)&arm_setups_rate[0],TST_SETUPS))
         {
                 printk("%s:arm_setups_rate\n",__FUNCTION__);
                 return -1;
         }
         ft_printk_info("arm_setups_rate=%lu(init clk),%lu,%lu,%lu\n",
                        clk_get_rate(arm_clk)/MHZ,arm_setups_rate[1]/MHZ,arm_setups_rate[2]/MHZ,arm_setups_rate[3]/MHZ);


        if(of_property_read_u32_array(parent,"rockchip,l1_tst_cnt",(u32 *)&l1_tst_cnt[0],TST_SETUPS))
        {
               printk("%s:l1_tst_cnt error\n",__FUNCTION__);
               return -1;
        }

        if(of_property_read_u32_array(parent,"rockchip,l2_cpy_cnt",(u32 *)&l2_cpy_cnt[0],TST_SETUPS))
         {
                 printk("%s:l1_tst_cnt error\n",__FUNCTION__);
                 return -1;
         }
        if(of_property_read_u32_array(parent,"rockchip,ftt_tst_cnt",(u32 *)&ftt_tst_cnt[0],TST_SETUPS))
        {
               printk("%s:ftt_tst_cnt error\n",__FUNCTION__);
               return -1;
        }
        if(of_property_read_u32_array(parent,"rockchip,pi_tst_cnt",(u32 *)&pi_tst_cnt[0],TST_SETUPS))
        {
               printk("%s:pi_tst_cnt error\n",__FUNCTION__);
               return -1;
        }
          if(of_property_read_u32_array(parent,"rockchip,stp_task_num",(u32 *)&stp_task_num[0],TST_SETUPS))
        {
               printk("%s:stp_task_num error\n",__FUNCTION__);
               return -1;
        }

        for(i=0;i<TST_SETUPS;i++)
        {   
            ft_printk_info("index=%d,l1_tst_cnt=%u,l2_cpy_cnt=%u,ftt_tst_cnt=%u,pi_tst_cnt=%u,stp_task_num=%u\n"
                                    ,i,l1_tst_cnt[i],l2_cpy_cnt[i],ftt_tst_cnt[i],pi_tst_cnt[i],stp_task_num[i]);
       
        }
        
        if(of_property_read_u32_array(parent,"rockchip,ft_end_cnt",(u32 *)&ft_end_cnt,1))
        {
             printk("%s:ft_end_cnt error\n",__FUNCTION__);
             return -1;
        }

        if(of_property_read_u32_array(parent,"rockchip,ft_vol_gpios",(u32 *)&temp_gpios[0],2))
        {
             printk("%s:no ft_vol_gpios info,so ft ctr vol is not support\n",__FUNCTION__);
        }  
        else
        {       
            ft_client_ready_pin=temp_gpios[0];        
            ft_client_idle_pin=temp_gpios[1];      
            ft_printk_info("ft_vol_gpio%x,%x\n",ft_client_ready_pin,ft_client_idle_pin);
        }

	//ddr_clk = clk_get(NULL, "clk_ddr");
	//clk_set_rate(ddr_clk,0);

	for(i=0;i<TST_SETUPS;i++)
	{	
		sema_init(&sem_setups[i], 0);	
	}

	for(i=0;i<1;i++)
	{	
		struct task_struct *task;
		
		for (cpu = 0; cpu < NR_CPUS; cpu++) 
		{	
			task= kthread_create(ft_cpu_test,(void *)&tst_task_date[i][cpu],"ft_test_task%d_cpu%d",i,cpu);
			tst_task_date[i][cpu].task=task;
			tst_task_date[i][cpu].thread_id=i;
			tst_task_date[i][cpu].cpu=cpu;
			
			tst_task_date[i][cpu].tst_step=0;
			
			sched_setscheduler_nocheck(task, /*task_policy[i%4]*/SCHED_RR, &param);
	
			get_task_struct(task);
			kthread_bind(task, cpu);
		}
	}

	
	for (cpu = 1; cpu < NR_CPUS; cpu++) 
	{	
		wake_up_process(tst_task_date[0][cpu].task);	
	}
	
	wake_up_process(tst_task_date[0][0].task);

	return ret;	
}

//core_initcall(rk_ft_tests_init);

static int rk_ft_tests_over(void)
{
	int ret = 0;
        //return 0;
        //int gpio_ret,gpio_ret1;
        ret=rk_ft_tests_init();
        if(ret)
        {
            printk("rk_ft_tests_init error\n");
            while(1);
        }
   
        if(ft_client_ready_pin&&ft_client_ready_pin)
        {
            //GPIO0_A0
            gpio_set_in_output(ft_client_ready_pin,RKPM_GPIO_OUTPUT);    
            gpio_set_output_level(ft_client_ready_pin,RKPM_GPIO_OUT_L);  

            gpio_set_in_output(ft_client_idle_pin,RKPM_GPIO_INPUT);

            pin_set_fun(ft_client_ready_pin);    //ready
            pin_set_fun(ft_client_idle_pin);    // idle    
        }
	ft_cpu_test_type0_check(0,"KERNEL");
	
	ft_cpu_test_type1_check(1,"HSPEED");
	
	ft_cpu_test_type1_check(2,"HSPEED2");
	
	ft_cpu_test_type1_check(3,"KERNEL2");

	ft_printk("#END%x*\n",ft_end_cnt);
	
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

