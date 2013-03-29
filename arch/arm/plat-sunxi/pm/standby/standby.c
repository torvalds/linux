/*
 * arch/arm/plat-sunxi/pm/standby/standby.c
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

#include "standby_i.h"

extern unsigned int save_sp(void);
extern void restore_sp(unsigned int sp);
extern void standby_flush_tlb(void);
extern void standby_preload_tlb(void);
extern char *__bss_start;
extern char *__bss_end;
extern char *__standby_start;
extern char *__standby_end;

static __u32 sp_backup;
static void standby(void);
static __u32 dcdc2, dcdc3;
static struct sun4i_clk_div_t  clk_div;
static struct sun4i_clk_div_t  tmp_clk_div;

/* parameter for standby, it will be transfered from sys_pwm module */
struct aw_pm_info  pm_info;

#define DRAM_BASE_ADDR      0xc0000000
#define DRAM_TRANING_SIZE   (16)
static __u32 dram_traning_area_back[DRAM_TRANING_SIZE];

/*
*********************************************************************************************************
*                                   STANDBY MAIN PROCESS ENTRY
*
* Description: standby main process entry.
*
* Arguments  : arg  pointer to the parameter that transfered from sys_pwm module.
*
* Returns    : none
*
* Note       :
*********************************************************************************************************
*/
int __attribute__((section(".startup")))main(struct aw_pm_info *arg)
{
    char    *tmpPtr = (char *)&__bss_start;

    if(!arg){
        /* standby parameter is invalid */
        return -1;
    }

    /* flush data and instruction tlb, there is 32 items of data tlb and 32 items of instruction tlb,
       The TLB is normally allocated on a rotating basis. The oldest entry is always the next allocated */
    standby_flush_tlb();
    /* preload tlb for standby */
    standby_preload_tlb();

    /* clear bss segment */
    do{*tmpPtr ++ = 0;}while(tmpPtr <= (char *)&__bss_end);

    /* copy standby parameter from dram */
    standby_memcpy(&pm_info, arg, sizeof(pm_info));
    /* copy standby code & data to load tlb */
    standby_memcpy((char *)&__standby_end, (char *)&__standby_start, (char *)&__bss_end - (char *)&__bss_start);
    /* backup dram traning area */
    standby_memcpy((char *)dram_traning_area_back, (char *)DRAM_BASE_ADDR, sizeof(__u32)*DRAM_TRANING_SIZE);

    /* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */
    /* init module before dram enter selfrefresh */
    /* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */

    /* initialise standby modules */
    standby_clk_init();
    standby_int_init();
    standby_tmr_init();
    standby_power_init();
    /* init some system wake source */
    if(pm_info.standby_para.event & SUSPEND_WAKEUP_SRC_EXINT){
        standby_enable_int(INT_SOURCE_EXTNMI);
    }
    if(pm_info.standby_para.event & SUSPEND_WAKEUP_SRC_KEY){
        standby_key_init();
        standby_enable_int(INT_SOURCE_LRADC);
    }
    if(pm_info.standby_para.event & SUSPEND_WAKEUP_SRC_IR){
        standby_ir_init();
        standby_enable_int(INT_SOURCE_IR0);
        standby_enable_int(INT_SOURCE_IR1);
    }
    if(pm_info.standby_para.event & SUSPEND_WAKEUP_SRC_ALARM){
        //standby_alarm_init();???
        standby_enable_int(INT_SOURCE_ALARM);
    }
    if(pm_info.standby_para.event & SUSPEND_WAKEUP_SRC_USB){
        standby_usb_init();
        standby_enable_int(INT_SOURCE_USB0);
    }
    if(pm_info.standby_para.event & SUSPEND_WAKEUP_SRC_TIMEOFF){
        /* set timer for power off */
        if(pm_info.standby_para.time_off) {
            standby_tmr_set(pm_info.standby_para.time_off);
            standby_enable_int(INT_SOURCE_TIMER0);
        }
    }

    /* save stack pointer registger, switch stack to sram */
    sp_backup = save_sp();
    /* enable dram enter into self-refresh */
    dram_power_save_process();
    /* process standby */
    standby();
    /* enable watch-dog to preserve dram training failed */
    standby_tmr_enable_watchdog();
    /* restore dram */
    dram_power_up_process();
    /* disable watch-dog    */
    standby_tmr_disable_watchdog();

    /* restore stack pointer register, switch stack back to dram */
    restore_sp(sp_backup);

    /* exit standby module */
    if(pm_info.standby_para.event & SUSPEND_WAKEUP_SRC_USB){
        standby_usb_exit();
    }
    if(pm_info.standby_para.event & SUSPEND_WAKEUP_SRC_IR){
        standby_ir_exit();
    }
    if(pm_info.standby_para.event & SUSPEND_WAKEUP_SRC_ALARM){
        //standby_alarm_exit();
    }
    if(pm_info.standby_para.event & SUSPEND_WAKEUP_SRC_KEY){
        standby_key_exit();
    }
    standby_power_exit();
    standby_tmr_exit();
    standby_int_exit();
    standby_clk_exit();

    /* restore dram traning area */
    standby_memcpy((char *)DRAM_BASE_ADDR, (char *)dram_traning_area_back, sizeof(__u32)*DRAM_TRANING_SIZE);

    /* report which wake source wakeup system */
    arg->standby_para.event = pm_info.standby_para.event;

    return 0;
}


/*
*********************************************************************************************************
*                                     SYSTEM PWM ENTER STANDBY MODE
*
* Description: enter standby mode.
*
* Arguments  : none
*
* Returns    : none;
*********************************************************************************************************
*/
static void standby(void)
{
    /* gating off dram clock */
    standby_clk_dramgating(0);

    /* switch cpu clock to HOSC, and disable pll */
    standby_clk_core2hosc();
    standby_clk_plldisable();

    /* backup voltages */
    dcdc2 = standby_get_voltage(POWER_VOL_DCDC2);
    dcdc3 = standby_get_voltage(POWER_VOL_DCDC3);

    /* adjust voltage */
    standby_set_voltage(POWER_VOL_DCDC3, STANDBY_DCDC3_VOL);
    standby_set_voltage(POWER_VOL_DCDC2, STANDBY_DCDC2_VOL);

    /* set clock division cpu:axi:ahb:apb = 2:2:2:1 */
    standby_clk_getdiv(&clk_div);
    tmp_clk_div.axi_div = 0;
    tmp_clk_div.ahb_div = 0;
    tmp_clk_div.apb_div = 0;
    standby_clk_setdiv(&tmp_clk_div);
    /* swtich apb1 to losc */
    standby_clk_apb2losc();
    standby_mdelay(10);
    /* switch cpu to 32k */
    standby_clk_core2losc();
    #if(ALLOW_DISABLE_HOSC)
    // disable HOSC, and disable LDO
    standby_clk_hoscdisable();
    standby_clk_ldodisable();
    #endif

    /* cpu enter sleep, wait wakeup by interrupt */
    asm("WFI");

    #if(ALLOW_DISABLE_HOSC)
    /* enable LDO, enable HOSC */
    standby_clk_ldoenable();
    /* delay 1ms for power be stable */
    standby_delay(1);
    standby_clk_hoscenable();
    standby_delay(1);
    #endif
    /* swtich apb1 to hosc */
    standby_clk_apb2hosc();
    /* switch clock to hosc */
    standby_clk_core2hosc();
    /* restore clock division */
    standby_clk_setdiv(&clk_div);

    /* check system wakeup event */
    pm_info.standby_para.event = 0;
    pm_info.standby_para.event |= standby_query_int(INT_SOURCE_EXTNMI)? 0:SUSPEND_WAKEUP_SRC_EXINT;
    pm_info.standby_para.event |= standby_query_int(INT_SOURCE_USB0)? 0:SUSPEND_WAKEUP_SRC_USB;
    pm_info.standby_para.event |= standby_query_int(INT_SOURCE_LRADC)? 0:SUSPEND_WAKEUP_SRC_KEY;
    pm_info.standby_para.event |= standby_query_int(INT_SOURCE_IR0)? 0:SUSPEND_WAKEUP_SRC_IR;
    pm_info.standby_para.event |= standby_query_int(INT_SOURCE_ALARM)? 0:SUSPEND_WAKEUP_SRC_ALARM;
    pm_info.standby_para.event |= standby_query_int(INT_SOURCE_TIMER0)? 0:SUSPEND_WAKEUP_SRC_TIMEOFF;

    /* restore voltage for exit standby */
    standby_set_voltage(POWER_VOL_DCDC2, dcdc2);
    standby_set_voltage(POWER_VOL_DCDC3, dcdc3);
    standby_mdelay(10);

    /* enable pll */
    standby_clk_pllenable();
    standby_mdelay(10);
    /* switch cpu clock to core pll */
    standby_clk_core2pll();
    standby_mdelay(10);

    /* gating on dram clock */
    standby_clk_dramgating(1);

    return;
}

