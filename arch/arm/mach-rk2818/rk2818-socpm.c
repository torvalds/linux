#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <asm/io.h>
#include <asm/tcm.h>
#include <mach/rk2818_iomap.h>
#include <mach/scu.h>
#include <mach/iomux.h>
#include <mach/gpio.h>
#include <mach/rk2818-socpm.h>
#include <linux/regulator/driver.h>
#if defined (CONFIG_RK2818_SOC_PM_DBG)

static struct rk2818_pm_attr_dbg_st __tcmdata pm_scu_attr_dbg[PM_SCU_ATTR_DBG_NUM];
static struct rk2818_pm_attr_dbg_st __tcmdata pm_general_attr_dbg[PM_GENERAL_ATTR_DBG_NUM];
static struct rk2818_pm_attr_dbg_st __tcmdata pm_gpio0_attr_dbg[PM_GPIO0_ATTR_DBG_NUM];
static struct rk2818_pm_attr_dbg_st __tcmdata pm_gpio1_attr_dbg[PM_GPIO1_ATTR_DBG_NUM];
//static void rk2818_socpm_attr_int(void);
//static void setreg_form_socpm_attr(unsigned int *reg,int regoff,struct rk2818_pm_soc_st *soc);
#else

#define setreg_form_socpm_attr(a,b,c)
#endif


#ifdef RK2818_PM_PRT_CHANGED_REG
static unsigned int __tcmdata pm_scu_reg_ch[PM_SCU_REG_NUM];
static unsigned int __tcmdata pm_general_reg_ch[PM_GENERAL_CPU_REG];
static unsigned int __tcmdata pm_gpio0_reg_ch[PM_SCU_GPIO_SWPORTC_NUM];
static unsigned int __tcmdata pm_gpio1_reg_ch[PM_SCU_GPIO_SWPORTC_NUM];
#endif
static unsigned int __tcmdata pm_scu_reg_save[PM_SCU_REG_NUM];
static unsigned int __tcmdata pm_scu_temp_reg[PM_SCU_REG_NUM];

static struct rk2818_pm_soc_st __tcmdata pm_scu_ctr={
.reg_save=&pm_scu_reg_save[0],
.reg_base_addr=(unsigned int *)RK2818_SCU_BASE,
.reg_ctrbit=0,
.reg_num=PM_SCU_REG_NUM,

#ifdef RK2818_PM_PRT_CHANGED_REG
.reg_ch=&pm_scu_reg_ch[0],
#endif
#if defined (CONFIG_RK2818_SOC_PM_DBG)
.attr_num=PM_SCU_ATTR_DBG_NUM,
.attr_dbg=&pm_scu_attr_dbg[0],
.attr_flag=0,
#endif
};


static unsigned int __tcmdata pm_general_reg_save[PM_GENERAL_CPU_REG];

static struct rk2818_pm_soc_st __tcmdata pm_general_ctr={
.reg_save=&pm_general_reg_save[0],
.reg_base_addr=(unsigned int *)RK2818_REGFILE_BASE,
.reg_ctrbit=0,
.reg_num=PM_GENERAL_CPU_REG,

#ifdef RK2818_PM_PRT_CHANGED_REG
.reg_ch=&pm_general_reg_ch[0],
#endif
#if defined (CONFIG_RK2818_SOC_PM_DBG)
.attr_num=PM_GENERAL_ATTR_DBG_NUM,
.attr_dbg=&pm_general_attr_dbg[0],
.attr_flag=0,
#endif
};


static unsigned int __tcmdata pm_gpio0_reg_save[PM_SCU_GPIO_SWPORTC_NUM];



static struct rk2818_pm_soc_st __tcmdata pm_gpio0_ctr={
.reg_save=&pm_gpio0_reg_save[0],
.reg_base_addr=(unsigned int *)RK2818_GPIO0_BASE,
.reg_ctrbit=0,
.reg_num=PM_SCU_GPIO_SWPORTC_NUM,

#ifdef RK2818_PM_PRT_CHANGED_REG
.reg_ch=&pm_gpio0_reg_ch[0],
#endif
#if defined (CONFIG_RK2818_SOC_PM_DBG)
.attr_num=PM_GPIO0_ATTR_DBG_NUM,
.attr_dbg=&pm_gpio0_attr_dbg[0],
.attr_flag=0,
#endif
};
static unsigned int __tcmdata pm_gpio1_reg_save[PM_SCU_GPIO_SWPORTC_NUM];

static struct rk2818_pm_soc_st __tcmdata pm_gpio1_ctr={
.reg_save=&pm_gpio1_reg_save[0],
.reg_base_addr=(unsigned int *)RK2818_GPIO1_BASE,
.reg_ctrbit=0,
.reg_num=PM_SCU_GPIO_SWPORTC_NUM,

#ifdef RK2818_PM_PRT_CHANGED_REG
.reg_ch=&pm_gpio1_reg_ch[0],
#endif
#if defined (CONFIG_RK2818_SOC_PM_DBG)
.attr_num=PM_GPIO1_ATTR_DBG_NUM,
.attr_dbg=&pm_gpio1_attr_dbg[0],
.attr_flag=0,
#endif
};

static unsigned int __tcmdata pm_savereg[PM_SAVE_REG_NUM];
static unsigned int __tcmdata savereg_ch[PM_SAVE_REG_NUM];

struct rk2818_pm_st __tcmdata rk2818_soc_pm={
.scu=(struct rk2818_pm_soc_st *)&pm_scu_ctr.reg_save,
.scu_tempreg=&pm_scu_temp_reg[0],
.general=(struct rk2818_pm_soc_st *)&pm_general_ctr.reg_save,
.gpio0=(struct rk2818_pm_soc_st *)&pm_gpio0_ctr.reg_save,
.gpio1=(struct rk2818_pm_soc_st *)&pm_gpio1_ctr.reg_save,
.save_reg=&pm_savereg[0],
.save_ch=&savereg_ch[0],
.scu_suspend=NULL,
.general_reg_suspend=NULL,
.set_suspendvol=NULL,
.resume_vol=NULL,
};

int __tcmfunc rk2818_socpm_set_gpio(unsigned int gpio,unsigned int output,unsigned int level)
{
	unsigned int *rk2818_gpio_reg;
	struct rk2818_pm_soc_st *gpioctr;
	unsigned int regoff;

	if(gpio>=RK2818GPIO_TOTAL)
		return -1;
	
	if(gpio<32)
	{
		gpioctr=rk2818_soc_pm.gpio0;
	}
	else
	{
		gpioctr=rk2818_soc_pm.gpio1;
		gpio-=32;
	}
	rk2818_gpio_reg=gpioctr->reg_base_addr;
	switch(gpio/8)
	{
		case 0:
			gpioctr->reg_ctrbit|=(0x1<<PM_GPIO_SWPORTA_DR)|(0x1<<PM_GPIO_SWPORTA_DDR);
			regoff=PM_GPIO_SWPORTA_DR;
			break;
		case 1:
			gpioctr->reg_ctrbit|=(0x1<<PM_GPIO_SWPORTB_DR)|(0x1<<PM_GPIO_SWPORTB_DDR);
			regoff=PM_GPIO_SWPORTB_DR;
			break;
		case 2:
			gpioctr->reg_ctrbit|=(0x1<<PM_GPIO_SWPORTC_DR)|(0x1<<PM_GPIO_SWPORTC_DDR);
			regoff=PM_GPIO_SWPORTC_DR;
			break;
		case 3:
			gpioctr->reg_ctrbit|=(0x1<<PM_GPIO_SWPORTD_DR)|(0x1<<PM_GPIO_SWPORTD_DDR);
			regoff=PM_GPIO_SWPORTD_DR;
		break;
		default:
			return -1;
	}
	gpio%=8;

	if(output)
	{
		rk2818_gpio_reg[regoff+1]|=(1<<gpio);//ddr set dir
		if(level==0)
			rk2818_gpio_reg[regoff]&=(~(1<<gpio));//ddr set value
		else
			rk2818_gpio_reg[regoff]|=(1<<gpio);
	}
	else
		rk2818_gpio_reg[regoff+1]&=(~(1<<gpio));//ddr set dir	
	return 0;
}

int __tcmfunc rk2818_socpm_gpio_pullupdown(unsigned int gpio,eGPIOPullType_t GPIOPullUpDown)
{
	unsigned int *general_reg;
	unsigned int regoff;
	int gpionum=0;

	if(gpio>=RK2818GPIO_TOTAL)
		return -1;
	general_reg=rk2818_soc_pm.general->reg_base_addr;
	regoff=PM_GPIO0_AB_PU_CON+gpio/16;
	rk2818_soc_pm.general->reg_ctrbit|=((0x1<<regoff));
	gpionum=gpio%16;
	general_reg[regoff]&=PM_BIT_CLEAR(gpionum*2,2)|PM_BIT_SET(gpionum*2,GPIOPullUpDown,2);
	return 0;
}

#if 0
static void __tcmlocalfunc noinline tcm_printch(char byte)
{
	unsigned int timeout;

	timeout = 0xffffffff;
	while (!(readl(RK2818_UART1_BASE + 0x7c) & (1<<1))) {
		if (!timeout--) 
			return;
	}
	writel(byte, RK2818_UART1_BASE);
	if (byte == '\n')
		tcm_printch('\r');
}

static void __tcmlocalfunc noinline tcm_printascii(const char *s)
{
	while (*s) {
		tcm_printch(*s);
		s++;
	}
}

static void __tcmlocalfunc noinline tcm_printhex(unsigned int hex)
{
	//int i = 8;
	tcm_printch('0');
	tcm_printch('x');
//	while (i--) 
}
#endif



static void __tcmfunc rk2818_pm_save_reg(unsigned int *save,unsigned int *source,int num)
{
	int i;
	if(save&&source)
	for(i=0;i<num;i++)
	{
		save[i]=source[i];
	}
		
}

static void __tcmfunc rk2818_soc_updata_scureg(unsigned int *tempdata,int regoff,int flag)
{
	
	if(rk2818_soc_pm.scu_suspend&&flag)
	{
		rk2818_soc_pm.scu_suspend(tempdata,regoff);
	}
	rk2818_soc_pm.scu->reg_base_addr[regoff]=*tempdata;
	rk2818_soc_pm.scu->reg_ctrbit|=(0x1<<regoff);
}

#if 0
static void __tcmfunc rk2818_soc_arm_stop(void)
{
	unsigned int *rk2818_scu_reg=rk2818_soc_pm.scu->reg_base_addr;
	rk2818_scu_reg[PM_SCU_MODE_CON]|=CPU_STOP_MODE;
	rk2818_scu_reg[PM_SCU_CPUPD]=0xdeedbabe;

}
#endif

static void __tcmfunc rk2818_soc_scu_suspend(void)
{
	unsigned int *rk2818_scu_reg=rk2818_soc_pm.scu->reg_base_addr;
	unsigned int *tempdata=&rk2818_soc_pm.scu_reg;

	
	*tempdata= SCU_GATE0CLK_ALL_DIS&(~DIS_ARM_CLK)&(~DIS_TIMER_CLK)
		&(~DIS_GPIO0_CLK)&(~DIS_GPIO1_CLK)&(~DIS_INTC_CLK)/*&(~DIS_UART0_CLK)&(~DIS_UART1_CLK)*/;
	rk2818_soc_updata_scureg(tempdata,PM_SCU_CLKGATE0_CON,1);
	tcm_udelay(1, 24);

	*tempdata=SCU_GATE1CLK_BASE_SET/*|EN_AXI_CLK&(~DIS_DDR_CLK)&(~DIS_DDR_HCLK)*/;
	rk2818_soc_updata_scureg(tempdata,PM_SCU_CLKGATE1_CON,1);
	tcm_udelay(1, 24);


      *tempdata=SCU_GATE2CLK_BASE_SET&(~DIS_ITCMBUS_CLK)&(~DIS_DTCM0BUS_CLK)&(~DIS_DTCM1BUS_CLK);
	rk2818_soc_updata_scureg(tempdata,PM_SCU_CLKGATE2_CON,1);
	tcm_udelay(1, 24);

	*tempdata=rk2818_scu_reg[PM_SCU_DPLL_CON] |DSPPLL_POERDOWN;    //dsp pll power down
	rk2818_soc_updata_scureg(tempdata,PM_SCU_DPLL_CON,1);
	tcm_udelay(1, 24);

	*tempdata=rk2818_scu_reg[PM_SCU_CPLL_CON] |CPLL_POERDOWN;    //dsp pll power down
	rk2818_soc_updata_scureg(tempdata,PM_SCU_CPLL_CON,1);
	tcm_udelay(1, 24);

       
	rk2818_scu_reg[PM_SCU_PMU_CON] |=LCDC_POWER_DOWN;
	*tempdata=rk2818_scu_reg[PM_SCU_PMU_CON] |DSP_POWER_DOWN;
	rk2818_soc_updata_scureg(tempdata,PM_SCU_PMU_CON,1);



	*tempdata=rk2818_scu_reg[PM_SCU_MODE_CON] &CPU_SLOW_MODE;	//general slow mode
	rk2818_soc_updata_scureg(tempdata,PM_SCU_MODE_CON,1);

	*tempdata=rk2818_scu_reg[PM_SCU_APLL_CON] |ARMPLL_BYPASSMODE;//enable arm pll bypass
	*tempdata=rk2818_scu_reg[PM_SCU_APLL_CON] | ARMPLL_POERDOWN;	//arm pll power down
	rk2818_soc_updata_scureg(tempdata,PM_SCU_APLL_CON,1);
	tcm_udelay(2, 24);

	*tempdata=(rk2818_scu_reg[PM_SCU_CLKSEL0_CON]&PM_BIT_CLEAR(CLKSEL0_HCLK,2)&PM_BIT_CLEAR(CLKSEL0_PCLK,2))
		|PM_BIT_SET(CLKSEL0_HCLK,CLK_ARM1_H1,2)|PM_BIT_SET(CLKSEL0_PCLK,CLK_HCLK1_P1,2);
	rk2818_soc_updata_scureg(tempdata,PM_SCU_CLKSEL0_CON,1);
	tcm_udelay(2, 24);

	
	*tempdata=(rk2818_scu_reg[PM_CLKSEL2_CON]&(~0xf))|0xF;
	rk2818_soc_updata_scureg(tempdata,PM_CLKSEL2_CON,1);

	tcm_udelay(2, 24);

	//*tempdata=rk2818_scu_reg[PM_SCU_PMU_CON] |DDR_POWER_DOWN;
	//rk2818_soc_updata_scureg(tempdata,PM_SCU_PMU_CON,0);


	//*tempdata=rk2818_scu_reg[PM_SCU_SOFTRST_CON]|(1<<RST_DDR_BUS)|(1<<RST_DDR_CORE_LOGIC);//RST_ALL&(~(1<<RST_ARM));
	//rk2818_soc_updata_scureg(tempdata,PM_SCU_SOFTRST_CON,1);
}



#if 0


static void __tcmfunc rk2818_soc_general_reg_suspend(void)
{
	struct rk2818_pm_soc_st *general=rk2818_soc_pm.general;
	struct rk2818_pm_soc_st *gpio0=rk2818_soc_pm.gpio0;
	struct rk2818_pm_soc_st *gpio1=rk2818_soc_pm.gpio1;

	unsigned int *rk2818_general_reg=general->reg_ch;
	unsigned int *rk2818_gpio0_reg=gpio0->reg_ch;
	unsigned int *rk2818_gpio1_reg=gpio1->reg_ch;

	int i;

	#if 1
	general->reg_ctrbit|=(0x1<<PM_GPIO0_AB_PU_CON);
	rk2818_general_reg[PM_GPIO0_AB_PU_CON] =GPIO0_AB_NORMAL;
	
	general->reg_ctrbit|=(0x1<<PM_GPIO0_CD_PU_CON);
	rk2818_general_reg[PM_GPIO0_CD_PU_CON] = GPIO0_CD_NORMAL;
	
	general->reg_ctrbit|=(0x1<<PM_GPIO1_AB_PU_CON);
	rk2818_general_reg[PM_GPIO1_AB_PU_CON] = GPIO1_AB_NORMAL;
	
	general->reg_ctrbit|=(0x1<<PM_GPIO1_CD_PU_CON);
	rk2818_general_reg[PM_GPIO1_CD_PU_CON] = GPIO1_CD_NORMAL;
	#endif
	general->reg_ctrbit|=(0x1<<PM_IOMUX_A_CON);
	general->reg_ctrbit|=(0x1<<PM_IOMUX_B_CON);

	if(rk2818_soc_pm.general_reg_callback)
	{
		rk2818_soc_pm.general_reg_callback(general->reg_ch);
	}

	rk2818_pm_save_reg(general->reg_base_addr,general->reg_ch,general->reg_num);

	rk2818_pm_save_reg(gpio0->reg_base_addr,gpio0->reg_ch,gpio0->reg_num);
	rk2818_pm_save_reg(gpio1->reg_base_addr,gpio1->reg_ch,gpio1->reg_num);

}
#endif
static void rk2818_pm_reg_print(unsigned int *pm_save_reg,unsigned int *pm_ch_reg,int num,char *name)
{

	 int i;

#ifdef RK2818_PM_PRT_ORIGINAL_REG
	printk("***the follow inf is %s original reg***\n",name);
	for(i=0;i<num;i++)
	{
	    printk(" %d,%x",i,pm_save_reg[i]);
	}
	printk("\n");
#endif

#ifdef RK2818_PM_PRT_CHANGED_REG
	printk("***the follow inf is %s changed reg***\n",name);
	for(i=0;i<num;i++)
	{
	    printk(" %d,%x",i,pm_ch_reg[i]);
	}
	printk("\n");
#endif
}
static void __tcmfunc rk2818_soc_resume(unsigned int *pm_save_reg,unsigned int *base_add,u16 regbit,int num)
{
	int i;
	for(i=0;i<num;i++)
	{
	       if((regbit>>i)&0x0001)
		base_add[i]=pm_save_reg[i];
	}
}
void __tcmfunc rk2818_socpm_suspend(void)
{
	tcm_udelay(1, 24); //DRVDelayUs(100); 
	rk2818_soc_scu_suspend();
	tcm_udelay(1, 24); //DRVDelayUs(100); 
	
	if(rk2818_soc_pm.general_reg_suspend)
	{
		rk2818_soc_pm.general_reg_suspend();
	}
#ifdef RK2818_PM_PRT_CHANGED_REG

	rk2818_pm_save_reg(rk2818_soc_pm.scu->reg_ch,rk2818_soc_pm.scu->reg_base_addr,
	rk2818_soc_pm.scu->reg_num);

	rk2818_pm_save_reg(rk2818_soc_pm.general->reg_ch,rk2818_soc_pm.general->reg_base_addr,
	rk2818_soc_pm.general->reg_num);

	rk2818_pm_save_reg(rk2818_soc_pm.gpio0->reg_ch,rk2818_soc_pm.gpio0->reg_base_addr,
	rk2818_soc_pm.gpio0->reg_num);
	
	rk2818_pm_save_reg(rk2818_soc_pm.gpio1->reg_ch,rk2818_soc_pm.gpio1->reg_base_addr,
	rk2818_soc_pm.gpio1->reg_num);
#endif
	//rk2818_soc_arm_stop();
		
}
void __tcmfunc rk2818_socpm_resume(void)
{
		tcm_udelay(1, 24); //DRVDelayUs(100); 
		rk2818_soc_resume(rk2818_soc_pm.scu->reg_save,rk2818_soc_pm.scu->reg_base_addr,
			rk2818_soc_pm.scu->reg_ctrbit,rk2818_soc_pm.scu->reg_num);
		tcm_udelay(2, 24);
		if(rk2818_soc_pm.general_reg_suspend)
		{

			rk2818_soc_resume(rk2818_soc_pm.general->reg_save,rk2818_soc_pm.general->reg_base_addr,
				rk2818_soc_pm.general->reg_ctrbit,rk2818_soc_pm.general->reg_num);

			rk2818_soc_resume(rk2818_soc_pm.gpio0->reg_save,rk2818_soc_pm.gpio0->reg_base_addr,
				rk2818_soc_pm.gpio0->reg_ctrbit,rk2818_soc_pm.gpio0->reg_num);
			
			rk2818_soc_resume(rk2818_soc_pm.gpio1->reg_save,rk2818_soc_pm.gpio1->reg_base_addr,
				rk2818_soc_pm.gpio1->reg_ctrbit,rk2818_soc_pm.gpio1->reg_num);
		}

	}

void __tcmfunc rk2818_socpm_suspend_first(void)
{
	tcm_udelay(1, 24);
	rk2818_pm_save_reg(rk2818_soc_pm.scu->reg_save,rk2818_soc_pm.scu->reg_base_addr,rk2818_soc_pm.scu->reg_num);
	//rk2818_pm_save_reg(rk2818_soc_pm.scu_tempreg,rk2818_soc_pm.scu->reg_base_addr,rk2818_soc_pm.scu->reg_num);

	rk2818_pm_save_reg(rk2818_soc_pm.general->reg_save,rk2818_soc_pm.general->reg_base_addr,rk2818_soc_pm.general->reg_num);
	rk2818_pm_save_reg(rk2818_soc_pm.gpio0->reg_save,rk2818_soc_pm.gpio0->reg_base_addr,rk2818_soc_pm.gpio0->reg_num);
	rk2818_pm_save_reg(rk2818_soc_pm.gpio1->reg_save,rk2818_soc_pm.gpio1->reg_base_addr,rk2818_soc_pm.gpio1->reg_num);
	rk2818_soc_pm.scu->reg_ctrbit=0;
	rk2818_soc_pm.general->reg_ctrbit=0;
	rk2818_soc_pm.gpio0->reg_ctrbit=0x6db;
	rk2818_soc_pm.gpio1->reg_ctrbit=0x6db;
	
	//rk2818_soc_pm.save_reg[0]=rk2818_ddr_reg[82];
	//rk2818_ddr_reg[82]=rk2818_ddr_reg[82]&(~(0xffff))&(~(0xf<<20));
	
		if(rk2818_soc_pm.set_suspendvol)
			rk2818_soc_pm.set_suspendvol();


}

void __tcmfunc rk2818_socpm_resume_first(void)
{
	//unsigned int *rk2818_ddr_reg=(unsigned int *)RK2818_SDRAMC_BASE;
		if(rk2818_soc_pm.resume_vol)
			rk2818_soc_pm.resume_vol();

}


void rk2818_socpm_print(void)
{
	rk2818_pm_reg_print(rk2818_soc_pm.scu->reg_save,rk2818_soc_pm.scu->reg_ch,
		rk2818_soc_pm.scu->reg_num,"scu");
	rk2818_pm_reg_print(rk2818_soc_pm.general->reg_save,rk2818_soc_pm.general->reg_ch,
		rk2818_soc_pm.general->reg_num,"general_general");
	rk2818_pm_reg_print(rk2818_soc_pm.gpio0->reg_save,rk2818_soc_pm.gpio0->reg_ch,
		rk2818_soc_pm.gpio0->reg_num,"gpio0");
	rk2818_pm_reg_print(rk2818_soc_pm.gpio1->reg_save,rk2818_soc_pm.gpio1->reg_ch,
		rk2818_soc_pm.gpio1->reg_num,"gpio1");
}



void rk2818_socpm_int(pm_scu_suspend scu,pm_general_reg_suspend general,
	pm_set_suspendvol setvol,pm_resume_vol resumevol)
{
		rk2818_soc_pm.scu_suspend=scu;
		rk2818_soc_pm.general_reg_suspend=general;
		rk2818_soc_pm.set_suspendvol=setvol;
		rk2818_soc_pm.resume_vol=resumevol;

}






#if defined (CONFIG_RK2818_SOC_PM_DBG)

static bool check_attr_dbg_value(struct rk2818_pm_attr_dbg_st *attr)
{
	if((attr->flag!=PM_DBG_SET_ONCE)&&(attr->flag!=PM_DBG_SET_ALWAY))
		return false;
	if(attr->bitsnum==0||attr->bitsnum>32)
		return false;

	if(attr->regbits_off>31)
		return false;
	if((attr->regbits_off+attr->bitsnum)>32)
		return false;

	return true;
}


#if 0
static void setreg_form_socpm_attr(unsigned int *reg,int regoff,struct rk2818_pm_soc_st *soc)
{
	struct rk2818_pm_attr_dbg_st *attr=soc->attr_dbg;
	int i;
	for(i=0;i<soc->attr_num;i++)
	{
	
		if(check_attr_dbg_value(attr))
		{
			if(attr->regoff==regoff)
			{

				*reg=(*reg)&PM_BIT_CLEAR(attr->regbits_off,attr->bitsnum)|PM_BIT_SET(attr->regbits_off,attr->value,attr->bitsnum);

			}
		}
	}

}

static void rk2818_socpm_attr_int(void)
{
	struct rk2818_pm_soc_st *scu=rk2818_soc_pm.scu;
	struct rk2818_pm_soc_st *general=rk2818_soc_pm.general;
	struct rk2818_pm_soc_st *gpio0=rk2818_soc_pm.gpio0;
	struct rk2818_pm_soc_st *gpio1=rk2818_soc_pm.gpio1;



	memset(scu->attr_dbg,0,sizeof(struct rk2818_pm_attr_dbg_st)*scu->attr_num);
	memset(general->attr_dbg,0,sizeof(struct rk2818_pm_attr_dbg_st)*general->attr_num);
	memset(gpio0->attr_dbg,0,sizeof(struct rk2818_pm_attr_dbg_st)*gpio0->attr_num);
	memset(gpio1->attr_dbg,0,sizeof(struct rk2818_pm_attr_dbg_st)*gpio1->attr_num);
}

#endif
static u32 rk2818_socpm_attr_atoh(const unsigned char *in, unsigned int len)
{
	u32 sum = 0;
	unsigned int mult = 1;
	unsigned char c;

	while (len) {
		c = in[len - 1];
		if ((c >= '0') && (c <= '9'))
			sum += mult * (c - '0');
		else if ((c >= 'A') && (c <= 'F'))
			sum += mult * (c - ('A' - 10));
		else if ((c >= 'a') && (c <= 'f'))
			sum += mult * (c - ('a' - 10));
		mult *= 16;
		--len;
	}
	return sum;
}


static bool rk2818_socpm_attr_is_number(char *p)
{
	if((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f')||(*p>='A'&&*p<='F'))
		return true;
	else
		return false;

}

#if 0
static bool rk2818_socpm_attr_check_value(unsigned int *info)
{
	if((info[PM_DBG_USER_FLAG]!=PM_DBG_SET_ONCE)&&(info[PM_DBG_USER_FLAG]!=PM_DBG_SET_ALWAY))
		return false;
	if(info[PM_DBG_USER_BITS_NUM]==0||info[PM_DBG_USER_BITS_NUM]>32)
		return false;
	if((info[PM_DBG_USER_BITS_NUM]+info[PM_DBG_USER_BITS_OFF])>31)
		return false;

	return true;
}
#endif

static bool rk2818_socpm_attr_reset_samesituation(struct rk2818_pm_attr_dbg_st *dbg,int num,unsigned int *info)
{

	int i;
	for (i=0;i<num;i++)
	{

		if((dbg[i].regoff==info[PM_DBG_USER_REGOFF])&&
			((dbg[i].regbits_off==info[PM_DBG_USER_BITS_OFF])))
		{

			dbg[i].value=info[PM_DBG_USER_VALUE];
			dbg[i].bitsnum=info[PM_DBG_USER_BITS_NUM];
			dbg[i].flag=info[PM_DBG_USER_FLAG];
				
				return true;
		}
	}
	return false;
}


static bool rk2818_socpm_attr_set_value(struct rk2818_pm_attr_dbg_st *attr_dbg,int num,unsigned int *info)
{
	int i;
	if(rk2818_socpm_attr_reset_samesituation(attr_dbg,num,info))
		return true;
	for (i=0;i<num;i++)
	{

		if(attr_dbg[i].flag==PM_DBG_NOT_SET)
		{
			attr_dbg[i].regoff=info[PM_DBG_USER_REGOFF];
			attr_dbg[i].regbits_off=info[PM_DBG_USER_BITS_OFF];
			attr_dbg[i].value=info[PM_DBG_USER_VALUE];
			attr_dbg[i].bitsnum=info[PM_DBG_USER_BITS_NUM];
			attr_dbg[i].flag=info[PM_DBG_USER_FLAG];
			return true;
		}
	}
	return false;
}

static bool rk2818_socpm_attr_parse_settinginf(struct rk2818_pm_soc_st *socpm,const char *buf, size_t n)
{
	const char *p=buf;
	char *e;
	//int len;
 	int i=0;
	unsigned int info[PM_DBG_USER_END];//regoff bitsoff value bitsnum user once
 	//int oneline=-1;
	
	printk("3x pm dbg %s\n",p);

	memset(&info[0],0,4*PM_DBG_USER_END);


	for(;p<(buf+n);p++)
	{
		if(*p=='s'||*p=='S')
		{
			if(*p!=' ')
			continue;
			
			while(*p==' ')
			{
				if(p>=(buf+n))
					return true;
				p++;
			}
			for(i=0;i<PM_DBG_USER_END;)
			{
				if(rk2818_socpm_attr_is_number((char*)p))
				{
					e= memchr(p, ' ', n-(p-buf));
					if(e==NULL)
					{
						return true;
					}
					if(e>=(buf+n))
						return true;
					info[i]=rk2818_socpm_attr_atoh(p,e-p);
					i++;
					p=e;
					if(i==(PM_DBG_USER_END-1))
					{
						if(rk2818_socpm_attr_set_value(socpm->attr_dbg,socpm->attr_num,&info[0])==true)
							return true;

						printk("value is%x, regoff is%x bitsoff is%x bitsnum is%x ctr is%x\n",  
						info[PM_DBG_USER_VALUE],info[PM_DBG_USER_REGOFF],info[PM_DBG_USER_BITS_OFF], 
						info[PM_DBG_USER_BITS_NUM], info[PM_DBG_USER_FLAG]);

						memset(&info[0],0,sizeof(int)*PM_DBG_USER_END);
					}
				}
				else
				{
					if((*p==' '))
					{
						while(*p==' ')
						{
							if(p>=(buf+n))
								return false;
							p++;
						}
					continue;
					}
					else
					{
						break;
					}
				}
				
			}
		}
		
	}
	
return true;
}


#if 0
static char * rk2818_socpm_attr_show_userinfo(struct rk2818_pm_soc_st *socpm,unsigned char regoff,char *buf)
{
	int i;
	struct rk2818_pm_attr_dbg_st *attr_dbg;
	for(i=0;i<socpm->attr_num;i++)
	{
		if((attr_dbg[i].regoff==regoff)&&(attr_dbg[i].flag!=PM_ATTR_NO_CTR)&&attr_dbg[i].bitsnum)
			buf += sprintf(buf, "value is%x, regoff is%x bitsoff is%x bitsnum is%x ctr is%x\n",  
			attr_dbg[i].value,attr_dbg[i].regoff,attr_dbg[i].regbits_off, attr_dbg[i].bitsnum, attr_dbg[i].flag);
	}

	return buf;


}

static ssize_t rk2818_socpm_attr_show_info(struct rk2818_pm_soc_st *socpm,char *name,char *buf)
{

	char *s = buf;
	int i;
	s += sprintf(s, "this is the %s reg value,when system enter suspend lately\n",name);
	for(i=0;i<socpm->reg_num;i++)
	{
		s += sprintf(s, "reg[%d] is %x,user seting is in follow\n", i,socpm->reg_ch[i]);
		s=rk2818_socpm_attr_show_userinfo(socpm,s,i);

	}
	
	if (s != buf)
		/* convert the last space to a newline */
		*(s-1) = '\n';
	return (s - buf);


}


ssize_t rk2818_socpm_attr_show(int type,char *buf)
{
	struct rk2818_pm_soc_st *socpm;
	char name[4][8]={"scu","general","gpio0","gpio1"};

	switch(type)
	{
		case 0:
			socpm=rk2818_soc_pm.scu;
		break;
		case 1:
			socpm=rk2818_soc_pm.general;
		break;
			case 2:
			socpm=rk2818_soc_pm.gpio0;
		break;
			case 3:
			socpm=rk2818_soc_pm.gpio1;
		break;
		default:
			return false;



	}
	return rk2818_socpm_attr_show_info(socpm,name[type],buf);
	
}
#endif



bool rk2818_socpm_attr_store(int type,const char *buf, size_t n)
{
	struct rk2818_pm_soc_st *socpm;

	switch(type)
	{
		case 0:
			socpm=rk2818_soc_pm.scu;
		break;
		case 1:
			socpm=rk2818_soc_pm.general;
		break;
			case 2:
			socpm=rk2818_soc_pm.gpio0;
		break;
			case 3:
			socpm=rk2818_soc_pm.gpio1;
		break;
		default:
			return false;



	}
	return rk2818_socpm_attr_parse_settinginf(socpm,buf,n);
	
}
#endif

#if 0
void rk2818_pm_updata_scu_reg(int regoff)
{
	if(0)//(rk2818_soc_pm.scu_suspend)
	{
		rk2818_soc_pm.scu_suspend(rk2818_soc_pm.scu_tempreg[regoff],regoff);
	}
	rk2818_soc_pm.scu->reg_base_addr[regoff]=rk2818_soc_pm.scu_tempreg[regoff];
	rk2818_soc_pm.scu->reg_ctrbit|=(0x1<<regoff);
		tcm_printascii("3x scu");

}

static void __tcmfunc rk2818_soc_scu_suspend(void)
{
	//unsigned int *rk2818_scu_reg=scu->reg_base_addr;
	unsigned int *rk2818_scu_reg=rk2818_soc_pm.scu_tempreg;


		
	rk2818_scu_reg[PM_SCU_CLKGATE0_CON] = SCU_GATE0CLK_ALL_DIS&(~DIS_ARM_CLK)&(~DIS_TIMER_CLK)
		&(~DIS_GPIO0_CLK)&(~DIS_GPIO1_CLK)&(~DIS_INTC_CLK)/*&(~DIS_UART0_CLK)&(~DIS_UART1_CLK)*/;
	rk2818_pm_updata_scu_reg(PM_SCU_CLKGATE0_CON);
	
	rk2818_scu_reg[PM_SCU_CLKGATE1_CON] =SCU_GATE1CLK_BASE_SET/*|EN_AXI_CLK&(~DIS_DDR_CLK)&(~DIS_DDR_HCLK)*/;
	rk2818_pm_updata_scu_reg(PM_SCU_CLKGATE1_CON);


        rk2818_scu_reg[PM_SCU_CLKGATE2_CON] =SCU_GATE2CLK_BASE_SET&(~DIS_ITCMBUS_CLK)&(~DIS_DTCM0BUS_CLK)&(~DIS_DTCM1BUS_CLK);
	rk2818_pm_updata_scu_reg(PM_SCU_CLKGATE2_CON);



	rk2818_scu_reg[PM_SCU_CLKSEL0_CON]&=PM_BIT_CLEAR(CLKSEL0_HCLK,2)&PM_BIT_CLEAR(CLKSEL0_PCLK,2)
		|PM_BIT_SET(CLKSEL0_HCLK,CLK_ARM1_H1,2)|PM_BIT_SET(CLKSEL0_PCLK,CLK_HCLK1_P1,2);
	rk2818_pm_updata_scu_reg(PM_SCU_CLKSEL0_CON);

	rk2818_scu_reg[PM_SCU_DPLL_CON] |= DSPPLL_POERDOWN;    //dsp pll power down
	rk2818_pm_updata_scu_reg(PM_SCU_DPLL_CON);
	
	rk2818_scu_reg[PM_SCU_CPLL_CON] |= CPLL_POERDOWN;    //dsp pll power down
	rk2818_pm_updata_scu_reg(PM_SCU_CPLL_CON);

	rk2818_scu_reg[PM_SCU_PMU_CON] |=LCDC_POWER_DOWN;
	rk2818_scu_reg[PM_SCU_PMU_CON] |=DSP_POWER_DOWN;
	
	
		rk2818_scu_reg[PM_SCU_MODE_CON] &= CPU_SLOW_MODE;	//general slow mode
	rk2818_pm_updata_scu_reg(PM_SCU_MODE_CON);

	rk2818_scu_reg[PM_SCU_APLL_CON] |= ARMPLL_BYPASSMODE;//enable arm pll bypass
	rk2818_scu_reg[PM_SCU_APLL_CON] |= ARMPLL_POERDOWN;	//arm pll power down
	rk2818_pm_updata_scu_reg(PM_SCU_APLL_CON);
	
	rk2818_scu_reg[PM_CLKSEL2_CON] =(rk2818_scu_reg[PM_CLKSEL2_CON]&(~0xf))|0xF;
	rk2818_pm_updata_scu_reg(PM_CLKSEL2_CON);





	rk2818_pm_updata_scu_reg(PM_SCU_PMU_CON);
	//rk2818_scu_reg[PM_SCU_PMU_CON] |=DDR_POWER_DOWN;
	//rk2818_pm_updata_scu_reg(PM_SCU_PMU_CON);

	//scu->reg_ctrbit|=(0x1<<PM_SCU_SOFTRST_CON);
	//rk2818_scu_reg[PM_SCU_SOFTRST_CON]|=(1<<RST_DDR_BUS)|(1<<RST_DDR_CORE_LOGIC);//RST_ALL&(~(1<<RST_ARM));
	//rk2818_pm_updata_scu_reg(PM_SCU_SOFTRST_CON);
}
#endif

