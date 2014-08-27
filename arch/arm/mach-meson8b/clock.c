/*
 * arch/arm/mach-meson8b/clock.c
 *
 * Copyright (C) 2011-2014 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

///#define DEBUG
///#define CONFIG_CPU_FREQ_DEBUG		1

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/cpu.h>

#include <linux/clkdev.h>
#include <linux/printk.h>
#include <plat/io.h>
#include <plat/cpufreq.h>
#include <mach/am_regs.h>
#include <mach/clock.h>
#ifdef CONFIG_AMLOGIC_USB
#include <mach/usbclock.h>
#endif
//#include <mach/hardware.h>
//#include <mach/clk_set.h>
//#include <mach/power_gate.h>

#ifdef CONFIG_MESON_TRUSTZONE
#include <mach/meson-secure.h>
#endif

#include <linux/delay.h>
extern struct arm_delay_ops arm_delay_ops;


static DEFINE_SPINLOCK(clockfw_lock);
static DEFINE_MUTEX(clock_ops_lock);
static int measure_cpu_clock = 0;

/**************** SYS PLL**************************/
#define SYS_PLL_TABLE_MIN	 24000000
#define SYS_PLL_TABLE_MAX	2112000000

#define CPU_FREQ_LIMIT 1536000000

struct sys_pll_s {
    unsigned int freq;
    unsigned int cntl0;
    unsigned int latency;

};
typedef union latency_data {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
	unsigned apb_div:4;	/* 0 */
	unsigned peri_div:4;	/* 4 */
	unsigned axi_div:4;	/* 8 */
	unsigned l2_div:4;		/* 12 */
	unsigned ext_div_n:8;	/* 16 */
	unsigned afc_dsel_bp_en:1; /* 24 */
	unsigned afc_dsel_bp_in: 1; /* 25 */
	unsigned reserved:6;	/* 26 */
    } b;
} latency_data_t;

static unsigned sys_pll_settings[][3] = {
                {   24, 0x40020238, 0x00063546 }, /* fvco 1344, / 4, /14 */
                {   48, 0x40020240, 0x00033546 }, /* fvco 1536, / 4, / 8 */
                {   72, 0x40020248, 0x03023546 }, /* fvco 1728, / 4, / 6 */
                {   96, 0x40020240, 0x00013546 }, /* fvco 1536, / 4, / 4 */
                {  120, 0x40020250, 0x03013546 }, /* fvco 1920, / 4, / 4 */
                {  144, 0x40020260, 0x00013546 }, /* fvco 2304, / 4, / 4 */
                {  168, 0x40010238, 0x00013546 }, /* fvco 1344, / 2, / 4 */
                {  192, 0x40010240, 0x00013546 }, /* fvco 1536, / 2, / 4 */
                {  216, 0x40010248, 0x03013546 }, /* fvco 1728, / 2, / 4 */
                {  240, 0x40010250, 0x03013546 }, /* fvco 1920, / 2, / 4 */
                {  264, 0x40010258, 0x00013546 }, /* fvco 2112, / 2, / 4 */
                {  288, 0x40010260, 0x00013546 }, /* fvco 2304, / 2, / 4 */
                {  312, 0x40020234, 0x00003546 }, /* fvco 1248, / 4, / 1 */
                {  336, 0x40020238, 0x00003546 }, /* fvco 1344, / 4, / 1 */
                {  360, 0x4002023C, 0x00003546 }, /* fvco 1440, / 4, / 1 */
                {  384, 0x40020240, 0x00003546 }, /* fvco 1536, / 4, / 1 */
                {  408, 0x40020244, 0x01003546 }, /* fvco 1632, / 4, / 1 */
                {  432, 0x40020248, 0x03003546 }, /* fvco 1728, / 4, / 1 */
                {  456, 0x4002024C, 0x03003546 }, /* fvco 1824, / 4, / 1 */
                {  480, 0x40020250, 0x03003546 }, /* fvco 1920, / 4, / 1 */
                {  504, 0x40020254, 0x03003546 }, /* fvco 2016, / 4, / 1 */
                {  528, 0x40020258, 0x00003546 }, /* fvco 2112, / 4, / 1 */
                {  552, 0x4002025C, 0x00003546 }, /* fvco 2208, / 4, / 1 */
                {  576, 0x40020260, 0x00003546 }, /* fvco 2304, / 4, / 1 */
                {  600, 0x40010232, 0x00003546 }, /* fvco 1200, / 2, / 1 */
                {  624, 0x40010234, 0x00003546 }, /* fvco 1248, / 2, / 1 */
                {  648, 0x40010236, 0x00003546 }, /* fvco 1296, / 2, / 1 */
                {  672, 0x40010238, 0x00003546 }, /* fvco 1344, / 2, / 1 */
                {  696, 0x4001023A, 0x00003546 }, /* fvco 1392, / 2, / 1 */
                {  720, 0x4001023C, 0x00003546 }, /* fvco 1440, / 2, / 1 */
                {  744, 0x4001023E, 0x00003546 }, /* fvco 1488, / 2, / 1 */
                {  768, 0x40010240, 0x00003546 }, /* fvco 1536, / 2, / 1 */
                {  792, 0x40010242, 0x00003546 }, /* fvco 1584, / 2, / 1 */
                {  816, 0x40010244, 0x01003546 }, /* fvco 1632, / 2, / 1 */
                {  840, 0x40010246, 0x01003546 }, /* fvco 1680, / 2, / 1 */
                {  864, 0x40010248, 0x03003546 }, /* fvco 1728, / 2, / 1 */
                {  888, 0x4001024A, 0x03003546 }, /* fvco 1776, / 2, / 1 */
                {  912, 0x4001024C, 0x03003546 }, /* fvco 1824, / 2, / 1 */
                {  936, 0x4001024E, 0x03003546 }, /* fvco 1872, / 2, / 1 */
                {  960, 0x40010250, 0x03003546 }, /* fvco 1920, / 2, / 1 */
                {  984, 0x40010252, 0x03003546 }, /* fvco 1968, / 2, / 1 */
                { 1008, 0x40010254, 0x03003546 }, /* fvco 2016, / 2, / 1 */
                { 1032, 0x40010256, 0x03003546 }, /* fvco 2064, / 2, / 1 */
                { 1056, 0x40010258, 0x00003546 }, /* fvco 2112, / 2, / 1 */
                { 1080, 0x4001025A, 0x00003546 }, /* fvco 2160, / 2, / 1 */
                { 1104, 0x4001025C, 0x00003546 }, /* fvco 2208, / 2, / 1 */
                { 1128, 0x4001025E, 0x00003546 }, /* fvco 2256, / 2, / 1 */
                { 1152, 0x40010260, 0x00003546 }, /* fvco 2304, / 2, / 1 */
                { 1176, 0x40010262, 0x00003546 }, /* fvco 2352, / 2, / 1 */
                { 1200, 0x40000232, 0x00003546 }, /* fvco 1200, / 1, / 1 */
                { 1224, 0x40000233, 0x00003546 }, /* fvco 1224, / 1, / 1 */
                { 1248, 0x40000234, 0x00003546 }, /* fvco 1248, / 1, / 1 */
                { 1272, 0x40000235, 0x00003546 }, /* fvco 1272, / 1, / 1 */
                { 1296, 0x40000236, 0x00003546 }, /* fvco 1296, / 1, / 1 */
                { 1320, 0x40000237, 0x00003546 }, /* fvco 1320, / 1, / 1 */
                { 1344, 0x40000238, 0x00003546 }, /* fvco 1344, / 1, / 1 */
                { 1368, 0x40000239, 0x00003546 }, /* fvco 1368, / 1, / 1 */
                { 1392, 0x4000023A, 0x00003546 }, /* fvco 1392, / 1, / 1 */
                { 1416, 0x4000023B, 0x00003546 }, /* fvco 1416, / 1, / 1 */
                { 1440, 0x4000023C, 0x00003546 }, /* fvco 1440, / 1, / 1 */
                { 1464, 0x4000023D, 0x00003546 }, /* fvco 1464, / 1, / 1 */
                { 1488, 0x4000023E, 0x00003546 }, /* fvco 1488, / 1, / 1 */
                { 1512, 0x4000023F, 0x00003546 }, /* fvco 1512, / 1, / 1 */
                { 1536, 0x40000240, 0x00003546 }, /* fvco 1536, / 1, / 1 */
                { 1560, 0x40000241, 0x00003546 }, /* fvco 1560, / 1, / 1 */
                { 1584, 0x40000242, 0x00003546 }, /* fvco 1584, / 1, / 1 */
                { 1608, 0x40000243, 0x01003546 }, /* fvco 1608, / 1, / 1 */
                { 1632, 0x40000244, 0x01003546 }, /* fvco 1632, / 1, / 1 */
                { 1656, 0x40004244, 0x01003546 }, /* fvco 1656, / 1, / 1 */
                { 1680, 0x40008244, 0x01003546 }, /* fvco 1680, / 1, / 1 */
                { 1704, 0x4000c244, 0x01003546 }, /* fvco 1704, / 1, / 1 */
                { 1728, 0x40000245, 0x01003546 }, /* fvco 1728, / 1, / 1 */
                { 1752, 0x40004245, 0x01003546 }, /* fvco 1752, / 1, / 1 */
                { 1776, 0x40008245, 0x01003546 }, /* fvco 1776, / 1, / 1 */
                { 1800, 0x4000c245, 0x01003546 }, /* fvco 1800, / 1, / 1 */
                { 1824, 0x40000246, 0x01003546 }, /* fvco 1824, / 1, / 1 */
                { 1848, 0x40004246, 0x01003546 }, /* fvco 1848, / 1, / 1 */
                { 1872, 0x40008246, 0x01003546 }, /* fvco 1872, / 1, / 1 */
                { 1896, 0x4000c246, 0x01003546 }, /* fvco 1896, / 1, / 1 */
                { 1920, 0x40000247, 0x01003546 }, /* fvco 1920, / 1, / 1 */
                { 1944, 0x40004247, 0x01003546 }, /* fvco 1944, / 1, / 1 */
                { 1968, 0x40008247, 0x01003546 }, /* fvco 1968, / 1, / 1 */
                { 1992, 0x4000c247, 0x01003546 }, /* fvco 1992, / 1, / 1 */
                { 2016, 0x40000248, 0x01003546 }, /* fvco 2016, / 1, / 1 */
                { 2040, 0x40004248, 0x01003546 }, /* fvco 2040, / 1, / 1 */
                { 2064, 0x40008248, 0x01003546 }, /* fvco 2064, / 1, / 1 */
                { 2088, 0x4000c248, 0x01003546 }, /* fvco 2088, / 1, / 1 */
                { 2112, 0x40000249, 0x00003546 }, /* fvco 2112, / 1, / 1 */
};
static unsigned setup_a9_clk_max = CPU_FREQ_LIMIT;
static unsigned setup_a9_clk_min =    24000000;


static unsigned int freq_limit = 1;

static int set_sys_pll(struct clk *clk,  unsigned long dst);

#define IS_CLK_ERR(a)  (IS_ERR(a) || a == 0)

static unsigned long clk_get_rate_a9(struct clk * clkdev);

#ifndef CONFIG_CLK_MSR_NEW
static unsigned int clk_util_clk_msr(unsigned int clk_mux)
{
	unsigned int  msr;
	unsigned int regval = 0;
	aml_write_reg32(P_MSR_CLK_REG0,0);
    // Set the measurement gate to 64uS
	clrsetbits_le32(P_MSR_CLK_REG0,0xffff,64-1);
    // Disable continuous measurement
    // disable interrupts
    clrbits_le32(P_MSR_CLK_REG0,((1 << 18) | (1 << 17)));
	clrsetbits_le32(P_MSR_CLK_REG0,(0x1f<<20),(clk_mux<<20)|(1<<19)|(1<<16));

	aml_read_reg32(P_MSR_CLK_REG0); 
    // Wait for the measurement to be done
      do {
        regval = aml_read_reg32(P_MSR_CLK_REG0);
    } while (regval & (1 << 31));
    // disable measuring
	clrbits_le32(P_MSR_CLK_REG0,(1 << 16));
	 msr=(aml_read_reg32(P_MSR_CLK_REG2)+31)&0x000FFFFF;
    // Return value in MHz*measured_val
    return (msr>>6)*1000000;

}
#else
static  unsigned int clk_util_clk_msr(unsigned int clk_mux)
{
    unsigned int regval = 0;
    /// Set the measurement gate to 64uS
    clrsetbits_le32(P_MSR_CLK_REG0,0xffff,121);///122us
    
    // Disable continuous measurement
    // disable interrupts
    clrsetbits_le32(P_MSR_CLK_REG0,
        ((1 << 18) | (1 << 17)|(0x1f << 20)),///clrbits
        (clk_mux << 20) |                    /// Select MUX
        (1 << 19) |                          /// enable the clock
        (1 << 16));
    // Wait for the measurement to be done
    regval = aml_read_reg32(P_MSR_CLK_REG0);
    do {
        regval = aml_read_reg32(P_MSR_CLK_REG0);
    } while (regval & (1 << 31));

    // disable measuring
    clrbits_le32(P_MSR_CLK_REG0, (1 << 16));
    regval = (aml_read_reg32(P_MSR_CLK_REG2)) & 0x000FFFFF;
    regval += (regval/10000) * 6;
    // Return value in MHz*measured_val
    return (regval << 13);
}

#endif

int    clk_measure(char  index )
{
	const char* clk_table[]={
	" CTS_MIPI_CSI_CFG_CLK(63)",
	" VID2_PLL_CLK(62)",
	" GPIO_CLK(61)",
	" USB_32K_ALT(60)",
	" CTS_HCODEC_CLK(59)",
	" Reserved(58)",
	" Reserved(57)",
	" Reserved(56)",
	" Reserved(55)",
	" Reserved(54)",
	" Reserved(53)",
	" Reserved(52)",
	" Reserved(51)",
	" Reserved(50)",	
	" CTS_PWM_E_CLK(49)",	
	" CTS_PWM_F_CLK(48)",	
	" DDR_DPLL_PT_CLK(47)",	
	" CTS_PCM2_SCLK(46)",		
	" CTS_PWM_A_CLK(45)",
	" CTS_PWM_B_CLK(44)",
	" CTS_PWM_C_CLK(43)",
	" CTS_PWM_D_CLK(42)",
	" CTS_ETH_RX_TX(41)",
	" CTS_PCM_MCLK(40)",
	" CTS_PCM_SCLK(39)",
	" CTS_VDIN_MEAS_CLK(38)",
	" Reserved(37)",
	" CTS_HDMI_TX_PIXEL_CLK(36)",
	" CTS_MALI_CLK (35)",
	" CTS_SDHC_SDCLK(34)",
	" CTS_SDHC_RXCLK(33)",
	" CTS_VDAC_CLK(32)",
	" CTS_AUDAC_CLKPI(31)",
	" MPLL_CLK_TEST_OUT(30)",
	" Reserved(29)",
	" CTS_SAR_ADC_CLK(28)",
	" Reserved(27)",
	" SC_CLK_INT(26)",
	" Reserved(25)",
	" LVDS_FIFO_CLK(24)",
	" HDMI_CH0_TMDSCLK(23)",
	" CLK_RMII_FROM_PAD (22)",
	" I2S_CLK_IN_SRC0(21)",
	" RTC_OSC_CLK_OUT(20)",
	" CTS_HDMI_SYS_CLK(19)",
	" A9_CLK_DIV16(18)",
	" Reserved(17)",
	" CTS_FEC_CLK_2(16)",
	" CTS_FEC_CLK_1 (15)",
	" CTS_FEC_CLK_0 (14)",
	" CTS_AMCLK(13)",
	" Reserved(12)",
	" CTS_ETH_RMII(11)",
	" Reserved(10)",
	" CTS_ENCL_CLK(9)",
	" CTS_ENCP_CLK(8)",
	" CLK81 (7)",
	" VID_PLL_CLK(6)",
	" Reserved (5)",
	" Reserved (4)",
	" A9_RING_OSC_CLK(3)",
	" AM_RING_OSC_CLK_OUT_EE2(2)",
	" AM_RING_OSC_CLK_OUT_EE1(1)",
	" AM_RING_OSC_CLK_OUT_EE0(0)",
	};   
	int  i;
	int len = sizeof(clk_table)/sizeof(char*) - 1;
	if (index  == 0xff)
	{
	 	for(i = 0;i < len;i++)
		{
			printk("[%10d]%s\n",clk_util_clk_msr(i),clk_table[len-i]);
		}
		return 0;
	}	
	printk("[%10d]%s\n",clk_util_clk_msr(index),clk_table[len-index]);
	return 0;
}

long clk_round_rate_sys(struct clk *clk, unsigned long rate)
{
	int idx,dst;
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;
	
	dst = rate;
	if (rate < SYS_PLL_TABLE_MIN) 
		dst = SYS_PLL_TABLE_MIN;
	else if (rate > SYS_PLL_TABLE_MAX) 
		dst = SYS_PLL_TABLE_MAX;

	if(dst < setup_a9_clk_min)
		dst = setup_a9_clk_min;
	else if(dst > setup_a9_clk_max)
		dst = setup_a9_clk_max;
 	if ((rate != 1250000000)) {
	    idx = ((dst - SYS_PLL_TABLE_MIN) / 1000000) / 24;
        //printk("sys round rate: %ld -- %d\n",rate,sys_pll_settings[idx][0]);
        rate = sys_pll_settings[idx][0] * 1000000;
    } 
	
	return rate;
}
long clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;
	if(clk->round_rate)
		return clk->round_rate(clk,rate);

	if (rate < clk->min)
		return clk->min;

	if (rate > clk->max)
		return clk->max;

	return rate;
}
EXPORT_SYMBOL(clk_round_rate);

unsigned long clk_get_rate(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

    if (clk->get_rate)
		return clk->get_rate(clk);
	else
		return clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);

int on_parent_changed(struct clk *clk, int rate, int before,int failed)
{
	struct clk_ops* pops = clk->clk_ops;
	while(pops){
		if(before == 1){
				if(pops->clk_ratechange_before)
					pops->clk_ratechange_before(rate,pops->privdata);
		}
		else{
				if(pops->clk_ratechange_after)
					pops->clk_ratechange_after(rate,pops->privdata,failed);			
		}
		pops = pops->next;
	}
	return 0;
}

int meson_notify_childs_changed(struct clk *clk,int before,int failed)
{
	struct clk* p;
	if(IS_CLK_ERR(clk))
		return 0;
	p = (struct clk*)(clk->child.next);
	if (p) {
		unsigned long flags;

		int rate = clk_get_rate(p);
		spin_lock_irqsave(&clockfw_lock, flags);
		on_parent_changed(p,rate,before,failed);
		spin_unlock_irqrestore(&clockfw_lock, flags);

		meson_notify_childs_changed(p,before,failed);

		p = (struct clk*)p->sibling.next;
		while(p){
		  spin_lock_irqsave(&clockfw_lock, flags);
			on_parent_changed(p,rate,before,failed);
			spin_unlock_irqrestore(&clockfw_lock, flags);

			meson_notify_childs_changed(p,before,failed);

			p = (struct clk*)p->sibling.next;
		}
	}
	return 0;
}

//flow. self -> child -> child slibling
int meson_clk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags=0;
	int ret;
	int ops_run_count;
	struct clk_ops *p;
	
	if(clk->set_rate == NULL || IS_CLK_ERR(clk))
			return 0;
	//post message before clk change.
	{
			ret = 0;
			ops_run_count = 0;
			p = clk->clk_ops;	
			while(p){
				ops_run_count++;
				if(p->clk_ratechange_before)
					ret = p->clk_ratechange_before(rate, p->privdata);
				if(ret != 0)
					break;
				p = p->next;
			}
			meson_notify_childs_changed(clk,1,ret);
	}		
	

	if(ret == 0){		
	  if (!clk->open_irq)
	      spin_lock_irqsave(&clockfw_lock, flags);
	  else
	      spin_lock(&clockfw_lock);
//		printk(KERN_INFO "%s() clk=%p rate=%lu\n", __FUNCTION__, clk, rate);
	  if(clk->set_rate)
	  	ret = clk->set_rate(clk, rate) ;
	  if (!clk->open_irq)
	      spin_unlock_irqrestore(&clockfw_lock, flags);
	  else
	      spin_unlock(&clockfw_lock);
	}

	//post message after clk change.
	{
			int idx = 0;
			p = clk->clk_ops;
			while(p){
				idx++;
				if(idx > ops_run_count)
					break;
				if(p->clk_ratechange_after)
						p->clk_ratechange_after(rate, p->privdata,ret);
				p = p->next;
			}			
	}		
	
	meson_notify_childs_changed(clk,0,ret);
 
  return ret;
}

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret=0;
	int parent_rate = 0;
	if(IS_CLK_ERR(clk))
		return 0;
	if(clk_get_rate(clk) == rate){
			return 0;			
	}
		
	if(clk->need_parent_changed){
		unsigned long flags;
	  spin_lock_irqsave(&clockfw_lock, flags);	
		parent_rate = clk->need_parent_changed(clk, rate);
	  spin_unlock_irqrestore(&clockfw_lock, flags);
	}
		
	if(parent_rate != 0)
		clk_set_rate(clk->parent,parent_rate);
	else{
		mutex_lock(&clock_ops_lock);
		//printk(KERN_INFO "%s() clk=%p rate=%lu\n", __FUNCTION__, clk, rate);
		ret = meson_clk_set_rate(clk,rate);
	 	mutex_unlock(&clock_ops_lock);
	}
	return ret;
}
EXPORT_SYMBOL(clk_set_rate);

unsigned long long clkparse(const char *ptr, char **retptr)
{
    char *endptr;   /* local pointer to end of parsed string */

    unsigned long long ret = simple_strtoull(ptr, &endptr, 0);

    switch (*endptr) {
    case 'G':
    case 'g':
        ret *= 1000;
    case 'M':
    case 'm':
        ret *= 1000;
    case 'K':
    case 'k':
        ret *= 1000;
        endptr++;
    default:
        break;
    }

    if (retptr) {
        *retptr = endptr;
    }

    return ret;
}

int meson_enable(struct clk *clk)
{
	if (IS_CLK_ERR(clk))
		return 0;

	if (clk_get_status(clk) == 1)
		return 0;

	if (meson_enable(clk->parent) == 0) {
			struct clk_ops *p;
			int idx;
			int ops_run_count = 0;
			int ret = 0;
			p = clk->clk_ops;
			while(p){
					ops_run_count++;
					if(p->clk_enable_before)
						ret = p->clk_enable_before(p->privdata);
					if(ret == 1)
						break;
					p = p->next;
			}
	
			if(ret == 0){	
				if(clk->enable)
					ret = clk->enable(clk);
				else if(clk->clk_gate_reg_adr != 0){
					aml_set_reg32_mask(clk->clk_gate_reg_adr,clk->clk_gate_reg_mask);
					ret = 0;
				}
			}
				
			p = clk->clk_ops;
			idx = 0;
			while(p){
				idx++;
				if(idx > ops_run_count)
					break;
				if(p->clk_enable_after)
					 p->clk_enable_after(p->privdata,ret);
				p = p->next;
			}
			
			return ret;
		}
		else
			return 1;
}
int clk_enable(struct clk *clk)
{
		int ret;
		mutex_lock(&clock_ops_lock);
		ret = meson_enable(clk);
		mutex_unlock(&clock_ops_lock);
		return ret;
}
EXPORT_SYMBOL(clk_enable);

int  meson_clk_disable(struct clk *clk)
{
		int ret = 0;
		int ops_run_count = 0;
		if(IS_CLK_ERR(clk))
			return 0;
		if(clk_get_status(clk) == 0)
			return 0;

		if(clk->child.next){
			struct clk* pchild = (struct clk*)(clk->child.next);
			if(meson_clk_disable(pchild) != 0)
				return 1;
			pchild = (struct clk*)pchild->sibling.next;
			while(pchild){
				if(meson_clk_disable(pchild) != 0)
					return 1;
				pchild = (struct clk*)pchild->sibling.next;
			}
		}

		//do clk disable
		//post message before clk disable.
		{
			struct clk_ops *p;
			ret = 0;
			p = clk->clk_ops;
			while(p){
				ops_run_count++;
				if(p->clk_disable_before)
					ret = p->clk_disable_before(p->privdata);
				if(ret != 0)
					break;
				p = p->next;
			}
		}
		
		//do clock gate disable
		if(ret == 0){
			if(clk->disable)
				ret = clk->disable(clk);
			else if(clk->clk_gate_reg_adr != 0){
					aml_clr_reg32_mask(clk->clk_gate_reg_adr,clk->clk_gate_reg_mask);
					ret = 0;
			}
		}
		
		//post message after clk disable.
		{
			struct clk_ops *p;
			int idx = 0;
			p = clk->clk_ops;
			while(p){
				idx++;
				if(idx > ops_run_count)
					break;
				if(p->clk_disable_after)
						p->clk_disable_after(p->privdata,ret);																	
				p = p->next;
			}
		}
		
		return ret;
}

void clk_disable(struct clk *clk)
{
		mutex_lock(&clock_ops_lock);
		meson_clk_disable(clk);
		mutex_unlock(&clock_ops_lock);
}
EXPORT_SYMBOL(clk_disable);

/**
 * Section all get rate functions
 */
static unsigned long clk_msr_get(struct clk * clk)
{
	uint32_t temp;
	uint32_t cnt = 0;
	
	if(clk->rate > 0)
	{
		return clk->rate;
	}
	if(clk->msr>0)
	{
		clk->rate = clk_util_clk_msr(clk->msr);
	}else if (clk->parent){
		cnt=clk_get_rate(clk->parent);
		cnt /= 1000000;
		clk->msr_mul=clk->msr_mul?clk->msr_mul:1;
		clk->msr_div=clk->msr_div?clk->msr_div:1;
		temp=cnt*clk->msr_mul;
		clk->rate=temp/clk->msr_div;
		clk->rate *= 1000000;
	}
	return clk->rate;
}

static unsigned long clk_get_rate_xtal(struct clk * clkdev)
{
	unsigned long clk;
	clk = aml_get_reg32_bits(P_PREG_CTLREG0_ADDR, 4, 6);
	clk = clk * 1000 * 1000;
	return clk;
}

static unsigned long clk_get_rate_sys(struct clk * clkdev)
{
	unsigned long clk;
	if (clkdev && clkdev->rate)
		clk = clkdev->rate;
	else {
		//using measure sys div3 to get sys pll clock. (25)
		unsigned long mul, div, od, temp;
		unsigned long long result;
		clk = clk_get_rate_xtal(NULL);
		temp = aml_read_reg32(P_HHI_SYS_PLL_CNTL);
		mul=temp&((1<<9)-1);
		div=(temp>>9)&0x3f;
		od=(temp>>16)&3;
		result=((u64)clk)*((u64)mul);
		do_div(result,div);
		clk = (unsigned long)(result>>od);
	}
	return clk;
}

static unsigned long clk_get_rate_a9(struct clk * clkdev)
{
	unsigned long clk = 0;
	unsigned int sysclk_cntl;

	if (clkdev && clkdev->rate)
		return clkdev->rate;

	sysclk_cntl = aml_read_reg32(P_HHI_SYS_CPU_CLK_CNTL);
	if((sysclk_cntl & (1<<7)) == 0)
		clk = clk_get_rate_xtal(NULL);
	else{
		unsigned long parent_clk = 0;
		unsigned int pll_sel = sysclk_cntl&3;
		if(pll_sel == 0)
			parent_clk = clk_get_rate_xtal(NULL);
		else if(pll_sel == 1)
			parent_clk = clk_get_rate_sys(clkdev->parent);
	    else if (pll_sel == 2) {
            clk = 1250000000;   // from MPLL / 2 
        } else { 
			printk(KERN_INFO "Error : A9 parent pll selection incorrect!\n");
        }
		if(parent_clk > 0){
			unsigned int N = (aml_read_reg32(P_HHI_SYS_CPU_CLK_CNTL1) >> 20) & 0x3FF;
			unsigned int div = 1;
			unsigned sel = (sysclk_cntl >> 2) & 3;
			if(sel == 1)
				div = 2;
			else if(sel == 2)
				div = 3;
			else if(sel == 3)
				div = 2 * N;
			clk = parent_clk / div;
		}
	}
	if (clk == 0) {
		pr_info("clk_get_rate_a9 measured clk=0 sysclk_cntl=%#x\n", sysclk_cntl);
	}

	return clk;
}

/**
 * udelay will delay depending on lpj.  lpj is adjusted before|after
 * cpu freq is changed, so udelay could take longer or shorter than
 * expected. This function scales the udelay value to get a more
 * accurate delay during cpu freq changes.
 * lpj is adjust elsewhere, so drivers don't need to worry about this.
 */
static inline void udelay_scaled(unsigned long usecs, unsigned int oldMHz,
                                 unsigned int newMHz)
{
	if(arm_delay_ops.ticks_per_jiffy)
		udelay(usecs);
	else
		udelay(usecs * newMHz / oldMHz);
}

/**
 *  Internal CPU clock rate setting function.
 *
 *  MUST be called with proper protection.
 */
static int _clk_set_rate_cpu(struct clk *clk, unsigned long cpu, unsigned long gpu)
{
	unsigned long parent = 0;
	unsigned long oldcpu = clk_get_rate_a9(clk);
	unsigned int cpu_clk_cntl = aml_read_reg32(P_HHI_SYS_CPU_CLK_CNTL);
	int test_n = 0;
	
//	if ((cpu_clk_cntl & 3) == 1) {
	{
		unsigned long real_cpu;
		parent = clk_get_rate_sys(clk->parent);
		// CPU switch to xtal 

		aml_write_reg32(P_HHI_SYS_CPU_CLK_CNTL, cpu_clk_cntl & ~(1 << 7));
		if (oldcpu <= cpu) {
			// when increasing frequency, lpj has already been adjusted
			udelay_scaled(10, cpu / 1000000, 24 /*clk_get_rate_xtal*/);
		} else {
			// when decreasing frequency, lpj has not yet been adjusted
			udelay_scaled(10, oldcpu / 1000000, 24 /*clk_get_rate_xtal*/);
		}

	    aml_set_reg32_bits(P_HHI_SYS_CPU_CLK_CNTL, 1, 0, 2);    // path select to syspll
        if (cpu == 1250000000) {
	    	aml_set_reg32_bits(P_HHI_MPLL_CNTL6, 1, 27, 1);
	    	aml_set_reg32_bits(P_HHI_SYS_CPU_CLK_CNTL, 2, 0, 2);    // select to mpll
			aml_set_reg32_bits(P_HHI_SYS_CPU_CLK_CNTL, 0, 2, 2);    // cancel external od
		    udelay_scaled(500, oldcpu / 1000000, 24 /*clk_get_rate_xtal*/);
	        printk(KERN_DEBUG"CTS_CPU_CLK %4ld --> %4ld (MHz)\n",
									clk->rate / 1000000, cpu / 1000000);
            clk->parent->rate = cpu;
        } else {
    		set_sys_pll(clk->parent, cpu);
        }

		// Read CBUS for short delay, then CPU switch to sys pll
		cpu_clk_cntl = aml_read_reg32(P_HHI_SYS_CPU_CLK_CNTL);
		aml_write_reg32(P_HHI_SYS_CPU_CLK_CNTL, (cpu_clk_cntl) | (1 << 7));
		if (oldcpu <= cpu) {
			// when increasing frequency, lpj has already been adjusted
			udelay(100);
		} else {
			// when decreasing frequency, lpj has not yet been adjusted
			udelay_scaled(100, oldcpu / 1000000, cpu / 1000000);
		}

        if (measure_cpu_clock) {
            while (test_n < 5) {
	            real_cpu = clk_util_clk_msr(18) << 4;
	            if ((real_cpu < cpu && (cpu - real_cpu) > 48000000) ||
	            	(real_cpu > cpu && (real_cpu - cpu) > 48000000)) {
	            	pr_info("hope to set cpu clk as %ld, real value is %ld, time %d\n", cpu, real_cpu, test_n);
	            }
                test_n++;
            }
        }
		// CPU switch to sys pll
		//cpu_clk_cntl = aml_read_reg32(P_HHI_SYS_CPU_CLK_CNTL);
		//aml_set_reg32_mask(P_HHI_SYS_CPU_CLK_CNTL, (1 << 7));
 	}

	clk->rate = cpu; 
 
#ifdef CONFIG_CPU_FREQ_DEBUG
	pr_debug("(CTS_CPU_CLK) CPU %ld.%ldMHz\n", clk_get_rate_a9(clk) / 1000000, clk_get_rate_a9(clk)%1000000);
#endif /* CONFIG_CPU_FREQ_DEBUG */

	return 0;
}

#ifdef CONFIG_SMP
#define USE_ON_EACH_CPU 0
struct clk_change_info{
  int cpu;
  struct clk * clk;
  unsigned long rate;
  int err;
};

#define MESON_CPU_STATUS(cpu) aml_read_reg32(MESON_CPU_STATUS_REG(cpu))
#define MESON_CPU_SET_STATUS(status) aml_write_reg32(MESON_CPU_STATUS_REG(smp_processor_id()),status)

void meson_set_cpu_power_ctrl(uint32_t cpu,int is_power_on)
{
	BUG_ON(cpu == 0);

	if(is_power_on){
		/* SCU Power on CPU & CPU PWR_A9_CNTL0 CTRL_MODE bit. 
		    CTRL_MODE bit may write forward to SCU when cpu reset. So, we need clean it here to avoid the forward write happen.*/
		aml_set_reg32_bits(MESON_CPU_POWER_CTRL_REG, 0x0 ,(cpu << 3),2);
		aml_set_reg32_bits(P_AO_RTI_PWR_A9_CNTL0, 0x0, 2*cpu + 16, 2);
		udelay(5);

#ifndef CONFIG_MESON_CPU_EMULATOR
		/* Reset enable*/
		aml_set_reg32_bits(P_HHI_SYS_CPU_CLK_CNTL, 1 , (cpu + 24), 1);
		/* Power on*/

		aml_set_reg32_bits(P_AO_RTI_PWR_A9_MEM_PD0, 0, (32 - cpu * 4) ,4);
		aml_set_reg32_bits(P_AO_RTI_PWR_A9_CNTL1, 0x0, ((cpu +1) << 1 ), 2);

		udelay(10);
		while(!(aml_read_reg32(P_AO_RTI_PWR_A9_CNTL1) & (1<<(cpu+16)))){
			printk("wait power...0x%08x 0x%08x\n",aml_read_reg32(P_AO_RTI_PWR_A9_CNTL0),aml_read_reg32(P_AO_RTI_PWR_A9_CNTL1));
			udelay(10);
		};
		/* Isolation disable */
		aml_set_reg32_bits(P_AO_RTI_PWR_A9_CNTL0, 0x0, cpu, 1);
		/* Reset disable */
		aml_set_reg32_bits(P_HHI_SYS_CPU_CLK_CNTL, 0 , (cpu + 24), 1);

		aml_set_reg32_bits(MESON_CPU_POWER_CTRL_REG, 0x0 ,(cpu << 3),2);
#endif
	}else{
		aml_set_reg32_bits(MESON_CPU_POWER_CTRL_REG,0x3,(cpu << 3),2);
		aml_set_reg32_bits(P_AO_RTI_PWR_A9_CNTL0, 0x3, 2*cpu + 16, 2);

#ifndef CONFIG_MESON_CPU_EMULATOR
		/* Isolation enable */
		aml_set_reg32_bits(P_AO_RTI_PWR_A9_CNTL0, 0x1, cpu, 1);
		udelay(10);
		/* Power off */
		aml_set_reg32_bits(P_AO_RTI_PWR_A9_CNTL1, 0x3, ((cpu +1) << 1 ), 2);
		aml_set_reg32_bits(P_AO_RTI_PWR_A9_MEM_PD0, 0xf , (32 - cpu * 4) ,4);
#endif
	}
	dsb();
	dmb();

	pr_debug("----CPU %d\n",cpu);
	pr_debug("----MESON_CPU_POWER_CTRL_REG(%08x) = %08x\n",MESON_CPU_POWER_CTRL_REG,aml_read_reg32(MESON_CPU_POWER_CTRL_REG));
	pr_debug("----P_AO_RTI_PWR_A9_CNTL0(%08x)    = %08x\n",P_AO_RTI_PWR_A9_CNTL0,aml_read_reg32(P_AO_RTI_PWR_A9_CNTL0));
	pr_debug("----P_AO_RTI_PWR_A9_CNTL1(%08x)    = %08x\n",P_AO_RTI_PWR_A9_CNTL1,aml_read_reg32(P_AO_RTI_PWR_A9_CNTL1));

}

void meson_set_cpu_ctrl_reg(int cpu,int is_on)
{
	spin_lock(&clockfw_lock);

#ifdef CONFIG_MESON_TRUSTZONE
	uint32_t value = 0;
	value = meson_read_corectrl();
	value = value & ~(1U << cpu) | (is_on << cpu);
	value |= 1;
	meson_modify_corectrl(value);
#else
	aml_set_reg32_bits(MESON_CPU_CONTROL_REG,is_on,cpu,1);
	aml_set_reg32_bits(MESON_CPU_CONTROL_REG,1,0,1);
#endif

	spin_unlock(&clockfw_lock);
}

void meson_set_cpu_ctrl_addr(uint32_t cpu, const uint32_t addr)
{
	spin_lock(&clockfw_lock);

#ifdef CONFIG_MESON_TRUSTZONE
	meson_auxcoreboot_addr(cpu, addr);
#else
	aml_write_reg32((MESON_CPU1_CONTROL_ADDR_REG + ((cpu-1) << 2)), addr);
#endif

	spin_unlock(&clockfw_lock);	
}

int meson_get_cpu_ctrl_addr(int cpu)
{

#ifdef CONFIG_MESON_TRUSTZONE
//	meson_auxcoreboot_addr(cpu, addr);
#else
//printk("sram=0x%x addr=0x%x\n",(MESON_CPU1_CONTROL_ADDR_REG + ((cpu-1) << 2)),addr);
	return aml_read_reg32(MESON_CPU1_CONTROL_ADDR_REG + ((cpu-1) << 2));
#endif

}

static inline unsigned long meson_smp_wait_others(unsigned status)
{
	unsigned long count = 0;
	int mask;
	int cpu = 0, my = smp_processor_id();

	mask = (((1 << nr_cpu_ids) - 1) & (~(1 << my)));
	do {
		__asm__ __volatile__ ("wfe" : : : "memory");
		for_each_online_cpu(cpu) {

			if (cpu != my && MESON_CPU_STATUS(cpu) == status) {
				count++;
				mask &= ~(1 << cpu);
			}
		}

	} while (mask);

	return count;
}

static inline void meson_smp_init_transaction(void)
{
    int cpu;

#ifdef CONFIG_MESON_TRUSTZONE
	meson_modify_corectrl(0);
#else
    aml_write_reg32(MESON_CPU_CONTROL_REG, 0);
#endif

    for_each_online_cpu(cpu) {
        aml_write_reg32(MESON_CPU_STATUS_REG(cpu), 0);
    }
}

#endif /* CONFIG_SMP */

static int clk_set_rate_a9(struct clk *clk, unsigned long rate)
{
	int ret;	
	unsigned long irq_flags;

	//printk("clk_set_rate_a9() clk: %d\n",rate);

	if(rate > CPU_FREQ_LIMIT)
		rate = CPU_FREQ_LIMIT;

	irq_flags = arch_local_irq_save();
	preempt_disable();

	ret = _clk_set_rate_cpu(clk, rate, 0);

	preempt_enable();
	arch_local_irq_restore(irq_flags);

	return ret;
}
static unsigned long clk_get_rate_vid(struct clk * clkdev)
{

	unsigned long clk;
	unsigned int vid_cntl = aml_read_reg32(P_HHI_VID_PLL_CNTL);
	unsigned long parent_clk;
	unsigned od,M,N;
	parent_clk = clk_get_rate(clkdev->parent);
	parent_clk /= 1000000;
	od = (vid_cntl>>16)&3;
	M = vid_cntl&0x1FF;
	N = (vid_cntl>>9)&0x1F;
	if(od == 0)
		od = 1;
	else if(od == 1)
		od = 2;
	else if(od == 2)
		od = 4;
	clk = parent_clk * M / N;
	clk /= od;
	clk *= 1000000;
	return clk;
}

static unsigned long clk_get_rate_fixed(struct clk * clkdev)
{

	unsigned long clk;
	unsigned int fixed_cntl = aml_read_reg32(P_HHI_MPLL_CNTL);
	unsigned long parent_clk;
	unsigned od,M,N;
	parent_clk = clk_get_rate(clkdev->parent);
	parent_clk /= 1000000;
	od = (fixed_cntl>>16)&3;
	M = fixed_cntl&0x1FF;
	N = (fixed_cntl>>9)&0x1F;
	if(od == 0)
		od = 1;
	else if(od == 1)
		od = 2;
	else if(od == 2)
		od = 4;
	clk = parent_clk * M / N;
	clk /= od;
	clk *= 1000000;
	return clk;
}

static unsigned long clk_get_rate_hpll(struct clk * clkdev)
{
	printk("TODO: clk_get_rate_hpll() is not implement in M8 now\n");
	return 0;
/*
	unsigned long clk;
	unsigned int vid_cntl = aml_read_reg32(P_HHI_VID_PLL_CNTL);
	unsigned long parent_clk;
	unsigned od_fb,od_hdmi,od_ldvs,M,N;
	parent_clk = clk_get_rate(clkdev->parent);
	parent_clk /= 1000000;
	od_ldvs = (vid_cntl>>16)&3;
	od_hdmi = (vid_cntl>>18)&3;
	od_fb = (vid_cntl>>20)&3;
	M = vid_cntl&0x3FF;
	N = (vid_cntl>>10)&0x1F;
	if(od_hdmi == 0)
		od_hdmi = 1;
	else if(od_hdmi == 1)
		od_hdmi = 2;
	else if(od_hdmi == 2)
		od_hdmi = 4;
	if(od_fb == 0)
		od_fb = 1;
	else if(od_fb == 1)
		od_fb = 2;
	else if(od_fb == 2)
		od_fb = 4;
printk("N=%d, od_hdmi=%d vid_cntl=0x%x\n",N,od_hdmi,vid_cntl);
	clk = parent_clk * M * od_fb / N;
	clk /= od_hdmi;
	clk *= 1000000;
	return clk;
	*/
}

static unsigned long clk_get_rate_clk81(struct clk * clkdev)
{
	unsigned long parent_clk;
	unsigned long fixed_div_src;

	parent_clk = clk_get_rate(clkdev->parent);
	fixed_div_src = (aml_read_reg32(P_HHI_MPEG_CLK_CNTL) >> 12) & 0x7;

	if(7 == fixed_div_src)
		parent_clk/=5;
	else if(6 == fixed_div_src)
		parent_clk/=3;
	else if(5 == fixed_div_src)
		parent_clk/=4;
	else{
		printk("Error: clk81 not in fixed_pll seleting.\n");
		return 0;
	}

	parent_clk/=((aml_read_reg32(P_HHI_MPEG_CLK_CNTL) & 0x3f))+1;
	
	return parent_clk;	
}
#define CLK_DEFINE(devid,conid,msr_id,setrate,getrate,en,dis,privdata)  \
    static struct clk clk_##devid={                                     \
        .set_rate=setrate,.get_rate=getrate,.enable=en,.disable=dis,    \
        .priv=privdata,.parent=&clk_##conid ,.msr=msr_id                \
    };                                                                  \
    static struct clk_lookup clk_lookup_##devid={                       \
        .dev_id=#devid,.con_id=#conid,.clk=&clk_##devid                 \
    };clkdev_add(&clk_lookup_##devid)

///TOP level
static struct clk clk_xtal = {
	.rate		= -1,
	.get_rate	= clk_get_rate_xtal,
};

static struct clk_lookup clk_lookup_xtal = {
	.dev_id		= "xtal",
	.con_id		= NULL,
	.clk		= &clk_xtal
};


static int __init a9_clk_max(char *str)
{
    unsigned long  clk=clkparse(str, 0);
    if(clk<SYS_PLL_TABLE_MIN || clk>SYS_PLL_TABLE_MAX)
        return 0;
    setup_a9_clk_max=clk-(clk%24000000);
    BUG_ON(setup_a9_clk_min>setup_a9_clk_max);
    return 0;
}
early_param("a9_clk_max", a9_clk_max);
static int __init a9_clk_min(char *str)
{
    unsigned long  clk = clkparse(str, 0);
    if (clk < SYS_PLL_TABLE_MIN || clk > SYS_PLL_TABLE_MAX)
        return 0;
    setup_a9_clk_min = clk - (clk % 24000000);
    BUG_ON(setup_a9_clk_min>setup_a9_clk_max);
    return 0;
}

early_param("a9_clk_min", a9_clk_min);
static int set_sys_pll(struct clk *clk,  unsigned long dst)
{
	int idx,loop = 0;
	static int only_once = 0;
	unsigned int curr_cntl = aml_read_reg32(P_HHI_SYS_PLL_CNTL);
	unsigned int cpu_clk_cntl = 0;
	unsigned int cntl;
	latency_data_t latency;

	if (dst < SYS_PLL_TABLE_MIN) dst = SYS_PLL_TABLE_MIN;
	if (dst > SYS_PLL_TABLE_MAX) dst = SYS_PLL_TABLE_MAX;
 
	idx = ((dst - SYS_PLL_TABLE_MIN) / 1000000) / 24;
	cpu_clk_cntl = sys_pll_settings[idx][1];
	latency.d32 =  sys_pll_settings[idx][2];

	if (cpu_clk_cntl != curr_cntl) {
SETPLL:
		aml_set_reg32_bits(P_HHI_SYS_CPU_CLK_CNTL1,latency.b.ext_div_n,20,10);
		if(latency.b.ext_div_n)
			aml_set_reg32_bits(P_HHI_SYS_CPU_CLK_CNTL, 3, 2, 2);
		else
			aml_set_reg32_bits(P_HHI_SYS_CPU_CLK_CNTL, 0, 2, 2);
      //aml_write_reg32(P_HHI_SYS_PLL_CNTL,  cpu_clk_cntl | (1 << 29));
        if((cpu_clk_cntl & 0x3fff) != (curr_cntl & 0x3fff)) {
            //dest M,N is equal to curr_cntl, So, we neednot reset the pll, just change the OD.
            aml_write_reg32(P_HHI_SYS_PLL_CNTL,  cpu_clk_cntl | (1 << 29));
        }

		if(only_once == 99){
			only_once = 1;
			aml_write_reg32(P_HHI_SYS_PLL_CNTL2, M8_SYS_PLL_CNTL_2);
			aml_write_reg32(P_HHI_SYS_PLL_CNTL3, M8_SYS_PLL_CNTL_3);
			aml_write_reg32(P_HHI_SYS_PLL_CNTL4, M8_SYS_PLL_CNTL_4);
			aml_write_reg32(P_HHI_SYS_PLL_CNTL5, M8_SYS_PLL_CNTL_5);
		}
		aml_set_reg32_bits(P_HHI_SYS_PLL_CNTL2,latency.b.afc_dsel_bp_in,12,1);
		aml_set_reg32_bits(P_HHI_SYS_PLL_CNTL2,latency.b.afc_dsel_bp_en,13,1);

		aml_write_reg32(P_HHI_SYS_PLL_CNTL,  cpu_clk_cntl);

		udelay_scaled(100, dst / 1000000, 24 /*clk_get_rate_xtal*/);

		cntl = aml_read_reg32(P_HHI_SYS_PLL_CNTL);
		if((cntl & (1<<31)) == 0){
			if(loop++ >= 10){
				loop = 0;
				printk(KERN_ERR"CPU freq: %ld MHz, syspll (%x) can't lock: \n",dst/1000000,cntl);
				printk(KERN_ERR"  [10c0..10c4]%08x, %08x, %08x, %08x, %08x: [10a5]%08x, [10c7]%08x \n",
					aml_read_reg32(P_HHI_SYS_PLL_CNTL),
					aml_read_reg32(P_HHI_SYS_PLL_CNTL2),	
					aml_read_reg32(P_HHI_SYS_PLL_CNTL3),
					aml_read_reg32(P_HHI_SYS_PLL_CNTL4),	
					aml_read_reg32(P_HHI_SYS_PLL_CNTL5),	
					aml_read_reg32(P_HHI_MPLL_CNTL6),
					aml_read_reg32(P_HHI_DPLL_TOP_1)
				);
				if(!(aml_read_reg32(P_HHI_DPLL_TOP_1) & 0x2)){
					printk(KERN_ERR"  SYS_TDC_CAL_DONE triggered, disable TDC_CAL_EN\n");
					aml_set_reg32_bits(P_HHI_SYS_PLL_CNTL4, 0, 10, 1);
					printk(KERN_ERR"  HHI_SYS_PLL_CNTL4: %08x\n", 
						aml_read_reg32(P_HHI_SYS_PLL_CNTL4));
				}else{
					latency.b.afc_dsel_bp_in = !latency.b.afc_dsel_bp_in;
					printk(KERN_ERR"  INV afc_dsel_bp_in, new latency=%08x\n",latency.d32);
					sys_pll_settings[idx][2] = latency.d32;/*write back afc_dsel_bp_in bit.*/
				}
				printk(KERN_ERR"  Try again!\n");
			}
			goto SETPLL;
		};

	}else {
		//printk(KERN_INFO "(CTS_CPU_CLK) No Change (0x%x)\n", cpu_clk_cntl);
	}

	if (clk)
		clk->rate = (idx * 24000000) + SYS_PLL_TABLE_MIN;

	return idx;
}

static int set_hpll_pll(struct clk * clk, unsigned long dst)
{
	printk("TODO: set_hpll_pll not implement\n");
	return 1;
}
static int set_fixed_pll(struct clk * clk, unsigned long dst)
{
	if(dst == 2000000000){
		//fixed pll = xtal * M(0:8) * OD_FB(4) /N(9:13) /OD(16:17)
		//M: 0~511  OD_FB:0~1 + 1, N:0~32 + 1 OD:0~3 + 1
		//recommend this pll is fixed as 2G.
		unsigned long xtal = 24000000;
		unsigned cntl = aml_read_reg32(P_HHI_MPLL_CNTL);
		unsigned m = cntl&0x1FF;
		unsigned n = ((cntl>>9)&0x1F);
		unsigned od = ((cntl >>16)&3) + 1;
		unsigned od_fb = ((aml_read_reg32(P_HHI_MPLL_CNTL4)>>4)&1) + 1;
		unsigned long rate;
		if(clk->parent)
			xtal = clk_get_rate(clk->parent);
		xtal /= 1000000;
		rate = xtal * m * od_fb;
		rate /= n;
		rate /= od;
		rate *= 1000000;
		if(dst != rate){
			M8_PLL_RESET(P_HHI_MPLL_CNTL);
			aml_write_reg32(P_HHI_MPLL_CNTL2, M8_MPLL_CNTL_2 );
			aml_write_reg32(P_HHI_MPLL_CNTL3, M8_MPLL_CNTL_3 );
			aml_write_reg32(P_HHI_MPLL_CNTL4, M8_MPLL_CNTL_4 );
			aml_write_reg32(P_HHI_MPLL_CNTL5, M8_MPLL_CNTL_5 );
			aml_write_reg32(P_HHI_MPLL_CNTL6, M8_MPLL_CNTL_6 );
			aml_write_reg32(P_HHI_MPLL_CNTL7, M8_MPLL_CNTL_7 );
			aml_write_reg32(P_HHI_MPLL_CNTL8, M8_MPLL_CNTL_8 );
			aml_write_reg32(P_HHI_MPLL_CNTL9, M8_MPLL_CNTL_9 );
			aml_write_reg32(P_HHI_MPLL_CNTL, M8_MPLL_CNTL );
			M8_PLL_WAIT_FOR_LOCK(P_HHI_MPLL_CNTL);
		}
	}
	else
		return -1;
	return 0;
}

static int set_vid_pll(struct clk * clk, unsigned long dst)
{
	printk("TODO: set_vid_pll not implement\n");
	return 1;
}

//------------------------------------
//return 0:not in the clock tree, 1:in the clock tree
static int clk_in_clocktree(struct clk *clktree, struct clk *clk)
{
	struct clk *p;
	int ret = 0;
	if(IS_CLK_ERR(clk) || IS_CLK_ERR(clktree))
		return 0;
	if(clktree == clk)
		return 1;
	p = (struct clk*)clktree->sibling.next;
	while(p){
		if(p == clk){
			ret = 1;
			break;
		}
		p = (struct clk*)p->sibling.next;
	}
	if(ret == 1)
		return ret;
	return clk_in_clocktree((struct clk*)clktree->child.next, clk);
}

//return 0:ok, 1:fail
static int meson_clk_register(struct clk* clk, struct clk* parent)
{
	if (clk_in_clocktree(parent,clk))
			return 0;
	mutex_lock(&clock_ops_lock);
	clk->parent = parent;
	if (parent->child.next == NULL) {
		parent->child.next = (struct list_head*)clk;
		clk->sibling.next = NULL;
		clk->sibling.prev = NULL;
	}
	else {
		struct clk* p = (       struct clk*)(parent->child.next);
		while(p->sibling.next != NULL)
			p = (       struct clk*)(p->sibling.next);
		p->sibling.next = (struct list_head*)clk;
		clk->sibling.prev = (struct list_head*)p;
		clk->sibling.next = NULL;
	}
	mutex_unlock(&clock_ops_lock);
	return 0;
}

int clk_register(struct clk *clk,const char *parent)
{
	struct clk* p = clk_get_sys(parent,0);
	if(!IS_CLK_ERR(p))
		return meson_clk_register(clk,p);
	return 1;
}
EXPORT_SYMBOL(clk_register);

void clk_unregister(struct clk *clk)
{
		if(IS_CLK_ERR(clk))
			return;
		mutex_lock(&clock_ops_lock);
		if(clk->sibling.next){
				struct clk* pnext = (struct clk*)(clk->sibling.next);
				pnext->sibling.prev = clk->sibling.prev;
				if(clk->sibling.prev)
					((struct clk*)(clk->sibling.prev))->sibling.next = (struct list_head*)pnext;
				else
					clk->parent->child.next = (struct list_head*)pnext;
				
		}
		else if(clk->sibling.prev){
				struct clk* prev = (struct clk*)(clk->sibling.prev);
				prev->sibling.next = clk->sibling.next;
				if(clk->sibling.next)
					((struct clk*)(clk->sibling.next))->sibling.prev =(struct list_head*) prev;
		}
		else{
			struct clk* parent = clk->parent;
			if(parent)
				parent->child.next = NULL;
		}
		clk->sibling.next = NULL;
		clk->sibling.prev = NULL;
		mutex_unlock(&clock_ops_lock);
}
EXPORT_SYMBOL(clk_unregister);

/**
 *  Check clock status.
 *
 *  0 -- Disabled
 *  1 -- Enabled
 *  2 -- Unknown
 */
int clk_get_status(struct clk *clk)
{
	int ret = 2;
	unsigned long flags;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (clk->status)
		ret = clk->status(clk);
	else if (clk->clk_gate_reg_adr != 0)
		ret = ((aml_read_reg32(clk->clk_gate_reg_adr) & clk->clk_gate_reg_mask) ? 1 : 0);
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_get_status);

//return: 0:success  1: fail
int clk_ops_register(struct clk *clk, struct clk_ops *ops)
{
	int found = 0;
	struct clk_ops *p;

	mutex_lock(&clock_ops_lock);
	ops->next = NULL;
	p = clk->clk_ops;
	while(p != NULL){
		if(p == ops){
			found = 1;
			break;
		}
		p = p->next;
	}

	if(found == 0){
		if(clk->clk_ops	== NULL)
			clk->clk_ops = ops;
		else{
			struct clk_ops* p = clk->clk_ops;
			while(p->next)
				p = p->next;
			p->next = ops;
		}
	}
	mutex_unlock(&clock_ops_lock);
	return 0;
}
EXPORT_SYMBOL(clk_ops_register);

//return: 0:success  1: fail
int clk_ops_unregister(struct clk *clk, struct clk_ops *ops)
{
	if(ops == NULL || IS_CLK_ERR(clk))
		return 0;
		
	mutex_lock(&clock_ops_lock);
	
	if(clk->clk_ops == ops){
		if(clk->clk_ops->next == NULL)
			clk->clk_ops = NULL;
		else
			clk->clk_ops = clk->clk_ops->next;
	}
	else if(clk->clk_ops){
		struct clk_ops *p, *p1;
		p = clk->clk_ops->next;
		p1 = clk->clk_ops;
		while(p != NULL && p != ops){
			p1 = p;
			p = p->next;
		}
		if(p == ops)
			p1->next = p->next;
	}
	ops->next = NULL;
	mutex_unlock(&clock_ops_lock);
	return 0;
}
EXPORT_SYMBOL(clk_ops_unregister);

///FIXME add data later
#define PLL_CLK_DEFINE(name,msr)    		\
	static unsigned pll_##name##_data[10];	\
    CLK_DEFINE(pll_##name,xtal,msr,set_##name##_pll, \
    		clk_msr_get,NULL,NULL,&pll_##name##_data)
_Pragma("GCC diagnostic ignored \"-Wdeclaration-after-statement\"");
#define PLL_RELATION_DEF(child,parent) meson_clk_register(&clk_pll_##child,&clk_##parent)
#define CLK_PLL_CHILD_DEF(child,parent) meson_clk_register(&clk_##child,&clk_pll_##parent)


#ifdef CONFIG_CLKTREE_DEBUG

extern struct clk_lookup * lookup_clk(struct clk* clk);
void print_clk_name(struct clk* clk)
{
		printk("Todo: we have not lookup_clk in 3.7 kernel!\n");
		//struct clk_lookup * p = lookup_clk(clk);
		//if(p)
		//	printk("  %s  \n",p->dev_id);
		//else
		//	printk(" unknown \n");
}

void dump_child(int nlevel, struct clk* clk)
{
		if(!IS_CLK_ERR(clk)){
			int i;
			for(i = 0; i < nlevel; i++)
				printk("  ");
			print_clk_name(clk);
			dump_child(nlevel+6,(struct clk*)(clk->child.next));
			{
				struct clk * p = (struct clk*)(clk->sibling.prev);
				while(p){
					for(i = 0; i < nlevel; i++)
						printk("  ");
					print_clk_name(p);
					dump_child(nlevel+6,(struct clk*)(p->child.next));
					p = (struct clk*)(p->sibling.prev);
				}
				
				p = (struct clk*)(clk->sibling.next);
				while(p){
					for(i = 0; i < nlevel; i++)
						printk("  ");
					print_clk_name(p);
					dump_child(nlevel+6,(struct clk*)(p->child.next));
					p = (struct clk*)(p->sibling.next);
				}
			}
		}
}

void dump_clock_tree(struct clk* clk)
{
	printk("========= dump clock tree==============\n");
	mutex_lock(&clock_ops_lock);

	int nlevel = 0;
	if(!IS_CLK_ERR(clk)){
		print_clk_name(clk);
		dump_child(nlevel + 6,(struct clk*)(clk->child.next));
			{	int i;
				struct clk * p = (struct clk*)clk->sibling.prev;
				while(p){
					for(i = 0; i < nlevel; i++)
						printk("  ");
					print_clk_name(p);
					dump_child(nlevel+6,(struct clk*)(p->child.next));
					p = (struct clk*)clk->sibling.prev;
				}
				
				p = (struct clk*)clk->sibling.next;
				while(p){
					for(i = 0; i < nlevel; i++)
						printk("  ");
					print_clk_name(p);
					dump_child(nlevel+6,(struct clk*)(p->child.next));
					p = (struct clk*)clk->sibling.next;
				}
			}
	}
	mutex_unlock(&clock_ops_lock);
	printk("========= dump clock tree end ==============\n");
}

static ssize_t  clock_tree_store(struct class *cla, struct class_attribute *attr, const char *buf,size_t count)
{
	char* p = (char *)buf;
	char cmd;
	char name[20];
	unsigned long rate = 0;
	int idx = 0;
	if(count < 1)
		return -1;
	while((idx < count) && ((*p == ' ') || (*p == '\t')|| (*p == '\r') || (*p == '\n'))){
		 p++;
		 idx++;
	}
	
	if(idx <= count){
		int i;
		cmd = *p;
		p++;
		while((idx < count) && ((*p == ' ') || (*p == '\t')|| (*p == '\r') || (*p == '\n'))){
		 p++;
		 idx++;
		}
		i = 0;
		while((idx < count) && (*p != ' ') && (*p != '\t') && (*p != '\r') && (*p != '\n')){
			name[i++] = *p;
			p++;
			idx++;
		}	
		name[i] = '\0';
		p++;
		while((idx < count) && ((*p == ' ') || (*p == '\t')|| (*p == '\r') || (*p == '\n'))){
		 p++;
		 idx++;
		}
		if(idx < count){
			int val;
			sscanf(p, "%d", &val);
			rate = val;
		}
				
		if(cmd == 'r'){
			if(strcmp(name,"tree") == 0){
				struct clk* clk = clk_get_sys("xtal",NULL);
				if(!IS_CLK_ERR(clk))
					dump_clock_tree(clk);
			}
			else{
				struct clk* clk = clk_get_sys(name,NULL);
				if(!IS_CLK_ERR(clk)){
					clk->rate = 0; //enforce update rate 
					printk("%s : %lu\n",name,clk_get_rate(clk));
				}
				else
					printk("no %s in tree.\n",name);
			}
		}	
		else if(cmd == 'w'){		
				struct clk* clk = clk_get_sys(name,NULL);
				if(!IS_CLK_ERR(clk)){
					if(rate < 1000000 || rate >1512000000)
						printk("Invalid rate : %lu\n",rate);
					else{
						if(clk_set_rate(clk,rate) ==0)
							printk("%s = %lu\n",name,rate);
						else
							printk("set %s = %lu failed.\n",name,rate);
					}
				}
				else
					printk("no %s in tree.\n",name);			
		}	
		else if(cmd == 'o'){
				struct clk* clk = clk_get_sys(name,NULL);
				if(!IS_CLK_ERR(clk)){
					if(clk_enable(clk) ==0)
							printk("%s gate on\n",name);
					else
							printk("gate on %s failed.\n",name);
				}
				else
					printk("no %s in tree.\n",name);			
			
		}
		else if(cmd == 'f'){
				struct clk* clk = clk_get_sys(name,NULL);
				if(!IS_CLK_ERR(clk)){
						clk_disable(clk);
						printk("gate off %s.\n",name);
				}
				else
					printk("no %s in tree.\n",name);						
		}
		else
			printk("command:%c invalid.\n",cmd);
	}

	return count;
}

static ssize_t  clock_tree_show(struct class *cla, struct class_attribute *attr, char *buf)
{
	printk("Usage:\n");
	printk("1. echo r tree >clkTree       ,display the clock tree.\n");
	printk("2. echo r clockname >clkTree  ,display the clock rate.\n");
	printk("3. echo w clockname rate >clkTree  ,modify the clock rate.\n");
	printk("4. echo o clockname >clkTree  ,gate on clock.\n");
	printk("5. echo f clockname >clkTree  ,gate off clock.\n");
	
	printk("Example:\n");
	printk("1. display the clock tree.\n");
	printk("   echo r tree >clkTree\n");
	printk("2. display clk81 rate.\n");
	printk("   echo r clk81 >clkTree\n");
	printk("3. modify sys pll as 792M.\n");
	printk("   echo w pll_sys 792000000 >clkTree\n");
	return 0;
}

static struct class_attribute clktree_class_attrs[] = {


	__ATTR(clkTree, S_IRWXU, clock_tree_show, clock_tree_store),
	__ATTR_NULL,
};

static struct class meson_clktree_class = {    
	.name = "meson_clocktree",
	.class_attrs = clktree_class_attrs,
};
#endif

// -------------------- frequency limit sysfs ---------------------
static ssize_t freq_limit_store(struct class *cla, struct class_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	freq_limit = input;
	return count;
}
static ssize_t freq_limit_show(struct class *cla, struct class_attribute *attr, char *buf)
{
	printk("%u\n", freq_limit);
	return sprintf(buf, "%d\n", freq_limit);
}

static ssize_t check_clock_store(struct class *cla, struct class_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	measure_cpu_clock = input;
	return count;
}
static ssize_t check_clock_show(struct class *cla, struct class_attribute *attr, char *buf)
{
	printk("%u\n", measure_cpu_clock);
	return sprintf(buf, "%d\n", measure_cpu_clock);
}

static struct class_attribute freq_limit_class_attrs[] = {
	__ATTR(limit, S_IRUGO|S_IWUSR|S_IWGRP, freq_limit_show, freq_limit_store),
	__ATTR(check_clock, S_IRUGO|S_IWUSR|S_IWGRP, check_clock_show, check_clock_store),
	__ATTR_NULL,
};

static struct class meson_freq_limit_class = {
	.name = "freq_limit",
	.class_attrs = freq_limit_class_attrs,
};

static int __init meson_clock_init(void)
{
#if 1
	clkdev_add(&clk_lookup_xtal);
	CLK_DEFINE(pll_ddr,xtal,-1/*=47??*/,NULL,NULL/*clk_msr_get*/,NULL,NULL,NULL);
	PLL_CLK_DEFINE(sys,(unsigned long)-1);
	PLL_CLK_DEFINE(vid,6);
	PLL_CLK_DEFINE(fixed,-1);
	PLL_CLK_DEFINE(hpll,19);///19 is right?
	clk_pll_fixed.msr_mul = 425 * 24; //This value just let get fixed_pll = 2.55G
	clk_pll_fixed.msr_div = 4;
	clk_pll_sys.get_rate = clk_get_rate_sys;
	clk_pll_vid.get_rate = clk_get_rate_vid;
	clk_pll_fixed.get_rate = clk_get_rate_fixed;
	clk_pll_hpll.get_rate = clk_get_rate_hpll;

	clk_pll_vid.max = 3000000000U;//3.0G
	clk_pll_vid.min = 1200000000U;//1.2G
	clk_pll_hpll.max = 3000000000U;//3.0G
	clk_pll_hpll.min = 1200000000U;//1.2G
	clk_pll_sys.max = 2500000000U;//2.5G
	clk_pll_sys.min = 1200000000U;//1.2G
	clk_pll_ddr.max = 1512000000U;//1.5G
	clk_pll_ddr.min = 750000000U;//750M
	clk_pll_fixed.max = 2550000000U;//2.55G
	clk_pll_fixed.min = 2550000000U;//2.55G

	//create pll tree
	PLL_RELATION_DEF(sys,xtal);
	PLL_RELATION_DEF(ddr,xtal);
	PLL_RELATION_DEF(vid,xtal);
	PLL_RELATION_DEF(fixed,xtal);
	PLL_RELATION_DEF(hpll,xtal);


	// Add clk81
	if(((aml_read_reg32(P_HHI_MPEG_CLK_CNTL) >> 12) & 0x7) >= 5)
	{
		CLK_DEFINE(clk81, pll_fixed, -1, NULL, clk_get_rate_clk81, NULL, NULL, NULL);

		// Add clk81 as pll_fixed's child
	    //CLK_PLL_CHILD_DEF(clk81, fixed);
	    clk_clk81.clk_gate_reg_adr = P_HHI_MPEG_CLK_CNTL;
	    clk_clk81.clk_gate_reg_mask = (1<<7);
		clk_clk81.open_irq = 1;
	}else{
		printk("Error: clk81 not be selected at fixed_pll.\n");
	}

	// Add CPU clock
	CLK_DEFINE(a9_clk, pll_sys, -1, clk_set_rate_a9, clk_get_rate_a9, NULL, NULL, NULL);
	clk_a9_clk.min = setup_a9_clk_min;
	clk_a9_clk.max = setup_a9_clk_max;
	clk_a9_clk.round_rate = clk_round_rate_sys;
	//clk_a9_clk.open_irq = 1;
	CLK_PLL_CHILD_DEF(a9_clk,sys);


#ifdef CONFIG_AMLOGIC_USB
    // Add clk usb0
    CLK_DEFINE(usb0,xtal,4,NULL,clk_msr_get,clk_enable_usb,clk_disable_usb,"usb0");
    meson_clk_register(&clk_usb0,&clk_xtal);
    //clk_usb0.clk_gate_reg_adr = P_USB_ADDR0;
    //clk_usb0.clk_gate_reg_mask = (1<<0);
    
    // Add clk usb1
    CLK_DEFINE(usb1,xtal,5,NULL,clk_msr_get,clk_enable_usb,clk_disable_usb,"usb1");
    meson_clk_register(&clk_usb1,&clk_xtal);
    //clk_usb1.clk_gate_reg_adr = P_USB_ADDR8;
    //clk_usb1.clk_gate_reg_mask = (1<<0);
#endif
		
	{
		// Dump clocks
		char *clks[] = { 
				"xtal",
				"pll_sys",
				"pll_fixed",
				"pll_vid",
				"pll_ddr",
				"a9_clk",
				"clk81",
		};
		int i;
		int count = ARRAY_SIZE(clks);
		struct clk *clk;

		for (i = 0; i < count; i++) {
			char *clk_name = clks[i];

			clk = clk_get_sys(clk_name, NULL);
			if (!IS_CLK_ERR(clk))
				printk("clkrate [ %s \t] : %lu\n", clk_name, clk_get_rate(clk));
		}
	}
		
#ifdef CONFIG_CLKTREE_DEBUG
	class_register(&meson_clktree_class);
#endif
	class_register(&meson_freq_limit_class);
#endif
	return 0;
}

/* initialize clocking early to be available later in the boot */
core_initcall(meson_clock_init);
