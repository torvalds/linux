#define pr_fmt(fmt) "pwm: " fmt
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <plat/pwm.h>
#include <mach/io.h>

static spinlock_t pwm_lock[4] = {
	__SPIN_LOCK_UNLOCKED(pwm_lock0),
	__SPIN_LOCK_UNLOCKED(pwm_lock1),
	__SPIN_LOCK_UNLOCKED(pwm_lock2),
	__SPIN_LOCK_UNLOCKED(pwm_lock3),
};

struct clk *rk_pwm_get_clk(unsigned id)
{
#if defined(CONFIG_ARCH_RK29)
	if (id < 4)
		return clk_get(NULL, "pwm");
#elif defined(CONFIG_ARCH_RK30) || defined(CONFIG_ARCH_RK3188)
	if (id == 0 || id == 1)
		return clk_get(NULL, "pwm01");
	else if (id== 2 || id == 3)
		return clk_get(NULL, "pwm23");
#elif defined(CONFIG_ARCH_RK2928)
	if (id < 3)
		return clk_get(NULL, "pwm01");
#endif
	pr_err("invalid pwm id %d\n", id);
	BUG();
	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL(rk_pwm_get_clk);

void __iomem *rk_pwm_get_base(unsigned id)
{
#if defined(CONFIG_ARCH_RK29)
	if (id < 4)
		return RK29_PWM_BASE + id * 0x10;
#elif defined(CONFIG_ARCH_RK30) || defined(CONFIG_ARCH_RK3188)
	if (id == 0 || id == 1)
		return RK30_PWM01_BASE + id * 0x10;
	else if (id== 2 || id == 3)
		return RK30_PWM23_BASE + id * 0x10;
#elif defined(CONFIG_ARCH_RK2928)
	if (id < 3)
		return RK2928_PWM_BASE + id * 0x10;
#endif
	pr_err("invalid pwm id %d\n", id);
	BUG();
	return 0;
}
EXPORT_SYMBOL(rk_pwm_get_base);

void rk_pwm_setup(unsigned id, enum pwm_div div, u32 hrc, u32 lrc)
{
	unsigned long flags;
	spinlock_t *lock;
	const void __iomem *base = rk_pwm_get_base(id);

	if (hrc > lrc) {
		pr_err("invalid hrc %d lrc %d\n", hrc, lrc);
		return;
	}

	lock = &pwm_lock[id];
	spin_lock_irqsave(lock, flags);
	__rk_pwm_setup(base, div, hrc, lrc);
	spin_unlock_irqrestore(lock, flags);
}
EXPORT_SYMBOL(rk_pwm_setup);
