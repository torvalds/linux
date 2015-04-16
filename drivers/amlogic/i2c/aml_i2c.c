/*
 * linux/drivers/amlogic/i2c/aml_i2c.c
 */

#include <asm/errno.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <plat/io.h>
#ifndef CONFIG_I2C
#error kkk
#endif
#include <linux/i2c-aml.h>
#include <linux/i2c-algo-bit.h>
#include <linux/hardirq.h>
#include <mach/am_regs.h>
#include <linux/export.h>
#include "aml_i2c.h"
#include <linux/module.h>
static int i2c_silence = 0;

#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON3
static struct mutex *ab_share_lock = 0;
#endif

#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>
#include <mach/pinmux.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of_i2c.h>
#include <linux/of_address.h>

#ifdef CONFIG_OF
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/of.h>
#else
#include <mach/gpio.h>
#include <mach/gpio_data.h>
#endif
int aml_i2c_device_num = 0;
static struct aml_i2c_platform *aml_i2c_properties_list;
static int i2c_speed[] = {AML_I2C_SPPED_50K, AML_I2C_SPPED_100K, AML_I2C_SPPED_200K, AML_I2C_SPPED_300K, AML_I2C_SPPED_400K};


static void aml_i2c_set_clk(struct aml_i2c *i2c, unsigned int speed)
{
	unsigned int i2c_clock_set;
	unsigned int sys_clk_rate;
	struct clk *sys_clk;
	struct aml_i2c_reg_ctrl* ctrl;

	sys_clk = clk_get_sys("clk81", NULL);
	sys_clk_rate = clk_get_rate(sys_clk);
	//sys_clk_rate = get_mpeg_clk();

	i2c_clock_set = sys_clk_rate / speed;
#if MESON_CPU_TYPE > MESON_CPU_TYPE_MESON8
	i2c_clock_set >>= 1;
	ctrl = (struct aml_i2c_reg_ctrl*)&(i2c->master_regs->i2c_ctrl);
	if (i2c_clock_set > 0xfff) i2c_clock_set = 0xfff;
	ctrl->clk_delay = i2c_clock_set & 0x3ff;
	ctrl->clk_delay_ext = i2c_clock_set >> 10;
	i2c->master_regs->i2c_slave_addr &= ~(0xfff<<16);
	i2c->master_regs->i2c_slave_addr |= (i2c_clock_set>>1)<<16;
	i2c->master_regs->i2c_slave_addr |= 1<<28;
	i2c->master_regs->i2c_slave_addr &= ~(0x3f<<8); //no filter on scl&sda
#else
	i2c_clock_set >>= 2;

	ctrl = (struct aml_i2c_reg_ctrl*)&(i2c->master_regs->i2c_ctrl);
	ctrl->clk_delay = i2c_clock_set & AML_I2C_CTRL_CLK_DELAY_MASK;
#endif
}

static void aml_i2c_set_platform_data(struct aml_i2c *i2c,
										struct aml_i2c_platform *plat)
{
	i2c->master_i2c_speed = plat->master_i2c_speed;
	i2c->wait_count = plat->wait_count;
	i2c->wait_ack_interval = plat->wait_ack_interval;
	i2c->wait_read_interval = plat->wait_read_interval;
	i2c->wait_xfer_interval = plat->wait_xfer_interval;
	i2c->mode = plat->use_pio & 3;
	i2c->irq = plat->use_pio >> 2;
	i2c->master_no = plat->master_no;
#ifdef ENABLE_GPIO_TRIGGER
	i2c->trig_gpio = -1;
#endif

	if(IS_ERR(plat->master_state_name)){
		printk("error: no master_state_name");
    }
    else{
		i2c->master_state_name=plat->master_state_name;
	}
}

static void aml_i2c_pinmux_master(struct aml_i2c *i2c)
{
#ifdef CONFIG_OF
#if 0
	i2c->p=devm_pinctrl_get_select(i2c->dev,i2c->master_state_name);
	if(IS_ERR(i2c->p)){
		printk("set i2c pinmux error\n");
		i2c->p=NULL;
	}
#endif
#else
	pinmux_set(&i2c->master_pinmux);
#endif
}

/*set to gpio for -EIO & -ETIMEOUT?*/
static void aml_i2c_clr_pinmux(struct aml_i2c *i2c)
{
#ifdef CONFIG_OF
#if 0
	if(i2c->p)
		devm_pinctrl_put(i2c->p);
#endif
#else
    pinmux_clr(&i2c->master_pinmux);
#endif
}


static void aml_i2c_dbg(struct aml_i2c *i2c)
{
	int i;
	struct aml_i2c_reg_ctrl* ctrl;
	unsigned int sys_clk_rate;
	struct clk *sys_clk;

	if(i2c->i2c_debug == 0)
		return ;

      printk("addr [%x]  \t token tag : ",
                i2c->master_regs->i2c_slave_addr>>1);
      	for(i=0; i<AML_I2C_MAX_TOKENS; i++)
		printk("%d,", i2c->token_tag[i]);

	sys_clk = clk_get_sys("clk81", NULL);
	sys_clk_rate = clk_get_rate(sys_clk);
	ctrl = ((struct aml_i2c_reg_ctrl*)&(i2c->master_regs->i2c_ctrl));
      printk("clk_delay %x,  clk is %dK \n", ctrl->clk_delay,
            sys_clk_rate/4/ctrl->clk_delay/1000);
      printk("w0 %x, w1 %x, r0 %x, r1 %x, cur_token %d, rd cnt %d, status %d,"
            "error %d, ack_ignore %d,start %d\n",
            i2c->master_regs->i2c_token_wdata_0,
            i2c->master_regs->i2c_token_wdata_1,
            i2c->master_regs->i2c_token_rdata_0,
            i2c->master_regs->i2c_token_rdata_1,
            ctrl->cur_token, ctrl->rd_data_cnt, ctrl->status, ctrl->error,
            ctrl->ack_ignore, ctrl->start);

      if(ctrl->manual_en)
            printk("[aml_i2c_dbg] manual_en, why?\n");
}

static void aml_i2c_clear_token_list(struct aml_i2c *i2c)
{
	i2c->master_regs->i2c_token_list_0 = 0;
	i2c->master_regs->i2c_token_list_1 = 0;
	memset(i2c->token_tag, TOKEN_END, AML_I2C_MAX_TOKENS);
}

static void aml_i2c_set_token_list(struct aml_i2c *i2c)
{
	int i;
	unsigned int token_reg=0;

	for(i=0; i<AML_I2C_MAX_TOKENS; i++)
		token_reg |= i2c->token_tag[i]<<(i*4);

	i2c->master_regs->i2c_token_list_0=token_reg;
}

/*poll status*/
static int aml_i2c_wait_ack(struct aml_i2c *i2c)
{
	int i;
	struct aml_i2c_reg_ctrl* ctrl;

	for(i=0; i<i2c->wait_count; i++) {
		ctrl = (struct aml_i2c_reg_ctrl*)&(i2c->master_regs->i2c_ctrl);
		if(ctrl->status == IDLE){
      i2c->cur_token = ctrl->cur_token;
      return (ctrl->error ? (-EIO) : 0);		  
		}
    if (!in_atomic() && (i2c->mode == I2C_DELAY_MODE))
			cond_resched();
    udelay(i2c->wait_ack_interval);
	}

	return -ETIMEDOUT;
}

static void aml_i2c_get_read_data(struct aml_i2c *i2c, unsigned char *buf,
														size_t len)
{
	int i;
	unsigned long rdata0 = i2c->master_regs->i2c_token_rdata_0;
	unsigned long rdata1 = i2c->master_regs->i2c_token_rdata_1;

	for(i=0; i< min_t(size_t, len, AML_I2C_MAX_TOKENS>>1); i++)
		*buf++ = (rdata0 >> (i*8)) & 0xff;

	for(; i< min_t(size_t, len, AML_I2C_MAX_TOKENS); i++)
		*buf++ = (rdata1 >> ((i - (AML_I2C_MAX_TOKENS>>1))*8)) & 0xff;
}

static void aml_i2c_fill_data(struct aml_i2c *i2c, unsigned char *buf,
							size_t len)
{
	int i;
	unsigned int wdata0 = 0;
	unsigned int wdata1 = 0;

	for(i=0; i< min_t(size_t, len, AML_I2C_MAX_TOKENS>>1); i++)
		wdata0 |= (*buf++) << (i*8);

	for(; i< min_t(size_t, len, AML_I2C_MAX_TOKENS); i++)
		wdata1 |= (*buf++) << ((i - (AML_I2C_MAX_TOKENS>>1))*8);

	i2c->master_regs->i2c_token_wdata_0 = wdata0;
	i2c->master_regs->i2c_token_wdata_1 = wdata1;
}

static void aml_i2c_xfer_prepare(struct aml_i2c *i2c, unsigned int speed)
{
	aml_i2c_pinmux_master(i2c);
	aml_i2c_set_clk(i2c, speed);
}

static void aml_i2c_start_token_xfer(struct aml_i2c *i2c)
{
	//((struct aml_i2c_reg_ctrl*)&(i2c->master_regs->i2c_ctrl))->start = 0;	/*clear*/
	//((struct aml_i2c_reg_ctrl*)&(i2c->master_regs->i2c_ctrl))->start = 1;	/*set*/
	i2c->master_regs->i2c_ctrl &= ~1;	/*clear*/
	i2c->master_regs->i2c_ctrl |= 1;	/*set*/
}

/*our controller should send write data with slave addr in a token list,
	so we can't do normal address, just set addr into addr reg*/
static int aml_i2c_do_address(struct aml_i2c *i2c, unsigned int addr)
{
	i2c->cur_slave_addr = addr&0x7f;
#if MESON_CPU_TYPE > MESON_CPU_TYPE_MESON8
	i2c->master_regs->i2c_slave_addr &= ~0xff;
	i2c->master_regs->i2c_slave_addr |= i2c->cur_slave_addr<<1;
#else
	i2c->master_regs->i2c_slave_addr = i2c->cur_slave_addr<<1;
#endif
	return 0;
}

static void aml_i2c_stop(struct aml_i2c *i2c)
{
	struct aml_i2c_reg_ctrl* ctrl;
	ctrl = (struct aml_i2c_reg_ctrl*)&(i2c->master_regs->i2c_ctrl);

  /* Controller has send the stop condition automatically when NACK error.
   * We must not send again at here, otherwize, the CLK line will be pulled down.
   */
  if (!ctrl->error) {
	aml_i2c_clear_token_list(i2c);
	i2c->token_tag[0]=TOKEN_STOP;
	aml_i2c_set_token_list(i2c);
	aml_i2c_start_token_xfer(i2c);
#if MESON_CPU_TYPE > MESON_CPU_TYPE_MESON8
	aml_i2c_wait_ack(i2c);
#else
  	udelay(i2c->wait_xfer_interval);
#endif
  }
	aml_i2c_clear_token_list(i2c);	
}

static int aml_i2c_read(struct aml_i2c *i2c, unsigned char *buf,
							size_t len)
{
	int i;
	int ret;
	size_t rd_len;
	int tagnum=0;

	if(!buf || !len) return -EINVAL; 
	aml_i2c_clear_token_list(i2c);

	if(! (i2c->msg_flags & I2C_M_NOSTART)){
		i2c->token_tag[tagnum++]=TOKEN_START;
		i2c->token_tag[tagnum++]=TOKEN_SLAVE_ADDR_READ;

		aml_i2c_set_token_list(i2c);
		aml_i2c_dbg(i2c);
		aml_i2c_start_token_xfer(i2c);

		ret = aml_i2c_wait_ack(i2c);
		if(ret<0)
			return ret;
		aml_i2c_clear_token_list(i2c);
	}

	while(len){
		tagnum = 0;
		rd_len = min_t(size_t, len, AML_I2C_MAX_TOKENS);
		if(rd_len == 1)
			i2c->token_tag[tagnum++]=TOKEN_DATA_LAST;
		else{
			for(i=0; i<rd_len-1; i++)
				i2c->token_tag[tagnum++]=TOKEN_DATA;
			if(len > rd_len)
				i2c->token_tag[tagnum++]=TOKEN_DATA;
			else
				i2c->token_tag[tagnum++]=TOKEN_DATA_LAST;
		}
		aml_i2c_set_token_list(i2c);
		aml_i2c_dbg(i2c);
		aml_i2c_start_token_xfer(i2c);

		ret = aml_i2c_wait_ack(i2c);
		if(ret<0)
			return ret;

		aml_i2c_get_read_data(i2c, buf, rd_len);
		len -= rd_len;
		buf += rd_len;

		aml_i2c_clear_token_list(i2c);
	}
	return 0;
}

static int aml_i2c_write(struct aml_i2c *i2c, unsigned char *buf,
							size_t len)
{
        int i;
        int ret;
        size_t wr_len;
	int tagnum=0;
	if(!buf || !len) return -EINVAL; 
	aml_i2c_clear_token_list(i2c);
	if(! (i2c->msg_flags & I2C_M_NOSTART)){
		i2c->token_tag[tagnum++]=TOKEN_START;
		i2c->token_tag[tagnum++]=TOKEN_SLAVE_ADDR_WRITE;
	}
	while(len){
		wr_len = min_t(size_t, len, AML_I2C_MAX_TOKENS-tagnum);
		for(i=0; i<wr_len; i++)
			i2c->token_tag[tagnum++]=TOKEN_DATA;

		aml_i2c_set_token_list(i2c);

		aml_i2c_fill_data(i2c, buf, wr_len);

		aml_i2c_dbg(i2c);
		aml_i2c_start_token_xfer(i2c);

		len -= wr_len;
		buf += wr_len;
		tagnum = 0;

		ret = aml_i2c_wait_ack(i2c);
		if(ret<0)
			return ret;

		aml_i2c_clear_token_list(i2c);
    	}
	return 0;
}

static struct aml_i2c_ops aml_i2c_m1_ops = {
	.xfer_prepare 	= aml_i2c_xfer_prepare,
	.read 		= aml_i2c_read,
	.write 		= aml_i2c_write,
	.do_address	= aml_i2c_do_address,
	.stop		= aml_i2c_stop,
};

static ktime_t get_xfer_time(struct aml_i2c *i2c)
{
  int delay_us;
	ktime_t kt;
  delay_us = (1000000 * 9 * i2c->xfer_num) / (i2c->master_i2c_speed) + 10;
  kt = ktime_set(0, delay_us * 1000);
  return kt;
}

static int aml_i2c_handle_data(struct aml_i2c *i2c, int tag_num)
{
  if (!tag_num) {
    aml_i2c_clear_token_list(i2c);
  }
  i2c->xfer_num = 0;
  while ((i2c->remain_len > 0) && (tag_num < AML_I2C_MAX_TOKENS)) {
		i2c->token_tag[tag_num] = TOKEN_DATA;
		if ((i2c->msg_flags & I2C_M_RD) && (i2c->remain_len == 1)) {
		  i2c->token_tag[tag_num] = TOKEN_DATA_LAST;
		}
		tag_num++;
		i2c->xfer_num++;
		i2c->remain_len--;
	}
	if ((!i2c->msgs_num) && (!i2c->remain_len)) {
	  /* the last msg and the last data */
	  if (tag_num < AML_I2C_MAX_TOKENS) {
	    /* the token list is not full, we can send the STOP together */
	    i2c->token_tag[tag_num] = TOKEN_STOP;
	  }
	  else {
	    /* the token list is full, we have to sign a stop flag and wait next trasfer*/
	    i2c->remain_len = -1;
	  }
	}
	else if (i2c->remain_len < 0) {
	  i2c->remain_len = 0;
	  i2c->token_tag[tag_num] = TOKEN_STOP;
  }

  aml_i2c_set_token_list(i2c);
	if (!(i2c->msg_flags & I2C_M_RD) && i2c->xfer_num) {
    aml_i2c_fill_data(i2c, i2c->xfer_buf, i2c->xfer_num);
    i2c->xfer_buf += i2c->xfer_num;
  }
  aml_i2c_start_token_xfer(i2c);
  if (i2c->mode == I2C_TIMER_POLLING_MODE) {
    hrtimer_set_expires(&i2c->aml_i2c_hrtimer, get_xfer_time(i2c));
  }
	return i2c->xfer_num;
}

static int aml_i2c_handle_msg(struct aml_i2c *i2c, struct i2c_msg *msg)
{
  int tag_num = 0;
  
	i2c->msg_flags = msg->flags;
	i2c->remain_len = msg->len;
	i2c->xfer_buf = msg->buf;
	i2c->msgs++;
	i2c->msgs_num--;
	i2c->ops->do_address(i2c, msg->addr);
	aml_i2c_clear_token_list(i2c);
	if (!(i2c->msg_flags & I2C_M_NOSTART)) {
		i2c->token_tag[tag_num++] = TOKEN_START;
	}
	i2c->token_tag[tag_num++] =
	(i2c->msg_flags & I2C_M_RD) ? TOKEN_SLAVE_ADDR_READ : TOKEN_SLAVE_ADDR_WRITE;

	aml_i2c_handle_data(i2c, tag_num);
	//printk("i2c msg: flag=%x, len=%d, addr=%x\n", p->flags, p->len, p->addr);
	return 0;
}

static int aml_i2c_int_xfer(struct aml_i2c *i2c, struct i2c_msg *msgs, int num)
{
  int ret;
    
  i2c->state = I2C_STATE_WORKING;
  i2c->msgs = msgs;
  i2c->msgs_num = num;
  aml_i2c_handle_msg(i2c, i2c->msgs);

  if (i2c->mode == I2C_TIMER_POLLING_MODE) {
    hrtimer_start(&i2c->aml_i2c_hrtimer, get_xfer_time(i2c), HRTIMER_MODE_REL);
  }
  wait_for_completion_interruptible_timeout(&i2c->aml_i2c_completion, msecs_to_jiffies(50));
  if (i2c->state == I2C_STATE_IDLE) {
    ret = 0;
  }
  else if (i2c->state == I2C_STATE_WORKING) {
    ret = -ETIME;
  }
  else {
    ret = i2c->state;
  }
  i2c->state = I2C_STATE_IDLE;
  return ret;
}

static irqreturn_t aml_i2c_complete_isr(int irq, void *dev_id)
{
  int ret;
  struct aml_i2c *i2c = (struct aml_i2c *)dev_id;
  
	if (i2c->state != I2C_STATE_WORKING) {
    i2c->state = I2C_STATE_IDLE;
    return IRQ_HANDLED;
  }

  ret = aml_i2c_wait_ack(i2c);
  if (ret < 0) {
    i2c->state = ret;
    complete(&i2c->aml_i2c_completion);
    return IRQ_HANDLED;
  }

  if ((i2c->msg_flags & I2C_M_RD) && (i2c->xfer_num)) {
    aml_i2c_get_read_data(i2c, i2c->xfer_buf, i2c->xfer_num);
    i2c->xfer_buf += i2c->xfer_num;
  }

  if (i2c->remain_len) {
    aml_i2c_handle_data(i2c, 0);
  }
  else if (i2c->msgs_num > 0) {
    aml_i2c_handle_msg(i2c, i2c->msgs);
  }
  else {
    i2c->state = I2C_STATE_IDLE;
    complete(&i2c->aml_i2c_completion);
  }
  
	return IRQ_HANDLED;
}

static enum hrtimer_restart aml_i2c_hrtimer_notify(struct hrtimer *hrtimer)
{
  int ret;
  struct aml_i2c *i2c = container_of(hrtimer, struct aml_i2c, aml_i2c_hrtimer);

	if (i2c->state != I2C_STATE_WORKING) {
    i2c->state = I2C_STATE_IDLE;
    return HRTIMER_NORESTART;
  }

  ret = aml_i2c_wait_ack(i2c);
  if (ret < 0) {
    i2c->state = ret;
    complete(&i2c->aml_i2c_completion);
    return HRTIMER_NORESTART;
  }

  if ((i2c->msg_flags & I2C_M_RD) && (i2c->xfer_num)) {
    aml_i2c_get_read_data(i2c, i2c->xfer_buf, i2c->xfer_num);
    i2c->xfer_buf += i2c->xfer_num;
  }

  if (i2c->remain_len) {
    aml_i2c_handle_data(i2c, 0);
    return HRTIMER_RESTART;
  }
  else if (i2c->msgs_num > 0) {
    aml_i2c_handle_msg(i2c, i2c->msgs);
    return HRTIMER_RESTART;
  }
  else {
    i2c->state = I2C_STATE_IDLE;
    complete(&i2c->aml_i2c_completion);
  }
  return HRTIMER_NORESTART;
}

/*General i2c master transfer*/
static int aml_i2c_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg *msgs,
							int num)
{
	struct aml_i2c *i2c = i2c_get_adapdata(i2c_adap);
	struct i2c_msg * p=NULL;
	unsigned int i;
	unsigned int ret=0, speed=0;

	if(i2c_silence || !msgs || !num) return -1;
	BUG_ON(!i2c);
	/*should not use spin_lock, cond_resched in wait ack*/
	mutex_lock(i2c->lock);

	if(i2c->state != I2C_STATE_IDLE) {
	  mutex_unlock(i2c->lock);
    return -EPERM;
	}

	speed = (msgs->flags>>1) & 0x7;
	if (speed > 0 && speed < 6) {
		ret = i2c->master_i2c_speed;
		i2c->master_i2c_speed = i2c_speed[speed-1];
		speed = ret;
	}
	else {
		speed = 0;
	}
	
	i2c->ops->xfer_prepare(i2c, i2c->master_i2c_speed);
	/*make sure change speed before start*/
    mb();
  if (i2c->mode != I2C_DELAY_MODE) {
    ret = aml_i2c_int_xfer(i2c, &msgs[0], num);
  }
  else {
	for (i = 0; !ret && i < num; i++) {
		p = &msgs[i];
		i2c->msg_flags = p->flags;
		ret = i2c->ops->do_address(i2c, p->addr);
		if (ret || !p->len)
			continue;
		if (p->flags & I2C_M_RD)
			ret = i2c->ops->read(i2c, p->buf, p->len);
		else
			ret = i2c->ops->write(i2c, p->buf, p->len);
	}

	i2c->ops->stop(i2c);
  }

	if (speed) {
		i2c->master_i2c_speed = speed;
	}
	aml_i2c_clr_pinmux(i2c);
	AML_I2C_PRINT_DATA(i2c->adap.name);

	if (ret) {
#ifdef ENABLE_GPIO_TRIGGER
    if (i2c->trig_gpio >= 0) {
      i = amlogic_get_value(i2c->trig_gpio, "aml_i2c_trig");
      amlogic_gpio_direction_output(i2c->trig_gpio, !i, "aml_i2c_trig");
    }
#endif
		dev_err(&i2c_adap->dev, "[aml_i2c_xfer] error ret = %d (%s) token %d, "
                   "master_no(%d) %dK addr 0x%x\n",
                   ret, ret == -EIO ? "-EIO" : "-ETIMEOUT", i2c->cur_token,
			i2c->master_no, i2c->master_i2c_speed/1000,
			i2c->cur_slave_addr);
	}

	mutex_unlock(i2c->lock);
	/* Return the number of messages processed, or the error code*/
	return ( ret ? (-EAGAIN) : num);	
	}

static u32 aml_i2c_func(struct i2c_adapter *i2c_adap)
{
	return I2C_FUNC_I2C |I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm aml_i2c_algorithm = {
    .master_xfer = aml_i2c_xfer,
    .functionality = aml_i2c_func,
};

/***************i2c class****************/

static ssize_t show_i2c_debug(struct class *class,
                    struct class_attribute *attr,	char *buf)
{
    struct aml_i2c *i2c = container_of(class, struct aml_i2c, cls);
    return sprintf(buf, "i2c debug is 0x%x\n", i2c->i2c_debug);
}

static ssize_t store_i2c_debug(struct class *class,
                    struct class_attribute *attr,	const char *buf, size_t count)
{
    unsigned int dbg;
    ssize_t r;
    struct aml_i2c *i2c = container_of(class, struct aml_i2c, cls);

    r = sscanf(buf, "%d", &dbg);
    if (r != 1)
        return -EINVAL;

    i2c->i2c_debug = dbg;
    return count;
}

static ssize_t show_i2c_info(struct class *class,
                    struct class_attribute *attr,	char *buf)
{
    struct aml_i2c *i2c = container_of(class, struct aml_i2c, cls);
    struct aml_i2c_reg_ctrl* ctrl;
    struct aml_i2c_reg_master __iomem* regs = i2c->master_regs;

    printk( "i2c master_no(%d) current slave addr is 0x%x\n",
        i2c->master_no, i2c->cur_slave_addr);
    printk( "wait ack timeout is 0x%x\n",
        i2c->wait_count * i2c->wait_ack_interval);
    printk( "master regs base is 0x%x \n", (unsigned int)regs);

    ctrl = ((struct aml_i2c_reg_ctrl*)&(i2c->master_regs->i2c_ctrl));
    printk( "i2c_ctrl:  0x%x\n", i2c->master_regs->i2c_ctrl);
    printk( "ctrl.rdsda  0x%x\n", ctrl->rdsda);
    printk( "ctrl.rdscl  0x%x\n", ctrl->rdscl);
    printk( "ctrl.wrsda  0x%x\n", ctrl->wrsda);
    printk( "ctrl.wrscl  0x%x\n", ctrl->wrscl);
    printk( "ctrl.manual_en  0x%x\n", ctrl->manual_en);
    printk( "ctrl.clk_delay  0x%x\n", ctrl->clk_delay);
    printk( "ctrl.rd_data_cnt  0x%x\n", ctrl->rd_data_cnt);
    printk( "ctrl.cur_token  0x%x\n", ctrl->cur_token);
    printk( "ctrl.error  0x%x\n", ctrl->error);
    printk( "ctrl.status  0x%x\n", ctrl->status);
    printk( "ctrl.ack_ignore  0x%x\n", ctrl->ack_ignore);
    printk( "ctrl.start  0x%x\n", ctrl->start);

    printk( "i2c_slave_addr:  0x%x\n", regs->i2c_slave_addr);
    printk( "i2c_token_list_0:  0x%x\n", regs->i2c_token_list_0);
    printk( "i2c_token_list_1:  0x%x\n", regs->i2c_token_list_1);
    printk( "i2c_token_wdata_0:  0x%x\n", regs->i2c_token_wdata_0);
    printk( "i2c_token_wdata_1:  0x%x\n", regs->i2c_token_wdata_1);
    printk( "i2c_token_rdata_0:  0x%x\n", regs->i2c_token_rdata_0);
    printk( "i2c_token_rdata_1:  0x%x\n", regs->i2c_token_rdata_1);

    if (i2c->master_pinmux.pinmux) {
        printk( "master pinmux\n");
        printk( "pinmux_reg:  0x%02x\n", i2c->master_pinmux.pinmux->reg);
        printk( "clrmask:  0x%08x\n", i2c->master_pinmux.pinmux->clrmask);
        printk( "setmask:  0x%08x\n", i2c->master_pinmux.pinmux->setmask);
    }
    return 0;
}

static ssize_t store_register(struct class *class,
                    struct class_attribute *attr,	const char *buf, size_t count)
{
    unsigned int reg, val, ret;
    int n=1,i;
    if(buf[0] == 'w'){
        ret = sscanf(buf, "w %x %x", &reg, &val);
        //printk("sscanf w reg = %x, val = %x\n",reg, val);
        printk("write cbus reg 0x%x value %x\n", reg, val);
        aml_write_reg32(CBUS_REG_ADDR(reg), val);
    }else{
        ret =  sscanf(buf, "%x %d", &reg,&n);
        printk("read %d cbus register from reg: %x \n",n,reg);
        for(i=0;i<n;i++)
        {
            val = aml_read_reg32(CBUS_REG_ADDR(reg+i));
            printk("reg 0x%x : 0x%x\n", reg+i, val);
        }
    }

    if (ret != 1 || ret !=2)
        return -EINVAL;

    return 0;
}

static unsigned int clock81_reading(void)
{
	int val;

	val = aml_read_reg32(P_HHI_OTHER_PLL_CNTL);
	printk( "1070=%x\n", val);
	val = aml_read_reg32(P_HHI_MPEG_CLK_CNTL);
	printk( "105d=%x\n", val);
	return 148;
}

static ssize_t rw_special_reg(struct class *class,
                    struct class_attribute *attr,	const char *buf, size_t count)
{
    unsigned int id, val, ret;

    if(buf[0] == 'w'){
    ret = sscanf(buf, "w %x", &id);
    switch(id)
    {
        case 0:
            break;
        default:
            printk( "'echo h > customize' for help\n");
            break;
    }
    //printk("sscanf w reg = %x, val = %x\n",reg, val);
    //printk("write cbus reg 0x%x value %x\n", reg, val);
    //WRITE_CBUS_REG(reg, val);
    }
    else if(buf[0] == 'r'){
        ret =  sscanf(buf, "r %x", &id);
        switch(id)
        {
            case 0:
                val = clock81_reading();
                printk("Reading Value=%04d Mhz\n", val);
                break;
            default:
                printk( "'echo h > customize' for help\n");
                break;
        }
        //printk("sscanf r reg = %x\n", reg);
        //val = READ_CBUS_REG(reg);
        //printk("read cbus reg 0x%x value %x\n", reg, val);
    }else if(buf[0] == 'h'){
        printk( "Customize sys fs help\n");
        printk( "**********************************************************\n");
        printk( "This interface for customer special register value getting\n");
        printk( "echo w id > customize: for write the value to customer specified register\n");
        printk( "echo r id > customize: for read the value from customer specified register\n");
        printk( "reading ID: 0--for clock81 reading\n");
        printk( "writting ID: reserved currently \n");
        printk( "**********************************************************\n");
    }
    else
        printk( "'echo h > customize' for help\n");

    if (ret != 1 || ret !=2)
        return -EINVAL;

    return 0;
}

static ssize_t show_i2c_silence(struct class *class,
        struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", i2c_silence);
}

static ssize_t store_i2c_silence(struct class *class,
        struct class_attribute *attr, const char *buf, size_t count)
{
    unsigned int dbg;
    ssize_t r;

    r = sscanf(buf, "%d", &dbg);
    if (r != 1)
    return -EINVAL;

    i2c_silence = dbg;
    return count;
}

static ssize_t test_slave_device(struct class *class,
                    struct class_attribute *attr,	const char *buf, size_t count)
{
 
    struct i2c_adapter *i2c_adap;
    struct aml_i2c *i2c;
    struct i2c_msg msgs[2];
    unsigned int bus_num=0, slave_addr=0, speed=0, wnum=0, rnum=0;
    u8 wbuf[4]={0}, rbuf[128]={0};
    int ret, i;
    bool nostart = 0;
    
    if (buf[0] == 'h') {
      printk("i2c slave test help\n");
      printk("You can test the i2c slave device even without its driver through this sysfs node\n");
      printk("echo bus_num slave_addr speed write_num read_num [wdata1 wdata2 wdata3 wdata4] >test_slave\n");
      printk("write (0x12 0x34) 2 bytes to the slave(addr=0x50) on i2c_bus 0, speed=50K\n");
      printk("  echo 0 0x50 50000 2 0 0x12 0x34 >test_slave\n");
      printk("read 2 bytes from the slave(addr=0x67) on i2c_bus 1, speed=300K\n");
      printk("  echo 1 0x67 300000 0 2 >test_slave\n");
      printk("write (0x12 0x34) 2 bytes to the slave(addr=0x50) on i2c_bus 0, then read 2 bytes, speed=50K\n");
      printk("  echo 0 0x50 50000 3 2 0x12 0x34 >test_slave \n");
      return count;
    }
    
    i = sscanf(buf, "%d%x%d%d%d%x%x%x%x", &bus_num, &slave_addr, &speed, &wnum, &rnum, 
     (unsigned int *)&wbuf[0], (unsigned int *)&wbuf[1], (unsigned int *)&wbuf[2], (unsigned int *)&wbuf[3]);
    nostart = (rnum & 0x80) ? I2C_M_NOSTART : 0;
    rnum &= 0x7f;
    printk("bus_num=%d, slave_addr=%x, speed=%d, wnum=%d, rnum=%d\n",
      bus_num, slave_addr, speed, wnum, rnum);
    if ((i<(wnum+5)) || (!slave_addr) || (!speed) ||(wnum>4) || (!(wnum | rnum))) {
      printk("invalid data\n");
      return -EINVAL;
    }

    i2c_adap = i2c_get_adapter(bus_num);
    if (!i2c_adap) {
      printk("invalid i2c adapter\n");      
      return -EINVAL;
    }
	  i2c= i2c_get_adapdata(i2c_adap);
    if (!i2c) {
      printk("invalid i2c master\n");      
      return -EINVAL;
    }
      
    msgs[0].flags = !I2C_M_RD;
    msgs[0].addr  = slave_addr;
    msgs[0].len   = wnum;
    msgs[0].buf   = &wbuf[0];

    msgs[1].flags = I2C_M_RD | nostart;
    msgs[1].addr  = slave_addr;
    msgs[1].len   = rnum;
    msgs[1].buf   = &rbuf[0];
    
  	mutex_lock(i2c->lock);
    i2c->master_i2c_speed = speed;
  	mutex_unlock(i2c->lock);
    ret = i2c_transfer(i2c_adap, msgs, 2);
    if (wnum) {
      printk("write %d data to slave (", wnum);
      for (i=0; i<wnum; i++)
        printk("0x%x, ", wbuf[i]);
      printk(") %s!\n", (ret==2) ? "success" : "failed");
    }
    if (rnum) {
      printk("read %d data from slave (", rnum);
      for (i=0; i<rnum; i++)
        printk("0x%x, ", rbuf[i]);
      printk(") %s!\n", (ret==2) ? "success" : "failed");
    }
     
    return count;
}

static ssize_t show_i2c_mode(struct class *class, struct class_attribute *attr, char *buf)
{
    struct aml_i2c *i2c = container_of(class, struct aml_i2c, cls);
    printk("i2c master(%d) work in mode %d\n", i2c->master_no, i2c->mode);
    return 1;
}

static ssize_t store_i2c_mode(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
    struct aml_i2c *i2c = container_of(class, struct aml_i2c, cls);
    unsigned int mode;
    int ret;

    ret = sscanf(buf, "%d", &mode);
    if ((ret != 1) || (mode > 2)) {
      printk("Invalid data\n");
      return -EINVAL;
    }

	  mutex_lock(i2c->lock);
    if (mode == i2c->mode) {
      printk("i2c master(%d) worked in mode %d already!\n", i2c->master_no, mode);
      ret = -EINVAL;
      goto exit_store_i2c_mode;
    }
    else if (mode == I2C_INTERRUPT_MODE) {
      if (!i2c->irq) {
        printk("i2c master(%d) cannot work in interrupt mode!\n", i2c->master_no);
        ret = -EINVAL;
        goto exit_store_i2c_mode;
      }
      ret = request_irq(i2c->irq, aml_i2c_complete_isr, IRQF_DISABLED, "aml_i2c", i2c);
      if (ret < 0) {
        printk("i2c master(%d) request irq %d failed\n", i2c->master_no, i2c->irq);
        goto exit_store_i2c_mode;
      }
    }
    else if (mode == I2C_TIMER_POLLING_MODE) {
      hrtimer_init(&i2c->aml_i2c_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
      i2c->aml_i2c_hrtimer.function = aml_i2c_hrtimer_notify;
    }

    if (i2c->mode == I2C_INTERRUPT_MODE)
	    free_irq(i2c->irq, i2c);
    else if (i2c->mode == I2C_TIMER_POLLING_MODE)
      hrtimer_cancel(&i2c->aml_i2c_hrtimer);

    printk("change i2c master(%d) mode: %d-->%d\n", i2c->master_no, i2c->mode, mode);
    i2c->mode = mode;
    ret = count;
exit_store_i2c_mode:
	  mutex_unlock(i2c->lock);
    return ret;
      }

#ifdef ENABLE_GPIO_TRIGGER
static ssize_t store_trig_gpio(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
    struct aml_i2c *i2c = container_of(class, struct aml_i2c, cls);
    unsigned char gpio_name[20];
    int gpio;
    
    if (count >= ARRAY_SIZE(gpio_name)) return -1;
    sscanf(buf, "%s", gpio_name);
    mutex_lock(i2c->lock);
    if (i2c->trig_gpio >= 0) {
      printk("free old trig_gpio(%d)!\n", i2c->trig_gpio);
      amlogic_gpio_free(i2c->trig_gpio, "aml_i2c_trig");
      i2c->trig_gpio = -1;
    }
    if ((gpio = amlogic_gpio_name_map_num(gpio_name)) < 0) {
      printk("error: %s cannot map to gpio!\n", gpio_name);
    }
    else if (amlogic_gpio_request(gpio, "aml_i2c_trig")) {
      printk("error: %s(%d) has been used!\n", gpio_name, gpio);
    }
    else {
      printk("success: select %s(%d) as trig_gpio!\n", gpio_name, gpio);
      i2c->trig_gpio = gpio;
      amlogic_gpio_direction_output(gpio, 1, "aml_i2c_trig");
    }
	  mutex_unlock(i2c->lock);
    return count;
}
#endif //AML_I2C_REDUCE_CPURATE

static struct class_attribute i2c_class_attrs[] = {
    __ATTR(silence,  S_IRUGO | S_IWUSR, show_i2c_silence,    store_i2c_silence),
    __ATTR(debug,  S_IRUGO | S_IWUSR, show_i2c_debug,    store_i2c_debug),
    __ATTR(info, (S_IRUSR|S_IRGRP), show_i2c_info,    NULL),
    __ATTR(cbus_reg,  S_IWUSR, NULL,    store_register),
    __ATTR(customize,  S_IWUSR, NULL,    rw_special_reg),
    __ATTR(test_slave,  S_IWUSR, NULL,    test_slave_device),
    __ATTR(mode,  S_IRUGO | S_IWUSR, show_i2c_mode,   store_i2c_mode),
#ifdef ENABLE_GPIO_TRIGGER
    __ATTR(trig_gpio,  S_IWUSR, NULL,   store_trig_gpio),
#endif
    __ATTR_NULL
};

#ifdef CONFIG_OF
static const struct of_device_id meson6_i2c_dt_match[];
#endif
static inline struct aml_i2c_platform   *aml_get_driver_data(
			struct platform_device *pdev)
{
#ifdef CONFIG_OF
	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(meson6_i2c_dt_match, pdev->dev.of_node);
		return (struct aml_i2c_platform *)match->data;
	}
#endif
	return (struct aml_i2c_platform *)
			platform_get_device_id(pdev)->driver_data;
}



static int aml_i2c_probe(struct platform_device *pdev)
{
	int ret;
	struct aml_i2c_platform *plat;
	int device_id=-1;
//    struct aml_i2c_platform *plat = (struct aml_i2c_platform *)(pdev->dev.platform_data);

	resource_size_t *res_start;
	struct aml_i2c *i2c = kzalloc(sizeof(struct aml_i2c), GFP_KERNEL);

		struct aml_i2c_platform *aml_i2c_property = kzalloc(sizeof(struct aml_i2c_platform), GFP_KERNEL);
	//printk(KERN_DEBUG "%s : %s\n", __FILE__, __FUNCTION__);

	if (!pdev->dev.of_node) {
			dev_err(&pdev->dev, "no platform data\n");
			return -EINVAL;
	}

	ret = of_property_read_u32(pdev->dev.of_node,"device_id",&device_id);
	if(ret){
			printk("don't find to match device_id\n");
			return -1;
	}

	pdev->id = device_id;


		if(!aml_i2c_property)
			printk("can't alloc mem for i2c_property\n");
		else{
			ret = of_property_read_u32(pdev->dev.of_node, "use_pio", &(aml_i2c_property->use_pio));
			if(ret){
				printk("not find match use_pio, use default\n"); //(INT_I2C_MASTER2<<2)|I2C_INTERRUPT_MODE,
				aml_i2c_property->use_pio = 0;
			}

			ret = of_property_read_u32(pdev->dev.of_node, "master_i2c_speed", &(aml_i2c_property->master_i2c_speed));
			if(ret){
				printk("not find match master_i2c_speed, use default\n");
				if(aml_i2c_device_num == 0)
					aml_i2c_property->master_i2c_speed = 100000;
				else
					aml_i2c_property->master_i2c_speed = 300000;
			}

			aml_i2c_property->wait_count         	= 50000;
			aml_i2c_property->wait_ack_interval 	= 5;
			aml_i2c_property->wait_read_interval = 5;
			aml_i2c_property->wait_xfer_interval = 5;
			aml_i2c_property->master_no          = device_id;
			aml_i2c_property->master_state_name  = NULL;
		}

		if(aml_i2c_device_num == 0){
			aml_i2c_properties_list = aml_i2c_property;
			INIT_LIST_HEAD(&aml_i2c_properties_list->list);
		}
		else{
			list_add_tail(&aml_i2c_property->list ,&aml_i2c_properties_list->list);
		}
		aml_i2c_device_num++;

	plat = (struct aml_i2c_platform*)aml_i2c_property;

	ret=of_property_read_string(pdev->dev.of_node,"pinctrl-names",&plat->master_state_name);
	printk(KERN_DEBUG "plat->state_name:%s\n",plat->master_state_name);
	
  i2c->ops = &aml_i2c_m1_ops;
  i2c->dev=&pdev->dev;


  res_start = of_iomap(pdev->dev.of_node,0);
	i2c->master_regs = (struct aml_i2c_reg_master __iomem*)(res_start);

  BUG_ON(!i2c->master_regs);
  BUG_ON(!plat);
	aml_i2c_set_platform_data(i2c, plat);
	printk(KERN_DEBUG "master_no = %d, master_regs=%p\n", i2c->master_no, i2c->master_regs);
	
	i2c->p=devm_pinctrl_get_select(i2c->dev,i2c->master_state_name);
	if(IS_ERR(i2c->p)){
		printk(KERN_ERR "set i2c pinmux error\n");
		i2c->p=NULL;
	}

    /*lock init*/
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON3
	if((AML_I2C_MASTER_A ==i2c->master_no) || (AML_I2C_MASTER_B ==i2c->master_no)) {
    if (!ab_share_lock) {
      ab_share_lock = kzalloc(sizeof(struct mutex), GFP_KERNEL);
      mutex_init(ab_share_lock);
    }
    i2c->lock = ab_share_lock;
  }
  else {
    i2c->lock = kzalloc(sizeof(struct mutex), GFP_KERNEL);
    mutex_init(i2c->lock);
  }
#else
    i2c->lock = kzalloc(sizeof(struct mutex), GFP_KERNEL);
    mutex_init(i2c->lock);
#endif

  /*setup adapter*/
  i2c->adap.nr = pdev->id==-1? 0: pdev->id;
  i2c->adap.class = I2C_CLASS_HWMON;
  i2c->adap.algo = &aml_i2c_algorithm;
  i2c->adap.retries = 2;
  i2c->adap.timeout = 5;

	i2c->adap.dev.of_node = pdev->dev.of_node;

  //memset(i2c->adap.name, 0 , 48);
  sprintf(i2c->adap.name, ADAPTER_NAME"%d", i2c->adap.nr);
  i2c_set_adapdata(&i2c->adap, i2c);
  ret = i2c_add_numbered_adapter(&i2c->adap);
  if (ret < 0)
  {
          dev_err(&pdev->dev, "Adapter %s registration failed\n",
              i2c->adap.name);
          kzfree(i2c);
          return -1;
  }
  dev_info(&pdev->dev, "add adapter %s(%p)\n", i2c->adap.name, &i2c->adap);
  of_i2c_register_devices(&i2c->adap);
  dev_info(&pdev->dev, "aml i2c bus driver.\n");

  	i2c->state = I2C_STATE_IDLE;
    init_completion(&i2c->aml_i2c_completion);
    if (i2c->mode == I2C_TIMER_POLLING_MODE) {
      hrtimer_init(&i2c->aml_i2c_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
      i2c->aml_i2c_hrtimer.function = aml_i2c_hrtimer_notify;
       printk(KERN_DEBUG "master %d work in timer polling mode\n", device_id);
    }
    else if (i2c->mode == I2C_INTERRUPT_MODE) {
      ret = request_irq(i2c->irq, aml_i2c_complete_isr, IRQF_DISABLED, "aml_i2c", i2c);
      if (ret < 0) {
        dev_err(&pdev->dev, "master %d request irq(%d) failed!\n", device_id, i2c->irq);
        i2c->mode = I2C_DELAY_MODE;
      }
      else {
        printk(KERN_DEBUG "master %d work in interrupt mode(irq=%d)\n", device_id, i2c->irq);
      }
    }
    /*setup class*/
    i2c->cls.name = kzalloc(NAME_LEN, GFP_KERNEL);
    if(i2c->adap.nr)
        sprintf((char*)i2c->cls.name, "i2c%d", i2c->adap.nr);
    else
        sprintf((char*)i2c->cls.name, "i2c");
    i2c->cls.class_attrs = i2c_class_attrs;
    ret = class_register(&i2c->cls);
    if(ret)
        printk(" class register i2c_class fail!\n");

    platform_set_drvdata(pdev, i2c);

    return 0;
}



static int aml_i2c_remove(struct platform_device *pdev)
{
    struct aml_i2c *i2c = platform_get_drvdata(pdev);

    if (i2c != NULL) {
        if (i2c->mode == I2C_INTERRUPT_MODE) {
            free_irq(i2c->irq, i2c);
        }

        if (i2c->mode == I2C_TIMER_POLLING_MODE) {
            hrtimer_cancel(&i2c->aml_i2c_hrtimer);
        }

        mutex_destroy(i2c->lock);
        i2c_del_adapter(&i2c->adap);
        class_unregister(&i2c->cls);
        kzfree(i2c);
    } else {
        printk(KERN_ALERT "[%s]: i2c is NULL\n", __func__);
    }

    return 0;
}


#ifdef CONFIG_OF
static const struct of_device_id meson6_i2c_dt_match[]={
	{	.compatible = "amlogic,aml_i2c",
	},
	{},
};
//MODULE_DEVICE_TABLE(of,meson6_rtc_dt_match);
#else
#define meson6_i2c_dt_match NULL
#endif

//static	int aml_i2c_suspend(struct platform_device *pdev, pm_message_t state)
static	int aml_i2c_suspend(struct device *dev)
{
    struct platform_device *pdev = to_platform_device(dev); 
	struct i2c_adapter *adapter;
	struct aml_i2c *i2c;

    printk("%s\n", __func__);
	adapter = i2c_get_adapter(pdev->id==-1? 0: pdev->id);
	BUG_ON(!adapter);
	i2c = i2c_get_adapdata(adapter);
	BUG_ON(!i2c);
	if (i2c->mode == I2C_INTERRUPT_MODE) {
  	mutex_lock(i2c->lock);
  	i2c->state = I2C_STATE_SUSPEND;
		disable_irq(i2c->irq);
	  mutex_unlock(i2c->lock);
	  printk("%s: disable #%d irq\n", __func__, i2c->irq);
	}

	return 0;
}

//static	int aml_i2c_resume(struct platform_device *pdev)
static	int aml_i2c_resume(struct device *dev)
{
    struct platform_device *pdev = to_platform_device(dev); 
	struct i2c_adapter *adapter;
	struct aml_i2c *i2c;

    printk("%s\n", __func__);
	adapter = i2c_get_adapter(pdev->id==-1? 0: pdev->id);
	BUG_ON(!adapter);
	i2c = i2c_get_adapdata(adapter);
	BUG_ON(!i2c);
	if (i2c->mode == I2C_INTERRUPT_MODE) {
    mutex_lock(i2c->lock);
    i2c->state = I2C_STATE_IDLE;
		enable_irq(i2c->irq);
	  mutex_unlock(i2c->lock);
	  printk("%s: enable #%d irq\n", __func__, i2c->irq);
	}
	return 0;
}

static struct dev_pm_ops aml_i2c_pm_ops = {
    .suspend_noirq = aml_i2c_suspend,
    .resume_noirq  = aml_i2c_resume,
};

static struct platform_driver aml_i2c_driver = {
    .probe = aml_i2c_probe,
    .remove = aml_i2c_remove,
    .driver = {
        .name = "aml-i2c",
        .owner = THIS_MODULE,
        .of_match_table=meson6_i2c_dt_match,
        .pm = &aml_i2c_pm_ops, 
    },
//	.suspend = aml_i2c_suspend,
//	.resume = aml_i2c_resume,
};

static int __init aml_i2c_init(void)
{
    int ret;
    printk(KERN_INFO "aml_i2c version: 20140813\n");
    ret = platform_driver_register(&aml_i2c_driver);
    return ret;
}

static void __exit aml_i2c_exit(void)
{
    //printk(KERN_INFO "%s : %s\n", __FILE__, __FUNCTION__);
    platform_driver_unregister(&aml_i2c_driver);
}

arch_initcall(aml_i2c_init);
module_exit(aml_i2c_exit);

MODULE_AUTHOR("AMLOGIC");
MODULE_DESCRIPTION("I2C driver for amlogic");
MODULE_LICENSE("GPL");

