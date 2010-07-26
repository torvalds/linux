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

#include <mach/spi_fpga.h>

#if defined(CONFIG_SPI_I2C_DEBUG)
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

#define MAXMSGLEN   8
#define DRV_NAME    "fpga_i2c"
#define SPI_I2C_TEST 0
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
	
	if(channel == I2C_CH2)
		reg =  ICE_SEL_I2C_INT|ICE_SEL_I2C_CH2;
	else
		reg =  ICE_SEL_I2C_INT|ICE_SEL_I2C_CH3;
	
	port->i2c.interrupt = 0;
	ret = spi_in(port,reg,SEL_I2C);
	DBG("Enter::%s,LINE=%d ret = [%d]\n",__FUNCTION__,__LINE__,ret);
	if(INT_I2C_READ_ACK == (ret & 0x07))
		port->i2c.interrupt = INT_I2C_READ_ACK;		
	else if(INT_I2C_READ_NACK ==(ret & 0x07))
	{
		printk("Error::read no ack!!check the I2C slave device ret=%d \n",ret);
	}
	else if(INT_I2C_WRITE_ACK == (ret & 0x07))
		port->i2c.interrupt = INT_I2C_WRITE_ACK;
	else if(INT_I2C_WRITE_NACK == (ret & 0x07))
	{
		printk("Error::write no ack!!check the I2C slave device ret=%d \n",ret);
	}
	else
		printk("Error:ack value error!!check the I2C slave device ret=%d \n",ret);
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

int spi_i2c_readbuf(struct spi_fpga_port *port ,struct i2c_msg *pmsg,int ch)
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
	
	if(ch == I2C_CH2)
		channel = ICE_SEL_I2C_CH2;
	else if(ch == I2C_CH3)
		channel = ICE_SEL_I2C_CH3;
	else
	{
		printk("Error:try to read form error i2c channel\n");
		return 0;
	}

	//printk("len = %d chan = %d read=%d,reg=%d\n",pmsg->len,ch,pmsg->read_type,pmsg->reg_type);
	
	if(pmsg->read_type == I2C_NORMAL)
	{
		//slaveaddr ;
		slaveaddr = slaveaddr<<1;
		reg = channel |ICE_SEL_I2C_START;
		spi_out(port,reg,slaveaddr,SEL_I2C);
		//speed;
		reg = channel |ICE_SEL_I2C_SPEED|ICE_SEL_I2C_TRANS;
		spi_out(port,reg,speed,SEL_I2C);
		//len;&&data
		reg = channel |ICE_SEL_I2C_FIFO |ICE_SEL_I2C_TRANS;
		if(pmsg->reg_type == I2C_8_BIT)
		{				
			spi_out(port,reg,1,SEL_I2C);
			reg = channel  |ICE_SEL_I2C_TRANS;
			spi_out(port,reg,pmsg->buf[0],SEL_I2C);
		}
		else if(pmsg->reg_type == I2C_16_BIT)
		{
			spi_out(port,reg,2,SEL_I2C);
			reg = channel  |ICE_SEL_I2C_TRANS;
			spi_out(port,reg,pmsg->buf[0],SEL_I2C);
			spi_out(port,reg,pmsg->buf[1],SEL_I2C);
		}
	}

	//handle irq after send stop cmd





	
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
	reg = channel |ICE_SEL_I2C_FIFO |ICE_SEL_I2C_STOP;
	spi_out(port,reg,len,SEL_I2C);
	
	msleep(100);	
	if(port->i2c.interrupt == INT_I2C_READ_ACK)
		{						
			//printk("%s:line=%d\n",__FUNCTION__,__LINE__);
			for(i = 0;i<len;i++)
			{
				result = spi_in(port,channel,SEL_I2C);
				pmsg->buf[i] = 0;
				pmsg->buf[i] = result & 0xff ;
			}
			spin_lock(&port->i2c.i2c_lock);
			port->i2c.interrupt &= INT_I2C_READ_MASK;
			spin_unlock(&port->i2c.i2c_lock);
		}	
	//for(i = 0;i<len;i++)
		//printk("pmsg->buf[%d] = 0x%x \n",i,pmsg->buf[i]);	
	return pmsg->len;
	
}

int spi_i2c_writebuf(struct spi_fpga_port *port ,struct i2c_msg *pmsg,int ch)
{
	
	unsigned int reg ;
	unsigned int len,i;
	unsigned int slaveaddr;
	unsigned int speed;
	unsigned int channel = 0;
	
	slaveaddr = pmsg->addr;
	len = pmsg->len;	
	speed = spi_i2c_select_speed(pmsg->scl_rate);
	
	if(ch == I2C_CH2)
		channel = ICE_SEL_I2C_CH2;
	else if(ch == I2C_CH3)
		channel = ICE_SEL_I2C_CH3;
	else
	{
		printk("Error: try to write the error i2c channel\n");
		return 0;
	}
	DBG("len = %d ch = %d\n",pmsg->len,ch);
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
	msleep(25);
	i = 50;
	while(i--)
	{		
		if(port->i2c.interrupt  == INT_I2C_WRITE_ACK)
		{		
			//printk("wait num= %d,port->i2c.interrupt = 0x%x\n",i,port->i2c.interrupt);
			spin_lock(&port->i2c.i2c_lock);
			port->i2c.interrupt &= INT_I2C_WRITE_MASK;
			spin_unlock(&port->i2c.i2c_lock);
			break;
		}			
	}
	
	return pmsg->len;
	
	
}


#if SPI_I2C_TEST
unsigned short rda5400[][2] = 
{
{0x3f,0x0000},//page 0
{0x0B,0x3200},// pll_cal_eachtime
{0x0E,0x5200},// rxfilter_op_cal_bit_dr rxfilter_sys_cal_bit_dr
{0x13,0x016D},// fts_cap=01, for high nak rate at high temperature。
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
		{0x16,0,len+2,i2c_buf,200,0,0}
	};
	
	for(i = 0;i < (sizeof(rda5400)/sizeof(rda5400[0]));i++)
	{	
		i2c_buf[0] = 0x22;
		i2c_buf[1] = rda5400[i][0];
		i2c_buf[2] = rda5400[i][1]>>8;
		i2c_buf[3] = rda5400[i][1]&0xFF;
		printk("i = %d\n",i);	
		spi_i2c_writebuf(port, msg,3);
		msg[0].len = 2;
		
		spi_i2c_readbuf(port, msg,3);
		if(msg->buf[0] != i2c_buf[2] ||msg->buf[1] != i2c_buf[3]  )
			printk("i=%d,msg[0]=%d,msg[1]=%d\n",i,msg->buf[0],msg->buf[1]);
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
		{0x16,0,len+1,i2c_buf,200,0,0}
	};
	
	for(i = 0;i < (sizeof(rda5400)/sizeof(rda5400[0]));i++)
	{	
		printk("i=%d\n",i);
		msg[0].len = 3;
		i2c_buf[0] = rda5400[i][0];
		i2c_buf[1] = rda5400[i][1]>>8;
		i2c_buf[2] = rda5400[i][1]&0xFF;
		spi_i2c_writebuf(port, msg,3);
		msg[0].len = 2;
		i2c_buf[1] = 0;
		i2c_buf[2] = 0;
		spi_i2c_readbuf(port, msg,3);
		if(msg->buf[0] !=  (rda5400[i][1]>>8) ||msg->buf[1] != (rda5400[i][1]&0xff)  )
			printk("i=%d,msg[0]=0x%x,msg[1]=0x%x\n",i,msg->buf[0],msg->buf[1]);
	}
	return 0;
	
}

int spi_i2c_test(void)
{
	struct spi_fpga_port *port = pFpgaPort;
	spi_i2c_8bit_test(port);
	return 0;
}

//EXPORT_SYMBOL(spi_i2c_test);
void spi_i2c_work_handler(struct work_struct *work)
{
	struct spi_fpga_port *port =
		container_of(work, struct spi_fpga_port, i2c.spi_i2c_work);
	
	printk("*************test spi_i2c now***************\n");		
	spi_i2c_8bit_test(port);

}

static void spi_testi2c_timer(unsigned long data)
{
	struct spi_fpga_port *port = (struct spi_fpga_port *)data;
	port->i2c.i2c_timer.expires  = jiffies + msecs_to_jiffies(2000);
	add_timer(&port->i2c.i2c_timer);
	queue_work(port->i2c.spi_i2c_workqueue, &port->i2c.spi_i2c_work);
}
#define BT_RST_PIN	SPI_GPIO_P1_07
#define BT_PWR_PIN	SPI_GPIO_P1_06
int spi_i2c_set_bt_power(void)
{
#if 1

	spi_gpio_set_pinlevel(BT_RST_PIN, SPI_GPIO_HIGH);
	spi_gpio_set_pindirection(BT_RST_PIN, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(BT_PWR_PIN, SPI_GPIO_HIGH);
	spi_gpio_set_pindirection(BT_PWR_PIN, SPI_GPIO_OUT);

#else
	spi_gpio_set_pinlevel(BT_PWR_PIN, SPI_GPIO_LOW);	
	spi_gpio_set_pindirection(BT_PWR_PIN, SPI_GPIO_OUT);
	mdelay(2);
	spi_gpio_set_pinlevel(BT_PWR_PIN, SPI_GPIO_HIGH);	
	spi_gpio_set_pindirection(BT_PWR_PIN, SPI_GPIO_OUT);
	
	mdelay(2);
	spi_gpio_set_pinlevel(BT_RST_PIN, SPI_GPIO_LOW);
	spi_gpio_set_pindirection(BT_RST_PIN, SPI_GPIO_OUT);
	mdelay(20);
	/*等待10ms以上，等待26M XTAL稳定，然后拉高RESETN*/	
	spi_gpio_set_pinlevel(BT_RST_PIN, SPI_GPIO_HIGH);
#endif
	return 0;
}
#endif
#if 0
int spi_i2c_register(struct spi_fpga_port *port,int num)
{	

	spin_lock_init(&port->i2c.i2c_lock);
	return 0;
}
#endif

int spi_i2c_unregister(struct spi_fpga_port *port)
{
	return 0;
}

int spi_i2c_xfer(struct i2c_adapter *adapter, struct i2c_msg *pmsg, int num)
{
	//struct spi_fpga_port *port1 = pFpgaPort;	
	struct spi_fpga_port *port = adapter->algo_data;
	
	DBG("%s:line=%d,channel = %d\n",__FUNCTION__,__LINE__,adapter->nr);
	if(pmsg->len > MAXMSGLEN)
		return 0;
	if(adapter->nr != I2C_CH2 && adapter->nr != I2C_CH3)
		return 0;
	if(pmsg->flags)	
		spi_i2c_readbuf(port,pmsg,adapter->nr);
		//spi_i2c_readbuf(port,pmsg,adapter->nr,num);
	else
		spi_i2c_writebuf(port,pmsg,adapter->nr);

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
#if 1
int spi_i2c_register(struct spi_fpga_port *port,int num)
{
	int ret;
	struct i2c_adapter *adapter;
	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);
	//spi_i2c_add_bus(port);
	adapter = kzalloc(sizeof(struct i2c_adapter),GFP_KERNEL);
	if(adapter == NULL)
		return -ENOMEM;
	sprintf(adapter->name,"spi_i2c");
	adapter->algo = &spi_i2c_algorithm;
	adapter->class = I2C_CLASS_HWMON;
	adapter->nr = num;
	adapter->algo_data = port;
	ret = i2c_add_numbered_adapter(adapter);
	if(ret)
	{
		printk(KERN_INFO "SPI2I2C: Failed to add bus\n");
		kfree(adapter);
		
		return ret;
	}

#if SPI_I2C_TEST
	char b[20];
	if(num != 3)
		return 0;
	sprintf(b, "spi_i2c_workqueue");
	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);
	port->i2c.spi_i2c_workqueue = create_freezeable_workqueue(b);
	if (!port->i2c.spi_i2c_workqueue) {
		printk("cannot create workqueue\n");
		return -EBUSY;
	}

	INIT_WORK(&port->i2c.spi_i2c_work, spi_i2c_work_handler);

	setup_timer(&port->i2c.i2c_timer, spi_testi2c_timer, (unsigned long)port);
	port->i2c.i2c_timer.expires  = jiffies+2000;//>1000ms	
	add_timer(&port->i2c.i2c_timer);
	printk("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);	
	
#endif

	return 0;
}

#endif





MODULE_DESCRIPTION("Driver for spi2i2c.");
MODULE_AUTHOR("swj <swj@rock-chips.com>");
MODULE_LICENSE("GPL");


