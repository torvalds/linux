
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

#define NUM_PWM		1

/* PWM registers  */
#if 0
#define PWM_REG_CNTR                  0x00  /* Counter Register */
#define PWM_REG_PERIOD			0x04  /* Period Register */
#define PWM_REG_DUTY        		0x08  /* Duty Cycle Register */
#define PWM_REG_CTRL                   0x0c /* Control Register */

/*bits definitions*/

#define PWM_ENABLE			(1 << 0)
#define PWM_DISABLE			(0 << 0)

#define PWM_SHOT			(0x00 << 1)
#define PWM_CONTINUMOUS 	(0x01 << 1)
#define PWM_CAPTURE		(0x01 << 1)

#define PWM_DUTY_POSTIVE	(0x01 << 3)
#define PWM_DUTY_NEGATIVE	(0x00 << 3)

#define PWM_INACTIVE_POSTIVE 		(0x01 << 4)
#define PWM_INACTIVE_NEGATIVE		(0x00 << 4)

#define PWM_OUTPUT_LEFT			(0x00 << 5)
#define PWM_OUTPUT_ENTER			(0x01 << 5)


#define PWM_LP_ENABLE		(1<<8)
#define PWM_LP_DISABLE		(0<<8)

#define PWM_CLK_SCALE		(1 << 9)
#define PWM_CLK_NON_SCALE 	(0 << 9)



#define PWMCR_MIN_PRESCALE	0x00

#define PWMCR_MIN_PRESCALE	0x00
#define PWMCR_MAX_PRESCALE	0x07

#define PWMDCR_MIN_DUTY		0x0001
#define PWMDCR_MAX_DUTY		0xFFFF

#define PWMPCR_MIN_PERIOD		0x0001
#define PWMPCR_MAX_PERIOD		0xFFFF


enum pwm_div {
        PWM_DIV1                 = (0x0 << 12),
        PWM_DIV2                 = (0x1 << 12),
        PWM_DIV4                 = (0x2 << 12),
        PWM_DIV8                 = (0x3 << 12),
        PWM_DIV16               = (0x4 << 12),
        PWM_DIV32               = (0x5 << 12),
        PWM_DIV64               = (0x6 << 12),
        PWM_DIV128   		= (0x7 << 12),
};
#endif
static int pwm_dbg_level = 0;
module_param_named(dbg_level, pwm_dbg_level, int, 0644);
#define DBG( args...) \
	do { \
		if (pwm_dbg_level) { \
			pr_info(args); \
		} \
	} while (0)

#define PWM_REG_CNTR    0x00
#define PWM_REG_HRC     0x04
#define PWM_REG_LRC     0x08
#define PWM_REG_CTRL    0x0c


#define PWM_REG_PERIOD			PWM_REG_HRC  /* Period Register */
#define PWM_REG_DUTY        		PWM_REG_LRC  /* Duty Cycle Register */
//#define PWM_REG_CTRL                   0x0c /* Control Register */



#define PWM_DIV_MASK    (0xf << 9)
#define PWM_CAPTURE     (1 << 8)
#define PWM_RESET       (1 << 7)
#define PWM_INTCLR      (1 << 6)
#define PWM_INTEN       (1 << 5)
#define PWM_SINGLE      (1 << 4)

#define PWM_ENABLE      (1 << 3)
#define PWM_TIMER_EN    (1 << 0)
#define PWM_TimeEN      PWM_TIMER_EN
#define PWMCR_MIN_PRESCALE	0x00

#define PWMCR_MIN_PRESCALE	0x00
#define PWMCR_MAX_PRESCALE	0x07

#define PWMDCR_MIN_DUTY		0x0001
#define PWMDCR_MAX_DUTY		0xFFFF

#define PWMPCR_MIN_PERIOD		0x0001
#define PWMPCR_MAX_PERIOD		0xFFFF

/**
 * struct rk_pwm_chip - struct representing pwm chip
 *
 * @base: base address of pwm chip
 * @clk: pointer to clk structure of pwm chip
 * @chip: linux pwm chip representation
 */

static spinlock_t pwm_lock[4] = {
        __SPIN_LOCK_UNLOCKED(pwm_lock0),
        __SPIN_LOCK_UNLOCKED(pwm_lock1),
        __SPIN_LOCK_UNLOCKED(pwm_lock2),
        __SPIN_LOCK_UNLOCKED(pwm_lock3),
};

struct rk_pwm_chip {
	void __iomem *base;
	struct clk *clk;
	struct pwm_chip chip;
};

#define PWM_CLK 1
#if 0
static void __iomem *rk30_grf_base = NULL;
static void __iomem *rk30_cru_base = NULL;
static void __iomem *rk30_pwm_base = NULL;
//#define SZ_16K                         0x4000
//#define SZ_8K				0x2000
#define RK30_GRF_PHYS           0x20008000
#define RK30_GRF_SIZE            SZ_8K
#define RK30_CRU_PHYS           0x20000000
#define RK30_CRU_SIZE            SZ_16K
#define RK30_PWM_PHYS           0x20050000
#define RK30_PWM_SIZE            SZ_16K

static void dump_register_of_pwm(void)
{
	int off;
//rk30_grf_base =  ioremap(RK30_GRF_PHYS, RK30_GRF_SIZE);
// rk30_cru_base = ioremap(RK30_CRU_PHYS, RK30_CRU_SIZE);
 //rk30_pwm_base = ioremap(RK30_PWM_PHYS, RK30_PWM_SIZE);

// DBG("GRF IOMUX GPIO3_D6 = 0x%08x\n",readl_relaxed(rk30_grf_base+ 0x9C) );
 //DBG("CRU                            = 0x%08x\n",readl_relaxed(rk30_cru_base+ 0xeC) );
//writel_relaxed(0x10001000, rk30_grf_base+ 0x9C);
 //DBG("GRF IOMUX GPIO3_D6 = 0x%08x\n",readl_relaxed(rk30_grf_base+ 0x9C) );
 //DBG("CRU                            = 0x%08x\n",readl_relaxed(rk30_cru_base+ 0xeC) );

#if 0
        barrier();
	writel_relaxed(off, rk30_pwm_base+3*0x10+ PWM_REG_CTRL);

        dsb();
	writel_relaxed(0x1900, rk30_pwm_base+3*0x10+ PWM_REG_HRC);//rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_HRC,0x1900);// dc);
	writel_relaxed(0x5dc0, rk30_pwm_base+3*0x10+ PWM_REG_LRC);//rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_LRC, 0x5dc0);//pv);
	writel_relaxed(0, rk30_pwm_base+3*0x10+ PWM_REG_CNTR);//rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CNTR,0);
        dsb();
	writel_relaxed(0x09, rk30_pwm_base+3*0x10+ PWM_REG_CTRL);// rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CTRL,on);
        dsb();
#endif
}
static void dump_pwm_register(struct rk_pwm_chip *chip)
{
DBG("dump pwm regitster start\n");
#if 1
DBG("PWM0\n");
DBG("PWM_REG_CTRL =0x%08x\n",rk_pwm_readl(chip, 0, PWM_REG_CTRL));
DBG("PWM_REG_HRC = 0x%08x\n",rk_pwm_readl(chip,0, PWM_REG_HRC));
DBG("PWM_REG_LRC = 0x%08x\n",rk_pwm_readl(chip,0, PWM_REG_LRC));
DBG("PWM_REG_CNTR = 0x%08x\n",rk_pwm_readl(chip,0, PWM_REG_CNTR));

DBG("PWM1\n");
DBG("PWM_REG_CTRL =0x%08x\n",rk_pwm_readl(chip, 1, PWM_REG_CTRL));
DBG("PWM_REG_HRC = 0x%08x\n",rk_pwm_readl(chip,1, PWM_REG_HRC));
DBG("PWM_REG_LRC = 0x%08x\n",rk_pwm_readl(chip,1, PWM_REG_LRC));
DBG("PWM_REG_CNTR = 0x%08x\n",rk_pwm_readl(chip,1, PWM_REG_CNTR));

DBG("PWM2\n");
DBG("PWM_REG_CTRL =0x%08x\n",rk_pwm_readl(chip, 2, PWM_REG_CTRL));
DBG("PWM_REG_HRC = 0x%08x\n",rk_pwm_readl(chip,2, PWM_REG_HRC));
DBG("PWM_REG_LRC = 0x%08x\n",rk_pwm_readl(chip,2, PWM_REG_LRC));
DBG("PWM_REG_CNTR = 0x%08x\n",rk_pwm_readl(chip,2, PWM_REG_CNTR));

DBG("PWM3\n");
DBG("PWM_REG_CTRL =0x%08x\n",rk_pwm_readl(chip,3, PWM_REG_CTRL));
DBG("PWM_REG_HRC = 0x%08x\n",rk_pwm_readl(chip,3, PWM_REG_HRC));
DBG("PWM_REG_LRC = 0x%08x\n",rk_pwm_readl(chip,3, PWM_REG_LRC));
DBG("PWM_REG_CNTR = 0x%08x\n",rk_pwm_readl(chip,3, PWM_REG_CNTR));
#endif
printk("dump pwm regitster end\n");

}

#endif
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


#if 1
static int  rk_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			    int duty_ns, int period_ns)
{
	struct rk_pwm_chip *pc = to_rk_pwm_chip(chip);
	u64 val, div, clk_rate;
	unsigned long prescale = PWMCR_MIN_PRESCALE, pv, dc;
	int ret;
	u32 off, on;
	int conf=0;
       unsigned long flags;
       spinlock_t *lock;

       lock = &pwm_lock[pwm->hwpwm];

        off =  PWM_RESET;
        on =  PWM_ENABLE | PWM_TIMER_EN;

	//dump_pwm_register(pc);

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
	 conf |= (prescale << 9);
#if PWM_CLK
	ret = clk_enable(pc->clk);
	if (ret)
		return ret;
#endif
        spin_lock_irqsave(lock, flags);

        barrier();
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CTRL,off);

        dsb();
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_HRC,dc);//0x1900);// dc);
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_LRC, pv);//0x5dc0);//pv);
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CNTR,0);
        dsb();
	 rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CTRL,on|conf);
        dsb();
        spin_unlock_irqrestore(lock, flags);	

#if PWM_CLK
	clk_disable(pc->clk);
#endif

	return 0;
}
#endif

static int rk_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rk_pwm_chip *pc = to_rk_pwm_chip(chip);
	int rc = 0;
	u32 val;
#if PWM_CLK
	rc = clk_enable(pc->clk);
	if (rc)
		return rc;
#endif
	val = rk_pwm_readl(pc, pwm->hwpwm, PWM_REG_CTRL);
	val |= PWM_ENABLE;
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CTRL, val);

	return 0;
}

static void rk_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rk_pwm_chip *pc = to_rk_pwm_chip(chip);
	u32 val;

	val = rk_pwm_readl(pc, pwm->hwpwm, PWM_REG_CTRL);
	val &= ~PWM_ENABLE;
	rk_pwm_writel(pc, pwm->hwpwm, PWM_REG_CTRL, val);
#if PWM_CLK
	clk_disable(pc->clk);
#endif
}


static const struct pwm_ops rk_pwm_ops = {
	.config = rk_pwm_config,
	.enable = rk_pwm_enable,
	.disable = rk_pwm_disable,
	.owner = THIS_MODULE,
};



static int rk_pwm_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct rk_pwm_chip *pc;
	struct resource *r;
	int ret;
	DBG("%s start \n",__FUNCTION__);
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "no memory resources defined\n");
		return -ENODEV;
	}
	pc = devm_kzalloc(&pdev->dev, sizeof(*pc), GFP_KERNEL);
	if (!pc) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}
	pc->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(pc->base))
		return PTR_ERR(pc->base);

#if PWM_CLK
	//pc->clk = devm_clk_get(&pdev->dev, NULL);
	pc->clk = clk_get(NULL,"g_p_pwm23");


	if (IS_ERR(pc->clk))
		return PTR_ERR(pc->clk);
#endif

	platform_set_drvdata(pdev, pc);

	pc->chip.dev = &pdev->dev;
	pc->chip.ops = &rk_pwm_ops;
	pc->chip.base = -1;
	pc->chip.npwm = NUM_PWM;

#if PWM_CLK
	ret = clk_prepare(pc->clk);
	if (ret)
		return ret;
#endif

#if PWM_CLK
	if (of_device_is_compatible(np, "rockchip,pwm")) {
		ret = clk_enable(pc->clk);
		if (ret) {
			clk_unprepare(pc->clk);
			return ret;
		}
		/*
		 * Following enables PWM chip, channels would still be
		 * enabled individually through their control register
		 */
#if PWM_CLK
//		clk_disable(pc->clk);
#endif
	}
#endif
	DBG("npwm = %d, of_pwm_ncells =%d \n", pc->chip.npwm,pc->chip.of_pwm_n_cells);
	ret = pwmchip_add(&pc->chip);
	if (ret < 0) {
		clk_unprepare(pc->clk);
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
	}
	DBG("%s end \n",__FUNCTION__);

	return ret;
}

static int rk_pwm_remove(struct platform_device *pdev)
{
	return 0;//pwmchip_remove(&pc->chip);
}


static const struct of_device_id rk_pwm_of_match[] = {
	{ .compatible = "rockchip,pwm" },
	{ }
};

MODULE_DEVICE_TABLE(of, rk_pwm_of_match);

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


