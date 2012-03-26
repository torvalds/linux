/* drivers/i2c/busses/i2c-rk30-adapter.c
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

/* Control register */
#define I2C_CON                 0x000
#define I2C_CON_EN              (1 << 0)
#define I2C_CON_MOD(mod)        ((mod) << 1)
#define I2C_CON_MASK            (3 << 1)
enum{
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
#define I2C_IPD_ALL_CLEAN       0x7f

/* finished count */
#define I2C_FCNT                0x020

/* I2C tx data register */
#define I2C_TXDATA_BASE         0X100

/* I2C rx data register */
#define I2C_RXDATA_BASE         0x200


static void rk30_show_regs(struct rk30_i2c *i2c)
{
        int i;
        i2c_dbg(i2c->dev, "i2c->start = %d\n", i2c->state);
        i2c_dbg(i2c->dev, "I2C_CON: 0x%08x\n", i2c_readl(i2c->regs + I2C_CON));
        i2c_dbg(i2c->dev, "I2C_CLKDIV: 0x%08x\n", i2c_readl(i2c->regs + I2C_CLKDIV));
        i2c_dbg(i2c->dev, "I2C_MRXADDR: 0x%08x\n", i2c_readl(i2c->regs + I2C_MRXADDR));
        i2c_dbg(i2c->dev, "I2C_MRXRADDR: 0x%08x\n", i2c_readl(i2c->regs + I2C_MRXRADDR));
        i2c_dbg(i2c->dev, "I2C_MTXCNT: 0x%08x\n", i2c_readl(i2c->regs + I2C_MTXCNT));
        i2c_dbg(i2c->dev, "I2C_MRXCNT: 0x%08x\n", i2c_readl(i2c->regs + I2C_MRXCNT));
        i2c_dbg(i2c->dev, "I2C_IEN: 0x%08x\n", i2c_readl(i2c->regs + I2C_IEN));
        i2c_dbg(i2c->dev, "I2C_IPD: 0x%08x\n", i2c_readl(i2c->regs + I2C_IPD));
        i2c_dbg(i2c->dev, "I2C_FCNT: 0x%08x\n", i2c_readl(i2c->regs + I2C_FCNT));
        for( i = 0; i < 8; i ++) 
                i2c_dbg(i2c->dev, "I2C_TXDATA%d: 0x%08x\n", i, i2c_readl(i2c->regs + I2C_TXDATA_BASE + i * 4));
        for( i = 0; i < 8; i ++) 
                i2c_dbg(i2c->dev, "I2C_RXDATA%d: 0x%08x\n", i, i2c_readl(i2c->regs + I2C_RXDATA_BASE + i * 4));
}
static inline void rk30_i2c_enable(struct rk30_i2c *i2c, unsigned int lastnak)
{
        unsigned int con = 0;

        con |= I2C_CON_EN;
        con |= I2C_CON_MOD(i2c->mode);
        if(lastnak)
                con |= I2C_CON_LASTACK;
        con |= I2C_CON_START;
        i2c_writel(con, i2c->regs + I2C_CON);
}
static inline void rk30_i2c_disable(struct rk30_i2c *i2c)
{
        i2c_writel( 0, i2c->regs + I2C_CON);
}

static inline void rk30_i2c_clean_start(struct rk30_i2c *i2c)
{
        unsigned int con = i2c_readl(i2c->regs + I2C_CON);

        con &= ~I2C_CON_START;
        i2c_writel(con, i2c->regs + I2C_CON);
}
static inline void rk30_i2c_send_start(struct rk30_i2c *i2c)
{
        unsigned int con = i2c_readl(i2c->regs + I2C_CON);

        con |= I2C_CON_START;
        if(con & I2C_CON_STOP)
                dev_warn(i2c->dev, "I2C_CON: stop bit is set\n");
        
        i2c_writel(con, i2c->regs + I2C_CON);
}
static inline void rk30_i2c_send_stop(struct rk30_i2c *i2c)
{
        unsigned int con = i2c_readl(i2c->regs + I2C_CON);

        con |= I2C_CON_STOP;
        if(con & I2C_CON_START)
                dev_warn(i2c->dev, "I2C_CON: start bit is set\n");
        
        i2c_writel(con, i2c->regs + I2C_CON);
}
static inline void rk30_i2c_clean_stop(struct rk30_i2c *i2c)
{
        unsigned int con = i2c_readl(i2c->regs + I2C_CON);

        con &= ~I2C_CON_STOP;
        i2c_writel(con, i2c->regs + I2C_CON);
}

static inline void rk30_i2c_disable_irq(struct rk30_i2c *i2c)
{
        i2c_writel(IRQ_ALL_DISABLE, i2c->regs + I2C_IEN);
}

static inline void rk30_i2c_enable_irq(struct rk30_i2c *i2c)
{
        i2c_writel(IRQ_MST_ENABLE, i2c->regs + I2C_IEN);
}

/* SCL Divisor = 8 * (CLKDIVL + CLKDIVH)
 * SCL = i2c_rate/ SCLK Divisor
*/
static void  rk30_i2c_set_clk(struct rk30_i2c *i2c, unsigned long scl_rate)
{
        unsigned long i2c_rate = clk_get_rate(i2c->clk);

        unsigned int div, divl, divh;

        if((scl_rate == i2c->scl_rate) && (i2c_rate == i2c->i2c_rate))
                return; 
        i2c->i2c_rate = i2c_rate;
        i2c->scl_rate = scl_rate;
        div = rk30_ceil(i2c_rate, scl_rate * 8);
        divh = divl = rk30_ceil(div, 2);
        i2c_writel(I2C_CLKDIV_VAL(divl, divh), i2c->regs + I2C_CLKDIV);
        i2c_dbg(i2c->dev, "set clk(I2C_CLKDIV: 0x%08x)\n", i2c_readl(i2c->regs + I2C_CLKDIV));
        return;
}
static void rk30_i2c_init_hw(struct rk30_i2c *i2c, unsigned long scl_rate)
{
        i2c->scl_rate = 0;
        rk30_i2c_set_clk(i2c, scl_rate);
	return;
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

static void rk30_i2c_stop(struct rk30_i2c *i2c, int ret)
{

    i2c->msg_ptr = 0;
	i2c->msg = NULL;
	i2c->msg_num = 0;
	if (ret)
		i2c->msg_idx = ret;
        i2c_writel(I2C_STOPIEN, i2c->regs + I2C_IEN);
        i2c->state = STATE_STOP;
        rk30_i2c_send_stop(i2c);
}
static inline void rk30_set_rx_mode(struct rk30_i2c *i2c, unsigned int lastnak)
{
        unsigned long con = i2c_readl(i2c->regs + I2C_CON);

        con &= (~I2C_CON_MASK);
        con |= (I2C_CON_MOD_RX << 1);
        if(lastnak)
                con |= I2C_CON_LASTACK;
        i2c_writel(con, i2c->regs + I2C_CON);
}
static void rk30_irq_read_prepare(struct rk30_i2c *i2c)
{
    unsigned int cnt, len = i2c->msg->len - i2c->msg_ptr;

    if(len <= 32 && i2c->msg_ptr != 0) 
            rk30_set_rx_mode(i2c, 1);
    else if(i2c->msg_ptr != 0)
            rk30_set_rx_mode(i2c, 0);

    if(is_msgend(i2c)) {
        rk30_i2c_stop(i2c, 0);
        return;
    }
    if(len > 32)
        cnt = 32;
    else
        cnt = len;
    i2c_writel(cnt, i2c->regs + I2C_MRXCNT);
}
static void rk30_irq_read_get_data(struct rk30_i2c *i2c)
{
     unsigned int i, len = i2c->msg->len - i2c->msg_ptr;
     unsigned int p;

     len = (len >= 32)?32:len;

     for(i = 0; i < len; i++){
         if(i%4 == 0)
             p = i2c_readl(i2c->regs + I2C_RXDATA_BASE +  (i/4) * 4);
         i2c->msg->buf[i2c->msg_ptr++] = (p >>((i%4) * 8)) & 0xff;
    }

     return;
}
static void rk30_irq_write_prepare(struct rk30_i2c *i2c)
{
    unsigned int data = 0, cnt = 0, i, j;
    unsigned char byte;

    if(is_msgend(i2c)) {
        rk30_i2c_stop(i2c, 0);
        return;
    }
    for(i = 0; i < 8; i++){
        data = 0;
        for(j = 0; j < 4; j++) {
            if(is_msgend(i2c)) 
                break;
            if(i2c->msg_ptr == 0 && cnt == 0)
                byte = (i2c->addr & 0x7f) << 1;
            else
                byte =  i2c->msg->buf[i2c->msg_ptr++];
            cnt++;
            data |= (byte << (j * 8));
        }
        i2c_writel(data, i2c->regs + I2C_TXDATA_BASE + 4 * i);
        if(is_msgend(i2c)) 
            break;
    }
    i2c_writel(cnt, i2c->regs + I2C_MTXCNT);
}
static void rk30_i2c_irq_nextblock(struct rk30_i2c *i2c, unsigned int ipd)
{
        switch (i2c->state) {
        case STATE_IDLE:
                dev_err(i2c->dev, "Addr[0x%02x] called in STATE_IDLE\n", i2c->addr);
                rk30_show_regs(i2c);
                i2c_writel(I2C_IPD_ALL_CLEAN, i2c->regs + I2C_IPD);
                goto out;
        case STATE_START:
                if(!(ipd & I2C_STARTIPD)){
                        rk30_i2c_stop(i2c, -ENXIO);
                        dev_err(i2c->dev, "Addr[0x%02x] no start irq in STATE_START\n", i2c->addr);
                        rk30_show_regs(i2c);
                        i2c_writel(I2C_IPD_ALL_CLEAN, i2c->regs + I2C_IPD);
                        goto out;
                }

                        rk30_show_regs(i2c);
                i2c_writel(I2C_STARTIPD, i2c->regs + I2C_IPD);
                rk30_i2c_clean_start(i2c);
                if(i2c->mode ==  I2C_CON_MOD_TX){
                        i2c_writel(I2C_MBTFIEN  | I2C_NAKRCVIEN, i2c->regs + I2C_IEN);
                        i2c->state = STATE_WRITE;
                        goto prepare_write;
                } else {
                        i2c_writel(I2C_MBRFIEN | I2C_NAKRCVIEN, i2c->regs + I2C_IEN);
                        i2c->state = STATE_READ;
                        goto prepare_read;
                }
        case STATE_WRITE:
                if(!(ipd & I2C_MBTFIPD)){
                        rk30_i2c_stop(i2c, -ENXIO);
                        dev_err(i2c->dev, "Addr[0x%02x] no mbtf irq in STATE_WRITE\n", i2c->addr);
                        rk30_show_regs(i2c);
                        i2c_writel(I2C_IPD_ALL_CLEAN, i2c->regs + I2C_IPD);
                        goto out;
                }
                        rk30_show_regs(i2c);
                i2c_writel(I2C_MBTFIPD, i2c->regs + I2C_IPD);
prepare_write:
                rk30_irq_write_prepare(i2c);
                break;
        case STATE_READ:
                if(!(ipd & I2C_MBRFIPD)){
                        rk30_i2c_stop(i2c, -ENXIO);
                        dev_err(i2c->dev, "Addr[0x%02x] no mbrf irq in STATE_READ\n", i2c->addr);
                        rk30_show_regs(i2c);
                        i2c_writel(I2C_IPD_ALL_CLEAN, i2c->regs + I2C_IPD);
                        goto out;
                }
                        rk30_show_regs(i2c);
                i2c_writel(I2C_MBRFIPD, i2c->regs + I2C_IPD);
                rk30_irq_read_get_data(i2c);
prepare_read:
                rk30_irq_read_prepare(i2c);
                break;
        case STATE_STOP:
                if(!(ipd & I2C_STOPIPD)){
                        rk30_i2c_stop(i2c, -ENXIO);
                        dev_err(i2c->dev, "Addr[0x%02x] no stop irq in STATE_STOP\n", i2c->addr);
                        rk30_show_regs(i2c);
                        i2c_writel(I2C_IPD_ALL_CLEAN, i2c->regs + I2C_IPD);
                        goto out;
                }
                        rk30_show_regs(i2c);
                rk30_i2c_clean_stop(i2c);
                i2c_writel(I2C_STOPIPD, i2c->regs + I2C_IPD);
                i2c->state = STATE_IDLE;
	        wake_up(&i2c->wait);
                break;
        default:
                break;
        }
out:
        return;
}
static irqreturn_t rk30_i2c_irq(int irq, void *dev_id)
{
        struct rk30_i2c *i2c = dev_id;
        unsigned int ipd;

        spin_lock(&i2c->lock);
        ipd = i2c_readl(i2c->regs + I2C_IPD);
#if 1
        if((ipd & I2C_NAKRCVIPD)  && (i2c->state != STATE_STOP)){
		dev_err(i2c->dev, "Addr[0x%02x] ack was not received\n", i2c->addr);
                rk30_show_regs(i2c);
                i2c_writel(I2C_NAKRCVIPD, i2c->regs + I2C_IPD);
                rk30_i2c_stop(i2c, -EAGAIN);
                goto out;
        }
#endif
        rk30_i2c_irq_nextblock(i2c, ipd);
out:
        spin_unlock(&i2c->lock);
        return IRQ_HANDLED;
}


static int rk30_i2c_set_master(struct rk30_i2c *i2c, struct i2c_msg *msgs, int num)
{
        unsigned int addr = (msgs[0].addr & 0x7f) << 1;
        unsigned int reg_valid_bits = 0;
        unsigned int reg_addr = 0;
    
        if(num == 1) {
                i2c->count = msgs[0].len;
                if(!(msgs[0].flags & I2C_M_RD)){
                        i2c->msg = &msgs[0];
                        i2c->mode = I2C_CON_MOD_TX;
                }
                else {
                        addr |= 1;
                        i2c->msg = &msgs[0];
                        i2c_writel(addr | I2C_MRXADDR_LOW, i2c->regs + I2C_MRXADDR);
                        i2c->mode = I2C_CON_MOD_RX;
                }
        }
        else if(num == 2) {
                i2c->count = msgs[1].len;
                switch(msgs[0].len){
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
                if((msgs[0].flags & I2C_M_RD) && (msgs[1].flags & I2C_M_RD)) {
                        addr |= 1;
                        i2c->msg = &msgs[1];
                        i2c_writel(addr | I2C_MRXADDR_LOW, i2c->regs + I2C_MRXADDR);
                        i2c_writel(reg_addr | reg_valid_bits, i2c->regs + I2C_MRXRADDR);
                        i2c->mode = I2C_CON_MOD_RRX;
                }
                else if(!(msgs[0].flags & I2C_M_RD) && (msgs[1].flags & I2C_M_RD)) {
                        i2c->msg = &msgs[1];
                        i2c_writel(addr | I2C_MRXADDR_LOW, i2c->regs + I2C_MRXADDR);
                        i2c_writel(reg_addr | reg_valid_bits, i2c->regs + I2C_MRXRADDR);
                        i2c->mode = I2C_CON_MOD_TRX;
                }
                else 
                        return -EIO;
        }
        else {
                dev_err(i2c->dev, "This case(num > 2) has not been support now\n");
                return -EIO;
        }

        return 0;
}
/* rk30_i2c_doxfer
 *
 * this starts an i2c transfer
*/
static int rk30_i2c_doxfer(struct rk30_i2c *i2c,
			      struct i2c_msg *msgs, int num)
{
	unsigned long timeout;
	int ret = 0;

	if (i2c->suspended)
		return -EIO;

	ret = rk30_i2c_set_master(i2c, msgs, num);
	if (ret != 0) {
                dev_err(i2c->dev, "addr[0x%02x] set master error\n", msgs[0].addr);  
    	        return ret;
        }
	spin_lock_irq(&i2c->lock);

        i2c->addr = msgs[0].addr;
	i2c->msg_num = num;
        i2c->msg_ptr = 0;
        i2c->msg_idx = num;
        i2c->state = STATE_START;

	spin_unlock_irq(&i2c->lock);

        i2c_writel(I2C_STARTIEN, i2c->regs + I2C_IEN);
        rk30_i2c_enable(i2c, (i2c->count > 32)?0:1); //if count > 32,  byte(32) send ack
        rk30_show_regs(i2c);
	//rk30_i2c_enable_irq(i2c);
        //rk30_i2c_send_start(i2c);

	timeout = wait_event_timeout(i2c->wait, (i2c->msg_num == 0), msecs_to_jiffies(I2C_WAIT_TIMEOUT));

	ret = i2c->msg_idx;

	if (timeout == 0){
                dev_err(i2c->dev, "addr[0x%02x] wait event timeout, state = %d\n", msgs[0].addr, i2c->state);  
                rk30_show_regs(i2c);
                if(i2c->state != STATE_STOP)
                        rk30_i2c_send_stop(i2c);
                ret = -ETIMEDOUT;
        }
        i2c_writel(I2C_IPD_ALL_CLEAN, i2c->regs + I2C_IPD);
	rk30_i2c_disable_irq(i2c);
        rk30_i2c_disable(i2c);
        i2c_dbg(i2c->dev, "i2c transfer finished ret = %d\n", ret);
        rk30_show_regs(i2c);
	return ret;
}

/* rk30_i2c_xfer
 *
 * first port of call from the i2c bus code when an message needs
 * transferring across the i2c bus.
*/

static int rk30_i2c_xfer(struct i2c_adapter *adap,
			struct i2c_msg *msgs, int num)
{
	int ret = 0;
        unsigned long scl_rate;
	struct rk30_i2c *i2c = (struct rk30_i2c *)adap->algo_data;

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

	rk30_i2c_set_clk(i2c, scl_rate);
        udelay(i2c->tx_setup);
        i2c_dbg(i2c->dev, "i2c transfer: addr[0x%x], scl_reate[%ldKhz], len = %d\n", msgs[0].addr, scl_rate/1000, i2c->count);
	ret = rk30_i2c_doxfer(i2c, msgs, num);

        i2c->state = STATE_IDLE;
        if(i2c->is_div_from_arm[i2c->adap.nr])
		wake_unlock(&i2c->idlelock[i2c->adap.nr]);
	return ret;
}

/* declare our i2c functionality */
static u32 rk30_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_PROTOCOL_MANGLING;
}

/* i2c bus registration info */

static const struct i2c_algorithm rk30_i2c_algorithm = {
	.master_xfer		= rk30_i2c_xfer,
	.functionality		= rk30_i2c_func,
};

int i2c_add_rk30_adapter(struct i2c_adapter *adap)
{
        int ret = 0;
        struct rk30_i2c *i2c = (struct rk30_i2c *)adap->algo_data;

        adap->algo = &rk30_i2c_algorithm;

        i2c->i2c_init_hw = &rk30_i2c_init_hw;
        i2c->i2c_set_clk = &rk30_i2c_set_clk;
        i2c->i2c_irq = &rk30_i2c_irq;

        ret = i2c_add_numbered_adapter(adap);

        return ret;
}

