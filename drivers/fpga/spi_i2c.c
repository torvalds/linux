#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/serial_reg.h>
#include <linux/circ_buf.h>
#include <linux/gfp.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <mach/board.h>
#include <mach/rk2818_iomap.h>

#include "spi_fpga.h"

#if defined(CONFIG_SPI_I2C_DEBUG)
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

#define SPI_I2C_TEST 0

#define MAXMSGLEN   16
#define DRV_NAME    "fpga_i2c"


struct spi_i2c_data {
	struct device *dev;
	struct i2c_adapter adapter;
	unsigned long scl_rate;
	spinlock_t i2c_lock;	
};


int spi_i2c_handle_irq(struct spi_fpga_port *port,unsigned char channel)
{
	int reg;
	int ret;

	if(channel == 0)
		reg =  ICE_SEL_I2C_INT|ICE_SEL_I2C_CH2;
	else
		reg =  ICE_SEL_I2C_INT|ICE_SEL_I2C_CH3;
	
	port->i2c.interrupt = 0;
	ret = spi_in(port,reg,SEL_I2C);
	if(ret == INT_I2C_READ_ACK)
		port->i2c.interrupt = INT_I2C_READ_ACK;		
	else if(ret == INT_I2C_READ_NACK)
	{
		printk("Error::read no ack!!check the I2C slave device \n");
	}
	else if(ret == INT_I2C_WRITE_ACK)
		port->i2c.interrupt = INT_I2C_WRITE_ACK;
	else if(ret == INT_I2C_WRITE_NACK)
	{
		printk("Error::write no ack!!check the I2C slave device \n");
	}
	else
		printk("Error:ack value error!!check the I2C slave device \n");
	return port->i2c.interrupt;
}

int spi_i2c_select_speed(int speed)
{
	int result = 0; 
	switch(speed)
	{
		case 10:
			result = ICE_SET_10K_I2C_SPEED;	
			break;			
		case 100:
			result = ICE_SET_100K_I2C_SPEED;		
			break;			
		case 200:
			result = ICE_SET_200K_I2C_SPEED;	
			break;			
		case 300:			
		       result = ICE_SET_300K_I2C_SPEED;	
			break;			
		case 400:			
			result = ICE_SET_400K_I2C_SPEED;	
			break;			
		default:			
			break;		
	}
	return result;
}

int spi_i2c_readbuf(struct spi_fpga_port *port ,struct i2c_msg *pmsg)
{
	
	unsigned int reg ;
	unsigned int len,i;
	unsigned int slaveaddr;
	unsigned int speed;
	unsigned int channel = 0 ;
	unsigned int result;
	
	slaveaddr = pmsg->addr;
	len = pmsg->len;	
	speed = spi_i2c_select_speed(pmsg->scl_rate);
	
	if(pmsg->channel == I2C_CH2)
		channel = ICE_SEL_I2C_CH2;
	else if(pmsg->channel == I2C_CH3)
		channel = ICE_SEL_I2C_CH3;
	else
	{
		printk("Error:try to read form error i2c channel\n");
		return 0;
	}
	
	if(pmsg->read_type == 0)
	{
		//slaveaddr ;
		slaveaddr = slaveaddr<<1;
		reg = channel |ICE_SEL_I2C_START;
		spi_out(port,reg,slaveaddr,SEL_I2C);
		//speed;
		reg = channel |ICE_SEL_I2C_SPEED|ICE_SEL_I2C_TRANS;
		spi_out(port,reg,speed,SEL_I2C);
		//len;
		reg = channel |ICE_SEL_I2C_FIFO |ICE_SEL_I2C_TRANS;
		spi_out(port,reg,len,SEL_I2C);
		reg = channel  |ICE_SEL_I2C_TRANS;
		//data;
		for(i = 0 ;i < len;i++)
		{
			if(i == len-1)
				reg = channel  |ICE_SEL_I2C_STOP;
			spi_out(port,reg,pmsg->buf[i],SEL_I2C);
		}		
		
	}
	//slaveaddr
	slaveaddr = slaveaddr|ICE_I2C_SLAVE_READ;
	if(pmsg->read_type == 0)
		reg = channel |ICE_SEL_I2C_RESTART;
	else
		reg = channel |ICE_SEL_I2C_START;
	spi_out(port,reg,slaveaddr,SEL_I2C);
	//speed;
	reg = channel |ICE_SEL_I2C_SPEED|ICE_SEL_I2C_TRANS;
	spi_out(port,reg,speed,SEL_I2C);
	//len;
	reg = channel |ICE_SEL_I2C_FIFO |ICE_SEL_I2C_TRANS;
	spi_out(port,reg,len,SEL_I2C);
	
	i=50;
	while(i--)
	{		
		if(port->i2c.interrupt == INT_I2C_READ_ACK)
		{						
			for(i = 0;i<len;i++)
			{
				result = spi_in(port,reg,SEL_I2C);
				pmsg->buf[i] = result & 0xFF;				
			}
			spin_lock(&port->i2c.i2c_lock);
			port->i2c.interrupt &= INT_I2C_READ_MASK;
			spin_unlock(&port->i2c.i2c_lock);
			break;
		}		
	}
	for(i = 0;i<len;i++)
		DBG("pmsg->buf[%d] = 0x%x \n",i,pmsg->buf[i]);	
	return pmsg->len;
}
int spi_i2c_writebuf(struct spi_fpga_port *port ,struct i2c_msg *pmsg)
{
	
	unsigned int reg ;
	unsigned int len,i;
	unsigned int slaveaddr;
	unsigned int speed;
	unsigned int channel = 0;
	
	slaveaddr = pmsg->addr;
	len = pmsg->len;	
	speed = spi_i2c_select_speed(pmsg->scl_rate);
	
	if(pmsg->channel == I2C_CH2)
		channel = ICE_SEL_I2C_CH2;
	else if(pmsg->channel == I2C_CH3)
		channel = ICE_SEL_I2C_CH3;
	else
	{
		printk("Error: try to write the error i2c channel\n");
		return 0;
	}

	//slaveaddr ;
	slaveaddr = slaveaddr<<1;
	reg = channel |ICE_SEL_I2C_START;
	spi_out(port,reg,slaveaddr,SEL_I2C);
	//speed;
	reg = channel |ICE_SEL_I2C_SPEED|ICE_SEL_I2C_TRANS;
	spi_out(port,reg,speed,SEL_I2C);
	//len;
	reg = channel |ICE_SEL_I2C_FIFO |ICE_SEL_I2C_TRANS;
	spi_out(port,reg,len,SEL_I2C);
	reg = channel  |ICE_SEL_I2C_TRANS;
	//data;
	for(i = 0 ;i < len;i++)
	{
		if(i == len-1)
			reg = channel|ICE_SEL_I2C_STOP;
		spi_out(port,reg,pmsg->buf[i],SEL_I2C);
	}
	
	i = 50;
	while(i--)
	{		
		if(port->i2c.interrupt  == INT_I2C_WRITE_ACK)
		{		
			spin_lock(&port->i2c.i2c_lock);
			port->i2c.interrupt &= INT_I2C_WRITE_MASK;
			spin_unlock(&port->i2c.i2c_lock);
			break;
		}			
	}
	DBG("wait num= %d,port->i2c.interrupt = 0x%x\n",i,port->i2c.interrupt);
	return pmsg->len;
	
	
}
#if defined(CONFIG_SPI_I2C_DEBUG)
unsigned short rda5400[][2] = 
{
{0x3f,0x0000},//page 0
{0x0B,0x3200},// pll_cal_eachtime
{0x0E,0x5200},// rxfilter_op_cal_bit_dr rxfilter_sys_cal_bit_dr
{0x13,0x016D},// fts_cap=01, for high nak rate at high temperature¡£
{0x16,0x9C23},// Load8.5pf  crystal,2.2pf cap
{0x17,0xBB12},// xtal_cpuclk_en,*
{0x19,0xEE18},// rxfilter_bp_mode rxfilter_tuning_cap_for die
{0x1A,0x59EE},// rmx_lo_reg=1011, tmx_iqswap
{0x1C,0x008F},//
{0x30,0x002B},//
{0x3B,0x33EA},// rxfilter_imgrej_lo
{0x3E,0x0040},// tmx_lo_reg set to1
{0x3f,0x0001},//page 1
{0x02,0x0001},// rxfilter_sys_cal_polarity
{0x04,0xE41E},// ldo_ictrl<5bit> set to 1
{0x05,0xBC00},//  ldo_ictrl<5bit> set to 1
{0x06,0x262D},// 
{0x0B,0x001F},// vco_bit=111
{0x10,0x0100},// thermo power setting
{0x13,0x001C},// pre_sca=1100
{0x19,0x001C},// lo_buff=1100, improve RF per formance at high temp
{0x1a,0x1404},
{0x1E,0x2A48},// resetn_ex_selfgen_enable=0, for 32K is no need when poweron
{0x27,0x0070},// power table setting, maxpower
{0x26,0x3254},// power table setting
{0x25,0x2180},// power table settings
{0x24,0x0000},// power table setting
{0x23,0x0000},// power table setting
{0x22,0x0000},// power table setting
{0x21,0x0000},// power table setting
{0x20,0x0000},// power table setting
{0x37,0x0600},// padrv_cal_bypass
{0x3A,0x06E0},// dcdc setting
{0x3f,0x0000},//page 0
};

int spi_i2c_16bit_test(struct spi_fpga_port *port)
{
	u8 i2c_buf[8];
	int len = 2;
	int i ;
	struct i2c_msg msg[1] = 
	{
		{0x16,0,len+2,i2c_buf,200,3,0}
	};
	
	for(i = 0;i < (sizeof(rda5400)/sizeof(rda5400[0]));i++)
	{	
		i2c_buf[0] = 0x22;
		i2c_buf[1] = rda5400[i][0];
		i2c_buf[1] = rda5400[i][1]>>8;
		i2c_buf[2] = rda5400[i][1]&0xFF;
		spi_i2c_writebuf(port, msg);
		msg[0].len = 2;
		spi_i2c_readbuf(port, msg);
	}
	return 0;
	
}

int spi_i2c_8bit_test(struct spi_fpga_port *port)
{
	u8 i2c_buf[8];
	int len = 2;
	int i ;
	struct i2c_msg msg[1] = 
	{
		{0x16,0,len+1,i2c_buf,200,2,0}
	};
	
	for(i = 0;i < (sizeof(rda5400)/sizeof(rda5400[0]));i++)
	{	
		i2c_buf[0] = rda5400[i][0];
		i2c_buf[1] = rda5400[i][1]>>8;
		i2c_buf[2] = rda5400[i][1]&0xFF;
		spi_i2c_writebuf(port, msg);
		msg[0].len = 1;
		spi_i2c_readbuf(port, msg);
	}
	return 0;
	
}
 int spi_i2c_test(void )
{
	struct spi_fpga_port *port = pFpgaPort;
	printk("IN::************spi_i2c_test********\r\n");	
	spi_i2c_8bit_test(port);
	spi_i2c_16bit_test(port);
	
	printk("OUT::************spi_i2c_test********\r\n");
	return 0;
	
}

#endif


 int spi_i2c_xfer(struct i2c_adapter *adapter, struct i2c_msg *pmsg, int num)
{
	
	struct spi_fpga_port *port = pFpgaPort;
	
	printk("%s:line=%d,channel = %d\n",__FUNCTION__,__LINE__,adapter->nr);
	if(pmsg->len > MAXMSGLEN)
		return 0;
	if(pmsg->flags)	
		spi_i2c_readbuf(port,pmsg);
	else
		spi_i2c_writebuf(port,pmsg);

	return pmsg->len;	

	return 0;
}

static unsigned int spi_i2c_func(struct i2c_adapter *adapter)
{
	return (I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL);
}
			
static const struct i2c_algorithm spi_i2c_algorithm = {
	.master_xfer		= spi_i2c_xfer,
	.functionality		= spi_i2c_func,
};

static int spi_i2c_probe(struct platform_device *pdev)
{
	int ret;
	struct spi_i2c_data *i2c;
	struct rk2818_i2c_platform_data *pdata;
	DBG("Enter::%s,LINE=%d************************\n",__FUNCTION__,__LINE__);
	pdata = pdev->dev.platform_data;
	if(!pdata)
	{
		dev_err(&pdev->dev,"no platform data\n");
		return -EINVAL;	
	}
	i2c = kzalloc(sizeof(struct spi_i2c_data),GFP_KERNEL);
	if(!i2c)
	{
		dev_err(&pdev->dev,"no memory for state\n");
		return -ENOMEM;		
	}
	strlcpy(i2c->adapter.name,DRV_NAME,sizeof(i2c->adapter.name));
	i2c->adapter.owner = THIS_MODULE;
	i2c->adapter.algo  = &spi_i2c_algorithm;
	i2c->adapter.class = I2C_CLASS_HWMON;	
	
	i2c->dev = &pdev->dev;
	i2c->adapter.algo_data = i2c;
	i2c->adapter.dev.parent = &pdev->dev;
	i2c->adapter.nr = pdata->bus_num;
	ret = i2c_add_numbered_adapter(&i2c->adapter);
	if(ret < 0){
		dev_err(&pdev->dev,"fail to add bus to i2c core fpga\n");
		kfree(i2c);
		return ret;
	}
	platform_set_drvdata(pdev,i2c);
	printk("Enter::%s,LINE=%d i2c->adap.nr = %d ************************\n",__FUNCTION__,__LINE__,i2c->adapter.nr);
	#if defined(CONFIG_SPI_I2C_DEBUG)
	
	#endif
	return 0;	
}

static int spi_i2c_remove(struct platform_device *pdev)
{
	return 0;	
}

static struct platform_driver spi_i2c_driver = {
	.probe   = spi_i2c_probe,
	.remove  = spi_i2c_remove,	
	.driver  = {
		.owner = THIS_MODULE,
		.name  = DRV_NAME,
	},
};

static int __init spi_i2c_adap_init(void)
{
	printk(" *************Enter::%s,LINE=%d ************\n",__FUNCTION__,__LINE__);
	return platform_driver_register(&spi_i2c_driver);	
}
static void __exit spi_i2c_adap_exit(void)
{
	platform_driver_unregister(&spi_i2c_driver);	
}

subsys_initcall(spi_i2c_adap_init);
module_exit(spi_i2c_adap_exit);

MODULE_DESCRIPTION("Driver for spi2i2c.");
MODULE_AUTHOR("swj <swj@rock-chips.com>");
MODULE_LICENSE("GPL");


