
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>


static int pwm_dbg_level = 0;
module_param_named(dbg_level, pwm_dbg_level, int, 0644);
#define DBG( args...) \
	do { \
		if (pwm_dbg_level) { \
			pr_info(args); \
		} \
	} while (0)

#define PWM_CLK                                 1
#define NUM_PWM		                  1

/* PWM registers  */
#define PWM_REG_CNTR    			0x00
#define PWM_REG_HRC    				0x04
#define PWM_REG_LRC    				0x08   
#define PWM_REG_CTRL    				0x0c  /* PWM Control Register */

#define PWM_REG_PERIOD				PWM_REG_HRC  /* Period Register */
#define PWM_REG_DUTY        			PWM_REG_LRC  /* Duty Cycle Register */

#define VOP_REG_CNTR       			0x0C
#define VOP_REG_CTRL    				0x00  /* VOP-PWM Control Register */

//#define PWM_REG_CTRL                   0x0c /* Control Register */

#define PWM_DIV_MASK    			(0xf << 9)
#define PWM_CAPTURE     				(1 << 8)
#define PWM_RESET      				(1 << 7)
#define PWM_INTCLR      				(1 << 6)
#define PWM_INTEN       				(1 << 5)
#define PWM_SINGLE      				(1 << 4)

#define PWM_ENABLE      				(1 << 3)
#define PWM_TIMER_EN    				(1 << 0)

#define RK_PWM_DISABLE                     (0 << 0) 
#define RK_PWM_ENABLE                       (1 << 0)

#define PWM_SHOT                                 (0 << 1)
#define PWM_CONTINUMOUS                 (1 << 1)
#define RK_PWM_CAPTURE                    (1 << 2)

#define PWM_DUTY_POSTIVE                (1 << 3)
#define PWM_DUTY_NEGATIVE              (0 << 3)

#define PWM_INACTIVE_POSTIVE         (1 << 4)
#define PWM_INACTIVE_NEGATIVE       (0 << 4)

#define PWM_OUTPUT_LEFT                 (0 << 5)
#define PWM_OUTPUT_ENTER               (1 << 5)

#define PWM_LP_ENABLE                     (1<<8)
#define PWM_LP_DISABLE                    (0<<8)

#define DW_PWM_PRESCALE		9
#define RK_PWM_PRESCALE		16

#define PWMCR_MIN_PRESCALE	0x00

#define PWMCR_MIN_PRESCALE	0x00
#define PWMCR_MAX_PRESCALE	0x07

#define PWMDCR_MIN_DUTY		0x0000
#define PWMDCR_MAX_DUTY		0xFFFFFFFF

#define PWMPCR_MIN_PERIOD		0x0001
#define PWMPCR_MAX_PERIOD		0xFFFFFFFF

/********************************************
 * struct rk_pwm_chip - struct representing pwm chip

 * @base: base address of pwm chip
 * @clk: pointer to clk structure of pwm chip
 * @chip: linux pwm chip representation
 *********************************************/
 struct rk_pwm_chip {
	void __iomem *base;
	struct clk *clk;
	struct clk *aclk_lcdc;
	struct clk *hclk_lcdc;
	struct pwm_chip chip;
	unsigned int pwm_id;
	spinlock_t		lock;
	int				pwm_ctrl;
	int				pwm_duty;
	int				pwm_period;
	int				pwm_count;
	int (*config)(struct pwm_chip *chip,
		struct pwm_device *pwm, int duty_ns, int period_ns);
	void (*set_enable)(struct pwm_chip *chip, struct pwm_device *pwm,bool enable);	
	void (*pwm_suspend)(struct pwm_chip *chip, struct pwm_device *pwm);
	void (*pwm_resume)(struct pwm_chip *chip, struct pwm_device *pwm);

};

#ifdef CONFIG_ARM64
extern int rk3368_lcdc_update_pwm(int bl_pwm_period, int bl_pwm_duty);
extern int rk3368_lcdc_cabc_status(void);
#else
static inline int rk3368_lcdc_update_pwm(int bl_pwm_period, int bl_pwm_duty)
{
	return 0;
}
static inline int rk3368_lcdc_cabc_status(void) { return 0; }
#endif

static inline void rk_pwm_writel(struct rk_pwm_chip *chip,
				    unsigned int num, unsigned long offset,
				    unsigned long val);
static inline u32 rk_pwm_readl(struct rk_pwm_chip *chip, unsigned int num,
				  unsigned long offset);
static struct rk_pwm_chip* s_rk_pwm_chip = NULL;
static struct rk_pwm_chip* rk_pwm_get_chip(void)
{
	BUG_ON(!s_rk_pwm_chip);
	return s_rk_pwm_chip;
}
void rk_pwm_set(int bl_pwm_period, int bl_pwm_duty)
{
	struct rk_pwm_chip* pc = rk_pwm_get_chip();
	rk_pwm_writel(pc, pc->chip.pwms->hwpwm, PWM_REG_DUTY, bl_pwm_duty);
	rk_pwm_writel(pc, pc->chip.pwms->hwpwm, PWM_REG_PERIOD, bl_pwm_period);
}

void rk_pwm_get(int* bl_pwm_period, int* bl_pwm_duty)
{
	struct rk_pwm_chip* pc = rk_pwm_get_chip();
	*bl_pwm_duty = rk_pwm_readl(pc, pc->chip.pwms->hwpwm, PWM_REG_DUTY);
	*bl_pwm_period = rk_pwm_readl(pc, pc->chip.pwms->hwpwm, PWM_REG_PERIOD);
}

static inline struct rk_pwm_chip *to_rk_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct rk_pwm_chip, chip);
}

static inline u32 rk_pwm_readl(struct rk_pwm_chip *chip, unsigned int num,
				  unsigned long offset)
{
	return readl_relaxed(chip->base + (num << 4) + offset);
}

static inline void rk_pwm_writel(struct rk_pwm_chip *chip,
				    unsigned int num, unsigned long offset,
				    unsigned long val)
{
	writel_relaxed(val, chip->base + (num << 4) + offset);
}

/* config for rockchip,pwm*/
static int  rk_pwm_config_v1(struct pwm_chip *chip, struct pwm_device *pwm,
			    int duty_ns, int period_ns)
{
	struct rk_pwm_chip *pc = to_rk_pwm_chip(chip);
	u64 val, div, clk_rate;
	unsigned long prescale = PWMCR_MIN_PRESCALE, pv, dc;
	u32 off, on;
	int conf=0;
       unsigned long flags;
       spinlock_t *lock;

       lock =(&pc->lock);// &pwm_lock[pwm->hwpwm];
	off =  PWM_RESET;
	on =  PWM_ENABLE | PWM_TIMER_EN;
	/*
	 * Find pv, dc and prescale to suit duty_ns and period_ns. This is done
	 * according to formulas described below:
	 *
	 * period_ns = 10^9 * (PRESCALE ) * PV / PWM_CLK_RATE
	 * duty_ns = 10^9 * (PRESCALE + 1) * DC / PWM_CLK_RATE
	 *
	 * PV = (PWM_CLK_RATE * period_ns) / (10^9 * (PRESCALE + 1))
	 * DC = (PWM_CLK_RATE * duty_ns) / (10^9 * (PRESCALE + 1))
	 */
#if PWM_CLK
	clk_rate = clk_get_rate(pc->clk);
#else
	clk_rate = 24000000;
#endif
	while (1) {
		div = 1000000000;
		div *= 1 + prescale;
		val = clk_rate * period_ns;
		pv = div64_u64(val, div);
		val = clk_rate * duty_ns;
		dc = div64_u64(val, div);

		/* if duty_ns and period_ns are not achievable then return */
		if (pv < PWMPCR_MIN_PERIOD || dc < PWMDCR_MIN_DUTY)
			return -EINVAL;

		/*
		 * if pv and dc have crossed their upper limit, then increase
		 * prescale and recalculate pv and dc.
		 */
		if (pv > PWMPCR_MAX_PERIOD || dc > PWMDCR_MAX_DUTY) {
			if (++prescale > PWMCR_MAX_PRESCALE)
				return -EINVAL;
			continue;
		}
		break;
	}

	/* NOTE: the clock to PWM has to be enabled first before writing to the registers. */

        spin_lock_irqsave(lock, flags);

	conf |= (prescale << DW_PWM_PRESCALE);
	barrier();
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CTRL,off);
	dsb(sy);
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_HRC,dc);//0x1900);// dc);
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_LRC, pv);//0x5dc0);//pv);
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CNTR,0);
	dsb(sy);
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CTRL,on|conf);
	
       spin_unlock_irqrestore(lock, flags);	
	return 0;
}
static void rk_pwm_set_enable_v1(struct pwm_chip *chip, struct pwm_device *pwm, bool enable)
{
	struct rk_pwm_chip *pc = to_rk_pwm_chip(chip);
	u32 val;

	val = rk_pwm_readl(pc, pwm->hwpwm, PWM_REG_CTRL);
	if (enable)
		val |= PWM_ENABLE;
	else
		val &= ~PWM_ENABLE;

	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CTRL, val);

}
static void rk_pwm_suspend_v1(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rk_pwm_chip *pc = to_rk_pwm_chip(chip);
	pc->pwm_ctrl = rk_pwm_readl(pc, pwm->hwpwm, PWM_REG_CTRL);
	pc->pwm_duty=  rk_pwm_readl(pc, pwm->hwpwm, PWM_REG_HRC);//0x1900);// dc);
	pc->pwm_period = rk_pwm_readl(pc, pwm->hwpwm, PWM_REG_LRC );//0x5dc0);//pv);
	pc->pwm_count=  rk_pwm_readl(pc, pwm->hwpwm, PWM_REG_CNTR);
}
static void rk_pwm_resume_v1(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rk_pwm_chip *pc = to_rk_pwm_chip(chip);
	int 	off =  PWM_RESET;

	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CTRL,off);
	dsb(sy);
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_HRC,pc->pwm_duty);//0x1900);// dc);
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_LRC, pc->pwm_period);//0x5dc0);//pv);
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CNTR,pc->pwm_count);
	dsb(sy);
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CTRL,pc->pwm_ctrl);
}

/* config for rockchip,pwm*/
static int  rk_pwm_config_v2(struct pwm_chip *chip, struct pwm_device *pwm,
			    int duty_ns, int period_ns)
{
	struct rk_pwm_chip *pc = to_rk_pwm_chip(chip);
	u64 val, div, clk_rate;
	unsigned long prescale = PWMCR_MIN_PRESCALE, pv, dc;
	u32  on;
	int conf=0;
       unsigned long flags;
       spinlock_t *lock;

       lock = (&pc->lock);//&pwm_lock[pwm->hwpwm];
	on   =  RK_PWM_ENABLE ;
	conf = PWM_OUTPUT_LEFT|PWM_LP_DISABLE|
	                    PWM_CONTINUMOUS|PWM_DUTY_POSTIVE|PWM_INACTIVE_NEGATIVE;
	/*
	 * Find pv, dc and prescale to suit duty_ns and period_ns. This is done
	 * according to formulas described below:
	 *
	 * period_ns = 10^9 * (PRESCALE ) * PV / PWM_CLK_RATE
	 * duty_ns = 10^9 * (PRESCALE + 1) * DC / PWM_CLK_RATE
	 *
	 * PV = (PWM_CLK_RATE * period_ns) / (10^9 * (PRESCALE + 1))
	 * DC = (PWM_CLK_RATE * duty_ns) / (10^9 * (PRESCALE + 1))
	 */
#if PWM_CLK
	clk_rate = clk_get_rate(pc->clk);
#else
	clk_rate = 24000000;
#endif
	while (1) {
		div = 1000000000;
		div *= 1 + prescale;
		val = clk_rate * period_ns;
		pv = div64_u64(val, div);
		val = clk_rate * duty_ns;
		dc = div64_u64(val, div);

		/* if duty_ns and period_ns are not achievable then return */
		if (pv < PWMPCR_MIN_PERIOD || dc < PWMDCR_MIN_DUTY)
			return -EINVAL;

		/*
		 * if pv and dc have crossed their upper limit, then increase
		 * prescale and recalculate pv and dc.
		 */
		if (pv > PWMPCR_MAX_PERIOD || dc > PWMDCR_MAX_DUTY) {
			if (++prescale > PWMCR_MAX_PRESCALE)
				return -EINVAL;
			continue;
		}
		break;
	}

	/*
	 * NOTE: the clock to PWM has to be enabled first before writing to the
	 * registers.
	 */
        spin_lock_irqsave(lock, flags);

	conf |= (prescale << RK_PWM_PRESCALE);	
	barrier();
	//rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CTRL,off);
	//dsb(sy);
	if (!rk3368_lcdc_cabc_status()) {
	    rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_DUTY,dc);//0x1900);// dc);
	    rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_PERIOD,pv);//0x5dc0);//pv);
	} else {
	    rk3368_lcdc_update_pwm(pv,dc);
	}
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CNTR,0);
	dsb(sy);
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CTRL,on|conf);
       spin_unlock_irqrestore(lock, flags);	

	return 0;
}

static void rk_pwm_set_enable_v2(struct pwm_chip *chip, struct pwm_device *pwm,bool enable)
{
	struct rk_pwm_chip *pc = to_rk_pwm_chip(chip);
	u32 val;

	val = rk_pwm_readl(pc, pwm->hwpwm, PWM_REG_CTRL);
	if (enable)
		val |= RK_PWM_ENABLE;
	else
		val &= ~RK_PWM_ENABLE;
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CTRL, val);
	DBG("%s %d \n", __FUNCTION__, rk_pwm_readl(pc, pwm->hwpwm, PWM_REG_CTRL));
}

static void rk_pwm_suspend_v2(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rk_pwm_chip *pc = to_rk_pwm_chip(chip);
	pc->pwm_ctrl      = rk_pwm_readl(pc, pwm->hwpwm, 	PWM_REG_CTRL);
	pc->pwm_duty    =  rk_pwm_readl(pc, pwm->hwpwm, PWM_REG_DUTY);//0x1900);// dc);
	pc->pwm_period = rk_pwm_readl(pc, pwm->hwpwm,  PWM_REG_PERIOD );//0x5dc0);//pv);
	pc->pwm_count  =  rk_pwm_readl(pc, pwm->hwpwm, PWM_REG_CNTR);
}
static void rk_pwm_resume_v2(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rk_pwm_chip *pc = to_rk_pwm_chip(chip);

	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_DUTY,    pc->pwm_duty);//0x1900);// dc);
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_PERIOD, pc->pwm_period);//0x5dc0);//pv);
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CNTR,pc->pwm_count);
	dsb(sy);
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CTRL,pc->pwm_ctrl);
}

/* config for rockchip,pwm*/
static int  rk_pwm_config_v3(struct pwm_chip *chip, struct pwm_device *pwm,
			    int duty_ns, int period_ns)
{
	struct rk_pwm_chip *pc = to_rk_pwm_chip(chip);
	u64 val, div, clk_rate;
	unsigned long prescale = PWMCR_MIN_PRESCALE, pv, dc;
	u32 on;
	int conf=0;
       unsigned long flags;
       spinlock_t *lock;

       lock = (&pc->lock);//&pwm_lock[pwm->hwpwm];
	on   =  RK_PWM_ENABLE ;
	conf = PWM_OUTPUT_LEFT|PWM_LP_DISABLE|
	                    PWM_CONTINUMOUS|PWM_DUTY_POSTIVE|PWM_INACTIVE_NEGATIVE;
	/*
	 * Find pv, dc and prescale to suit duty_ns and period_ns. This is done
	 * according to formulas described below:
	 *
	 * period_ns = 10^9 * (PRESCALE ) * PV / PWM_CLK_RATE
	 * duty_ns = 10^9 * (PRESCALE + 1) * DC / PWM_CLK_RATE
	 *
	 * PV = (PWM_CLK_RATE * period_ns) / (10^9 * (PRESCALE + 1))
	 * DC = (PWM_CLK_RATE * duty_ns) / (10^9 * (PRESCALE + 1))
	 */
#if PWM_CLK
	clk_rate = clk_get_rate(pc->clk);
#else
	clk_rate = 24000000;
#endif
	while (1) {
		div = 1000000000;
		div *= 1 + prescale;
		val = clk_rate * period_ns;
		pv = div64_u64(val, div);
		val = clk_rate * duty_ns;
		dc = div64_u64(val, div);

		/* if duty_ns and period_ns are not achievable then return */
		if (pv < PWMPCR_MIN_PERIOD || dc < PWMDCR_MIN_DUTY)
			return -EINVAL;

		/*
		 * if pv and dc have crossed their upper limit, then increase
		 * prescale and recalculate pv and dc.
		 */
		if (pv > PWMPCR_MAX_PERIOD || dc > PWMDCR_MAX_DUTY) {
			if (++prescale > PWMCR_MAX_PRESCALE)
				return -EINVAL;
			continue;
		}
		break;
	}

	/*
	 * NOTE: the clock to PWM has to be enabled first before writing to the
	 * registers.
	 */
#if 0
	ret = clk_enable(pc->clk);
	if (ret)
		return ret;
#endif
        spin_lock_irqsave(lock, flags);

	conf |= (prescale << RK_PWM_PRESCALE);
	
	barrier();
//	rk_pwm_writel(pc, pwm->hwpwm, VOP_REG_CTRL,off);
	
//	dsb(sy);
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_DUTY,dc);   //   2    0x1900);// dc);
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_PERIOD,pv);   // 4 0x5dc0);//pv);
	rk_pwm_writel(pc, pwm->hwpwm, VOP_REG_CNTR,0);
	dsb(sy);
	rk_pwm_writel(pc, pwm->hwpwm, VOP_REG_CTRL,on|conf);

       spin_unlock_irqrestore(lock, flags);	

	return 0;
}
static void rk_pwm_set_enable_v3(struct pwm_chip *chip, struct pwm_device *pwm,bool enable)
{
	struct rk_pwm_chip *pc = to_rk_pwm_chip(chip);
	u32 val;
	
	val = rk_pwm_readl(pc, pwm->hwpwm, VOP_REG_CTRL);
	if (enable)
		val |= RK_PWM_ENABLE;
	else
		val &= ~RK_PWM_ENABLE;
	rk_pwm_writel(pc, pwm->hwpwm, VOP_REG_CTRL, val);

}
static void rk_pwm_suspend_v3(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rk_pwm_chip *pc = to_rk_pwm_chip(chip);
	pc->pwm_ctrl      = rk_pwm_readl(pc, pwm->hwpwm, 	VOP_REG_CTRL);
	pc->pwm_duty    =  rk_pwm_readl(pc, pwm->hwpwm, PWM_REG_DUTY);//0x1900);// dc);
	pc->pwm_period = rk_pwm_readl(pc, pwm->hwpwm,  PWM_REG_PERIOD );//0x5dc0);//pv);
	pc->pwm_count  =  rk_pwm_readl(pc, pwm->hwpwm, VOP_REG_CNTR);
}
static void rk_pwm_resume_v3(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rk_pwm_chip *pc = to_rk_pwm_chip(chip);

	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_DUTY,    pc->pwm_duty);//0x1900);// dc);
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_PERIOD, pc->pwm_period);//0x5dc0);//pv);
	rk_pwm_writel(pc, pwm->hwpwm, VOP_REG_CNTR,pc->pwm_count);
	dsb(sy);
	rk_pwm_writel(pc, pwm->hwpwm, VOP_REG_CTRL,pc->pwm_ctrl);
}


static int  rk_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			    int duty_ns, int period_ns)
{
	struct rk_pwm_chip *pc = to_rk_pwm_chip(chip);
	int ret;
	
	ret = clk_enable(pc->clk);
	if (ret)
		return ret;

	if (pc->aclk_lcdc) {
		ret = clk_enable(pc->aclk_lcdc);
		if (ret)
			return ret;
	}
	if (pc->hclk_lcdc) {
		ret = clk_enable(pc->hclk_lcdc);
		if (ret)
			return ret;
	}

	ret = pc->config(chip, pwm, duty_ns, period_ns);

	if (pc->aclk_lcdc)
		clk_disable(pc->aclk_lcdc);
	if (pc->hclk_lcdc)
		clk_disable(pc->hclk_lcdc);

	clk_disable(pc->clk);

	return 0;
}
static int rk_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rk_pwm_chip *pc = to_rk_pwm_chip(chip);
	int ret = 0;

	ret = clk_enable(pc->clk);
	if (ret)
		return ret;

	if (pc->aclk_lcdc) {
		ret = clk_enable(pc->aclk_lcdc);
		if (ret)
			return ret;
	}
	if (pc->hclk_lcdc) {
		ret = clk_enable(pc->hclk_lcdc);
		if (ret)
			return ret;
	}

	pc->set_enable(chip, pwm,true);
	return 0;
}

static void rk_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rk_pwm_chip *pc = to_rk_pwm_chip(chip);

	pc->set_enable(chip, pwm,false);

	if (pc->aclk_lcdc)
		clk_disable(pc->aclk_lcdc);
	if (pc->hclk_lcdc)
		clk_disable(pc->hclk_lcdc);

	clk_disable(pc->clk);

}

static const struct pwm_ops rk_pwm_ops = {
	.config = rk_pwm_config,
	.enable = rk_pwm_enable,
	.disable = rk_pwm_disable,
	.owner = THIS_MODULE,
};

struct rk_pwm_data{
	int   (*config)(struct pwm_chip *chip,	struct pwm_device *pwm, int duty_ns, int period_ns);
	void (*set_enable)(struct pwm_chip *chip, struct pwm_device *pwm,bool enable);	
	void (*pwm_suspend)(struct pwm_chip *chip, struct pwm_device *pwm);
	void (*pwm_resume)(struct pwm_chip *chip, struct pwm_device *pwm);

	
};

static struct rk_pwm_data rk_pwm_data_v1={
	.config = rk_pwm_config_v1,
	.set_enable = rk_pwm_set_enable_v1,
	.pwm_suspend = rk_pwm_suspend_v1,
	.pwm_resume = rk_pwm_resume_v1,
	
};

static struct rk_pwm_data rk_pwm_data_v2={
	.config = rk_pwm_config_v2,
	.set_enable = rk_pwm_set_enable_v2,	
	.pwm_suspend = rk_pwm_suspend_v2,
	.pwm_resume = rk_pwm_resume_v2,

};

static struct rk_pwm_data rk_pwm_data_v3={
	.config = rk_pwm_config_v3,
	.set_enable = rk_pwm_set_enable_v3,	
	.pwm_suspend = rk_pwm_suspend_v3,
	.pwm_resume = rk_pwm_resume_v3,

};

static const struct of_device_id rk_pwm_of_match[] = {
	{ .compatible = "rockchip,pwm",          .data = &rk_pwm_data_v1,},
	{ .compatible =  "rockchip,rk-pwm",    .data = &rk_pwm_data_v2,},
	{ .compatible =  "rockchip,vop-pwm",  .data = &rk_pwm_data_v3,},
	{ }
};

MODULE_DEVICE_TABLE(of, rk_pwm_of_match);
static int rk_pwm_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
		of_match_device(rk_pwm_of_match, &pdev->dev);
	struct device_node *np = pdev->dev.of_node;
	const struct rk_pwm_data *data;
	struct rk_pwm_chip *pc;
	int ret;

	if (!of_id){
		dev_err(&pdev->dev, "failed to match device\n");
		return -ENODEV;
	}
	pc = devm_kzalloc(&pdev->dev, sizeof(*pc), GFP_KERNEL);
	if (!pc) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	s_rk_pwm_chip = pc;
	pc->base = of_iomap(np, 0);
	if (IS_ERR(pc->base)) {
		printk("PWM base ERR \n");
		return PTR_ERR(pc->base);
	}
	pc->clk = devm_clk_get(&pdev->dev, "pclk_pwm");
	if (IS_ERR(pc->clk))
		return PTR_ERR(pc->clk);

	if (of_device_is_compatible(np, "rockchip,vop-pwm")) {
		pc->aclk_lcdc = devm_clk_get(&pdev->dev, "aclk_lcdc");
		if (IS_ERR(pc->aclk_lcdc))
			return PTR_ERR(pc->aclk_lcdc);

		pc->hclk_lcdc = devm_clk_get(&pdev->dev, "hclk_lcdc");
		if (IS_ERR(pc->hclk_lcdc))
			return PTR_ERR(pc->hclk_lcdc);

		ret = clk_prepare(pc->aclk_lcdc);
		if (ret)
			return ret;
		clk_prepare(pc->hclk_lcdc);
		if (ret)
			return ret;
	}

	platform_set_drvdata(pdev, pc);
	data = of_id->data;
	pc->config = data->config;
	pc->set_enable = data->set_enable;
	pc->pwm_suspend = data->pwm_suspend;
	pc->pwm_resume = data->pwm_resume;
	pc->chip.dev = &pdev->dev;
	pc->chip.ops = &rk_pwm_ops;  
	pc->chip.base = -1;
	pc->chip.npwm = NUM_PWM;
	spin_lock_init(&pc->lock);
	ret = clk_prepare(pc->clk);
	if (ret)
		return ret;

	/* Following enables PWM chip, channels would still
	be enabled individually through their control register */
	DBG("npwm = %d, of_pwm_ncells =%d \n"
		, pc->chip.npwm, pc->chip.of_pwm_n_cells);
	ret = pwmchip_add(&pc->chip);
	if (ret < 0){
		printk("failed to add pwm\n");
		return ret;
	}

	DBG("%s end \n",__FUNCTION__);
	return ret;
}
#if 0
//(struct platform_device *, pm_message_t state);
static int rk_pwm_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct pwm_device *pwm;
	struct rk_pwm_chip *pc;
	struct pwm_chip *chip;

	pc = platform_get_drvdata(pdev);
	chip = &(pc->chip);
	pwm = chip->pwms;

	pc->pwm_suspend(chip,pwm);

 	return 0;//pwmchip_remove(&pc->chip);
}
static int rk_pwm_resume(struct platform_device *pdev)
{
	struct pwm_device *pwm;
	struct rk_pwm_chip *pc;
	struct pwm_chip *chip;

	pc = platform_get_drvdata(pdev);
	chip = &(pc->chip);
	pwm = chip->pwms;

	pc->pwm_resume(chip,pwm);
	return 0;//pwmchip_remove(&pc->chip);
}
#endif
static int rk_pwm_remove(struct platform_device *pdev)
{
	return 0;//pwmchip_remove(&pc->chip);
}

static struct platform_driver rk_pwm_driver = {
	.driver = {
		.name = "rk-pwm",
		.of_match_table = rk_pwm_of_match,
	},
	.probe = rk_pwm_probe,
 	.remove = rk_pwm_remove,
};

module_platform_driver(rk_pwm_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("<xsf@rock-chips.com>");
MODULE_ALIAS("platform:rk-pwm");


