#ifndef __I2C_KEYWEST_H__
#define __I2C_KEYWEST_H__

/* The Tumbler audio equalizer can be really slow sometimes */
#define POLL_TIMEOUT		(2*HZ)

/* Register indices */
typedef enum {
	reg_mode = 0,
	reg_control,
	reg_status,
	reg_isr,
	reg_ier,
	reg_addr,
	reg_subaddr,
	reg_data
} reg_t;


/* Mode register */
#define KW_I2C_MODE_100KHZ	0x00
#define KW_I2C_MODE_50KHZ	0x01
#define KW_I2C_MODE_25KHZ	0x02
#define KW_I2C_MODE_DUMB	0x00
#define KW_I2C_MODE_STANDARD	0x04
#define KW_I2C_MODE_STANDARDSUB	0x08
#define KW_I2C_MODE_COMBINED	0x0C
#define KW_I2C_MODE_MODE_MASK	0x0C
#define KW_I2C_MODE_CHAN_MASK	0xF0

/* Control register */
#define KW_I2C_CTL_AAK		0x01
#define KW_I2C_CTL_XADDR	0x02
#define KW_I2C_CTL_STOP		0x04
#define KW_I2C_CTL_START	0x08

/* Status register */
#define KW_I2C_STAT_BUSY	0x01
#define KW_I2C_STAT_LAST_AAK	0x02
#define KW_I2C_STAT_LAST_RW	0x04
#define KW_I2C_STAT_SDA		0x08
#define KW_I2C_STAT_SCL		0x10

/* IER & ISR registers */
#define KW_I2C_IRQ_DATA		0x01
#define KW_I2C_IRQ_ADDR		0x02
#define KW_I2C_IRQ_STOP		0x04
#define KW_I2C_IRQ_START	0x08
#define KW_I2C_IRQ_MASK		0x0F

/* Physical interface */
struct keywest_iface
{
	struct device_node	*node;
	void __iomem *		base;
	unsigned		bsteps;
	int			irq;
	spinlock_t		lock;
	struct keywest_chan	*channels;
	unsigned		chan_count;
	u8			cur_mode;
	char			read_write;
	u8			*data;
	unsigned		datalen;
	int			state;
	int			result;
	struct timer_list	timeout_timer;
	struct completion	complete;
};

enum {
	state_idle,
	state_addr,
	state_read,
	state_write,
	state_stop,
	state_dead
};

/* Channel on an interface */
struct keywest_chan
{
	struct i2c_adapter	adapter;
	struct keywest_iface*	iface;
	unsigned		chan_no;
};

/* Register access */

static inline u8 __read_reg(struct keywest_iface *iface, reg_t reg)
{
	return in_8(iface->base
		+ (((unsigned)reg) << iface->bsteps));
}

static inline void __write_reg(struct keywest_iface *iface, reg_t reg, u8 val)
{
	out_8(iface->base
		+ (((unsigned)reg) << iface->bsteps), val);
	(void)__read_reg(iface, reg_subaddr);
}

#define write_reg(reg, val)	__write_reg(iface, reg, val) 
#define read_reg(reg)		__read_reg(iface, reg) 



#endif /* __I2C_KEYWEST_H__ */
