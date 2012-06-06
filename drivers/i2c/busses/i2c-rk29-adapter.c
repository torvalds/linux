/* drivers/i2c/busses/i2c-rk29-adapter.c
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
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
#include "i2c-rk30.h"

/* master transmit */
#define I2C_MTXR                (0x0000)
/* master receive */
#define I2C_MRXR                (0x0004)
/* slave address */
#define I2C_SADDR               (0x0010)
/* interrupt enable control */
#define I2C_IER                 (0x0014)
#define I2C_IER_ARBITR_LOSE     (1<<7)
#define I2C_IER_MRX_NEED_ACK    (1<<1)
#define I2C_IER_MTX_RCVD_ACK    (1<<0)

#define IRQ_MST_ENABLE        	(I2C_IER_ARBITR_LOSE | \
		                         I2C_IER_MRX_NEED_ACK | \
		                         I2C_IER_MTX_RCVD_ACK)
#define IRQ_ALL_DISABLE         (0x00)

/* interrupt status, write 0 to clear */
#define I2C_ISR                 (0x0018)
#define I2C_ISR_ARBITR_LOSE     (1<<7)
#define I2C_ISR_MRX_NEED_ACK    (1<<1)
#define I2C_ISR_MTX_RCVD_ACK    (1<<0)

/* stop/start/resume command, write 1 to set */
#define I2C_LCMR                (0x001c)
#define I2C_LCMR_RESUME         (1<<2)
#define I2C_LCMR_STOP           (1<<1)
#define I2C_LCMR_START          (1<<0)

/* i2c core status */
#define I2C_LSR                 (0x0020)
#define I2C_LSR_RCV_NAK         (1<<1)
#define I2C_LSR_RCV_ACK         (~(1<<1))
#define I2C_LSR_BUSY            (1<<0)

/* i2c config */
#define I2C_CONR                (0x0024)
#define I2C_CONR_NAK    	    (1<<4)
#define I2C_CONR_ACK	         (~(1<<4))
#define I2C_CONR_MTX_MODE       (1<<3)
#define I2C_CONR_MRX_MODE       (~(1<<3))
#define I2C_CONR_MPORT_ENABLE   (1<<2)
#define I2C_CONR_MPORT_DISABLE  (~(1<<2))

/* i2c core config */
#define I2C_OPR                 (0x0028)
#define I2C_OPR_RESET_STATUS    (1<<7)
#define I2C_OPR_CORE_ENABLE     (1<<6)

#define I2CCDVR_REM_BITS        (0x03)
#define I2CCDVR_REM_MAX         (1<<(I2CCDVR_REM_BITS))
#define I2CCDVR_EXP_BITS        (0x03)
#define I2CCDVR_EXP_MAX         (1<<(I2CCDVR_EXP_BITS))


#define RK29_I2C_START_TMO_COUNT        100 // msleep 1 * 100

int i2c_suspended(struct i2c_adapter *adap)
{
    return 1;
}
EXPORT_SYMBOL(i2c_suspended);

static inline void rk29_i2c_disable_ack(struct rk30_i2c *i2c)
{
    unsigned long conr = readl(i2c->regs + I2C_CONR);

	conr |= I2C_CONR_NAK;
	writel(conr,i2c->regs + I2C_CONR);
}

static inline void rk29_i2c_enable_ack(struct rk30_i2c *i2c)
{
    unsigned long conr = readl(i2c->regs + I2C_CONR);

	conr &= I2C_CONR_ACK;
	writel(conr,i2c->regs + I2C_CONR);
}
static inline void rk29_i2c_disable_mport(struct rk30_i2c *i2c)
{
    unsigned long conr = readl(i2c->regs + I2C_CONR);

	conr &= I2C_CONR_MPORT_DISABLE;
	writel(conr,i2c->regs + I2C_CONR);
}

static inline void rk29_i2c_enable_mport(struct rk30_i2c *i2c)
{
    unsigned long conr = readl(i2c->regs + I2C_CONR);

	conr |= I2C_CONR_MPORT_ENABLE;
	writel(conr,i2c->regs + I2C_CONR);
}

static inline void rk29_i2c_disable_irq(struct rk30_i2c *i2c)
{
    writel(IRQ_ALL_DISABLE, i2c->regs + I2C_IER);
}

static inline void rk29_i2c_enable_irq(struct rk30_i2c *i2c)
{
    writel(IRQ_MST_ENABLE, i2c->regs + I2C_IER);
}

/* scl = pclk/(5 *(rem+1) * 2^(exp+1)) */
static void rk29_i2c_calcdivisor(unsigned long pclk, 
								unsigned long scl_rate, 
								unsigned long *real_rate,
								unsigned int *rem, unsigned int *exp)
{
	unsigned int calc_rem = 0;
	unsigned int calc_exp = 0;

	for(calc_exp = 0; calc_exp < I2CCDVR_EXP_MAX; calc_exp++)
	{
		calc_rem = pclk / (5 * scl_rate * (1 <<(calc_exp +1)));
		if(calc_rem < I2CCDVR_REM_MAX)
			break;
	}
	if(calc_rem >= I2CCDVR_REM_MAX || calc_exp >= I2CCDVR_EXP_MAX)
	{
		calc_rem = I2CCDVR_REM_MAX - 1;
		calc_exp = I2CCDVR_EXP_MAX - 1;
	}
	*rem = calc_rem;
	*exp = calc_exp;
	*real_rate = pclk/(5 * (calc_rem + 1) * (1 <<(calc_exp +1)));
	return;
}
static void  rk29_i2c_set_clk(struct rk30_i2c *i2c, unsigned long scl_rate)
{

	unsigned int rem = 0, exp = 0;
	unsigned long real_rate = 0, tmp;

	unsigned long i2c_rate = clk_get_rate(i2c->clk);

    if((scl_rate == i2c->scl_rate) && (i2c_rate == i2c->i2c_rate))
        return;

    i2c->i2c_rate = i2c_rate;
    i2c->scl_rate = scl_rate;

	rk29_i2c_calcdivisor(i2c->i2c_rate, i2c->scl_rate, &real_rate, &rem, &exp);

	tmp = readl(i2c->regs + I2C_OPR);
	tmp &= ~0x3f;
	tmp |= exp;
	tmp |= rem<<I2CCDVR_EXP_BITS;	
	writel(tmp, i2c->regs + I2C_OPR);
	return;
}

static void rk29_i2c_init_hw(struct rk30_i2c *i2c, unsigned long scl_rate)
{
	unsigned long opr = readl(i2c->regs + I2C_OPR);
	
	opr |= I2C_OPR_RESET_STATUS;
	writel(opr, i2c->regs + I2C_OPR);

	udelay(10);
	opr = readl(i2c->regs + I2C_OPR);
	opr &= ~I2C_OPR_RESET_STATUS;
	writel(opr, i2c->regs + I2C_OPR);
	
	rk29_i2c_set_clk(i2c, scl_rate); 

	rk29_i2c_disable_irq(i2c);
	writel(0, i2c->regs + I2C_LCMR);
	writel(0, i2c->regs + I2C_LCMR);
	
	opr = readl(i2c->regs + I2C_OPR);
	opr |= I2C_OPR_CORE_ENABLE;
	writel(opr, i2c->regs + I2C_OPR);
    udelay(i2c->tx_setup);

	return;
}

static inline void rk29_i2c_master_complete(struct rk30_i2c *i2c, int ret)
{
	i2c_dbg(i2c->dev, "master_complete %d\n", ret);

	i2c->msg_ptr = 0;
	i2c->msg = NULL;
	i2c->msg_idx++;
	i2c->msg_num = 0;
	if (ret)
		i2c->msg_idx = ret;

	wake_up(&i2c->wait);
}

static void rk29_i2c_message_start(struct rk30_i2c *i2c,
				      struct i2c_msg *msg)
{
	unsigned int addr = (msg->addr & 0x7f) << 1;
	unsigned long stat, conr;

	stat = 0;

    i2c->addr = msg->addr & 0x7f;

	if (msg->flags & I2C_M_RD)
		addr |= 1;

	if (msg->flags & I2C_M_REV_DIR_ADDR)
		addr ^= 1;

	rk29_i2c_enable_ack(i2c);
	i2c_dbg(i2c->dev, "START: set addr 0x%02x to DS\n", addr);

    conr = readl(i2c->regs + I2C_CONR);
    conr |= I2C_CONR_MTX_MODE;
	writel(conr, i2c->regs + I2C_CONR);
    writel(addr, i2c->regs + I2C_MTXR);

	udelay(i2c->tx_setup);
    writel(I2C_LCMR_START|I2C_LCMR_RESUME, i2c->regs + I2C_LCMR);
    
}

static inline void rk29_i2c_stop(struct rk30_i2c *i2c, int ret)
{
	i2c_dbg(i2c->dev, "STOP\n");
    udelay(i2c->tx_setup);

    writel(I2C_LCMR_STOP|I2C_LCMR_RESUME, i2c->regs + I2C_LCMR);

	i2c->state = STATE_STOP;

	rk29_i2c_master_complete(i2c, ret);
	rk29_i2c_disable_irq(i2c);
}

/* returns TRUE if the current message is the last in the set */
static inline int is_lastmsg(struct rk30_i2c *i2c)
{
	return i2c->msg_idx >= (i2c->msg_num - 1);
}

/* returns TRUE if we this is the last byte in the current message */
static inline int is_msglast(struct rk30_i2c *i2c)
{
	return i2c->msg_ptr == i2c->msg->len-1;
}

/* returns TRUE if we reached the end of the current message */
static inline int is_msgend(struct rk30_i2c *i2c)
{
	return i2c->msg_ptr >= i2c->msg->len;
}

static int rk29_i2c_irq_nextbyte(struct rk30_i2c *i2c, unsigned long isr)
{
	unsigned long lsr, conr;
	unsigned char byte;
	int ret = 0;

	lsr = readl(i2c->regs + I2C_LSR);

	switch (i2c->state) {
	case STATE_IDLE:
		dev_err(i2c->dev, "%s: called in STATE_IDLE\n", __func__);
		goto out;

	case STATE_STOP:
		dev_err(i2c->dev, "%s: called in STATE_STOP\n", __func__);
		rk29_i2c_disable_irq(i2c);
		goto out;

	case STATE_START:
        if(!(isr & I2C_ISR_MTX_RCVD_ACK)){
            writel(isr & ~I2C_ISR_MTX_RCVD_ACK, i2c->regs + I2C_ISR);
			rk29_i2c_stop(i2c, -ENXIO);
			dev_err(i2c->dev, "START: addr[0x%02x] ack was not received(isr)\n", i2c->addr);
			goto out;
        }
		if ((lsr & I2C_LSR_RCV_NAK) &&
		    !(i2c->msg->flags & I2C_M_IGNORE_NAK)) {
            writel(isr & ~I2C_ISR_MTX_RCVD_ACK, i2c->regs + I2C_ISR);

			rk29_i2c_stop(i2c, -EAGAIN);
			dev_err(i2c->dev, "START: addr[0x%02x] ack was not received(lsr)\n", i2c->addr);
			goto out;
		}
		if (i2c->msg->flags & I2C_M_RD)
			i2c->state = STATE_READ;
		else
			i2c->state = STATE_WRITE;

		/* terminate the transfer if there is nothing to do
		 * as this is used by the i2c probe to find devices. */
		if (is_lastmsg(i2c) && i2c->msg->len == 0) {
            writel(isr & ~I2C_ISR_MTX_RCVD_ACK, i2c->regs + I2C_ISR);
			rk29_i2c_stop(i2c, 0);
			goto out;
		}

		if (i2c->state == STATE_READ){
            writel(isr & ~I2C_ISR_MTX_RCVD_ACK, i2c->regs + I2C_ISR);
			goto prepare_read;
        }

	case STATE_WRITE:
		if (!(i2c->msg->flags & I2C_M_IGNORE_NAK)) {
		    if (!(isr & I2C_ISR_MTX_RCVD_ACK)){

				rk29_i2c_stop(i2c, -ECONNREFUSED);
				dev_err(i2c->dev, "WRITE: addr[0x%02x] No Ack\n", i2c->addr);
				goto out;
			}
		}
        writel(isr & ~I2C_ISR_MTX_RCVD_ACK, i2c->regs + I2C_ISR);

retry_write:

		if (!is_msgend(i2c)) {
			byte = i2c->msg->buf[i2c->msg_ptr++];
            conr = readl(i2c->regs + I2C_CONR);
            conr |= I2C_CONR_MTX_MODE;
	        writel(conr, i2c->regs + I2C_CONR);
            writel(byte, i2c->regs + I2C_MTXR);

            writel(I2C_LCMR_RESUME, i2c->regs + I2C_LCMR);

		} else if (!is_lastmsg(i2c)) {
			/* we need to go to the next i2c message */

			i2c_dbg(i2c->dev, "WRITE: Next Message\n");

			i2c->msg_ptr = 0;
			i2c->msg_idx++;
	    	i2c->msg++;

			/* check to see if we need to do another message */
			if (i2c->msg->flags & I2C_M_NOSTART) {
				if (i2c->msg->flags & I2C_M_RD) {
					/* cannot do this, the controller
					 * forces us to send a new START
					 * when we change direction */

					rk29_i2c_stop(i2c, -EINVAL);
				}

				goto retry_write;
			} else {
				/* send the new start */
				rk29_i2c_message_start(i2c, i2c->msg);
				i2c->state = STATE_START;
			}

		} else {
			/* send stop */

			rk29_i2c_stop(i2c, 0);
		}
		break;

	case STATE_READ:
        if(!(isr & I2C_ISR_MRX_NEED_ACK)){
			rk29_i2c_stop(i2c, -ENXIO);
			dev_err(i2c->dev, "READ: addr[0x%02x] not recv need ack interrupt\n", i2c->addr);
			goto out;
        }
        writel(isr & ~I2C_ISR_MRX_NEED_ACK, i2c->regs + I2C_ISR);
        byte = readl(i2c->regs + I2C_MRXR);
		i2c_dbg(i2c->dev, "READ: byte = %d\n", byte);
		i2c->msg->buf[i2c->msg_ptr++] = byte;
 prepare_read:
		if (is_msgend(i2c)) {
			if (is_lastmsg(i2c)) {
				/* last message, send stop and complete */
                rk29_i2c_disable_ack(i2c);
				i2c_dbg(i2c->dev, "READ: Send Stop\n");

				rk29_i2c_stop(i2c, 0);
			} else {
				/* go to the next transfer */
				i2c_dbg(i2c->dev, "READ: Next Transfer\n");

				i2c->msg_ptr = 0;
				i2c->msg_idx++;
				i2c->msg++;
			    rk29_i2c_message_start(i2c, i2c->msg);
			    i2c->state = STATE_START;
			}
		}else{
            conr = readl(i2c->regs + I2C_CONR);
            conr &= I2C_CONR_MRX_MODE;
	        writel(conr, i2c->regs + I2C_CONR);

            writel(I2C_LCMR_RESUME, i2c->regs + I2C_LCMR);
        }
		break;
	}

 out:
	return ret;
}

static irqreturn_t rk29_i2c_irq(int irqno, void *dev_id)
{
	struct rk30_i2c *i2c = dev_id;
	unsigned long isr;

	spin_lock(&i2c->lock);
    udelay(i2c->tx_setup);

    isr = readl(i2c->regs + I2C_ISR);

	if (isr & I2C_ISR_ARBITR_LOSE) {
        writel(isr & ~I2C_ISR_ARBITR_LOSE, i2c->regs + I2C_ISR);
		dev_err(i2c->dev, "deal with arbitration loss\n");
	}

	if (i2c->state == STATE_IDLE) {
		i2c_dbg(i2c->dev, "IRQ: error i2c->state == IDLE\n");
		goto out;
	}

	rk29_i2c_irq_nextbyte(i2c, isr);

 out:
	spin_unlock(&i2c->lock);

	return IRQ_HANDLED;
}


/* rk29_i2c_set_master
 *
 * get the i2c bus for a master transaction
*/

static int rk29_i2c_set_master(struct rk30_i2c *i2c)
{
	int tmo =  RK29_I2C_START_TMO_COUNT;
    unsigned long lsr;

	while (tmo-- > 0) {
	    lsr = readl(i2c->regs + I2C_LSR);
		if (!(lsr & I2C_LSR_BUSY))
			return 0;
        writel(I2C_LCMR_STOP|I2C_LCMR_RESUME, i2c->regs + I2C_LCMR);
		msleep(1);
	}
	return -ETIMEDOUT;
}

/* rk29_i2c_doxfer
 *
 * this starts an i2c transfer
*/

static int rk29_i2c_doxfer(struct rk30_i2c *i2c,
			      struct i2c_msg *msgs, int num)
{
	unsigned long timeout;
	int ret;

	if (i2c->suspended)
		return -EIO;

	ret = rk29_i2c_set_master(i2c);
	if (ret != 0) {
		dev_err(i2c->dev, "addr[0x%02x] cannot get bus (error %d)\n", msgs[0].addr, ret);
		ret = -EAGAIN;
		goto out;
	}

	spin_lock_irq(&i2c->lock);

	i2c->msg     = msgs;
	i2c->msg_num = num;
	i2c->msg_ptr = 0;
	i2c->msg_idx = 0;
	i2c->state   = STATE_START;

	rk29_i2c_enable_irq(i2c);
	rk29_i2c_message_start(i2c, msgs);
	spin_unlock_irq(&i2c->lock);

	timeout = wait_event_timeout(i2c->wait, i2c->msg_num == 0, msecs_to_jiffies(I2C_WAIT_TIMEOUT));

	ret = i2c->msg_idx;

	if (timeout == 0)
	    i2c_dbg(i2c->dev, "addr[0x%02x] wait event timeout\n", msgs[0].addr);
	else if (ret != num)
		i2c_dbg(i2c->dev, "addr[0x%02x ]incomplete xfer (%d)\n", msgs[0].addr, ret);
    if((readl(i2c->regs + I2C_LSR) &  I2C_LSR_BUSY) ||
       readl(i2c->regs + I2C_LCMR) &  I2C_LCMR_STOP ){
        msleep(1);
        writel(I2C_LCMR_STOP|I2C_LCMR_RESUME, i2c->regs + I2C_LCMR);
        if((readl(i2c->regs + I2C_LSR) &  I2C_LSR_BUSY) ||
            readl(i2c->regs + I2C_LCMR) &  I2C_LCMR_STOP ){
            dev_warn(i2c->dev, "WARNING: STOP abnormal, addr[0x%02x] isr = 0x%x, lsr = 0x%x, lcmr = 0x%x\n",
                    msgs[0].addr,
                    readl(i2c->regs + I2C_ISR),
                    readl(i2c->regs + I2C_LSR),
                    readl(i2c->regs + I2C_LCMR)
                );
        }
    }
 out:
	return ret;
}

/* rk29_i2c_xfer
 *
 * first port of call from the i2c bus code when an message needs
 * transferring across the i2c bus.
*/

static int rk29_i2c_xfer(struct i2c_adapter *adap,
			struct i2c_msg *msgs, int num)
{
	struct rk30_i2c *i2c = (struct rk30_i2c *)adap->algo_data;
	int ret = 0;
    unsigned long scl_rate;

    clk_enable(i2c->clk);

    if(msgs[0].scl_rate <= 400000 && msgs[0].scl_rate >= 10000)
		scl_rate = msgs[0].scl_rate;
	else if(msgs[0].scl_rate > 400000){
		dev_warn(i2c->dev, "Warning: addr[0x%x] msg[0].scl_rate( = %dKhz) is too high!",
			msgs[0].addr, msgs[0].scl_rate/1000);
		scl_rate = 400000;	
	}
	else{
		dev_warn(i2c->dev, "Warning: addr[0x%x] msg[0].scl_rate( = %dKhz) is too low!",
			msgs[0].addr, msgs[0].scl_rate/1000);
		scl_rate = 10000;
	}
    if(i2c->is_div_from_arm[i2c->adap.nr])
		wake_lock(&i2c->idlelock[i2c->adap.nr]);

	rk29_i2c_set_clk(i2c, scl_rate);
    rk29_i2c_enable_mport(i2c);
    udelay(i2c->tx_setup);

	ret = rk29_i2c_doxfer(i2c, msgs, num);

    rk29_i2c_disable_mport(i2c);
    if(i2c->is_div_from_arm[i2c->adap.nr])
		wake_unlock(&i2c->idlelock[i2c->adap.nr]);
    clk_disable(i2c->clk);
	return ret;
}

/* declare our i2c functionality */
static u32 rk29_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_PROTOCOL_MANGLING;
}

/* i2c bus registration info */

static const struct i2c_algorithm rk29_i2c_algorithm = {
	.master_xfer		= rk29_i2c_xfer,
	.functionality		= rk29_i2c_func,
};

int i2c_add_rk29_adapter(struct i2c_adapter *adap)
{
    int ret = 0;
    struct rk30_i2c *i2c = (struct rk30_i2c *)adap->algo_data;

    adap->algo = &rk29_i2c_algorithm;

    i2c->i2c_init_hw = &rk29_i2c_init_hw;
    i2c->i2c_set_clk = &rk29_i2c_set_clk;
    i2c->i2c_irq = &rk29_i2c_irq;

    ret = i2c_add_numbered_adapter(adap);

    return ret;
}


