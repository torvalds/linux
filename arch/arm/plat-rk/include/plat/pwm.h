#ifndef __PLAT_PWM_H
#define __PLAT_PWM_H

#include <linux/io.h>

enum pwm_div {
	PWM_DIV2        = (0x0 << 9),
	PWM_DIV4        = (0x1 << 9),
	PWM_DIV8        = (0x2 << 9),
	PWM_DIV16       = (0x3 << 9),
	PWM_DIV32       = (0x4 << 9),
	PWM_DIV64       = (0x5 << 9),
	PWM_DIV128      = (0x6 << 9),
	PWM_DIV256      = (0x7 << 9),
	PWM_DIV512      = (0x8 << 9),
	PWM_DIV1024     = (0x9 << 9),
	PWM_DIV2048     = (0xa << 9),
	PWM_DIV4096     = (0xb << 9),
	PWM_DIV8192     = (0xc << 9),
	PWM_DIV16384    = (0xd << 9),
	PWM_DIV32768    = (0xe << 9),
	PWM_DIV65536    = (0xf << 9),
};

#define PWM_DIV_MASK    (0xf << 9)
#define PWM_CAPTURE     (1 << 8)
#define PWM_RESET       (1 << 7)
#define PWM_INTCLR      (1 << 6)
#define PWM_INTEN       (1 << 5)
#define PWM_SINGLE      (1 << 4)

#define PWM_ENABLE      (1 << 3)
#define PWM_TIMER_EN    (1 << 0)
#define PWM_TimeEN      PWM_TIMER_EN

#define PWM_REG_CNTR    0x00
#define PWM_REG_HRC     0x04
#define PWM_REG_LRC     0x08
#define PWM_REG_CTRL    0x0c

static inline void __rk_pwm_setup(const void __iomem *base, enum pwm_div div, u32 hrc, u32 lrc)
{
	u32 off = div | PWM_RESET;
	u32 on = div | PWM_ENABLE | PWM_TIMER_EN;

	barrier();
	writel_relaxed(off, base + PWM_REG_CTRL);
	dsb();
	writel_relaxed(hrc, base + PWM_REG_HRC);
	writel_relaxed(lrc, base + PWM_REG_LRC);
	writel_relaxed(0, base + PWM_REG_CNTR);
	dsb();
	writel_relaxed(on, base + PWM_REG_CTRL);
	dsb();
}

struct clk *rk_pwm_get_clk(unsigned pwm_id);
void __iomem *rk_pwm_get_base(unsigned pwm_id);
void rk_pwm_setup(unsigned pwm_id, enum pwm_div div, u32 hrc, u32 lrc);

#endif
