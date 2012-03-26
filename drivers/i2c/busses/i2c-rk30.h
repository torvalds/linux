#ifndef __RK30_I2C_H__
#define __RK30_I2C_H__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/wakelock.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/io.h>

#include <mach/board.h>
#include <mach/iomux.h>
#include <asm/irq.h>

#if 0
#define i2c_dbg(dev, format, arg...)		\
	dev_printk(KERN_INFO , dev , format , ## arg)
#else
#define i2c_dbg(dev, format, arg...)
#endif

#define i2c_writel                 writel_relaxed
#define i2c_readl                  readl_relaxed

#define I2C_WAIT_TIMEOUT            200  //100ms

#define rk30_set_bit(p, v, b)        (((p) & ~(1 << (b))) | ((v) << (b)))
#define rk30_get_bit(p, b)           (((p) & (1 << (b))) >> (b))

#define rk30_set_bits(p, v, b, m)	(((p) & ~(m)) | ((v) << (b)))
#define rk30_get_bits(p, b, m)	    (((p) & (m)) >> (b))

#define rk30_ceil(x, y) \
	({ unsigned long __x = (x), __y = (y); (__x + __y - 1) / __y; })

#define GRF_I2C_CON_BASE            (RK30_GRF_BASE + GRF_SOC_CON1)
#define I2C_ADAP_SEL_BIT(nr)        ((nr) + 11)
enum rk30_i2c_state {
	STATE_IDLE,
	STATE_START,
	STATE_READ,
	STATE_WRITE,
	STATE_STOP
};
struct rk30_i2c {
	spinlock_t		    lock;
	wait_queue_head_t	wait;
	unsigned int		suspended:1;

	struct i2c_msg		*msg;
	unsigned int		msg_num;
	unsigned int		msg_idx;
	unsigned int		msg_ptr;

	unsigned int		tx_setup;
	unsigned int		irq;

	enum rk30_i2c_state	state;
	unsigned long		clkrate;

	void __iomem		*regs;
    void __iomem        *con_base;
	struct clk		    *clk;
	struct device		*dev;
	struct resource		*ioarea;
	struct i2c_adapter	adap;
    
    unsigned long		scl_rate;
	unsigned long		i2c_rate;
    unsigned int        addr;
    unsigned int        mode;
    unsigned int        count;

    struct wake_lock    idlelock[5];
    int is_div_from_arm[5];

#ifdef CONFIG_CPU_FREQ
	struct notifier_block	freq_transition;
#endif

    void (*i2c_init_hw)(struct rk30_i2c *, unsigned long scl_rate);
    void (*i2c_set_clk)(struct rk30_i2c *, unsigned long);
    irqreturn_t (*i2c_irq)(int, void *);
};
void i2c_adap_sel(struct rk30_i2c *i2c, int nr, int adap_type);
int i2c_add_rk29_adapter(struct i2c_adapter *);
int i2c_add_rk30_adapter(struct i2c_adapter *);
#endif
