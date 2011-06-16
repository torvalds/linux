
#include <mach/rk29_iomap.h>
#include <mach/board.h>
#include <mach/sram.h>
#include <asm/io.h>
#include <mach/cru.h>
#include <linux/regulator/rk29-pwm-regulator.h>

#define pwm_write_reg(addr, val)	__raw_writel(val, addr + (RK29_PWM_BASE + 2*0x10))
#define pwm_read_reg(addr)		__raw_readl(addr + (RK29_PWM_BASE + 2*0x10))
#define cru_readl(offset)	readl(RK29_CRU_BASE + offset)
#define cru_writel(v, offset)	do { writel(v, RK29_CRU_BASE + offset); dsb(); } while (0)

void interface_ctr_reg_pread(void)
{
	readl(RK29_PWM_BASE);
	readl(RK29_GRF_BASE);
}
static unsigned int __sramdata pwm_lrc,pwm_hrc;

static void __sramfunc rk29_pwm_set_core_voltage(unsigned int uV)
{
	u32 gate1;

	gate1 = cru_readl(CRU_CLKGATE1_CON);
	cru_writel(gate1 & ~((1 << CLK_GATE_PCLK_PEIRPH % 32) | (1 << CLK_GATE_ACLK_PEIRPH % 32) | (1 << CLK_GATE_ACLK_CPU_PERI % 32)), CRU_CLKGATE1_CON);

	/* iomux pwm2 */
	writel((readl(RK29_GRF_BASE + 0x58) & ~(0x3<<6)) | (0x2<<6), RK29_GRF_BASE + 0x58);

	if (uV) {
		pwm_lrc = pwm_read_reg(PWM_REG_LRC);
		pwm_hrc = pwm_read_reg(PWM_REG_HRC);
	}

	pwm_write_reg(PWM_REG_CTRL, PWM_DIV|PWM_RESET);
	if (uV == 1000000) {
		pwm_write_reg(PWM_REG_LRC, 12);
		pwm_write_reg(PWM_REG_HRC, 10);
	} else {
		pwm_write_reg(PWM_REG_LRC, pwm_lrc);
		pwm_write_reg(PWM_REG_HRC, pwm_hrc);
	}
	pwm_write_reg(PWM_REG_CNTR, 0);
	pwm_write_reg(PWM_REG_CTRL, PWM_DIV|PWM_ENABLE|PWM_TimeEN);

	LOOP(10 * 1000 * LOOPS_PER_USEC); /* delay 10ms */

	cru_writel(gate1, CRU_CLKGATE1_CON);
}

unsigned int __sramfunc rk29_suspend_voltage_set(unsigned int vol)
{
	
	rk29_pwm_set_core_voltage(1000000);
	return 0;

}
void __sramfunc rk29_suspend_voltage_resume(unsigned int vol)
{
	rk29_pwm_set_core_voltage(0);
}

