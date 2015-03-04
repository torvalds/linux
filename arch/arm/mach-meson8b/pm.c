/*
 * Meson Power Management Routines
 *
 * Copyright (C) 2010 Amlogic, Inc. http://www.amlogic.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/fs.h>

#include <asm/cacheflush.h>
#include <asm/delay.h>
#include <asm/uaccess.h>

#include <mach/pm.h>
#include <mach/am_regs.h>
#include <plat/sram.h>
#include <mach/power_gate.h>
#include <mach/gpio.h>
//#include <mach/pctl.h>
#include <mach/clock.h>
#include <plat/regops.h>
#include <plat/io.h>
#include <plat/wakeup.h>
#include <linux/of.h>
#ifdef CONFIG_MESON_TRUSTZONE
#include <mach/meson-secure.h>
#endif

#ifdef CONFIG_SUSPEND_WATCHDOG
#include <mach/watchdog.h>
#endif /* CONFIG_SUSPEND_WATCHDOG */

#include <mach/mod_gate.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
static struct early_suspend early_suspend;
static int early_suspend_flag = 0;
#endif

#define CONFIG_AO_TRIG_CLK 1
#ifdef CONFIG_AO_TRIG_CLK
#include "arc_trig_clk.h"
#endif

#define ON  1
#define OFF 0

static unsigned int  cec_config;       // 4 bytes: use to control cec switch on/off,distinguish between Mbox and Tablet. bit[0]:1:Mbox; 0:Tablet
static struct meson_pm_config *pdata;
static struct device_node *cec_np = NULL;

#define CLK(addr)  \
{ \
	.clk_name=#addr, \
	.clk_addr=addr, \
	.clk_flag=0, \
}

struct clk_desc{
    char* clk_name;
    unsigned clk_addr;
    unsigned clk_flag;
} ;

struct clk_desc clks[] = {
	CLK(P_HHI_MPEG_CLK_CNTL),
};

static void uart_change_buad(unsigned reg,unsigned clk_rate){
	aml_clr_reg32_mask(reg, 0x7FFFFF);
	aml_set_reg32_bits(reg, (((clk_rate / (115200 * 4)) - 1) & 0x7fffff)|(1<<23), 0, 24);
}
static void wait_uart_empty(void)
{
	do{
		udelay(100);
	}while((aml_read_reg32(P_AO_UART_STATUS) & (1<<22)) == 0);	
}
struct clk* clk81;
struct clk* clkxtal;

void clk_switch(int flag)
{
	int i;
	int clk_count=sizeof(clks)/sizeof(clks[0]);
	unsigned int uart_rate_clk;
	if (flag) {
		for (i = clk_count - 1; i >= 0; i--) {
			if (clks[i].clk_flag) {
				if (clks[i].clk_addr == P_HHI_MPEG_CLK_CNTL) {
					uart_rate_clk = clk_get_rate(clk81);
					wait_uart_empty();
					aml_set_reg32_mask(clks[i].clk_addr,(1<<7));//gate on pll
					udelay(10);
					aml_set_reg32_mask(clks[i].clk_addr,(1<<8));//switch to pll
					udelay(10);
					uart_change_buad(P_AO_UART_REG5,uart_rate_clk);
					clks[i].clk_flag = 0;
				}
                	printk(KERN_INFO "clk %s(%x) on\n", clks[i].clk_name, ((clks[i].clk_addr)&0xffff)>>2);
			}
		}
	} else {
	        for (i = 0; i < clk_count; i++) {
	 		if (clks[i].clk_addr == P_HHI_MPEG_CLK_CNTL) {
				if (aml_read_reg32(clks[i].clk_addr) & (1 << 8)) {
					uart_rate_clk = clk_get_rate(clkxtal);
					clks[i].clk_flag  = 1;
					wait_uart_empty();
					aml_clr_reg32_mask(clks[i].clk_addr, (1 << 8)); // gate off from pll
					udelay(10);
					aml_clr_reg32_mask(clks[i].clk_addr, (1 << 7)); // switch to 24M
					udelay(10);
					uart_change_buad(P_AO_UART_REG5,uart_rate_clk);
					clks[i].clk_flag=1;
				}
			} 
			if (clks[i].clk_flag) {
				printk(KERN_INFO "clk %s(%x) off\n", clks[i].clk_name, ((clks[i].clk_addr)&0xffff)>>2);
			}
		}
	}
}
EXPORT_SYMBOL(clk_switch);


typedef struct {
    char name[32];
    unsigned reg_addr;
    unsigned set_bits;
    unsigned clear_bits;
    unsigned reg_value;
    unsigned enable; // 1:cbus 2:apb 3:ahb 0:disable
} analog_t;

// ... Meson8 baby don't have LED_PWM_REG0
#define ANALOG_COUNT    1
static analog_t analog_regs[ANALOG_COUNT] = {
    {"SAR_ADC",             P_SAR_ADC_REG3,       1 << 28, (1 << 30) | (1 << 21),    0,  1},
};
/*
#define ANALOG_COUNT    2
static analog_t analog_regs[ANALOG_COUNT] = {
    {"SAR_ADC",             P_SAR_ADC_REG3,       1 << 28, (1 << 30) | (1 << 21),    0,  1},
#ifdef ADJUST_CORE_VOLTAGE
    {"LED_PWM_REG0",        P_LED_PWM_REG0,       1 << 13,          1 << 12,              0,  0}, // needed for core voltage adjustment, so not off
#else
    {"LED_PWM_REG0",        P_LED_PWM_REG0,       1 << 13,          1 << 12,              0,  1},
#endif
};
*/

void analog_switch(int flag)
{
    int i;
    unsigned reg_value = 0;

    if (flag) {
        printk(KERN_INFO "analog on\n");
        aml_set_reg32_mask(P_AM_ANALOG_TOP_REG0, 1 << 1); // set 0x206e bit[1] 1 to power on top analog
        for (i = 0; i < ANALOG_COUNT; i++) {
            if (analog_regs[i].enable && (analog_regs[i].set_bits || analog_regs[i].clear_bits)) {
                if (analog_regs[i].enable == 1) {
                		aml_write_reg32(analog_regs[i].reg_addr, analog_regs[i].reg_value);
                } else if (analog_regs[i].enable == 2) {
                    aml_write_reg32(analog_regs[i].reg_addr, analog_regs[i].reg_value);
                } else if (analog_regs[i].enable == 3) {
                    aml_write_reg32(analog_regs[i].reg_addr, analog_regs[i].reg_value);
                }
            }
        }
    } else {
        printk(KERN_INFO "analog off\n");
        for (i = 0; i < ANALOG_COUNT; i++) {
            if (analog_regs[i].enable && (analog_regs[i].set_bits || analog_regs[i].clear_bits)) {
                if (analog_regs[i].enable == 1) {
                    analog_regs[i].reg_value = aml_read_reg32(analog_regs[i].reg_addr);
                    printk("%s(0x%x):0x%x", analog_regs[i].name, analog_regs[i].reg_addr, analog_regs[i].reg_value);
                    if (analog_regs[i].clear_bits) {
                        aml_clr_reg32_mask(analog_regs[i].reg_addr, analog_regs[i].clear_bits);
                        printk(" & ~0x%x", analog_regs[i].clear_bits);
                    }
                    if (analog_regs[i].set_bits) {
                        aml_set_reg32_mask(analog_regs[i].reg_addr, analog_regs[i].set_bits);
                        printk(" | 0x%x", analog_regs[i].set_bits);
                    }
                    reg_value = aml_read_reg32(analog_regs[i].reg_addr);
                    printk(" = 0x%x\n", reg_value);
                } else if (analog_regs[i].enable == 2) {
                    analog_regs[i].reg_value = aml_read_reg32(analog_regs[i].reg_addr);
                    printk("%s(0x%x):0x%x", analog_regs[i].name, analog_regs[i].reg_addr, analog_regs[i].reg_value);
                    if (analog_regs[i].clear_bits) {
                    		aml_clr_reg32_mask(analog_regs[i].reg_addr, analog_regs[i].clear_bits);
                        printk(" & ~0x%x", analog_regs[i].clear_bits);
                    }
                    if (analog_regs[i].set_bits) {
                    		aml_set_reg32_mask(analog_regs[i].reg_addr, analog_regs[i].set_bits);
                        printk(" | 0x%x", analog_regs[i].set_bits);
                    }
                    reg_value = aml_read_reg32(analog_regs[i].reg_addr);
                    printk(" = 0x%x\n", reg_value);
                } else if (analog_regs[i].enable == 3) {
                    analog_regs[i].reg_value = aml_read_reg32(analog_regs[i].reg_addr);
                    printk("%s(0x%x):0x%x", analog_regs[i].name, analog_regs[i].reg_addr, analog_regs[i].reg_value);
                    if (analog_regs[i].clear_bits) {
                        aml_clr_reg32_mask(analog_regs[i].reg_addr, analog_regs[i].clear_bits);
                        printk(" & ~0x%x", analog_regs[i].clear_bits);
                    }
                    if (analog_regs[i].set_bits) {
                        aml_set_reg32_mask(analog_regs[i].reg_addr, analog_regs[i].set_bits);
                        printk(" | 0x%x", analog_regs[i].set_bits);
                    }
                    reg_value = aml_read_reg32(analog_regs[i].reg_addr);
                    printk(" = 0x%x\n", reg_value);
                }
            }
        }
        aml_clr_reg32_mask(P_AM_ANALOG_TOP_REG0, 1 << 1); // set 0x206e bit[1] 0 to shutdown top analog
    }
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void meson_system_early_suspend(struct early_suspend *h)
{
	if (!early_suspend_flag) {
	printk(KERN_INFO "%s\n",__func__);
	if (pdata->set_exgpio_early_suspend) {
		pdata->set_exgpio_early_suspend(OFF);
	}
		//early_clk_switch(OFF);
		//early_power_gate_switch(OFF);
		early_suspend_flag = 1;
	}
}

static void meson_system_late_resume(struct early_suspend *h)
{
	if (early_suspend_flag) {
		//early_power_gate_switch(ON);
		//early_clk_switch(ON);
		early_suspend_flag = 0;
		printk(KERN_INFO "%s\n",__func__);
	}
}
#endif

#ifdef CONFIG_AO_TRIG_CLK
int run_arc_program(void)
{
//	int i;
	unsigned vaddr2,v;
	unsigned* pbuffer;
	vaddr2 = IO_SRAM_BASE;
	
	if(cec_config & 0x1)// 4 bytes: use to control cec switch on/off,distinguish between Mbox and Tablet. bit[0]:1:Mbox; 0:Tablet
    {
    	aml_write_reg32(P_AO_REMAP_REG0,0);
    	udelay(10);
    	pbuffer = (unsigned*)vaddr2;
    
    	memcpy(pbuffer,arc_code,sizeof(arc_code));//need not flush cache for sram. Sram locates at io mapping.
    
//    	for(i = 0; i<sizeof(arc_code)/4; i+=4,pbuffer+=4)
//    		printk(" 0x%x	0x%x	0x%x	0x%x \n",*(pbuffer),*(pbuffer+1),*(pbuffer+2),*(pbuffer+3));
        v = ((IO_SRAM_PHY_BASE & 0xFFFFF)>>12);
        aml_write_reg32(P_AO_SECURE_REG0, v<<8 | aml_read_reg32(P_AO_SECURE_REG0)); //TEST_N : 1->output mode; 0->input mode
    
        aml_write_reg32(P_AO_RTI_STATUS_REG1, 0);//clean status
    
//    	writel(0x200,P_AO_CPU_CNTL);//halt first
    	aml_write_reg32(P_RESET2_REGISTER, aml_read_reg32(P_RESET2_REGISTER)|(1<<13));//reset AO_CPU
    
    	udelay(10);
    
//      enable arc
        aml_write_reg32(P_AO_CPU_CNTL, 0x0c900101);//remap is right?
    
    	udelay(20);
    	if(aml_read_reg32(P_AO_RTI_STATUS_REG1) == 0xeeeeaaaa){
    		printk("AO cpu runs ok.\n");
    		return 0;
    	}
    	else{
    		printk("AO cpu runs fail. 0x%x\n",aml_read_reg32(P_AO_RTI_STATUS_REG1));
    		return -1;
    	}
    }
    return -1;
}

int stop_ao_cpu(void)
{
	if(cec_config & 0x1)// 4 bytes: use to control cec switch on/off,distinguish between Mbox and Tablet. bit[0]:1:Mbox; 0:Tablet
    {
    	aml_write_reg32(P_AO_RTI_STATUS_REG1, 0xddddeeee); //ask ao to halt.
		udelay(40);
    	if(aml_read_reg32(P_AO_RTI_STATUS_REG1) == 0x0){
    		printk("AO cpu stop ok.\n");
    		return 0;
    	}
    	else{
    		printk("AO cpu stop fail.\n");
    		return -1;
    	}
    }
    return -1;
}
#endif

extern int det_pwr_key(void);
extern void clr_pwr_key(void);

static void meson_pm_suspend(void)
{
	printk(KERN_INFO "enter meson_pm_suspend!\n");
#ifdef CONFIG_SUSPEND_WATCHDOG
	ENABLE_SUSPEND_WATCHDOG;
#endif    

#ifdef CONFIG_AO_TRIG_CLK
	stop_ao_cpu();
#endif
	//analog_switch(OFF);
	 if (pdata->set_vccx2) {
		pdata->set_vccx2(OFF);
	}

	clk_switch(OFF);
	//power_gate_switch(OFF);	
	//switch_mod_gate_by_type(MOD_MEDIA_CPU, 1);
	printk(KERN_INFO "sleep ...\n");
	//switch A9 clock to xtal 24MHz
	aml_clr_reg32_mask(P_HHI_SYS_CPU_CLK_CNTL, 1 << 7);
	aml_clr_reg32_mask(P_HHI_SYS_PLL_CNTL, 1 << 30);//disable sys pll

#ifdef CONFIG_AML_GPIO_KEY
	if(det_pwr_key())//get pwr key and wakeup im
	{
		clr_pwr_key();
		WRITE_AOBUS_REG(AO_RTI_STATUS_REG2, FLAG_WAKEUP_PWRKEY);
	}else
#endif
        {
#ifdef CONFIG_MESON_SUSPEND
#ifdef CONFIG_MESON_TRUSTZONE
		meson_suspend_firmware();
#else
		meson_power_suspend();
#endif
#else
#if 0
		//k101 power key
		aml_set_reg32_mask(P_AO_GPIO_O_EN_N, (1 << 3));
		aml_set_reg32_mask(P_AO_RTI_PULL_UP_REG, (1 << 3)|(1<<19));
		do{
			udelay(1000);
		}while((aml_read_reg32(P_AO_GPIO_I)&(1<<3)));
#endif
#endif
	}
	aml_set_reg32_mask(P_HHI_SYS_PLL_CNTL, (1 << 30)); //enable sys pll
	printk(KERN_INFO "... wake up\n");
#if 1
	if (aml_read_reg32(P_AO_RTC_ADDR1) & (1<<12)) {
	// Woke from alarm, not power button. Set flag to inform key_input driver.
		WRITE_AOBUS_REG(AO_RTI_STATUS_REG2, FLAG_WAKEUP_ALARM);
	}
	// clear RTC interrupt
	aml_write_reg32((P_AO_RTC_ADDR1),aml_read_reg32(P_AO_RTC_ADDR1)|(0xf000));
	printk(KERN_INFO "RTCADD3=0x%x\n",aml_read_reg32(P_AO_RTC_ADDR3));
	if(aml_read_reg32(P_AO_RTC_ADDR3)|(1<<29))
	{
		aml_write_reg32((P_AO_RTC_ADDR3),aml_read_reg32(P_AO_RTC_ADDR3)&(~(1<<29)));
		udelay(1000);
	}
	printk(KERN_INFO "RTCADD3=0x%x\n",aml_read_reg32(P_AO_RTC_ADDR3));
#endif
	if (pdata->set_vccx2) {
		pdata->set_vccx2(ON);
	}
	wait_uart_empty();
	aml_set_reg32_mask(P_HHI_SYS_CPU_CLK_CNTL , (1 << 7)); //a9 use pll
	//switch_mod_gate_by_type(MOD_MEDIA_CPU, 0);
	//power_gate_switch(ON);
	clk_switch(ON);
	//analog_switch(ON);

#ifdef CONFIG_AO_TRIG_CLK
	run_arc_program();
#endif
}

static int meson_pm_prepare(void)
{
	  printk(KERN_INFO "enter meson_pm_prepare!\n");
	  return 0;
}

static int meson_pm_enter(suspend_state_t state)
{
	int ret = 0;
	switch (state) {
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		meson_pm_suspend();
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static void meson_pm_finish(void)
{
    printk(KERN_INFO "enter meson_pm_finish!\n");
}

static struct platform_suspend_ops meson_pm_ops = {
    .enter        = meson_pm_enter,
    .prepare    = meson_pm_prepare,
    .finish       = meson_pm_finish,
    .valid        = suspend_valid_only_mem,
};

static void m6ref_set_vccx2(int power_on)
{
    if(power_on == OFF) {
        printk("m6ref_set_vccx2: OFF");
        CLEAR_AOBUS_REG_MASK(AO_GPIO_O_EN_N, 1<<15);
        SET_AOBUS_REG_MASK(AO_GPIO_O_EN_N, 1<<31);
    } else {
        printk("m6ref_set_vccx2: ON");
        CLEAR_AOBUS_REG_MASK(AO_GPIO_O_EN_N, 1<<15);
        CLEAR_AOBUS_REG_MASK(AO_GPIO_O_EN_N, 1<<31);
    }
    return;
}

static struct meson_pm_config aml_pm_pdata = {
    .pctl_reg_base = (void *)IO_APB_BUS_BASE,
    .mmc_reg_base = (void *)APB_REG_ADDR(0x1000),
    .hiu_reg_base = (void *)CBUS_REG_ADDR(0x1000),
    .power_key = (1<<8),
    .ddr_clk = 0x00110820,
    .sleepcount = 128,
    .set_vccx2 = m6ref_set_vccx2,
    .core_voltage_adjust = 7,  //5,8
};

static int __init meson_pm_probe(struct platform_device *pdev)
{
	printk(KERN_INFO "enter meson_pm_probe!\n");
#ifdef CONFIG_HAS_EARLYSUSPEND
	early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB;
	early_suspend.suspend = meson_system_early_suspend;
	early_suspend.resume = meson_system_late_resume;
	register_early_suspend(&early_suspend);
#endif
	pdev->dev.platform_data=&aml_pm_pdata;
	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "cannot get platform data\n");
		return -ENOENT;
	}
	suspend_set_ops(&meson_pm_ops);
	
	clk81 = clk_get_sys("clk81", NULL);
	clkxtal = clk_get_sys("xtal", NULL);
	
	cec_np = of_find_node_by_name(NULL, "vend_data");
	if(cec_np){
	    if(of_property_read_u32(cec_np, "cec_config", &cec_config))
	        cec_config = 0x0;
	}
	else
	{
	    cec_config = 0x0;
	}
    printk("hdmi: cec_pm: cec config:0x%x\n", cec_config);
    
	printk(KERN_INFO "meson_pm_probe done !\n");

#ifdef CONFIG_AO_TRIG_CLK
	return run_arc_program();
#else
	return 0;
#endif
}

static int __exit meson_pm_remove(struct platform_device *pdev)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&early_suspend);
#endif
	return 0;
}
#ifdef CONFIG_OF
static const struct of_device_id amlogic_pm_dt_match[]={
	{	.compatible = "amlogic,pm-m8",
	},
};
#else
#define amlogic_nand_dt_match NULL
#endif

static struct platform_driver meson_pm_driver = {
	.driver = {
		.name     = "pm-meson",
		.owner     = THIS_MODULE,
		.of_match_table=amlogic_pm_dt_match,
	},
	.remove = __exit_p(meson_pm_remove),
};

static int __init meson_pm_init(void)
{
	printk("enter %s\n",__func__);
	return platform_driver_probe(&meson_pm_driver, meson_pm_probe);
}
late_initcall(meson_pm_init);

