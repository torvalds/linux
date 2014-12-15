/*
 *
 * arch/arm/mach-meson6/usbclock.c
 *
 *  Copyright (C) 2013 AMLOGIC, INC.
 *
 *	by Victor Wan 2013.3 @Shanghai
 *
 * License terms: GNU General Public License (GPL) version 2
 * Platform machine definition.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/delay.h>
#include <plat/lm.h>
#include <mach/memory.h>
#include <mach/clock.h>
#include <mach/am_regs.h>
#include <mach/usbclock.h>
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#endif
/*
 * M chip USB clock setting
 */
 
/*
 * Clock source name index must sync with chip's spec
 * M1/M2/M3/M6 are different!
 * This is only for M6
 */
static const char * clock_src_name[] = {
    "XTAL input",
    "XTAL input divided by 2",
    "DDR PLL",
    "MPLL OUT0"
    "MPLL OUT1",
    "MPLL OUT2",
    "FCLK / 2",
    "FCLK / 3"
};
static int init_count;
int clk_enable_usb(struct clk *clk)
{
	int port_idx;
	char * clk_name;
	usb_peri_reg_t * peri_a,* peri_b,*peri;
	usb_config_data_t config;
	usb_ctrl_data_t control;
  usb_adp_bc_data_t adp_bc;
	int clk_sel,clk_div,clk_src;
	int time_dly = 500; //usec
	
	if(!clk)
		return -1;

	if(!init_count)
	{
		init_count++;
		aml_set_reg32_bits(P_RESET1_REGISTER, 1, 2, 1);
		//for(i = 0; i < 1000; i++)
		//	udelay(time_dly);
	}
	
	clk_name = (char*)clk->priv;
	switch_mod_gate_by_name(clk_name, 1);

	peri_a = (usb_peri_reg_t *)P_USB_ADDR0;
	peri_b = (usb_peri_reg_t *)P_USB_ADDR8;

	if(!strcmp(clk_name,"usb0")){
		peri = peri_a;
		port_idx = USB_PORT_IDX_A;
	}else if(!strcmp(clk_name,"usb1")){
		peri = peri_b;
		port_idx = USB_PORT_IDX_B;
	}else{
		printk(KERN_ERR "bad usb clk name: %s\n",clk_name);
		return -1;
	}
	

	clk_sel = USB_PHY_CLK_SEL_XTAL;
	clk_div = 1;
	clk_src = 24000000;

	config.d32 = peri->config;
	config.b.clk_32k_alt_sel= 1;
	peri->config = config.d32;


	printk(KERN_NOTICE"USB (%d) use clock source: %s\n",port_idx,clock_src_name[clk_sel]);

	control.d32 = peri->ctrl;
	control.b.fsel = 5;//2;	/* PHY default is 24M (5), change to 12M (2) */
	control.b.por = 1;
	peri->ctrl = control.d32;
	udelay(time_dly);
	control.b.por = 0;
	peri->ctrl = control.d32;
	udelay(time_dly);
	

	/* read back clock detected flag*/
	control.d32 = peri->ctrl;
	if(!control.b.clk_detected){
		printk(KERN_ERR"USB (%d) PHY Clock not detected!\n",port_idx);
	}

	/* force ACA enable */
	if(IS_MESON_M8M2_CPU && port_idx == USB_PORT_IDX_B){
		adp_bc.d32 = peri->adp_bc;
		adp_bc.b.aca_enable = 1;
		peri->adp_bc = adp_bc.d32;
		udelay(50);
		adp_bc.d32 = peri->adp_bc;
		if(adp_bc.b.aca_pin_float){
			printk(KERN_ERR "USB-B ID detect failed!\n");
			printk(KERN_ERR "Please use the chip after version RevA1!\n");
			return -1;
		}
	}
	dmb();
	return 0;
}
EXPORT_SYMBOL(clk_enable_usb);

int clk_disable_usb(struct clk *clk)
{
	char * clk_name;
	usb_peri_reg_t * peri_a,* peri_b,*peri;

	if(!clk)
		return -1;

	clk_name = (char*)clk->priv;
	peri_a = (usb_peri_reg_t *)P_USB_ADDR0;
	peri_b = (usb_peri_reg_t *)P_USB_ADDR8;

	if(!strcmp(clk_name,"usb0"))
		peri = peri_a;
	else if(!strcmp(clk_name,"usb1"))
		peri = peri_b;
	else{
		printk(KERN_ERR "bad usb clk name: %s\n",clk_name);
		return -1;
	}
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	switch_mod_gate_by_name(clk_name, 0);
#endif
	//if(init_count){
	//	init_count--;
		//uart.d32 = peri->dbg_uart;
		//uart.b.set_iddq = 1;
		//peri->dbg_uart = uart.d32;
	//}
	dmb();
	return 0;
}
EXPORT_SYMBOL(clk_disable_usb);

