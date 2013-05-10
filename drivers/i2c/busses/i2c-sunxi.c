/*
 * (C) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * Pan Nan <pannan@allwinnertech.com>
 * Tom Cubie <tanglaing@allwinnertech.com>
 * Victor Wei <weiziheng@allwinnertech.com>
 *
 * SUNXI I2C Platform Driver
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>

#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/io.h>

#include <asm/irq.h>

#include <plat/sys_config.h>
#include <mach/irqs.h>
#include <plat/i2c.h>



#define SUNXI_I2C_DEBUG

#ifdef SUNXI_I2C_DEBUG
#define i2c_dbg(x...)   printk(x)
#else
#define i2c_dbg(x...)
#endif


#define AWXX_I2C_OK      0
#define AWXX_I2C_FAIL   -1
#define AWXX_I2C_RETRY  -2
#define AWXX_I2C_SFAIL  -3  /* start fail */
#define AWXX_I2C_TFAIL  -4  /* stop  fail */

#if 0
#define SYS_FPGA_SIM
#endif

/* aw_i2c_adapter: transfer status */
enum
{
	I2C_XFER_IDLE    = 0x1,
	I2C_XFER_START   = 0x2,
	I2C_XFER_RUNNING = 0x4,
};

struct sunxi_i2c {

	int bus_num;
	unsigned int      status; /* start, running, idle */
	unsigned int      suspend_flag;

	spinlock_t          lock; /* syn */
	wait_queue_head_t   wait;
	struct i2c_msg      *msg;
	unsigned int		msg_num;
	unsigned int		msg_idx;
	unsigned int		msg_ptr;

	struct i2c_adapter	adap;

	struct clk		 *clk;
	struct clk       *pclk;
	unsigned int     bus_freq;
	unsigned int     gpio_hdle;

	void __iomem	 *base_addr;

	unsigned long		iobase; // for remove
	unsigned long		iosize; // for remove

	int			irq;

#ifdef CONFIG_CPU_FREQ
	struct notifier_block	freq_transition;
#endif
	unsigned int debug_state; /* log the twi machine state */

};


#define SYS_I2C_PIN

#ifndef SYS_I2C_PIN

static void* __iomem gpio_addr = NULL;
// gpio twi0
#define _PIO_BASE_ADDRESS    (0x01c20800)
#define _Pn_CFG0(n) ( (n)*0x24 + 0x00 + gpio_addr )
#define _Pn_DRV0(n) ( (n)*0x24 + 0x14 + gpio_addr )
#define _Pn_PUL0(n) ( (n)*0x24 + 0x1C + gpio_addr )
//gpio twi1 twi2
#define _Pn_CFG1(n) ( (n)*0x24 + 0x08 + gpio_addr )
#define _Pn_DRV1(n) ( (n)*0x24 + 0x18 + gpio_addr )
#define _Pn_PUL1(n) ( (n)*0x24 + 0x20 + gpio_addr )

#endif

/* clear the interrupt flag */
static inline void aw_twi_clear_irq_flag(void *base_addr)
{
	unsigned int reg_val = readl(base_addr + TWI_CTL_REG);
	reg_val &= ~TWI_CTL_INTFLG;//0x 1111_0111
	writel(reg_val ,base_addr + TWI_CTL_REG);

	/* read two more times to make sure that interrupt flag does really be cleared */
	{
		unsigned int temp;
		temp = readl(base_addr + TWI_CTL_REG);
		temp |= readl(base_addr + TWI_CTL_REG);
	}
}

/* get data first, then clear flag */
static inline void aw_twi_get_byte(void *base_addr, unsigned char  *buffer)
{
	*buffer = (unsigned char)( TWI_DATA_MASK & readl(base_addr + TWI_DATA_REG) );
	aw_twi_clear_irq_flag(base_addr);
}

/* only get data, we will clear the flag when stop */
static inline void aw_twi_get_last_byte(void *base_addr, unsigned char  *buffer)
{
	*buffer = (unsigned char)( TWI_DATA_MASK & readl(base_addr + TWI_DATA_REG) );
}

/* write data and clear irq flag to trigger send flow */
static inline void aw_twi_put_byte(void *base_addr, const unsigned char *buffer)
{
	writel((unsigned int)*buffer, base_addr + TWI_DATA_REG);
	aw_twi_clear_irq_flag(base_addr);
}

static inline void aw_twi_enable_irq(void *base_addr)
{
	unsigned int reg_val = readl(base_addr + TWI_CTL_REG);

	/*
	 * 1 when enable irq for next operation, set intflag to 1 to prevent to clear it by a mistake
	 *   (intflag bit is write-0-to-clear bit)
	 * 2 Similarly, mask startbit and stopbit to prevent to set it twice by a mistake
	 *   (start bit and stop bit are self-clear-to-0 bits)
	 */
	reg_val |= (TWI_CTL_INTEN | TWI_CTL_INTFLG);
	reg_val &= ~(TWI_CTL_STA | TWI_CTL_STP);
	writel(reg_val, base_addr + TWI_CTL_REG);
}

static inline void aw_twi_disable_irq(void *base_addr)
{
	unsigned int reg_val = readl(base_addr + TWI_CTL_REG);
	reg_val &= ~TWI_CTL_INTEN;
	writel(reg_val, base_addr + TWI_CTL_REG);
}

static inline void aw_twi_disable_bus(void *base_addr)
{
	unsigned int reg_val = readl(base_addr + TWI_CTL_REG);
	reg_val &= ~TWI_CTL_BUSEN;
	writel(reg_val, base_addr + TWI_CTL_REG);
}

static inline void aw_twi_enable_bus(void *base_addr)
{
	unsigned int reg_val = readl(base_addr + TWI_CTL_REG);
	reg_val |= TWI_CTL_BUSEN;
	writel(reg_val, base_addr + TWI_CTL_REG);
}

/* trigger start signal, the start bit will be cleared automatically */
static inline void aw_twi_set_start(void *base_addr)
{
	unsigned int reg_val = readl(base_addr + TWI_CTL_REG);
	reg_val |= TWI_CTL_STA;
	writel(reg_val, base_addr + TWI_CTL_REG);
}

/* get start bit status, poll if start signal is sent */
static inline unsigned int aw_twi_get_start(void *base_addr)
{
	unsigned int reg_val = readl(base_addr + TWI_CTL_REG);
	reg_val >>= 5;
	return reg_val & 1;
}

/* trigger stop signal, the stop bit will be cleared automatically */
static inline void aw_twi_set_stop(void *base_addr)
{
	unsigned int reg_val = readl(base_addr + TWI_CTL_REG);
	reg_val |= TWI_CTL_STP;
	writel(reg_val, base_addr + TWI_CTL_REG);
}

/* get stop bit status, poll if stop signal is sent */
static inline unsigned int aw_twi_get_stop(void *base_addr)
{
	unsigned int reg_val = readl(base_addr + TWI_CTL_REG);
	reg_val >>= 4;
	return reg_val & 1;
}

static inline void aw_twi_disable_ack(void *base_addr)
{
	unsigned int reg_val = readl(base_addr + TWI_CTL_REG);
	reg_val &= ~TWI_CTL_ACK;
	writel(reg_val, base_addr + TWI_CTL_REG);
	return;
}

/* when sending ack or nack, it will send ack automatically */
static inline void aw_twi_enable_ack(void *base_addr)
{
	unsigned int reg_val = readl(base_addr + TWI_CTL_REG);
	reg_val |= TWI_CTL_ACK;
	writel(reg_val, base_addr + TWI_CTL_REG);
}

/* get the interrupt flag */
static inline unsigned int aw_twi_query_irq_flag(void *base_addr)
{
	unsigned int reg_val = readl(base_addr + TWI_CTL_REG);
	return (reg_val & TWI_CTL_INTFLG);//0x 0000_1000
}

/* get interrupt status */
static inline unsigned int aw_twi_query_irq_status(void *base_addr)
{
	unsigned int reg_val = readl(base_addr + TWI_STAT_REG);
	return (reg_val & TWI_STAT_MASK);
}

/* set twi clock
 *
 * clk_n: clock divider factor n
 * clk_m: clock divider factor m
 */
static inline void twi_clk_write_reg(unsigned int clk_n, unsigned int clk_m, void *base_addr)
{
	unsigned int reg_val = readl(base_addr + TWI_CLK_REG);
	pr_debug("%s: clk_n = %d, clk_m = %d\n", __FUNCTION__, clk_n, clk_m);
	reg_val &= ~(TWI_CLK_DIV_M | TWI_CLK_DIV_N);
	reg_val |= ( clk_n |(clk_m << 3) );
	writel(reg_val, base_addr + TWI_CLK_REG);
}

/*
* Fin is APB CLOCK INPUT;
* Fsample = F0 = Fin/2^CLK_N;
* F1 = F0/(CLK_M+1);
* Foscl = F1/10 = Fin/(2^CLK_N * (CLK_M+1)*10);
* Foscl is clock SCL;100KHz or 400KHz
*
* clk_in: apb clk clock
* sclk_req: freqence to set in HZ
*/
static void aw_twi_set_clock(unsigned int clk_in, unsigned int sclk_req, void *base_addr)
{
	unsigned int clk_m = 0;
	unsigned int clk_n = 0;
	unsigned int _2_pow_clk_n = 1;
	unsigned int src_clk      = clk_in/10;
	unsigned int divider      = src_clk/sclk_req;  // 400khz or 100khz
	unsigned int sclk_real    = 0;      // the real clock frequency

#ifdef CONFIG_FPGA_SIM
	{
		clk_m = 2;
		clk_n = 3;
		goto set_clk;
	}
#endif

	if (divider==0) {
		clk_m = 1;
		goto set_clk;
	}
	/* search clk_n and clk_m,from large to small value so that can quickly find suitable m & n. */
	while (clk_n < 8) { // 3bits max value is 8
		/* (m+1)*2^n = divider -->m = divider/2^n -1 */
		clk_m = (divider/_2_pow_clk_n) - 1;
		/* clk_m = (divider >> (_2_pow_clk_n>>1))-1 */
		while (clk_m < 16) { /* 4bits max value is 16 */
			sclk_real = src_clk/(clk_m + 1)/_2_pow_clk_n;  /* src_clk/((m+1)*2^n) */
			if (sclk_real <= sclk_req) {
				goto set_clk;
			}
			else {
				clk_m++;
			}
		}
		clk_n++;
		_2_pow_clk_n *= 2; /* mutilple by 2 */
	}

set_clk:
	twi_clk_write_reg(clk_n, clk_m, base_addr);

	return;
}

static inline void aw_twi_soft_reset(void *base_addr)
{
	unsigned int reg_val = readl(base_addr + TWI_SRST_REG);
	reg_val |= TWI_SRST_SRST; /* set soft reset bit,0x0000 0001 */
	writel(reg_val, base_addr + TWI_SRST_REG);
}

/* Enhanced Feature Register */
static inline void aw_twi_set_EFR(void *base_addr, unsigned int efr)
{
	unsigned int reg_val = readl(base_addr + TWI_EFR_REG);

	reg_val &= ~TWI_EFR_MASK;
	efr     &= TWI_EFR_MASK;
	reg_val |= efr;
	writel(reg_val, base_addr + TWI_EFR_REG);
}

static int i2c_sunxi_xfer_complete(struct sunxi_i2c *i2c, int code);
static int i2c_sunxi_do_xfer(struct sunxi_i2c *i2c, struct i2c_msg *msgs, int num);

static int aw_twi_enable_sys_clk(struct sunxi_i2c *i2c)
{
	int     result;

	result = clk_enable(i2c->pclk);
	if(result) {
		return result;
	}

	return clk_enable(i2c->clk);
}

static void aw_twi_disable_sys_clk(struct sunxi_i2c *i2c)
{
	clk_disable(i2c->clk);
	clk_disable(i2c->pclk);
}

static int aw_twi_request_gpio(struct sunxi_i2c *i2c)
{
	if(i2c->bus_num == 0) {
		/* pb0-pb1 TWI0 SDA,SCK */
		i2c_dbg("config i2c gpio with gpio_config api \n");

		i2c->gpio_hdle = gpio_request_ex("twi0_para", NULL);
		if(!i2c->gpio_hdle) {
			pr_warning("twi0 request gpio fail!\n");
			return -1;
		}
	}
	else if(i2c->bus_num == 1) {
		/* pb18-pb19 TWI1 scl,sda */
		i2c->gpio_hdle = gpio_request_ex("twi1_para", NULL);
		if(!i2c->gpio_hdle) {
			pr_warning("twi1 request gpio fail!\n");
			return -1;
		}
	}
	else if(i2c->bus_num == 2) {
		/* pb20-pb21 TWI2 scl,sda */
		i2c->gpio_hdle = gpio_request_ex("twi2_para", NULL);
		if(!i2c->gpio_hdle) {
			pr_warning("twi2 request gpio fail!\n");
			return -1;
		}
	}

	return 0;
}

static void aw_twi_release_gpio(struct sunxi_i2c *i2c)
{
	if(i2c->bus_num == 0) {
		/* pb0-pb1 TWI0 SDA,SCK */
		gpio_release(i2c->gpio_hdle, 0);
	}
	else if(i2c->bus_num == 1) {
		/* pb18-pb19 TWI1 scl,sda */
		gpio_release(i2c->gpio_hdle, 0);
	}
	else if(i2c->bus_num == 2) {
		/* pb20-pb21 TWI2 scl,sda */
		gpio_release(i2c->gpio_hdle, 0);
	}
}


/* function  */
static int aw_twi_start(void *base_addr)
{
	unsigned int timeout = 0xff;

	aw_twi_set_start(base_addr);
	while((1 == aw_twi_get_start(base_addr))&&(--timeout));
	if(timeout == 0) {
		i2c_dbg("START can't sendout!\n");
		return AWXX_I2C_FAIL;
	}

	return AWXX_I2C_OK;
}

static int aw_twi_restart(void  *base_addr)
{
	unsigned int timeout = 0xff;
	aw_twi_set_start(base_addr);
	aw_twi_clear_irq_flag(base_addr);
	while((1 == aw_twi_get_start(base_addr))&&(--timeout));
	if(timeout == 0) {
		i2c_dbg("Restart can't sendout!\n");
		return AWXX_I2C_FAIL;
	}
	return AWXX_I2C_OK;
}

static int aw_twi_stop(void *base_addr)
{
	unsigned int timeout = 0xff;

	aw_twi_set_stop(base_addr);
	aw_twi_clear_irq_flag(base_addr);

	aw_twi_get_stop(base_addr);/* it must delay 1 nop to check stop bit */
	while(( 1 == aw_twi_get_stop(base_addr))&& (--timeout));
	if(timeout == 0) {
		i2c_dbg("1.STOP can't sendout!\n");
		return AWXX_I2C_FAIL;
	}

	timeout = 0xff;
	while((TWI_STAT_IDLE != readl(base_addr + TWI_STAT_REG))&&(--timeout));
	if(timeout == 0)
	{
		i2c_dbg("i2c state isn't idle(0xf8)\n");
		return AWXX_I2C_FAIL;
	}

	timeout = 0xff;
	while((TWI_LCR_IDLE_STATUS != readl(base_addr + TWI_LCR_REG))&&(--timeout));
	if(timeout == 0) {
		i2c_dbg("2.STOP can't sendout!\n");
		return AWXX_I2C_FAIL;
	}

	return AWXX_I2C_OK;
}

/*
****************************************************************************************************
*
*  FunctionName:           i2c_sunxi_addr_byte
*
*  Description:
*            发送slave地址，7bit的全部信息，及10bit的第一部分地址。供外部接口调用，内部实现。
*         7bits addr: 7-1bits addr+0 bit r/w
*         10bits addr: 1111_11xx_xxxx_xxxx-->1111_0xx_rw,xxxx_xxxx
*         send the 7 bits addr,or the first part of 10 bits addr
*  Parameters:
*
*
*  Return value:
*           无
*  Notes:
*
****************************************************************************************************
*/
static void i2c_sunxi_addr_byte(struct sunxi_i2c *i2c)
{
	unsigned char addr = 0;//address
	unsigned char tmp  = 0;

	if(i2c->msg[i2c->msg_idx].flags & I2C_M_TEN) {
		tmp = 0x78 | ( ( (i2c->msg[i2c->msg_idx].addr)>>8 ) & 0x03);//0111_10xx,ten bits address--9:8bits
		addr = tmp << 1;//1111_0xx0
		//how about the second part of ten bits addr???
		//Answer: deal at twi_core_process()
	}
	else {
		addr = (i2c->msg[i2c->msg_idx].addr & 0x7f) << 1;// 7-1bits addr,xxxx_xxx0
#ifdef CONFIG_SUNXI_IIC_PRINT_TRANSFER_INFO
		if(i2c->bus_num == CONFIG_SUNXI_IIC_PRINT_TRANSFER_INFO_WITH_BUS_NUM){
			i2c_dbg("i2c->msg->addr = 0x%x. \n", addr);
		}
#endif
	}
	//read,default value is write
	if (i2c->msg[i2c->msg_idx].flags & I2C_M_RD) {
		addr |= 1;
	}
	//send 7bits+r/w or the first part of 10bits
	aw_twi_put_byte(i2c->base_addr, &addr);

	return;
}


static int i2c_sunxi_core_process(struct sunxi_i2c *i2c)
{
	void *base_addr = i2c->base_addr;
	int  ret        = AWXX_I2C_OK;
	int  err_code   = 0;
	unsigned char  state = 0;
	unsigned char  tmp   = 0;

	state = aw_twi_query_irq_status(base_addr);

#ifdef CONFIG_SUNXI_IIC_PRINT_TRANSFER_INFO
	if(i2c->bus_num == CONFIG_SUNXI_IIC_PRINT_TRANSFER_INFO_WITH_BUS_NUM){
		i2c_dbg("sunxi_i2c->bus_num = %d, sunxi_i2c->msg->addr = (0x%x) state = (0x%x)\n", \
			i2c->bus_num, i2c->msg->addr, state);
	}
#endif

    if(i2c->msg == NULL) {
        printk("i2c->msg is NULL, err_code = 0xfe\n");
        err_code = 0xfe;
        goto msg_null;
    }

	switch(state) {
	case 0xf8: /* On reset or stop the bus is idle, use only at poll method */
		err_code = 0xf8;
		goto err_out;
	case 0x08: /* A START condition has been transmitted */
	case 0x10: /* A repeated start condition has been transmitted */
		i2c_sunxi_addr_byte(i2c);/* send slave address */
		break;
	case 0xd8: /* second addr has transmitted, ACK not received!    */
	case 0x20: /* SLA+W has been transmitted; NOT ACK has been received */
		err_code = 0x20;
		goto err_out;
	case 0x18: /* SLA+W has been transmitted; ACK has been received */
		/* if any, send second part of 10 bits addr */
		if(i2c->msg[i2c->msg_idx].flags & I2C_M_TEN) {
			tmp = i2c->msg[i2c->msg_idx].addr & 0xff;  /* the remaining 8 bits of address */
			aw_twi_put_byte(base_addr, &tmp); /* case 0xd0: */
			break;
		}
		/* for 7 bit addr, then directly send data byte--case 0xd0:  */
	case 0xd0: /* second addr has transmitted,ACK received!     */
	case 0x28: /* Data byte in DATA REG has been transmitted; ACK has been received */
		/* after send register address then START send write data  */
		if(i2c->msg_ptr < i2c->msg[i2c->msg_idx].len) {
			aw_twi_put_byte(base_addr, &(i2c->msg[i2c->msg_idx].buf[i2c->msg_ptr]));
			i2c->msg_ptr++;
			break;
		}

		i2c->msg_idx++; /* the other msg */
		i2c->msg_ptr = 0;
		if (i2c->msg_idx == i2c->msg_num) {
			err_code = AWXX_I2C_OK;/* Success,wakeup */
			goto ok_out;
		}
		else if(i2c->msg_idx < i2c->msg_num) {/* for restart pattern */
			ret = aw_twi_restart(base_addr);/* read spec, two msgs */
			if(ret == AWXX_I2C_FAIL) {
				err_code = AWXX_I2C_SFAIL;
				goto err_out;/* START can't sendout */
			}
		}
		else {
			err_code = AWXX_I2C_FAIL;
			goto err_out;
		}
		break;
	case 0x30: /* Data byte in I2CDAT has been transmitted; NOT ACK has been received */
		err_code = 0x30;//err,wakeup the thread
		goto err_out;
	case 0x38: /* Arbitration lost during SLA+W, SLA+R or data bytes */
		err_code = 0x38;//err,wakeup the thread
		goto err_out;
	case 0x40: /* SLA+R has been transmitted; ACK has been received */
		/* with Restart,needn't to send second part of 10 bits addr,refer-"I2C-SPEC v2.1" */
		/* enable A_ACK need it(receive data len) more than 1. */
		if(i2c->msg[i2c->msg_idx].len > 1) {
			/* send register addr complete,then enable the A_ACK and get ready for receiving data */
			aw_twi_enable_ack(base_addr);
			aw_twi_clear_irq_flag(base_addr);/* jump to case 0x50 */
		}
		else if(i2c->msg[i2c->msg_idx].len == 1) {
			aw_twi_clear_irq_flag(base_addr);/* jump to case 0x58 */
		}
		break;
	case 0x48: /* SLA+R has been transmitted; NOT ACK has been received */
		err_code = 0x48;//err,wakeup the thread
		goto err_out;
	case 0x50: /* Data bytes has been received; ACK has been transmitted */
		/* receive first data byte */
		if (i2c->msg_ptr < i2c->msg[i2c->msg_idx].len) {
			/* more than 2 bytes, the last byte need not to send ACK */
			if( (i2c->msg_ptr + 2) == i2c->msg[i2c->msg_idx].len ) {
				aw_twi_disable_ack(base_addr);/* last byte no ACK */
			}
			/* get data then clear flag,then next data comming */
			aw_twi_get_byte(base_addr, &i2c->msg[i2c->msg_idx].buf[i2c->msg_ptr]);
			i2c->msg_ptr++;

			break;
		}
		/* err process, the last byte should be @case 0x58 */
		err_code = AWXX_I2C_FAIL;/* err, wakeup */
		goto err_out;
	case 0x58: /* Data byte has been received; NOT ACK has been transmitted */
		/* received the last byte  */
		if ( i2c->msg_ptr == i2c->msg[i2c->msg_idx].len - 1 ) {
			aw_twi_get_last_byte(base_addr, &i2c->msg[i2c->msg_idx].buf[i2c->msg_ptr]);
			i2c->msg_idx++;
			i2c->msg_ptr = 0;
			if (i2c->msg_idx == i2c->msg_num) {
				err_code = AWXX_I2C_OK; // succeed,wakeup the thread
				goto ok_out;
			}
			else if(i2c->msg_idx < i2c->msg_num) {
				/* repeat start */
				ret = aw_twi_restart(base_addr);
				if(ret == AWXX_I2C_FAIL) {/* START fail */
					err_code = AWXX_I2C_SFAIL;
					goto err_out;
				}
				break;
			}
		}
		else {
			err_code = 0x58;
			goto err_out;
		}
	case 0x00: /* Bus error during master or slave mode due to illegal level condition */
		err_code = 0xff;
		goto err_out;
	default:
		err_code = state;
		goto err_out;
	}
	i2c->debug_state = state;/* just for debug */
	return ret;

ok_out:
err_out:
	if(AWXX_I2C_FAIL == aw_twi_stop(base_addr)) {
		i2c_dbg("STOP failed!\n");
	}

msg_null:
	ret = i2c_sunxi_xfer_complete(i2c, err_code);/* wake up */

	i2c->debug_state = state;/* just for debug */
	return ret;
}

static irqreturn_t i2c_sunxi_handler(int this_irq, void * dev_id)
{
	struct sunxi_i2c *i2c = (struct sunxi_i2c *)dev_id;
	int ret = AWXX_I2C_FAIL;

	if(!aw_twi_query_irq_flag(i2c->base_addr)) {
		pr_warning("unknown interrupt!");
		return ret;
		//return IRQ_HANDLED;
	}

	/* disable irq */
	aw_twi_disable_irq(i2c->base_addr);
	//twi core process
	ret = i2c_sunxi_core_process(i2c);

	/* enable irq only when twi is transfering, otherwise,disable irq */
	if(i2c->status != I2C_XFER_IDLE) {
		aw_twi_enable_irq(i2c->base_addr);
	}

	return IRQ_HANDLED;
}

static int i2c_sunxi_xfer_complete(struct sunxi_i2c *i2c, int code)
{
	int ret = AWXX_I2C_OK;

	i2c->msg     = NULL;
	i2c->msg_num = 0;
	i2c->msg_ptr = 0;
	i2c->status  = I2C_XFER_IDLE;
	/* i2c->msg_idx  store the information */

	if(code == AWXX_I2C_FAIL) {
		i2c_dbg("Maybe Logic Error,debug it!\n");
		i2c->msg_idx = code;
		ret = AWXX_I2C_FAIL;
	}
	else if(code != AWXX_I2C_OK) {
		//return the ERROR code, for debug or detect error type
		i2c->msg_idx = code;
		ret = AWXX_I2C_FAIL;
	}

	wake_up(&i2c->wait);

	return ret;
}

static int i2c_sunxi_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct sunxi_i2c *i2c = (struct sunxi_i2c *)adap->algo_data;
	int ret = AWXX_I2C_FAIL;
	int i   = 0;

	if(i2c->suspend_flag) {
		i2c_dbg("[i2c-%d] has already suspend, dev addr:%x!\n", i2c->adap.nr, msgs->addr);
		return -ENODEV;
	}

	for(i = adap->retries; i >= 0; i--) {
		ret = i2c_sunxi_do_xfer(i2c, msgs, num);

		if(ret != AWXX_I2C_RETRY) {
			goto out;
		}

		i2c_dbg("Retrying transmission %d\n", i);
		udelay(100);
	}

	ret = -EREMOTEIO;
out:
	return ret;
}

static int i2c_sunxi_do_xfer(struct sunxi_i2c *i2c, struct i2c_msg *msgs, int num)
{
	unsigned long timeout = 0;
	int ret = AWXX_I2C_FAIL;
	//int i = 0, j =0;

	aw_twi_soft_reset(i2c->base_addr);
	udelay(100);

	/* test the bus is free,already protect by the semaphore at DEV layer */
	while( TWI_STAT_IDLE != aw_twi_query_irq_status(i2c->base_addr)&&
	       TWI_STAT_BUS_ERR != aw_twi_query_irq_status(i2c->base_addr) &&
	       TWI_STAT_ARBLOST_SLAR_ACK != aw_twi_query_irq_status(i2c->base_addr) ) {
		i2c_dbg("bus is busy, status = %x\n", aw_twi_query_irq_status(i2c->base_addr));
		ret = AWXX_I2C_RETRY;
		goto out;
	}
	//i2c_dbg("bus num = %d\n", i2c->adap.nr);
	//i2c_dbg("bus name = %s\n", i2c->adap.name);
	/* may conflict with xfer_complete */
	spin_lock_irq(&i2c->lock);
	i2c->msg     = msgs;
	i2c->msg_num = num;
	i2c->msg_ptr = 0;
	i2c->msg_idx = 0;
	i2c->status  = I2C_XFER_START;
    aw_twi_enable_irq(i2c->base_addr);  /* enable irq */
	aw_twi_disable_ack(i2c->base_addr); /* disabe ACK */
	aw_twi_set_EFR(i2c->base_addr, 0);  /* set the special function register,default:0. */
	spin_unlock_irq(&i2c->lock);
/*
	for(i =0 ; i < num; i++){
		for(j = 0; j < msgs->len; j++){
			i2c_dbg("baddr = %x \n",msgs->addr);
			i2c_dbg("data = %x \n", msgs->buf[j]);
		}
		i2c_dbg("\n\n");
}
*/

	ret = aw_twi_start(i2c->base_addr);/* START signal,needn't clear int flag  */
	if(ret == AWXX_I2C_FAIL) {
		aw_twi_soft_reset(i2c->base_addr);
		aw_twi_disable_irq(i2c->base_addr);  /* disable irq */
		i2c->status  = I2C_XFER_IDLE;
		ret = AWXX_I2C_RETRY;
		goto out;
	}
	/* sleep and wait, do the transfer at interrupt handler ,timeout = 5*HZ */
	timeout = wait_event_timeout(i2c->wait, i2c->msg_num == 0, i2c->adap.timeout);
	/* return code,if(msg_idx == num) succeed */
	ret = i2c->msg_idx;

	if (timeout == 0){
		//dev_dbg(i2c->adap.dev, "timeout \n");
		pr_warning("i2c-%d, xfer timeout\n", i2c->bus_num);
		ret = -ETIME;
	}
	else if (ret != num){
		printk("incomplete xfer (0x%x)\n", ret);
		ret = -ECOMM;
		//dev_dbg(i2c->adap.dev, "incomplete xfer (%d)\n", ret);
	}
out:
	return ret;
}

static unsigned int i2c_sunxi_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C|I2C_FUNC_10BIT_ADDR|I2C_FUNC_SMBUS_EMUL;
}


static const struct i2c_algorithm i2c_sunxi_algorithm = {
	.master_xfer	  = i2c_sunxi_xfer,
	.functionality	  = i2c_sunxi_functionality,
};

static int i2c_sunxi_clk_init(struct sunxi_i2c *i2c)
{
	int ret = 0;

	unsigned int apb_clk = 0;

	// enable APB clk
	ret = aw_twi_enable_sys_clk(i2c);
	if(ret == -1){
		i2c_dbg("enable i2c clock failed!\n");
		return -1;
	}

	// enable twi bus
	aw_twi_enable_bus(i2c->base_addr);

	// set twi module clock
	apb_clk  =  clk_get_rate(i2c->clk);
	if(apb_clk == 0){
		i2c_dbg("get i2c source clock frequency failed!\n");
		return -1;
	}
	i2c_dbg("twi%d, apb clock = %d \n",i2c->bus_num, apb_clk);
	aw_twi_set_clock(apb_clk, i2c->bus_freq, i2c->base_addr);

	return 0;

}

static int i2c_sunxi_clk_exit(struct sunxi_i2c *i2c)
{
	void *base_addr = i2c->base_addr;

	// aw_twi_disable_irq(base_addr);
	// disable twi bus
	aw_twi_disable_bus(base_addr);
	// disable APB clk
	aw_twi_disable_sys_clk(i2c);

	return 0;

}

static int i2c_sunxi_hw_init(struct sunxi_i2c *i2c)
{
	int ret = 0;

	ret = aw_twi_request_gpio(i2c);
	if(ret == -1){
		i2c_dbg("request i2c gpio failed!\n");
		return -1;
	}

	if(i2c_sunxi_clk_init(i2c)) {
		return -1;
	}

	aw_twi_soft_reset(i2c->base_addr);

	return ret;
}

static void i2c_sunxi_hw_exit(struct sunxi_i2c *i2c)
{
	if(i2c_sunxi_clk_exit(i2c)) {
		return;
	}
	aw_twi_release_gpio(i2c);

}

static int i2c_sunxi_probe(struct platform_device *dev)
{
	struct sunxi_i2c *i2c = NULL;
	struct resource *res = NULL;
	struct sunxi_i2c_platform_data *pdata = NULL;
	char *i2c_clk[] ={"twi0","twi1","twi2"};
	char *i2c_pclk[] ={"apb_twi0","apb_twi1","apb_twi2"};
	int ret;
	int irq;

	pdata = dev->dev.platform_data;
	if(pdata == NULL) {
		return -ENODEV;
	}

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(dev, 0);
	if (res == NULL || irq < 0) {
		return -ENODEV;
	}

	if (!request_mem_region(res->start, resource_size(res), res->name)) {
		return -ENOMEM;
	}

	i2c = kzalloc(sizeof(struct sunxi_i2c), GFP_KERNEL);
	if (!i2c) {
		ret = -ENOMEM;
		goto emalloc;
	}
	strlcpy(i2c->adap.name, "sunxi-i2c", sizeof(i2c->adap.name));
	i2c->adap.owner   = THIS_MODULE;
	i2c->adap.nr      = pdata->bus_num;
	i2c->adap.retries = 2;
	i2c->adap.timeout = 5*HZ;
	i2c->adap.class   = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	i2c->bus_freq     = pdata->frequency;
	i2c->irq 		  = irq;
	i2c->bus_num      = pdata->bus_num;
	i2c->status       = I2C_XFER_IDLE;
	i2c->suspend_flag = 0;
	spin_lock_init(&i2c->lock);
	init_waitqueue_head(&i2c->wait);

	i2c->pclk = clk_get(NULL, i2c_pclk[i2c->adap.nr]);
	if(NULL == i2c->pclk){
		i2c_dbg("request apb_i2c clock failed\n");
		ret = -EIO;
		goto eremap;
	}

	i2c->clk = clk_get(NULL, i2c_clk[i2c->adap.nr]);
	if(NULL == i2c->clk){
		i2c_dbg("request i2c clock failed\n");
        clk_put(i2c->pclk);
		ret = -EIO;
		goto eremap;
	}

	snprintf(i2c->adap.name, sizeof(i2c->adap.name),\
		 "sunxi-i2c.%u", i2c->adap.nr);
	i2c->base_addr = ioremap(res->start, resource_size(res));
	if (!i2c->base_addr) {
		ret = -EIO;
		goto eremap;
	}
	i2c_dbg("!!! base_Addr = 0x%x \n", (unsigned int)i2c->base_addr );


#ifndef SYS_I2C_PIN
	gpio_addr = ioremap(_PIO_BASE_ADDRESS, 0x1000);
	if(!gpio_addr) {
	    ret = -EIO;
	    goto ereqirq;
	}
#endif

	i2c->adap.algo = &i2c_sunxi_algorithm;
	ret = request_irq(irq, i2c_sunxi_handler, IRQF_DISABLED, i2c->adap.name, i2c);
	if (ret)
	{
		goto ereqirq;
	}

	if(-1 == i2c_sunxi_hw_init(i2c)){
		ret = -EIO;
		goto eadapt;
	}

	i2c->adap.algo_data  = i2c;
	i2c->adap.dev.parent = &dev->dev;

	i2c->iobase = res->start;
	i2c->iosize = resource_size(res);

	ret = i2c_add_numbered_adapter(&i2c->adap);
	if (ret < 0) {
		i2c_dbg(KERN_INFO "I2C: Failed to add bus\n");
		goto eadapt;
	}

	platform_set_drvdata(dev, i2c);

	i2c_dbg(KERN_INFO "I2C: %s: AW16XX I2C adapter\n",
	       dev_name(&i2c->adap.dev));


	return 0;

eadapt:
	free_irq(irq, i2c);
	clk_disable(i2c->clk);

ereqirq:
#ifndef SYS_I2C_PIN
    if(gpio_addr){
        iounmap(gpio_addr);
    }
#endif
	iounmap(i2c->base_addr);
	clk_put(i2c->clk);

eremap:
	kfree(i2c);

emalloc:
	release_mem_region(i2c->iobase, i2c->iosize);

	return ret;
}


static int __exit i2c_sunxi_remove(struct platform_device *dev)
{
	struct sunxi_i2c *i2c = platform_get_drvdata(dev);

	platform_set_drvdata(dev, NULL);

	i2c_del_adapter(&i2c->adap);

	free_irq(i2c->irq, i2c);

	i2c_sunxi_hw_exit(i2c); /* disable clock and release gpio */
	clk_put(i2c->clk);
	clk_put(i2c->pclk);

	iounmap(i2c->base_addr);
	release_mem_region(i2c->iobase, i2c->iosize);
	kfree(i2c);

	return 0;
}



#ifdef CONFIG_PM
static int i2c_sunxi_suspend(struct platform_device *pdev,  pm_message_t state)
{
	struct sunxi_i2c *i2c = platform_get_drvdata(pdev);

	i2c->suspend_flag = 1;

	if(i2c->status != I2C_XFER_IDLE) {
		i2c_dbg("[i2c-%d] suspend wihle xfer,dev addr = %x\n",
			i2c->adap.nr, i2c->msg? i2c->msg->addr : 0xff);
	}

	if(0 == i2c->bus_num) {
		/* twi0 is for power, it will be accessed by axp driver
		   before twi resume, so, don't suspend twi0            */
		i2c->suspend_flag = 0;
		return 0;
	}

	if(i2c_sunxi_clk_exit(i2c)) {
		i2c_dbg("[i2c%d] suspend failed.. \n", i2c->bus_num);
		i2c->suspend_flag = 0;
		return -1;
	}

	i2c_dbg("[i2c%d] suspend okay.. \n", i2c->bus_num);
	return 0;
}


static int i2c_sunxi_resume(struct platform_device *pdev)
{
	struct sunxi_i2c *i2c = platform_get_drvdata(pdev);

	i2c->suspend_flag = 0;

	if(0 == i2c->bus_num) {
		return 0;
	}

	if(i2c_sunxi_clk_init(i2c)) {
		i2c_dbg("[i2c%d] resume failed.. \n", i2c->bus_num);
		return -1;
	}

	aw_twi_soft_reset(i2c->base_addr);

	i2c_dbg("[i2c%d] resume okay.. \n", i2c->bus_num);
	return 0;
}
#else
static int i2c_sunxi_suspend(struct platform_device *pdev,  pm_message_t state)
{
	pr_info("i2c fake suspend\n");
	return 0;
}

static int i2c_sunxi_resume(struct platform_device *pdev)
{
	pr_info("i2c fake resume\n");
	return 0;
}
#endif

static struct platform_driver i2c_sunxi_driver = {
	.probe		= i2c_sunxi_probe,
	.remove		= __exit_p(i2c_sunxi_remove),
	.suspend        = i2c_sunxi_suspend,
	.resume         = i2c_sunxi_resume,
	.driver		= {
		.name	= "sunxi-i2c",
		.owner	= THIS_MODULE,
	},
};

static int __init i2c_adap_sunxi_init(void)
{
	return platform_driver_register(&i2c_sunxi_driver);
}

module_init(i2c_adap_sunxi_init);

static void __exit i2c_adap_sunxi_exit(void)
{
	platform_driver_unregister(&i2c_sunxi_driver);
}

module_exit(i2c_adap_sunxi_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sunxi-i2c");
MODULE_DESCRIPTION("SUNXI I2C Bus Driver");
MODULE_AUTHOR("VictorWei");
