#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/suspend.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/suspend.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/rockchip/common.h>

#include <asm/io.h>
#include "pm.h"

/*************************dump reg********************************************/

void rkpm_ddr_reg_offset_dump(void __iomem * base_addr,u32 _offset)
{
    rkpm_ddr_printhex(_offset);     
    rkpm_ddr_printch('-');
    rkpm_ddr_printhex(readl_relaxed((base_addr + _offset)));  
}

void  rkpm_ddr_regs_dump(void __iomem * base_addr,u32 start_offset,u32 end_offset)
{
	u32 i;
        //u32 line=0;

        rkpm_ddr_printascii("start from:");     
        rkpm_ddr_printhex((u32)(base_addr +start_offset));       
        rkpm_ddr_printch('\n');
                   
        
	for(i=start_offset;i<=end_offset;)
	{
         
            rkpm_ddr_printhex(reg_readl((base_addr + i)));  
            if(i%16==12) 
            {   
                rkpm_ddr_printch('\n');
            }
            else
            {
                    if(i!=end_offset)
                    rkpm_ddr_printch('-');
                    else                        
                    rkpm_ddr_printch('\n');
            }
            i=i+4;
	} 
    
    
}

static struct rkpm_ops pm_ops={NULL};

static struct rkpm_sram_ops *p_pm_sram_ops=NULL;//pie point for pm_sram_ops
static rkpm_sram_suspend_arg_cb p_suspend_pie_cb=NULL;

// for user setting
static u32   rkpm_ctrbits=0;
//for judging rkpm_ctrbits valid ,save ifself
static u32   rkpm_jdg_ctrbits=0;
static u32   rkpm_jdg_sram_ctrbits=0;

/**************************************ddr callback setting***************************************/

void rkpm_set_pie_info(struct rkpm_sram_ops *pm_sram_ops,rkpm_sram_suspend_arg_cb pie_cb)
{

    p_pm_sram_ops=pm_sram_ops;
    p_suspend_pie_cb=pie_cb;


}

void rkpm_set_ops_prepare_finish(rkpm_ops_void_callback prepare,rkpm_ops_void_callback finish)
{
	pm_ops.prepare=prepare;	
	pm_ops.finish=finish;	
}

void rkpm_set_ops_pwr_dmns(rkpm_ops_void_callback pwr_dmns,rkpm_ops_void_callback re_pwr_dmns)
{
	pm_ops.pwr_dmns=pwr_dmns;	
	pm_ops.re_pwr_dmns=re_pwr_dmns;
}

void rkpm_set_ops_gtclks(rkpm_ops_void_callback gtclks,rkpm_ops_void_callback re_gtclks)
{
	pm_ops.gtclks=gtclks;	
	pm_ops.re_gtclks=re_gtclks;
}


void rkpm_set_ops_plls(rkpm_ops_void_callback plls,rkpm_ops_void_callback re_plls)
{
	pm_ops.plls=plls;	
	pm_ops.re_plls=re_plls;
}


void rkpm_set_ops_gpios(rkpm_ops_void_callback gpios,rkpm_ops_void_callback re_gpios)
{
	pm_ops.gpios=gpios;	
	pm_ops.re_gpios=re_gpios;
}
void rkpm_set_ops_save_setting(rkpm_ops_paramter_u32_cb save_setting,rkpm_ops_void_callback re_save_setting)
{
	pm_ops.save_setting=save_setting;	
	pm_ops.re_save_setting=re_save_setting;
}



void rkpm_set_ops_printch(rkpm_ops_printch_callback printch)
{
	pm_ops.printch=printch;	
}

void rkpm_set_ops_regs_pread(rkpm_ops_void_callback regs_pread)
{
	pm_ops.regs_pread=regs_pread;	
}

void rkpm_set_ops_regs_sleep(rkpm_ops_void_callback slp_setting,rkpm_ops_void_callback re_last)
{	

	pm_ops.slp_setting=slp_setting;    

	pm_ops.slp_re_first=re_last;    
}


/**************************************sram callback setting***************************************/
void rkpm_set_sram_ops_volt(rkpm_ops_void_callback volts,rkpm_ops_void_callback re_volts)
{
        if(p_pm_sram_ops)
        {
            p_pm_sram_ops->volts=volts;	
            p_pm_sram_ops->re_volts=re_volts;
        }
}

void rkpm_set_sram_ops_gtclks(rkpm_ops_void_callback gtclks,rkpm_ops_void_callback re_gtclks)
{
         if(p_pm_sram_ops)
        {
        	p_pm_sram_ops->gtclks=gtclks;	
        	p_pm_sram_ops->re_gtclks=re_gtclks;
        }
}

void rkpm_set_sram_ops_sysclk(rkpm_ops_paramter_u32_cb sysclk,rkpm_ops_paramter_u32_cb re_sysclk)
{
         if(p_pm_sram_ops)
        {
        	p_pm_sram_ops->sysclk=sysclk;	
        	p_pm_sram_ops->re_sysclk=re_sysclk;
        }
}

void rkpm_set_sram_ops_pmic(rkpm_ops_void_callback pmic,rkpm_ops_void_callback re_pmic)
{
     if(p_pm_sram_ops)
    {
        p_pm_sram_ops->pmic=pmic;	
        p_pm_sram_ops->re_pmic=re_pmic;
    }
}

void rkpm_set_sram_ops_ddr(rkpm_ops_void_callback ddr,rkpm_ops_void_callback re_ddr)
{
    if(p_pm_sram_ops)
    {
        p_pm_sram_ops->ddr=ddr;	
        p_pm_sram_ops->re_ddr=re_ddr;
    }
}
void rkpm_set_sram_ops_printch(rkpm_ops_printch_callback printch)
{  
    if(p_pm_sram_ops)
	p_pm_sram_ops->printch=printch;	
}

/******************for user ************************/
void rkpm_set_ctrbits(u32 bits)
{	
	rkpm_ctrbits = bits;
	
}
void rkpm_add_ctrbits(u32 bits)
{	
	rkpm_ctrbits |= bits;
	
}
u32 rkpm_get_ctrbits(void)
{	
	return rkpm_ctrbits;
}

u32 rkpm_chk_ctrbits(u32 bits)
{	
	return (rkpm_ctrbits&bits);
}

//clear
void rkpm_clr_ctrbits(u32 bits)
{
	rkpm_ctrbits&=~bits;
}

/****************** for pm.c************************/

static void inline rkpm_set_jdg_ctrbits(u32 bits)
{	
	rkpm_jdg_ctrbits = bits;
	
}
static u32  inline rkpm_get_jdg_ctrbits(void)
{	
	return rkpm_jdg_ctrbits;
}

static void inline rkpm_add_jdg_ctrbits(int bit)
{	
	rkpm_jdg_ctrbits|=bit;
}

#if 0
static u32 inline rkpm_chk_jdg_ctrbit(int bit)
{	
	return (rkpm_jdg_ctrbits&bit);
}
#endif

static u32 inline rkpm_chk_jdg_ctrbits(int bits)
{	
	return (rkpm_jdg_ctrbits&bits);
}
//clear
static void inline rkpm_clr_jdg_ctrbits(int bit)
{
	rkpm_jdg_ctrbits&=~bit;
}


#define  RKPM_DDR_FUN(fun) \
	if(pm_ops.fun)\
		(pm_ops.fun)()

// fun with paramater  param (p1,p2,p3)
#define  RKPM_DDR_PFUN(fun,param) \
        if(pm_ops.fun) \
            {(pm_ops.fun)param;} while(0)

#define  RKPM_BITCTR_DDR_FUN(ctr,fun) \
	if(rkpm_chk_jdg_ctrbits(RKPM_CTR_##ctr)&&pm_ops.fun)\
		(pm_ops.fun)()

#define  RKPM_BITSCTR_DDR_FUN(bits,fun) \
        if(rkpm_chk_jdg_ctrbits(bits)&&pm_ops.fun)\
            (pm_ops.fun)()


        
#define  RKPM_LPMD_BITSCTR_DDR_PFUN(bits,fun,param) \
                if(rkpm_chk_jdg_ctrbits(RKPM_CTRBITS_SOC_DLPMD)&&pm_ops.fun)\
                    (pm_ops.fun)param

#define  RKPM_LPMD_BITSCTR_DDR_FUN(bits,fun) \
                if(rkpm_chk_jdg_ctrbits(RKPM_CTRBITS_SOC_DLPMD)&&pm_ops.fun)\
                        (pm_ops.fun)()



void rkpm_ctrbits_prepare(void)
{
	
	//rkpm_sram_ctrbits=rkpm_ctrbits;
	
	rkpm_jdg_ctrbits=rkpm_ctrbits;

        //if plls is no pd,clk rate is high, volts can not setting low,so we need to judge ctrbits
	//if(rkpm_chk_jdg_ctrbits(RKPM_CTR_VOLTS))
	{
		//rkpm_clr_jdg_ctrbits(RKPM_CTR_VOLTS);
	}
    
        rkpm_jdg_sram_ctrbits=rkpm_jdg_ctrbits;
        
        //clk gating will gate ddr clk in sram
        if(!rkpm_chk_val_ctrbits(rkpm_jdg_sram_ctrbits,RKPM_CTR_DDR))
        {
           // rkpm_clr_val_ctrbit(rkpm_jdg_sram_ctrbits,RKPM_CTR_GTCLKS);
        }
    
}

struct rk_soc_pm_info_st {
    int offset;
    char *name;
};

#define RK_SOC_PM_HELP_(id,NAME)\
        {\
        .offset= RKPM_CTR_##id,\
        .name= NAME,\
        }
    
struct rk_soc_pm_info_st rk_soc_pm_helps[]={
#if 0
    RK_SOC_PM_HELP_(NO_PD,"pd is not power dn"),
    RK_SOC_PM_HELP_(NO_CLK_GATING,"clk is not gating"),
    RK_SOC_PM_HELP_(NO_PLL,"pll is not power dn"),
    RK_SOC_PM_HELP_(NO_VOLT,"volt is not set suspend"),
    RK_SOC_PM_HELP_(NO_GPIO,"gpio is not control "),
    //RK_SOC_PM_HELP_(NO_SRAM,"not enter sram code"),
    RK_SOC_PM_HELP_(NO_DDR,"ddr is not reflash"),
    RK_SOC_PM_HELP_(NO_PMIC,"pmic is not suspend"),
    RK_SOC_PM_HELP_(RET_DIRT,"sys return from pm_enter directly"),
    RK_SOC_PM_HELP_(SRAM_NO_WFI,"sys is not runing wfi in sram"),
    RK_SOC_PM_HELP_(WAKE_UP_KEY,"send a power key to wake up lcd"),
#endif
};
    
ssize_t rk_soc_pm_helps_sprintf(char *buf)
{
    char *s = buf;
    int i;

    for(i=0;i<ARRAY_SIZE(rk_soc_pm_helps);i++)
    {
        s += sprintf(s, "bit(%d): %s\n", rk_soc_pm_helps[i].offset,rk_soc_pm_helps[i].name);
    }

    return (s-buf);
}   
    
void rk_soc_pm_helps_printk(void)
{
    int i;
    printk("**************rkpm_ctr_bits bits help***********:\n");
    for(i=0;i<ARRAY_SIZE(rk_soc_pm_helps);i++)
    {
        printk("bit(%d): %s\n", rk_soc_pm_helps[i].offset,rk_soc_pm_helps[i].name);
    }
}   

#if 0
static int __init early_param_rk_soc_pm_ctr(char *str)
{
    get_option(&str, &rkpm_ctrbits);
    
    printk("********rkpm_ctr_bits information is following:*********\n");
    printk("rkpm_ctr_bits=%x\n",rkpm_ctrbits);
    if(rkpm_ctrbits)
    {
        rk_soc_pm_helps_printk();
    }
    printk("********rkpm_ctr_bits information end*********\n");
    return 0;
}
#endif

/*******************************************log*********************************************/


bool  pm_log;

extern void pm_emit_log_char(char c);

/********************************ddr print**********************************/
void rkpm_ddr_printch(char byte)
{
        if(pm_ops.printch)
            pm_ops.printch(byte);	
	//if (byte == '\n')
		//rkpm_ddr_printch('\r');
}
void rkpm_ddr_printascii(const char *s)
{
	while (*s) {
		rkpm_ddr_printch(*s);
		s++;
	}
}

void  rkpm_ddr_printhex(unsigned int hex)
{
	int i = 8;
	rkpm_ddr_printch('0');
	rkpm_ddr_printch('x');
	while (i--) {
		unsigned char c = (hex & 0xF0000000) >> 28;
		rkpm_ddr_printch(c < 0xa ? c + '0' : c - 0xa + 'a');
		hex <<= 4;
	}
}
static int rk_lpmode_enter(unsigned long arg)
{

        //RKPM_DDR_PFUN(slp_setting(rkpm_jdg_sram_ctrbits),slp_setting); 
    
        RKPM_DDR_FUN(slp_setting); 
                
        local_flush_tlb_all();
        flush_cache_all();
        outer_flush_all();
        outer_disable();
        cpu_proc_fin();
        //outer_inv_all();// ???
        //  l2x0_inv_all_pm(); //rk319x is not need
        flush_cache_all();
        
        rkpm_ddr_printch('d');

        //rkpm_udelay(3*10);

        dsb();
        wfi();  
        
        rkpm_ddr_printch('D');
	return 0;
}


int cpu_suspend(unsigned long arg, int (*fn)(unsigned long));
static int rkpm_enter(suspend_state_t state)
{
	//static u32 test_count=0;
        // printk(KERN_DEBUG"pm: ");
        printk("%s:\n",__FUNCTION__);
        //printk("pm test times=%d\n",++test_count);
       
        RKPM_DDR_FUN(prepare);   
        
        rkpm_ctrbits_prepare();
         
        //  if(rkpm_chk_jdg_ctrbits(RKPM_CTR_RET_DIRT))
        //  return 0;
      
        rkpm_ddr_printch('0');

        RKPM_BITCTR_DDR_FUN(PWR_DMNS,pwr_dmns);

        rkpm_ddr_printch('1');

        local_fiq_disable();
    
        RKPM_DDR_PFUN(save_setting,(rkpm_jdg_sram_ctrbits)); 
        
        rkpm_ddr_printch('2');
        
        RKPM_BITCTR_DDR_FUN(GTCLKS,gtclks);

        rkpm_ddr_printch('3');

        RKPM_BITCTR_DDR_FUN(PLLS,plls);

        rkpm_ddr_printch('4');

        RKPM_BITCTR_DDR_FUN(GPIOS,gpios);

        RKPM_DDR_FUN(regs_pread);

        rkpm_ddr_printch('5');

        if(rkpm_chk_jdg_ctrbits(RKPM_CTRBITS_SOC_DLPMD))
        {   
            if(cpu_suspend(0,rk_lpmode_enter)==0)
            {
                RKPM_DDR_FUN(slp_re_first);
                rkpm_ddr_printch('D');
                //rk_soc_pm_ctr_bits_prepare();
            }	  	              
            rkpm_ddr_printch('d');          
        }
        else if(rkpm_chk_jdg_ctrbits(RKPM_CTR_IDLESRAM_MD)&&p_suspend_pie_cb)
        {
            call_with_stack(p_suspend_pie_cb,&rkpm_jdg_sram_ctrbits, rockchip_sram_stack);
        }
        else
        {
            dsb();
            wfi();
        }

        rkpm_ddr_printch('5');

        RKPM_BITCTR_DDR_FUN(GPIOS,re_gpios);

        rkpm_ddr_printch('4');

        RKPM_BITCTR_DDR_FUN(PLLS,re_plls);

        rkpm_ddr_printch('3');

        RKPM_BITCTR_DDR_FUN(GTCLKS,re_gtclks);
        
        rkpm_ddr_printch('2');
        
        RKPM_DDR_FUN(re_save_setting); 

        local_fiq_enable();
        rkpm_ddr_printch('1');
        
        RKPM_BITCTR_DDR_FUN(PWR_DMNS,re_pwr_dmns);

        rkpm_ddr_printch('0');
        rkpm_ddr_printch('\n');
        
        RKPM_DDR_FUN(finish);           
        return 0;
}

#if 0
static int rkpm_enter_tst(void)
{

       return rkpm_enter(0);

}
#endif

static int rkpm_suspend_prepare(void)
{
	/* disable entering idle by disable_hlt() */
	//disable_hlt();
	return 0;
}

static void rkpm_suspend_finish(void)
{
	//enable_hlt();
	
	#if 0 //def CONFIG_KEYS_RK29
	if(rkpm_check_ctrbits(1<<RKPM_CTR_WAKE_UP_KEY))
	{
		rk28_send_wakeup_key();
		printk("rk30_pm_finish rk28_send_wakeup_key\n");
	}
	#endif
}


static struct platform_suspend_ops rockchip_suspend_ops = {
	.enter		= rkpm_enter,
	.valid		= suspend_valid_only_mem,
	.prepare 	= rkpm_suspend_prepare,
	.finish		= rkpm_suspend_finish,
};
void __init rockchip_suspend_init(void)
{
    //printk("%s\n",__FUNCTION__);
    suspend_set_ops(&rockchip_suspend_ops);
    return;
}

static enum rockchip_pm_policy pm_policy;
static BLOCKING_NOTIFIER_HEAD(policy_notifier_list);

int rockchip_pm_policy_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&policy_notifier_list, nb);
}

int rockchip_pm_policy_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&policy_notifier_list, nb);
}

static int rockchip_pm_policy_notify(void)
{
	return blocking_notifier_call_chain(&policy_notifier_list,
			pm_policy, NULL);
}

enum rockchip_pm_policy rockchip_pm_get_policy(void)
{
	return pm_policy;
}

int rockchip_pm_set_policy(enum rockchip_pm_policy policy)
{
	if (policy < ROCKCHIP_PM_NR_POLICYS && policy != pm_policy) {
		printk(KERN_INFO "pm policy %d -> %d\n", pm_policy, policy);
		pm_policy = policy;
		rockchip_pm_policy_notify();
	}

	return 0;
}

static unsigned int policy;

static int set_policy(const char *val, const struct kernel_param *kp)
{
	int ret;

	ret = param_set_uint(val, kp);
	if (ret < 0)
		return ret;

	rockchip_pm_set_policy(policy);
	policy = rockchip_pm_get_policy();

	return 0;
}

static struct kernel_param_ops policy_param_ops = {
	.set = set_policy,
	.get = param_get_uint,
};

module_param_cb(policy, &policy_param_ops, &policy, 0600);
