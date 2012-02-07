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
#include <asm/irq.h>

#if 0
#define i2c_dbg(dev, format, arg...)		\
	dev_printk(KERN_INFO , dev , format , ## arg)
#else
#define i2c_dbg(dev, format, arg...)
#endif


enum rk30_i2c_state {
	STATE_IDLE,
	STATE_START,
	STATE_READ,
	STATE_WRITE,
	STATE_STOP
};

struct rk30_i2c {
	spinlock_t		lock;
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
	struct clk		    *clk;
	struct device		*dev;
	struct resource		*ioarea;
	struct i2c_adapter	adap;
    
    unsigned long		scl_rate;
	unsigned long		i2c_rate;
    unsigned int        addr;

    struct wake_lock    idlelock[5];
    int is_div_from_arm[5];

#ifdef CONFIG_CPU_FREQ
	struct notifier_block	freq_transition;
#endif

    void (*i2c_init_hw)(struct rk30_i2c *);
    void (*i2c_set_clk)(struct rk30_i2c *, unsigned long);
    irqreturn_t (*i2c_irq)(int, void *);
};

int i2c_add_rk29_adapter(struct i2c_adapter *);
int i2c_add_rk30_adapter(struct i2c_adapter *);
#endif
