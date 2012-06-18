/*
 * arch/arm/mach-sun3i/pm/standby/sun3i_sram_main.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
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

 #include <mach/platform.h>
 #include <mach/irqs.h>
 #include <sun3i_standby.h>

#if EN_POWER_D
static unsigned char data[4];
#endif

void standby_delay(unsigned int count)
 {
 	volatile unsigned int tick=count;
 	while(tick--);
 }

void standby_save_env(struct sys_reg_t *save_env)
{
	/* save cmu regs */
	save_env->cmu_regs.core_pll = aw_readl(SW_CCM_CORE_VE_PLL_REG);
	save_env->cmu_regs.aud_hosc = aw_readl(SW_CCM_AUDIO_HOSC_PLL_REG);
	save_env->cmu_regs.ddr_pll = aw_readl(SW_CCM_SDRAM_PLL_REG);
	save_env->cmu_regs.bus_clk = aw_readl(SW_CCM_AHB_APB_CFG_REG);
#if MODIFY_AHB_APB_EN
	save_env->cmu_regs.ahb_clk = aw_readl(SW_CCM_AHB_GATE_REG);
	save_env->cmu_regs.apb_clk = aw_readl(SW_CCM_APB_GATE_REG);
#endif
#if EN_POWER_D
	save_env->twi_regs.reg_clkr = aw_readl(SW_TWI1_CCR_REG);  //for TWI address mapping
#endif
}

void standby_restore_env(struct sys_reg_t *restore_env)
{
	/*restore 24M and LDO*/
	aw_writel(restore_env->cmu_regs.aud_hosc,SW_CCM_AUDIO_HOSC_PLL_REG);
	//standby_delay(50);

	/*COREPLL to 24M*/
	aw_writel((aw_readl(SW_CCM_AHB_APB_CFG_REG)&BUS_CCLK_MASK)|BUS_CCLK_24M,SW_CCM_AHB_APB_CFG_REG);
	standby_delay(50);

	/*restore core power*/
#if EN_POWER_D
	twi_byte_rw(TWI_OP_WR,0x34,0x23,&data[0]);
	standby_twi_exit();
#endif

	/* restore cmu regs*/
	aw_writel(restore_env->cmu_regs.core_pll,SW_CCM_CORE_VE_PLL_REG);
	aw_writel(restore_env->cmu_regs.ddr_pll,SW_CCM_SDRAM_PLL_REG);
	aw_writel(restore_env->cmu_regs.bus_clk,SW_CCM_AHB_APB_CFG_REG);
#if MODIFY_AHB_APB_EN
	aw_writel(restore_env->cmu_regs.ahb_clk,SW_CCM_AHB_GATE_REG);
	aw_writel(restore_env->cmu_regs.apb_clk,SW_CCM_APB_GATE_REG);
#endif
}


void standby_enter_low(void)
{
	/*sdram self-refresh*/
	aw_writel(aw_readl(SW_DRAM_SDR_CTL_REG)|SDR_ENTER_SELFRFH,SW_DRAM_SDR_CTL_REG);
	while(!(aw_readl(SW_DRAM_SDR_CTL_REG)&SDR_SELFRFH_STATUS));

	/*gate off sdram*/
	aw_writel(aw_readl(SW_CCM_SDRAM_PLL_REG)&~SDR_CLOCK_GATE_EN,SW_CCM_SDRAM_PLL_REG);

	/*disable VE pll*/
	aw_writel(aw_readl(SW_CCM_CORE_VE_PLL_REG)&~(1<<15),SW_CCM_CORE_VE_PLL_REG);

	/*COREPLL to 24M*/
	aw_writel((aw_readl(SW_CCM_AHB_APB_CFG_REG)&BUS_CCLK_MASK)|BUS_CCLK_24M,SW_CCM_AHB_APB_CFG_REG);
	standby_delay(100);

	/*down core power*/
#if EN_POWER_D
	standby_twi_init(0);
	twi_byte_rw(TWI_OP_RD,0x34,0x23,&data[0]);
	data[2] = 0x0C;  	// 1V
	twi_byte_rw(TWI_OP_WR,0x34,0x23,&data[2]);
#endif

	/*COREPLL to 32K*/
	aw_writel((aw_readl(SW_CCM_AHB_APB_CFG_REG)&BUS_CCLK_MASK)|BUS_CCLK_32K,SW_CCM_AHB_APB_CFG_REG);
	standby_delay(50);

	/*disable HOSC and LDO*/
	aw_writel(aw_readl(SW_CCM_AUDIO_HOSC_PLL_REG)&~(1|(1<<15)),SW_CCM_AUDIO_HOSC_PLL_REG);

#if MODIFY_AHB_APB_EN
	aw_writel((1<<13)|(1<<16)|(1),SW_CCM_AHB_GATE_REG);
	aw_writel((1<<5)|1,SW_CCM_APB_GATE_REG);
#endif

}

void standby_exit_low(void)
{
	/*gate on sdram*/
	aw_writel(aw_readl(SW_CCM_SDRAM_PLL_REG)|SDR_CLOCK_GATE_EN,SW_CCM_SDRAM_PLL_REG);

	/*sdram exit self-refresh*/
	aw_writel(aw_readl(SW_DRAM_SDR_CTL_REG)&~SDR_ENTER_SELFRFH,SW_DRAM_SDR_CTL_REG);
	while(aw_readl(SW_DRAM_SDR_CTL_REG)&SDR_SELFRFH_STATUS);
}

void standby_loop(struct aw_pm_arg *arg)
{
	int tmp=0,wake_cond=0;

	/*set irqs to wake up system*/
	wake_cond = (1<<SW_INT_IRQNO_TOUCH_PANEL)|(1<<SW_INT_IRQNO_LRADC);

	/*wait irqs*/
	do{
		WAIT_FOR_IRQ(tmp);
		tmp = aw_readl(SW_INT_PENDING_REG0);
		if(tmp&wake_cond)
			break;
	}while(1);

	//easy_print_reg("IRQPEND0=%x\n",aw_readl(SW_INT_PENDING_REG0));
	//easy_print_reg("IRQPEND1=%x\n",aw_readl(SW_INT_PENDING_REG1));
}
