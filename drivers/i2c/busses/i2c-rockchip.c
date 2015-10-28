/*
 * Copyright (C) 2012-2014 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/wakelock.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/of_i2c.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <asm/irq.h>

#if 0
#define i2c_dbg(dev, format, arg...)		\
	dev_printk(KERN_INFO , dev , format , ## arg)
#else
#define i2c_dbg(dev, format, arg...)
#endif

enum {
	I2C_IDLE = 0,
	I2C_SDA_LOW,
	I2C_SCL_LOW,
	BOTH_LOW,
};

#define i2c_writel                      writel_relaxed
#define i2c_readl                       readl_relaxed

#define I2C_WAIT_TIMEOUT                200  //200ms

#define rockchip_set_bit(p, v, b)       (((p) & ~(1 << (b))) | ((v) << (b)))
#define rockchip_get_bit(p, b)          (((p) & (1 << (b))) >> (b))

#define rockchip_set_bits(p, v, b, m)   (((p) & ~(m)) | ((v) << (b)))
#define rockchip_get_bits(p, b, m)      (((p) & (m)) >> (b))

#define rockchip_ceil(x, y) \
	({ unsigned long __x = (x), __y = (y); (__x + __y - 1) / __y; })

enum rockchip_i2c_state {
	STATE_IDLE,
	STATE_START,
	STATE_READ,
	STATE_WRITE,
	STATE_STOP
};

struct rockchip_i2c {
	spinlock_t		lock;
	wait_queue_head_t	wait;
	unsigned int		suspended:1;

	struct i2c_msg		*msg;
	unsigned int		is_busy;
	int			error;
	unsigned int		msg_ptr;

	unsigned int		irq;

	enum rockchip_i2c_state	state;
	unsigned int		complete_what;
	unsigned long		clkrate;

	void __iomem		*regs;
	struct clk		*clk;
	struct device		*dev;
	struct resource		*ioarea;
	struct i2c_adapter	adap;
	struct mutex 		suspend_lock;

	unsigned long		scl_rate;
	unsigned long		i2c_rate;
	unsigned int		addr;
	unsigned char		addr_1st, addr_2nd;
	unsigned int		mode;
	unsigned int		count;

	unsigned int		check_idle;
	int			sda_gpio, scl_gpio;
	struct pinctrl_state	*gpio_state;
};

#define COMPLETE_READ      (1<<STATE_START | 1<<STATE_READ  | 1<<STATE_STOP)
#define COMPLETE_WRITE     (1<<STATE_START | 1<<STATE_WRITE | 1<<STATE_STOP)

/* Control register */
#define I2C_CON                 0x000
#define I2C_CON_EN              (1 << 0)
#define I2C_CON_MOD(mod)        ((mod) << 1)
#define I2C_CON_MASK            (3 << 1)

enum {
	I2C_CON_MOD_TX = 0,
	I2C_CON_MOD_TRX,
	I2C_CON_MOD_RX,
	I2C_CON_MOD_RRX,
};

#define I2C_CON_START           (1 << 3)
#define I2C_CON_STOP            (1 << 4)
#define I2C_CON_LASTACK         (1 << 5)
#define I2C_CON_ACTACK          (1 << 6)

/* Clock dividor register */
#define I2C_CLKDIV              0x004
#define I2C_CLKDIV_VAL(divl, divh) (((divl) & 0xffff) | (((divh) << 16) & 0xffff0000))

/* the slave address accessed  for master rx mode */
#define I2C_MRXADDR             0x008
#define I2C_MRXADDR_LOW         (1 << 24)
#define I2C_MRXADDR_MID         (1 << 25)
#define I2C_MRXADDR_HIGH        (1 << 26)

/* the slave register address accessed  for master rx mode */
#define I2C_MRXRADDR            0x00c
#define I2C_MRXRADDR_LOW        (1 << 24)
#define I2C_MRXRADDR_MID        (1 << 25)
#define I2C_MRXRADDR_HIGH       (1 << 26)

/* master tx count */
#define I2C_MTXCNT              0x010

/* master rx count */
#define I2C_MRXCNT              0x014

/* interrupt enable register */
#define I2C_IEN                 0x018
#define I2C_BTFIEN              (1 << 0)
#define I2C_BRFIEN              (1 << 1)
#define I2C_MBTFIEN             (1 << 2)
#define I2C_MBRFIEN             (1 << 3)
#define I2C_STARTIEN            (1 << 4)
#define I2C_STOPIEN             (1 << 5)
#define I2C_NAKRCVIEN           (1 << 6)
#define IRQ_MST_ENABLE          (I2C_MBTFIEN | I2C_MBRFIEN | I2C_NAKRCVIEN | I2C_STARTIEN | I2C_STOPIEN)
#define IRQ_ALL_DISABLE         0

/* interrupt pending register */
#define I2C_IPD                 0x01c
#define I2C_BTFIPD              (1 << 0)
#define I2C_BRFIPD              (1 << 1)
#define I2C_MBTFIPD             (1 << 2)
#define I2C_MBRFIPD             (1 << 3)
#define I2C_STARTIPD            (1 << 4)
#define I2C_STOPIPD             (1 << 5)
#define I2C_NAKRCVIPD           (1 << 6)

#define I2C_HOLD_SCL           	(1 << 7)
#define I2C_IPD_ALL_CLEAN       0x7f

/* finished count */
#define I2C_FCNT                0x020

/* I2C tx data register */
#define I2C_TXDATA_BASE         0X100

/* I2C rx data register */
#define I2C_RXDATA_BASE         0x200

static void rockchip_show_regs(struct rockchip_i2c *i2c)
{
	int i;

	dev_info(i2c->dev, "i2c->clk = %lu\n", clk_get_rate(i2c->clk));
	dev_info(i2c->dev, "i2c->state = %d\n", i2c->state);
	dev_info(i2c->dev, "I2C_CON: 0x%08x\n", i2c_readl(i2c->regs + I2C_CON));
	dev_info(i2c->dev, "I2C_CLKDIV: 0x%08x\n", i2c_readl(i2c->regs + I2C_CLKDIV));
	dev_info(i2c->dev, "I2C_MRXADDR: 0x%08x\n", i2c_readl(i2c->regs + I2C_MRXADDR));
	dev_info(i2c->dev, "I2C_MRXRADDR: 0x%08x\n", i2c_readl(i2c->regs + I2C_MRXRADDR));
	dev_info(i2c->dev, "I2C_MTXCNT: 0x%08x\n", i2c_readl(i2c->regs + I2C_MTXCNT));
	dev_info(i2c->dev, "I2C_MRXCNT: 0x%08x\n", i2c_readl(i2c->regs + I2C_MRXCNT));
	dev_info(i2c->dev, "I2C_IEN: 0x%08x\n", i2c_readl(i2c->regs + I2C_IEN));
	dev_info(i2c->dev, "I2C_IPD: 0x%08x\n", i2c_readl(i2c->regs + I2C_IPD));
	dev_info(i2c->dev, "I2C_FCNT: 0x%08x\n", i2c_readl(i2c->regs + I2C_FCNT));
	for (i = 0; i < 8; i++)
		dev_info(i2c->dev, "I2C_TXDATA%d: 0x%08x\n", i, i2c_readl(i2c->regs + I2C_TXDATA_BASE + i * 4));
	for (i = 0; i < 8; i++)
		dev_info(i2c->dev, "I2C_RXDATA%d: 0x%08x\n", i, i2c_readl(i2c->regs + I2C_RXDATA_BASE + i * 4));
}

static int rockchip_i2c_check_idle(struct rockchip_i2c *i2c)
{
	int ret = I2C_IDLE;
	int sda_lev, scl_lev;

	if (!gpio_is_valid(i2c->sda_gpio))
		return ret;

	if (pinctrl_select_state(i2c->dev->pins->p, i2c->gpio_state))
		return ret;

	sda_lev = gpio_get_value(i2c->sda_gpio);
	scl_lev = gpio_get_value(i2c->scl_gpio);

	pinctrl_select_state(i2c->dev->pins->p, i2c->dev->pins->default_state);

	if (sda_lev == 1 && scl_lev == 1)
		return I2C_IDLE;
	else if (sda_lev == 0 && scl_lev == 1)
		return I2C_SDA_LOW;
	else if (sda_lev == 1 && scl_lev == 0)
		return I2C_SCL_LOW;
	else
		return BOTH_LOW;
}

static inline void rockchip_i2c_enable(struct rockchip_i2c *i2c, unsigned int lastnak)
{
	unsigned int con = 0;

	con |= I2C_CON_EN;
	con |= I2C_CON_MOD(i2c->mode);
	if (lastnak)
		con |= I2C_CON_LASTACK;
	con |= I2C_CON_START;
	i2c_writel(con, i2c->regs + I2C_CON);
}

static inline void rockchip_i2c_disable(struct rockchip_i2c *i2c)
{
	i2c_writel(0, i2c->regs + I2C_CON);
}

static inline void rockchip_i2c_clean_start(struct rockchip_i2c *i2c)
{
	unsigned int con = i2c_readl(i2c->regs + I2C_CON);

	con &= ~I2C_CON_START;
	i2c_writel(con, i2c->regs + I2C_CON);
}

static inline void rockchip_i2c_send_start(struct rockchip_i2c *i2c)
{
	unsigned int con = i2c_readl(i2c->regs + I2C_CON);

	con |= I2C_CON_START;
	if (con & I2C_CON_STOP)
		dev_warn(i2c->dev, "I2C_CON: stop bit is set\n");

	i2c_writel(con, i2c->regs + I2C_CON);
}

static inline void rockchip_i2c_send_stop(struct rockchip_i2c *i2c)
{
	unsigned int con = i2c_readl(i2c->regs + I2C_CON);

	con |= I2C_CON_STOP;
	if (con & I2C_CON_START)
		dev_warn(i2c->dev, "I2C_CON: start bit is set\n");

	i2c_writel(con, i2c->regs + I2C_CON);
}

static inline void rockchip_i2c_clean_stop(struct rockchip_i2c *i2c)
{
	unsigned int con = i2c_readl(i2c->regs + I2C_CON);

	con &= ~I2C_CON_STOP;
	i2c_writel(con, i2c->regs + I2C_CON);
}

static inline void rockchip_i2c_disable_irq(struct rockchip_i2c *i2c)
{
	i2c_writel(IRQ_ALL_DISABLE, i2c->regs + I2C_IEN);
}

static inline void rockchip_i2c_enable_irq(struct rockchip_i2c *i2c)
{
	i2c_writel(IRQ_MST_ENABLE, i2c->regs + I2C_IEN);
}

static void rockchip_get_div(int div, int *divh, int *divl)
{
	if (div % 2 == 0) {
		*divh = div / 2;
		*divl = div / 2;
	} else {
		*divh = rockchip_ceil(div, 2);
		*divl = div / 2;
	}
}

/* SCL Divisor = 8 * (CLKDIVL+1 + CLKDIVH+1)
 * SCL = i2c_rate/ SCLK Divisor
*/
static void rockchip_i2c_set_clk(struct rockchip_i2c *i2c, unsigned long scl_rate)
{
	unsigned long i2c_rate = i2c->i2c_rate;
	int div, divl, divh;

	if (scl_rate == i2c->scl_rate)
		return;
	i2c->scl_rate = scl_rate;
	div = rockchip_ceil(i2c_rate, (scl_rate * 8)) - 2;
	if (unlikely(div < 0)) {
		dev_warn(i2c->dev, "Divisor(%d) is negative, set divl = divh = 0\n", div);
		divh = divl = 0;
	} else {
		rockchip_get_div(div, &divh, &divl);
	}
	i2c_writel(I2C_CLKDIV_VAL(divl, divh), i2c->regs + I2C_CLKDIV);
	i2c_dbg(i2c->dev, "set clk(I2C_CLKDIV: 0x%08x)\n", i2c_readl(i2c->regs + I2C_CLKDIV));
}

static void rockchip_i2c_init_hw(struct rockchip_i2c *i2c, unsigned long scl_rate)
{
	i2c->scl_rate = 0;
	clk_enable(i2c->clk);
	rockchip_i2c_set_clk(i2c, scl_rate);
	clk_disable(i2c->clk);
}

/* returns TRUE if we this is the last byte in the current message */
static inline int is_msglast(struct rockchip_i2c *i2c)
{
	return i2c->msg_ptr == i2c->msg->len - 1;
}

/* returns TRUE if we reached the end of the current message */
static inline int is_msgend(struct rockchip_i2c *i2c)
{
	return i2c->msg_ptr >= i2c->msg->len;
}

static void rockchip_i2c_stop(struct rockchip_i2c *i2c, int ret)
{

	i2c->msg_ptr = 0;
	i2c->msg = NULL;
	if (ret == -EAGAIN) {
		i2c->state = STATE_IDLE;
		i2c->is_busy = 0;
		wake_up(&i2c->wait);
		return;
	}
	i2c->error = ret;
	i2c_writel(I2C_STOPIEN, i2c->regs + I2C_IEN);
	i2c->state = STATE_STOP;
	rockchip_i2c_send_stop(i2c);
}

static inline void rockchip_set_rx_mode(struct rockchip_i2c *i2c, unsigned int lastnak)
{
	unsigned long con = i2c_readl(i2c->regs + I2C_CON);

	con &= (~I2C_CON_MASK);
	con |= (I2C_CON_MOD_RX << 1);
	if (lastnak)
		con |= I2C_CON_LASTACK;
	i2c_writel(con, i2c->regs + I2C_CON);
}

static void rockchip_irq_read_prepare(struct rockchip_i2c *i2c)
{
	unsigned int cnt, len = i2c->msg->len - i2c->msg_ptr;

	if (len <= 32 && i2c->msg_ptr != 0)
		rockchip_set_rx_mode(i2c, 1);
	else if (i2c->msg_ptr != 0)
		rockchip_set_rx_mode(i2c, 0);

	if (is_msgend(i2c)) {
		rockchip_i2c_stop(i2c, i2c->error);
		return;
	}
	if (len > 32)
		cnt = 32;
	else
		cnt = len;
	i2c_writel(cnt, i2c->regs + I2C_MRXCNT);
}

static void rockchip_irq_read_get_data(struct rockchip_i2c *i2c)
{
	unsigned int i, len = i2c->msg->len - i2c->msg_ptr;
	unsigned int p = 0;

	len = (len >= 32) ? 32 : len;

	for (i = 0; i < len; i++) {
		if (i % 4 == 0)
			p = i2c_readl(i2c->regs + I2C_RXDATA_BASE + (i / 4) * 4);
		i2c->msg->buf[i2c->msg_ptr++] = (p >> ((i % 4) * 8)) & 0xff;
	}
}

static void rockchip_irq_write_prepare(struct rockchip_i2c *i2c)
{
	unsigned int data = 0, cnt = 0, i, j;
	unsigned char byte;

	if (is_msgend(i2c)) {
		rockchip_i2c_stop(i2c, i2c->error);
		return;
	}
	for (i = 0; i < 8; i++) {
		data = 0;
		for (j = 0; j < 4; j++) {
			if (is_msgend(i2c))
				break;
			if ((i2c->msg_ptr == 0) && (cnt == 0))
				byte = (i2c->addr_1st & 0x7f) << 1;
			else if ((i2c->msg_ptr == 0) && (cnt == 1) && (i2c->msg->flags & I2C_M_TEN))
				byte = i2c->addr_2nd;
			else
				byte = i2c->msg->buf[i2c->msg_ptr++];
			cnt++;
			data |= (byte << (j * 8));
		}
		i2c_writel(data, i2c->regs + I2C_TXDATA_BASE + 4 * i);
		if (is_msgend(i2c))
			break;
	}
	i2c_writel(cnt, i2c->regs + I2C_MTXCNT);
}

static void rockchip_i2c_irq_nextblock(struct rockchip_i2c *i2c, unsigned int ipd)
{
	switch (i2c->state) {
	case STATE_START:
		if (!(ipd & I2C_STARTIPD)) {
			rockchip_i2c_stop(i2c, -ENXIO);
			dev_err(i2c->dev, "Addr[0x%04x] no start irq in STATE_START\n", i2c->addr);
			rockchip_show_regs(i2c);
			i2c_writel(I2C_IPD_ALL_CLEAN, i2c->regs + I2C_IPD);
			goto out;
		}
		i2c->complete_what |= 1 << i2c->state;
		i2c_writel(I2C_STARTIPD, i2c->regs + I2C_IPD);
		rockchip_i2c_clean_start(i2c);
		if (i2c->mode == I2C_CON_MOD_TX) {
			i2c_writel(I2C_MBTFIEN | I2C_NAKRCVIEN, i2c->regs + I2C_IEN);
			i2c->state = STATE_WRITE;
			goto prepare_write;
		} else {
			i2c_writel(I2C_MBRFIEN | I2C_NAKRCVIEN, i2c->regs + I2C_IEN);
			i2c->state = STATE_READ;
			goto prepare_read;
		}
	case STATE_WRITE:
		if (!(ipd & I2C_MBTFIPD)) {
			rockchip_i2c_stop(i2c, -ENXIO);
			dev_err(i2c->dev, "Addr[0x%04x] no mbtf irq in STATE_WRITE\n", i2c->addr);
			rockchip_show_regs(i2c);
			i2c_writel(I2C_IPD_ALL_CLEAN, i2c->regs + I2C_IPD);
			goto out;
		}
		i2c->complete_what |= 1 << i2c->state;
		i2c_writel(I2C_MBTFIPD, i2c->regs + I2C_IPD);
prepare_write:
		rockchip_irq_write_prepare(i2c);
		break;
	case STATE_READ:
		if (!(ipd & I2C_MBRFIPD)) {
			rockchip_i2c_stop(i2c, -ENXIO);
			dev_err(i2c->dev, "Addr[0x%04x] no mbrf irq in STATE_READ, ipd = 0x%x\n", i2c->addr, ipd);
			rockchip_show_regs(i2c);
			i2c_writel(I2C_IPD_ALL_CLEAN, i2c->regs + I2C_IPD);
			goto out;
		}
		i2c->complete_what |= 1 << i2c->state;
		i2c_writel(I2C_MBRFIPD, i2c->regs + I2C_IPD);
		rockchip_irq_read_get_data(i2c);
prepare_read:
		rockchip_irq_read_prepare(i2c);
		break;
	case STATE_STOP:
		if (!(ipd & I2C_STOPIPD)) {
			rockchip_i2c_stop(i2c, -ENXIO);
			dev_err(i2c->dev, "Addr[0x%04x] no stop irq in STATE_STOP\n", i2c->addr);
			rockchip_show_regs(i2c);
			i2c_writel(I2C_IPD_ALL_CLEAN, i2c->regs + I2C_IPD);
			goto out;
		}
		rockchip_i2c_clean_stop(i2c);
		i2c_writel(I2C_STOPIPD, i2c->regs + I2C_IPD);
		i2c->is_busy = 0;
		i2c->complete_what |= 1 << i2c->state;
		i2c->state = STATE_IDLE;
		wake_up(&i2c->wait);
		break;
	default:
		break;
	}
out:
	return;
}

static irqreturn_t rockchip_i2c_irq(int irq, void *dev_id)
{
	struct rockchip_i2c *i2c = dev_id;
	unsigned int ipd;

	spin_lock(&i2c->lock);
	ipd = i2c_readl(i2c->regs + I2C_IPD);
	if (i2c->state == STATE_IDLE) {
		dev_info(i2c->dev, "Addr[0x%04x]  irq in STATE_IDLE, ipd = 0x%x\n", i2c->addr, ipd);
		i2c_writel(I2C_IPD_ALL_CLEAN, i2c->regs + I2C_IPD);
		goto out;
	}

	if (ipd & I2C_NAKRCVIPD) {
		i2c_writel(I2C_NAKRCVIPD, i2c->regs + I2C_IPD);
		i2c->error = -EAGAIN;
		goto out;
	}
	rockchip_i2c_irq_nextblock(i2c, ipd);
out:
	spin_unlock(&i2c->lock);
	return IRQ_HANDLED;
}

static int rockchip_i2c_set_master(struct rockchip_i2c *i2c, struct i2c_msg *msgs, int num)
{
	unsigned int reg_valid_bits = 0;
	unsigned int reg_addr = 0;
	unsigned int addr = (i2c->addr_1st << 1) | 1;

	if (num == 1) {
		i2c->count = msgs[0].len;
		if (!(msgs[0].flags & I2C_M_RD)) {
			i2c->msg = &msgs[0];
			i2c->mode = I2C_CON_MOD_TX;
		} else {
			i2c->msg = &msgs[0];
			i2c_writel(addr | I2C_MRXADDR_LOW,
				   i2c->regs + I2C_MRXADDR);
			i2c_writel(0, i2c->regs + I2C_MRXRADDR);
			i2c->mode = I2C_CON_MOD_TRX;
			//i2c->mode = I2C_CON_MOD_RX;
		}
	} else if (num == 2) {
		i2c->count = msgs[1].len;

		if (msgs[0].flags & I2C_M_TEN) {
			switch (msgs[0].len) {
			case 1:
				reg_addr = i2c->addr_2nd | (msgs[0].buf[0] << 8);
				reg_valid_bits |= I2C_MRXADDR_LOW | I2C_MRXADDR_MID;
				break;
			case 2:
				reg_addr = i2c->addr_2nd | (msgs[0].buf[0] << 8) | (msgs[0].buf[1] << 16);
				reg_valid_bits |= I2C_MRXADDR_LOW | I2C_MRXADDR_MID | I2C_MRXADDR_HIGH;
				break;
			default:
				return -EIO;
			}
		} else {
			switch (msgs[0].len) {
			case 1:
				reg_addr = msgs[0].buf[0];
				reg_valid_bits |= I2C_MRXADDR_LOW;
				break;
			case 2:
				reg_addr = msgs[0].buf[0] | (msgs[0].buf[1] << 8);
				reg_valid_bits |= I2C_MRXADDR_LOW | I2C_MRXADDR_MID;
				break;
			case 3:
				reg_addr = msgs[0].buf[0] | (msgs[0].buf[1] << 8) | (msgs[0].buf[2] << 16);
				reg_valid_bits |= I2C_MRXADDR_LOW | I2C_MRXADDR_MID | I2C_MRXADDR_HIGH;
				break;
			default:
				return -EIO;
			}
		}
		if ((msgs[0].flags & I2C_M_RD) && (msgs[1].flags & I2C_M_RD)) {
			i2c->msg = &msgs[1];
			i2c_writel(addr | I2C_MRXADDR_LOW, i2c->regs + I2C_MRXADDR);
			i2c_writel(reg_addr | reg_valid_bits, i2c->regs + I2C_MRXRADDR);
			i2c->mode = I2C_CON_MOD_RRX;
		} else if (!(msgs[0].flags & I2C_M_RD) && (msgs[1].flags & I2C_M_RD)) {
			i2c->msg = &msgs[1];
			i2c_writel(addr | I2C_MRXADDR_LOW, i2c->regs + I2C_MRXADDR);
			i2c_writel(reg_addr | reg_valid_bits, i2c->regs + I2C_MRXRADDR);
			i2c->mode = I2C_CON_MOD_TRX;
		} else
			return -EIO;
	} else {
		dev_err(i2c->dev, "This case(num > 2) has not been support now\n");
		return -EIO;
	}

	return 0;
}

/* rockchip_i2c_doxfer
 *
 * this starts an i2c transfer
*/
static int rockchip_i2c_doxfer(struct rockchip_i2c *i2c,
			      struct i2c_msg *msgs, int num)
{
	unsigned long timeout, flags;
	int error = 0;
	/* 32 -- max transfer bytes
	 * 2 -- addr bytes * 2
	 * 3 -- max reg addr bytes
	 * 9 -- cycles per bytes
	 * max cycles: (32 + 2 + 3) * 9 --> 400 cycles
	 */
	int msleep_time = 400 * 1000 / i2c->scl_rate;	// ms
	int can_sleep = !(in_atomic() || irqs_disabled());

	spin_lock_irqsave(&i2c->lock, flags);
	i2c->addr = msgs[0].addr;
	if (msgs[0].flags & I2C_M_TEN) {
		i2c->addr_1st = ((i2c->addr & 0x0300) >> 8) | 0x78;
		i2c->addr_2nd = i2c->addr & 0x00ff;
	} else {
		i2c->addr_1st = i2c->addr & 0x007f;
		i2c->addr_2nd = 0;
	}
	i2c_dbg(i2c->dev, "addr: 0x%04x, addr_1st: 0x%02x, addr_2nd: 0x%02x\n",
		i2c->addr, i2c->addr_1st, i2c->addr_2nd);

	if (rockchip_i2c_set_master(i2c, msgs, num) < 0) {
		spin_unlock_irqrestore(&i2c->lock, flags);
		dev_err(i2c->dev, "addr[0x%04x] set master error\n", msgs[0].addr);
		return -EIO;
	}
	i2c->msg_ptr = 0;
	i2c->error = 0;
	i2c->is_busy = 1;
	i2c->state = STATE_START;
	i2c->complete_what = 0;
	i2c_writel(I2C_STARTIEN, i2c->regs + I2C_IEN);

	rockchip_i2c_enable(i2c, (i2c->count > 32) ? 0 : 1);	//if count > 32,  byte(32) send ack

	if (can_sleep) {
		long ret = msecs_to_jiffies(I2C_WAIT_TIMEOUT);
		if (i2c->is_busy) {
			DEFINE_WAIT(wait);
			for (;;) {
				prepare_to_wait(&i2c->wait, &wait, TASK_UNINTERRUPTIBLE);
				if (i2c->is_busy == 0)
					break;
				spin_unlock_irqrestore(&i2c->lock, flags);
				ret = schedule_timeout(ret);
				spin_lock_irqsave(&i2c->lock, flags);
				if (!ret)
					break;
			}
			if (!ret && (i2c->is_busy == 0))
				ret = 1;
			finish_wait(&i2c->wait, &wait);
		}
		timeout = ret;
	} else {
		int cpu = raw_smp_processor_id();
		int tmo = I2C_WAIT_TIMEOUT * USEC_PER_MSEC;
		while (tmo-- && i2c->is_busy != 0) {
			spin_unlock_irqrestore(&i2c->lock, flags);
			udelay(1);
			if (cpu == 0 && irqs_disabled()
			    && (i2c_readl(i2c->regs + I2C_IPD)
				& i2c_readl(i2c->regs + I2C_IEN))) {
				rockchip_i2c_irq(i2c->irq, i2c);
			}
			spin_lock_irqsave(&i2c->lock, flags);
		}
		timeout = (tmo <= 0) ? 0 : 1;
	}

	error = i2c->error;

	if (timeout == 0) {
		unsigned int ipd = i2c_readl(i2c->regs + I2C_IPD);
		if (error < 0)
			i2c_dbg(i2c->dev, "error = %d\n", error);
		else if (i2c->complete_what != COMPLETE_READ && i2c->complete_what != COMPLETE_WRITE) {
			if (ipd & I2C_HOLD_SCL)
				dev_err(i2c->dev, "SCL was hold by slave\n");
			dev_err(i2c->dev, "Addr[0x%04x] wait event timeout, state: %d, is_busy: %d, error: %d, complete_what: 0x%x, ipd: 0x%x\n",
				msgs[0].addr, i2c->state, i2c->is_busy, error, i2c->complete_what, ipd);
			//rockchip_show_regs(i2c);
			error = -ETIMEDOUT;
			mdelay(msleep_time);
			rockchip_i2c_send_stop(i2c);
			mdelay(1);
		} else
			i2c_dbg(i2c->dev, "Addr[0x%02x] wait event timeout, but transfer complete\n", i2c->addr);
	}

	i2c->state = STATE_IDLE;

	i2c_writel(I2C_IPD_ALL_CLEAN, i2c->regs + I2C_IPD);
	rockchip_i2c_disable_irq(i2c);
	rockchip_i2c_disable(i2c);
	spin_unlock_irqrestore(&i2c->lock, flags);

	if (error == -EAGAIN)
		i2c_dbg(i2c->dev, "No ack(complete_what: 0x%x), Maybe slave(addr: 0x%04x) not exist or abnormal power-on\n",
			i2c->complete_what, i2c->addr);

	return error;
}

/* rockchip_i2c_xfer
 *
 * first port of call from the i2c bus code when an message needs
 * transferring across the i2c bus.
*/

static int rockchip_i2c_xfer(struct i2c_adapter *adap,
			struct i2c_msg *msgs, int num)
{
	int ret;
	struct rockchip_i2c *i2c = i2c_get_adapdata(adap);
	unsigned long scl_rate = i2c->scl_rate;
	int can_sleep = !(in_atomic() || irqs_disabled());

	if (can_sleep) {
		mutex_lock(&i2c->suspend_lock);
		if (i2c->suspended) {
			dev_err(i2c->dev, "i2c is suspended\n");
			mutex_unlock(&i2c->suspend_lock);
			return -EIO;
		}
	}

	clk_enable(i2c->clk);
	if (i2c->check_idle) {
		int state, retry = 10;
		while (retry) {
			state = rockchip_i2c_check_idle(i2c);
			if (state == I2C_IDLE)
				break;
			if (can_sleep)
				msleep(10);
			else
				mdelay(10);
			retry--;
		}
		if (retry == 0) {
			dev_err(i2c->dev, "i2c is not in idle(state = %d)\n", state);
			ret = -EIO;
			goto out;
		}
	}

#ifdef CONFIG_I2C_ROCKCHIP_COMPAT
	if (msgs[0].scl_rate <= 400000 && msgs[0].scl_rate >= 10000)
		scl_rate = msgs[0].scl_rate;
	else if (msgs[0].scl_rate > 400000) {
		dev_warn_ratelimited(i2c->dev, "Warning: addr[0x%04x] msg[0].scl_rate( = %dKhz) is too high!",
			msgs[0].addr, msgs[0].scl_rate/1000);
		scl_rate = 400000;
	} else {
		dev_warn_ratelimited(i2c->dev, "Warning: addr[0x%04x] msg[0].scl_rate( = %dKhz) is too low!",
			msgs[0].addr, msgs[0].scl_rate/1000);
		scl_rate = 100000;
	}

	rockchip_i2c_set_clk(i2c, scl_rate);
#endif

	i2c_dbg(i2c->dev, "i2c transfer start: addr: 0x%04x, scl_reate: %ldKhz, len: %d\n", msgs[0].addr, scl_rate/1000, num);
	ret = rockchip_i2c_doxfer(i2c, msgs, num);
	i2c_dbg(i2c->dev, "i2c transfer stop: addr: 0x%04x, state: %d, ret: %d\n", msgs[0].addr, ret, i2c->state);

out:
	clk_disable(i2c->clk);
	if (can_sleep)
		mutex_unlock(&i2c->suspend_lock);

	return (ret < 0) ? ret : num;
}

/* declare our i2c functionality */
static u32 rockchip_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_PROTOCOL_MANGLING;
}

/* i2c bus registration info */

static const struct i2c_algorithm rockchip_i2c_algorithm = {
	.master_xfer		= rockchip_i2c_xfer,
	.functionality		= rockchip_i2c_func,
};

/* rockchip_i2c_probe
 *
 * called by the bus driver when a suitable device is found
*/

static int rockchip_i2c_probe(struct platform_device *pdev)
{
	struct rockchip_i2c *i2c = NULL;
	struct resource *res;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	if (!np) {
		dev_err(&pdev->dev, "Missing device tree node.\n");
		return -EINVAL;
	}

	i2c = devm_kzalloc(&pdev->dev, sizeof(struct rockchip_i2c), GFP_KERNEL);
	if (!i2c) {
		dev_err(&pdev->dev, "no memory for state\n");
		return -ENOMEM;
	}

	strlcpy(i2c->adap.name, "rockchip_i2c", sizeof(i2c->adap.name));
	i2c->dev = &pdev->dev;
	i2c->adap.owner = THIS_MODULE;
	i2c->adap.class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	i2c->adap.retries = 2;
	i2c->adap.timeout = msecs_to_jiffies(100);
	i2c->adap.algo = &rockchip_i2c_algorithm;
	i2c_set_adapdata(&i2c->adap, i2c);
	i2c->adap.dev.parent = &pdev->dev;
	i2c->adap.dev.of_node = np;

	spin_lock_init(&i2c->lock);
	init_waitqueue_head(&i2c->wait);

	/* map the registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2c->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2c->regs))
		return PTR_ERR(i2c->regs);

	i2c_dbg(&pdev->dev, "registers %p (%p, %p)\n",
		i2c->regs, i2c->ioarea, res);

	i2c->check_idle = true;
	of_property_read_u32(np, "rockchip,check-idle", &i2c->check_idle);
	if (i2c->check_idle) {
		i2c->sda_gpio = of_get_gpio(np, 0);
		if (!gpio_is_valid(i2c->sda_gpio)) {
			dev_err(&pdev->dev, "sda gpio is invalid\n");
			return -EINVAL;
		}
		ret = devm_gpio_request(&pdev->dev, i2c->sda_gpio, dev_name(&i2c->adap.dev));
		if (ret) {
			dev_err(&pdev->dev, "failed to request sda gpio\n");
			return ret;
		}
		i2c->scl_gpio = of_get_gpio(np, 1);
		if (!gpio_is_valid(i2c->scl_gpio)) {
			dev_err(&pdev->dev, "scl gpio is invalid\n");
			return -EINVAL;
		}
		ret = devm_gpio_request(&pdev->dev, i2c->scl_gpio, dev_name(&i2c->adap.dev));
		if (ret) {
			dev_err(&pdev->dev, "failed to request scl gpio\n");
			return ret;
		}
		i2c->gpio_state = pinctrl_lookup_state(i2c->dev->pins->p, "gpio");
		if (IS_ERR(i2c->gpio_state)) {
			dev_err(&pdev->dev, "no gpio pinctrl state\n");
			return PTR_ERR(i2c->gpio_state);
		}
		pinctrl_select_state(i2c->dev->pins->p, i2c->gpio_state);
		gpio_direction_input(i2c->sda_gpio);
		gpio_direction_input(i2c->scl_gpio);
		pinctrl_select_state(i2c->dev->pins->p, i2c->dev->pins->default_state);
	}

	/* setup info block for the i2c core */
	ret = i2c_add_adapter(&i2c->adap);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add adapter\n");
		return ret;
	}

	platform_set_drvdata(pdev, i2c);

	/* find the clock and enable it */
	i2c->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(i2c->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		return PTR_ERR(i2c->clk);
	}

	i2c_dbg(&pdev->dev, "clock source %p\n", i2c->clk);

	/* find the IRQ for this unit (note, this relies on the init call to
	 * ensure no current IRQs pending
	 */
	i2c->irq = ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot find IRQ\n");
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, i2c->irq, rockchip_i2c_irq, 0,
			dev_name(&i2c->adap.dev), i2c);
	if (ret) {
		dev_err(&pdev->dev, "cannot claim IRQ %d\n", i2c->irq);
		return ret;
	}

	ret = clk_prepare(i2c->clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not prepare clock\n");
		return ret;
	}

	i2c->i2c_rate = clk_get_rate(i2c->clk);

	rockchip_i2c_init_hw(i2c, 100 * 1000);
	dev_info(&pdev->dev, "%s: Rockchip I2C adapter\n", dev_name(&i2c->adap.dev));

	of_i2c_register_devices(&i2c->adap);
	mutex_init(&i2c->suspend_lock);

	return 0;
}

/* rockchip_i2c_remove
 *
 * called when device is removed from the bus
*/

static int rockchip_i2c_remove(struct platform_device *pdev)
{
	struct rockchip_i2c *i2c = platform_get_drvdata(pdev);

	mutex_lock(&i2c->suspend_lock);
	i2c->suspended = 1;
	i2c_del_adapter(&i2c->adap);
	clk_unprepare(i2c->clk);
	mutex_unlock(&i2c->suspend_lock);

	return 0;
}

/* rockchip_i2c_shutdown
 *
 * called when device is shutdown from the bus
*/
static void rockchip_i2c_shutdown(struct platform_device *pdev)
{
	struct rockchip_i2c *i2c = platform_get_drvdata(pdev);

	mutex_lock(&i2c->suspend_lock);
	i2c->suspended = 1;
	mutex_unlock(&i2c->suspend_lock);
}

#ifdef CONFIG_PM
static int rockchip_i2c_suspend_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rockchip_i2c *i2c = platform_get_drvdata(pdev);

	mutex_lock(&i2c->suspend_lock);
	i2c->suspended = 1;
	pinctrl_pm_select_sleep_state(dev);
	mutex_unlock(&i2c->suspend_lock);

	return 0;
}

static int rockchip_i2c_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rockchip_i2c *i2c = platform_get_drvdata(pdev);

	mutex_lock(&i2c->suspend_lock);
	pinctrl_pm_select_default_state(dev);
	rockchip_i2c_init_hw(i2c, i2c->scl_rate);
	i2c->suspended = 0;
	mutex_unlock(&i2c->suspend_lock);

	return 0;
}

static const struct dev_pm_ops rockchip_i2c_dev_pm_ops = {
	.suspend_noirq = rockchip_i2c_suspend_noirq,
	.resume_noirq = rockchip_i2c_resume_noirq,
};

#define ROCKCHIP_I2C_PM_OPS (&rockchip_i2c_dev_pm_ops)
#else
#define ROCKCHIP_I2C_PM_OPS NULL
#endif

static const struct of_device_id rockchip_i2c_of_match[] = {
	{ .compatible = "rockchip,rk30-i2c", .data = NULL, },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_i2c_of_match);

static struct platform_driver rockchip_i2c_driver = {
	.probe		= rockchip_i2c_probe,
	.remove		= rockchip_i2c_remove,
	.shutdown	= rockchip_i2c_shutdown,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "rockchip_i2c",
		.pm	= ROCKCHIP_I2C_PM_OPS,
		.of_match_table	= of_match_ptr(rockchip_i2c_of_match),
	},
};

static int __init rockchip_i2c_init_driver(void)
{
	return platform_driver_register(&rockchip_i2c_driver);
}

subsys_initcall(rockchip_i2c_init_driver);

static void __exit rockchip_i2c_exit_driver(void)
{
	platform_driver_unregister(&rockchip_i2c_driver);
}

module_exit(rockchip_i2c_exit_driver);

static int detect_read(struct i2c_client *client, char *buf, int len)
{
	struct i2c_msg msg;

	msg.addr = client->addr;
	msg.flags = client->flags | I2C_M_RD;
	msg.buf = buf;
	msg.len = len;
#ifdef CONFIG_I2C_ROCKCHIP_COMPAT
	msg.scl_rate = 100 * 1000;
#endif

	return i2c_transfer(client->adapter, &msg, 1);
}

static void detect_set_client(struct i2c_client *client, __u16 addr, int nr)
{
	client->flags = 0;
	client->addr = addr;
	client->adapter = i2c_get_adapter(nr);
}

static void slave_detect(int nr)
{
	int ret = 0;
	unsigned short addr;
	char val[8];
	char buf[6 * 0x80 + 20];
	struct i2c_client client;

	memset(buf, 0, 6 * 0x80 + 20);

	sprintf(buf, "I2c%d slave list: ", nr);
	do {
		for (addr = 0x01; addr < 0x80; addr++) {
			detect_set_client(&client, addr, nr);
			ret = detect_read(&client, val, 1);
			if (ret > 0)
				sprintf(buf, "%s  0x%02x", buf, addr);
		}
		printk("%s\n", buf);
	}
	while (0);
}

static ssize_t i2c_detect_write(struct file *file,
			const char __user *buf, size_t count, loff_t *offset)
{
	char nr_buf[8];
	int nr = 0, ret;

	if (count > 4)
		return -EFAULT;
	ret = copy_from_user(nr_buf, buf, count);
	if (ret < 0)
		return -EFAULT;

	sscanf(nr_buf, "%d", &nr);
	if (nr >= 5 || nr < 0)
		return -EFAULT;

	slave_detect(nr);

	return count;
}

static const struct file_operations i2c_detect_fops = {
	.write = i2c_detect_write,
};

static struct miscdevice i2c_detect_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "i2c_detect",
	.fops = &i2c_detect_fops,
};

static int __init i2c_detect_init(void)
{
	return misc_register(&i2c_detect_device);
}

static void __exit i2c_detect_exit(void)
{
	misc_deregister(&i2c_detect_device);
}

module_init(i2c_detect_init);
module_exit(i2c_detect_exit);

MODULE_DESCRIPTION("Driver for Rockchip I2C Bus");
MODULE_AUTHOR("kfx, kfx@rock-chips.com");
MODULE_LICENSE("GPL");
