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
#include <linux/module.h>
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
#include <mach/rk2818_iomap.h>

#include <mach/spi_fpga.h>

#if defined(CONFIG_SPI_GPIO_DEBUG)
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

#define SPI_GPIO_TEST 0
#define HIGH_SPI_TEST 1
spinlock_t		gpio_lock;
spinlock_t		gpio_state_lock;
spinlock_t		gpio_irq_lock;
static unsigned short int gGpio0State = 0;		
#define SPI_GPIO_IRQ_NUM 16
static SPI_GPIO_PDATA g_spiGpioVectorTable[SPI_GPIO_IRQ_NUM] = \
{{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}; 


static void spi_gpio_write_reg(int reg, int PinNum, int set)
{
	unsigned int old_set;
	unsigned int new_set;
	struct spi_fpga_port *port = pFpgaPort;
	PinNum = PinNum % 16;
	
	old_set= spi_in(port, reg, SEL_GPIO);

	if(1 == set)
	new_set = old_set | (1 << PinNum );	
	else
	new_set = old_set & (~(1 << PinNum ));
	spi_out(port, reg, new_set, SEL_GPIO);	
}

static int spi_gpio_read_reg(int reg)
{
	int ret = 0;
	struct spi_fpga_port *port = pFpgaPort;
	
	ret = spi_in(port, reg, SEL_GPIO);

	return ret;	
}


static int get_gpio_addr(eSpiGpioPinNum_t PinNum)
{
	int gpio = PinNum / 16;
	int reg = -1;
	switch(gpio)
	{
		case 0:
		reg = ICE_SEL_GPIO0;
		break;
		case 1:
		reg = ICE_SEL_GPIO1;
		break;
		case 2:
		reg = ICE_SEL_GPIO2;
		break;
		case 3:
		reg = ICE_SEL_GPIO3;
		break;
		case 4:
		reg = ICE_SEL_GPIO4;
		break;
		case 5:
		reg = ICE_SEL_GPIO5;
		break;
		default:
		break;

	}

	return reg;

}


int spi_gpio_int_sel(eSpiGpioPinNum_t PinNum,eSpiGpioTypeSel_t type)
{
	int reg = get_gpio_addr(PinNum);
	//struct spi_fpga_port *port = pFpgaPort;
	
	if(ICE_SEL_GPIO0 == reg)
	{
		reg |= ICE_SEL_GPIO0_TYPE;
		spin_lock(&gpio_state_lock);
		if(SPI_GPIO0_IS_INT == type)
			gGpio0State |= (1 << PinNum );
		else
			gGpio0State &= (~(1 << PinNum ));
		spin_unlock(&gpio_state_lock);
		DBG("%s,PinNum=%d,GPIO[%d]:type=%d\n",__FUNCTION__,PinNum,PinNum/16,type);
		
		spi_gpio_write_reg(reg, PinNum, type);
		return 0;
	}
	else
	{
		printk("%s:error\n",__FUNCTION__);
		return -1;
	}
	
}

	
int spi_gpio_set_pindirection(eSpiGpioPinNum_t PinNum,eSpiGpioPinDirection_t direction)
{
	int reg = get_gpio_addr(PinNum);
	//struct spi_fpga_port *port = pFpgaPort;
	int state;
	spin_lock(&gpio_state_lock);
	state = gGpio0State;
	spin_unlock(&gpio_state_lock);
	
	if(reg == -1)
	{
		printk("%s:error\n",__FUNCTION__);
		return -1;
	}

	if(ICE_SEL_GPIO0 == reg)
	{
		reg |= ICE_SEL_GPIO0_DIR;
		if((state & (1 << PinNum )) != 0)
		{
			printk("Fail to set direction because it is int pin!\n");
			return -1;
		}
		DBG("%s,PinNum=%d,direction=%d,GPIO[%d]:PinNum/16=%d\n",__FUNCTION__,PinNum,direction,PinNum/16,PinNum%16);		
		spi_gpio_write_reg(reg, PinNum, direction);	
	}
	else
	{
		reg |= ICE_SEL_GPIO_DIR;
		DBG("%s,PinNum=%d,direction=%d,GPIO[%d]:PinNum/16=%d\n",__FUNCTION__,PinNum,direction,PinNum/16,PinNum%16);	
		spi_gpio_write_reg(reg, PinNum, direction);	
	}
	return 0;
}

eSpiGpioPinDirection_t spi_gpio_get_pindirection(eSpiGpioPinNum_t PinNum)
{
	int ret = 0;
	int reg = get_gpio_addr(PinNum);
	int dir = 0;
	//struct spi_fpga_port *port = pFpgaPort;
	int state;
	spin_lock(&gpio_state_lock);
	state = gGpio0State;
	spin_unlock(&gpio_state_lock);
	
	if(reg == -1)
	{
		printk("%s:error\n",__FUNCTION__);
		return SPI_GPIO_DIR_ERR;
	}

	if(ICE_SEL_GPIO0 == reg)
	{
		reg |= ICE_SEL_GPIO0_DIR;
		if((state & (1 << PinNum )) != 0)
		{
			printk("Fail to get pindirection because it is int pin!\n");
			return SPI_GPIO_DIR_ERR;
		}
		ret = spi_gpio_read_reg(reg);	
	}
	else
	{
		reg |= ICE_SEL_GPIO_DIR;	
		ret = spi_gpio_read_reg(reg);	
	}

	if((ret & (1 << (PinNum%16) )) == 0)
		dir  = SPI_GPIO_IN;
	else
		dir  = SPI_GPIO_OUT;

	DBG("%s,PinNum=%d,ret=0x%x,GPIO[%d]:PinNum/16=%d,pindirection=%d\n\n",__FUNCTION__,PinNum,ret,PinNum/16,PinNum%16,dir);

	return dir;
	
}


int spi_gpio_set_pinlevel(eSpiGpioPinNum_t PinNum, eSpiGpioPinLevel_t PinLevel)
{
	int reg = get_gpio_addr(PinNum);
	//struct spi_fpga_port *port = pFpgaPort;
	int state;
	spin_lock(&gpio_state_lock);
	state = gGpio0State;
	spin_unlock(&gpio_state_lock);
	
	if(reg == -1)
	{
		printk("%s:error\n",__FUNCTION__);
		return -1;
	}
	
	if(ICE_SEL_GPIO0 == reg)
	{
		reg |= ICE_SEL_GPIO0_DATA;
		if((state & (1 << PinNum )) != 0)
		{
			printk("Fail to set PinLevel because PinNum=%d is int pin!\n",PinNum);
			return -1;
		}
		DBG("%s,PinNum=%d,GPIO[%d]:PinNum/16=%d,PinLevel=%d\n",__FUNCTION__,PinNum,PinNum/16,PinNum%16,PinLevel);	
		spi_gpio_write_reg(reg, PinNum, PinLevel);	
		
	}
	else
	{
		reg |= ICE_SEL_GPIO_DATA;
		DBG("%s,PinNum=%d,GPIO[%d]:PinNum/16=%d,PinLevel=%d\n",__FUNCTION__,PinNum,PinNum/16,PinNum%16,PinLevel);	
		spi_gpio_write_reg(reg, PinNum, PinLevel);	
	}

	return 0;
	
}


eSpiGpioPinLevel_t spi_gpio_get_pinlevel(eSpiGpioPinNum_t PinNum)
{
	int ret = 0;
	int reg = get_gpio_addr(PinNum);
	int level = 0;
	//struct spi_fpga_port *port = pFpgaPort;
	int state;
	spin_lock(&gpio_state_lock);
	state = gGpio0State;
	spin_unlock(&gpio_state_lock);
	
	if(reg == -1)
	{
		printk("%s:error\n",__FUNCTION__);
		return SPI_GPIO_LEVEL_ERR;
	}

	if(ICE_SEL_GPIO0 == reg)
	{
		reg |= ICE_SEL_GPIO0_DATA;
		if((state & (1 << PinNum )) != 0)
		{
			printk("Fail to get PinLevel because it is int pin!\n");
			return SPI_GPIO_LEVEL_ERR;
		}	
		ret = spi_gpio_read_reg(reg);	
	}
	else
	{
		reg |= ICE_SEL_GPIO_DATA;	
		ret = spi_gpio_read_reg(reg);	
	}
	
	if((ret & (1 << (PinNum%16) )) == 0)
		level = SPI_GPIO_LOW;
	else
		level = SPI_GPIO_HIGH;

	DBG("%s,PinNum=%d,ret=0x%x,GPIO[%d]:PinNum/16=%d,PinLevel=%d\n\n",__FUNCTION__,PinNum,ret,PinNum/16,PinNum%16,level);

	return level;
	
}


int spi_gpio_enable_int(eSpiGpioPinNum_t PinNum)
{
	int reg = get_gpio_addr(PinNum);
	//struct spi_fpga_port *port = pFpgaPort;
	int state;
	spin_lock(&gpio_state_lock);
	state = gGpio0State;
	spin_unlock(&gpio_state_lock);
	
	if(ICE_SEL_GPIO0 == reg)
	{
		reg |= ICE_SEL_GPIO0_INT_EN;
		if((state & (1 << PinNum )) == 0)
		{
			printk("Fail to enable int because it is gpio pin!\n");
			return -1;
		}
		DBG("%s,PinNum=%d,IntEn=%d\n",__FUNCTION__,PinNum,SPI_GPIO_INT_ENABLE);		
		spi_gpio_write_reg(reg, PinNum, SPI_GPIO_INT_ENABLE);		
	}
	else
	{
		printk("%s:error\n",__FUNCTION__);
		return -1;
	}

	return 0;
}


int spi_gpio_disable_int(eSpiGpioPinNum_t PinNum)
{
	int reg = get_gpio_addr(PinNum);
	//struct spi_fpga_port *port = pFpgaPort;
	int state;
	spin_lock(&gpio_state_lock);
	state = gGpio0State;
	spin_unlock(&gpio_state_lock);
	
	if(ICE_SEL_GPIO0 == reg)
	{
		reg |= ICE_SEL_GPIO0_INT_EN;

		if((state & (1 << PinNum )) == 0)
		{
			printk("Fail to enable int because it is gpio pin!\n");
			return -1;
		}
	
		DBG("%s,PinNum=%d,IntEn=%d\n",__FUNCTION__,PinNum,SPI_GPIO_INT_DISABLE);	
		spi_gpio_write_reg(reg, PinNum, SPI_GPIO_INT_DISABLE);		
	}
	else
	{
		printk("%s:error\n",__FUNCTION__);
		return -1;
	}

	return 0;
}


int spi_gpio_set_int_trigger(eSpiGpioPinNum_t PinNum,eSpiGpioIntType_t IntType)
{
	int reg = get_gpio_addr(PinNum);
	//struct spi_fpga_port *port = pFpgaPort;
	int state;
	spin_lock(&gpio_state_lock);
	state = gGpio0State;
	spin_unlock(&gpio_state_lock);
	
	if(ICE_SEL_GPIO0 == reg)
	{
		reg |= ICE_SEL_GPIO0_INT_TRI;

		if((state & (1 << PinNum )) == 0)
		{
			printk("Fail to enable int because it is gpio pin!\n");
			return -1;
		}
	
		DBG("%s,PinNum=%d,IntType=%d\n",__FUNCTION__,PinNum,IntType);	
		
		spi_gpio_write_reg(reg, PinNum, IntType);
		
	}
	else
	{
		printk("%s:error\n",__FUNCTION__);
		return -1;
	}

	return 0;
}


int spi_gpio_read_iir(void)
{
	int reg = ICE_SEL_GPIO0 | ICE_SEL_GPIO0_INT_STATE;
	int ret = 0;

	ret = spi_gpio_read_reg(reg);
	DBG("%s,IntState=%d\n",__FUNCTION__,ret);
	return ret;
}

int spi_request_gpio_irq(eSpiGpioPinNum_t PinNum, pSpiFunc Routine, eSpiGpioIntType_t IntType,void *dev_id)
{			
	if(PinNum >= SPI_GPIO_IRQ_NUM)
	return -1;
	DBG("Enter::%s,LINE=%d,PinNum=%d\n",__FUNCTION__,__LINE__,PinNum);
	if(spi_gpio_int_sel(PinNum,SPI_GPIO0_IS_INT))
	{
		printk("%s err:fail to enable select intterupt when PinNum=%d\n",__FUNCTION__,PinNum);
		return -1;
	}
	if(spi_gpio_set_int_trigger(PinNum,IntType))
	{
		printk("%s err:fail to enable set intterrupt trigger when PinNum=%d\n",__FUNCTION__,PinNum);
		return -1;
	}
	
	if(g_spiGpioVectorTable[PinNum].gpio_vector) 
	{
		printk("%s err:fail to enable g_spiGpioVectorTable[%d] have been used\n",__FUNCTION__,PinNum);
		return -1;
	}
	spin_lock(&gpio_irq_lock);
	g_spiGpioVectorTable[PinNum].gpio_vector = (pSpiFuncIntr)Routine;
	g_spiGpioVectorTable[PinNum].gpio_devid= dev_id;
	spin_unlock(&gpio_irq_lock);
	if(spi_gpio_enable_int(PinNum))
	{
		printk("%s err:fail to enable gpio intterupt when PinNum=%d\n",__FUNCTION__,PinNum);
		return -1;
	}
	
	return 0;
}

int spi_free_gpio_irq(eSpiGpioPinNum_t PinNum)
{	
	spi_gpio_disable_int(PinNum);
	spin_lock(&gpio_irq_lock);
	g_spiGpioVectorTable[PinNum].gpio_vector = NULL;
	g_spiGpioVectorTable[PinNum].gpio_devid= NULL;
	spin_unlock(&gpio_irq_lock);

	return 0;
}
	

int spi_gpio_handle_irq(struct spi_device *spi)
{
	int gpio_iir, i;
	int state;
	spin_lock(&gpio_state_lock);
	state = gGpio0State;
	spin_unlock(&gpio_state_lock);

	gpio_iir = spi_gpio_read_iir() & 0xffff;	
	if(gpio_iir == 0xffff)
		return -1;

	DBG("gpio_iir=0x%x\n",gpio_iir);
	for(i=0; i<SPI_GPIO_IRQ_NUM; i++)
	{
		if(((gpio_iir & (1 << i)) == 0) && ((state & (1 << i)) != 0))
		{
			if(g_spiGpioVectorTable[i].gpio_vector)
			{
				spin_lock(&gpio_irq_lock);
				g_spiGpioVectorTable[i].gpio_vector(i,g_spiGpioVectorTable[i].gpio_devid);
				spin_unlock(&gpio_irq_lock);
				DBG("%s:spi_gpio_irq=%d\n",__FUNCTION__,i);
			}
		}			
	}	

	return  0;

}

#if SPI_GPIO_TEST
static irqreturn_t spi_gpio_int_test_0(int irq, void *dev)
{
	printk("%s:LINE=%d,dev=0x%x\n",__FUNCTION__,__LINE__,(int)dev);
	return 0;
}

static irqreturn_t spi_gpio_int_test_1(int irq, void *dev)
{
	printk("%s:LINE=%d,dev=0x%x\n",__FUNCTION__,__LINE__,(int)dev);
	return 0;
}

static irqreturn_t spi_gpio_int_test_2(int irq, void *dev)
{
	printk("%s:LINE=%d,dev=0x%x\n",__FUNCTION__,__LINE__,(int)dev);
	return 0;
}

static irqreturn_t spi_gpio_int_test_3(int irq, void *dev)
{
	printk("%s:LINE=%d,dev=0x%x\n",__FUNCTION__,__LINE__,(int)dev);
	return 0;
}


volatile int TestGpioPinLevel = 0;
void spi_gpio_work_handler(struct work_struct *work)
{
	//struct spi_fpga_port *port =
		//container_of(work, struct spi_fpga_port, gpio.spi_gpio_work);
	int i,ret;
	printk("*************test spi_gpio now***************\n");
	
	if(TestGpioPinLevel == 0)
		TestGpioPinLevel = 1;
	else
		TestGpioPinLevel = 0;

#if (FPGA_TYPE == ICE_CC72)
	for(i=0;i<32;i++)
	{
		spi_gpio_set_pinlevel(i, TestGpioPinLevel);
		ret = spi_gpio_get_pinlevel(i);
		if(ret != TestGpioPinLevel)
		DBG("PinNum=%d,set_pinlevel=%d,get_pinlevel=%d\n",i,TestGpioPinLevel,ret);
		//spi_gpio_set_pindirection(i, SPI_GPIO_OUT);	
	}

#elif (FPGA_TYPE == ICE_CC196)

	for(i=16;i<81;i++)
	{
		spi_gpio_set_pinlevel(i, TestGpioPinLevel);
		ret = spi_gpio_get_pinlevel(i);
		if(ret != TestGpioPinLevel)
		{
			#if SPI_FPGA_TEST_DEBUG
			spi_test_wrong_handle();
			#endif
			printk("err:PinNum=%d,set_pinlevel=%d but get_pinlevel=%d\n",i,TestGpioPinLevel,ret);	
			ret = spi_gpio_get_pindirection(i);
			printk("spi_gpio_get_pindirection=%d\n\n",ret);
		}
	}

	DBG("%s:LINE=%d\n",__FUNCTION__,__LINE__);

#endif

}

static void spi_testgpio_timer(unsigned long data)
{
	struct spi_fpga_port *port = (struct spi_fpga_port *)data;
	port->gpio.gpio_timer.expires  = jiffies + msecs_to_jiffies(2000);
	add_timer(&port->gpio.gpio_timer);
	//schedule_work(&port->gpio.spi_gpio_work);
	queue_work(port->gpio.spi_gpio_workqueue, &port->gpio.spi_gpio_work);
}

#endif

int spi_gpio_init(void)
{
	int i,ret;
	struct spi_fpga_port *port = pFpgaPort;
#if HIGH_SPI_TEST	
	printk("*************test spi communication now***************\n");
	printk("%s:LINE=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);
	for(i=0;i<0xff;i++)
	{
		spi_out(port, (ICE_SEL_GPIO1 | ICE_SEL_GPIO_DIR), i, SEL_GPIO);//all gpio is input
		ret = spi_in(port, (ICE_SEL_GPIO1 | ICE_SEL_GPIO_DIR), SEL_GPIO) & 0xffff;
		if(i != ret)
		printk("err:i=0x%x but ret=0x%x\n",i,ret);
	}

#endif

#if (FPGA_TYPE == ICE_CC72)
	for(i=0; i<16; i++)
	{
		DBG("i=%d\n\n",i);
		spi_gpio_int_sel(i,SPI_GPIO0_IS_GPIO);
		spi_gpio_set_pinlevel(i, SPI_GPIO_LOW);
		spi_gpio_set_pindirection(i, SPI_GPIO_OUT);	
	}
/*	
	for(i=6; i<16; i++)
	{
		DBG("i=%d\n\n",i);
		spi_gpio_int_sel(i,SPI_GPIO0_IS_INT);
		spi_gpio_set_int_trigger(i,SPI_GPIO_EDGE_FALLING);
		spi_gpio_disable_int(i);	
	}	
*/	
	for(i=16; i<32; i++)
	{
		spi_gpio_set_pinlevel(i, SPI_GPIO_LOW);
		spi_gpio_set_pindirection(i, SPI_GPIO_OUT);
	}
	
	//spi_out(port, (ICE_SEL_GPIO1 | ICE_SEL_GPIO_DATA), 0, SEL_GPIO);//all gpio is zero
	//spi_out(port, (ICE_SEL_GPIO1 | ICE_SEL_GPIO_DIR), 0, SEL_GPIO);//all gpio is input
	
#elif (FPGA_TYPE == ICE_CC196)

#if 0
	DBG("%s:LINE=%d\n",__FUNCTION__,__LINE__);
	spi_out(port, (ICE_SEL_GPIO0 | ICE_SEL_GPIO0_TYPE), 0x0000, SEL_GPIO);
	ret = spi_in(port, (ICE_SEL_GPIO0 | ICE_SEL_GPIO0_TYPE), SEL_GPIO) & 0xffff;
	if(ret != 0x0000)
	DBG("%s:Line=%d,set=0x0000,ret=0x%x\n",__FUNCTION__,__LINE__,ret);
	
	spi_out(port, (ICE_SEL_GPIO0 | ICE_SEL_GPIO_DATA), 0x0000, SEL_GPIO);
	ret = spi_in(port, (ICE_SEL_GPIO0 | ICE_SEL_GPIO_DATA), SEL_GPIO) & 0xffff;
	if(ret != 0x0000)
	DBG("%s:Line=%d,set=0x0000,ret=0x%x\n",__FUNCTION__,__LINE__,ret);
	
	spi_out(port, (ICE_SEL_GPIO0 | ICE_SEL_GPIO_DIR), 0xffff, SEL_GPIO);
	ret = spi_in(port, (ICE_SEL_GPIO0 | ICE_SEL_GPIO_DIR), SEL_GPIO) & 0xffff;
	if(ret != 0xffff)
	DBG("%s:Line=%d,set=0x0000,ret=0x%x\n",__FUNCTION__,__LINE__,ret);

	spi_out(port, (ICE_SEL_GPIO1 | ICE_SEL_GPIO_DATA), 0x0224, SEL_GPIO);
	ret = spi_in(port, (ICE_SEL_GPIO1 | ICE_SEL_GPIO_DATA), SEL_GPIO) & 0xffff;
	if(ret != 0x0224)
	DBG("%s:Line=%d,set=0x0000,ret=0x%x\n",__FUNCTION__,__LINE__,ret);
	
	spi_out(port, (ICE_SEL_GPIO1 | ICE_SEL_GPIO_DIR), 0xf7ef, SEL_GPIO);
	ret = spi_in(port, (ICE_SEL_GPIO1 | ICE_SEL_GPIO_DIR), SEL_GPIO) & 0xffff;
	if(ret != 0xf7ef)
	DBG("%s:Line=%d,set=0x0000,ret=0x%x\n",__FUNCTION__,__LINE__,ret);
	
	spi_out(port, (ICE_SEL_GPIO2 | ICE_SEL_GPIO_DATA), 0x2008, SEL_GPIO);
	ret = spi_in(port, (ICE_SEL_GPIO2 | ICE_SEL_GPIO_DATA), SEL_GPIO) & 0xffff;
	if(ret != 0x2008)
	DBG("%s:Line=%d,set=0x2008,ret=0x%x\n",__FUNCTION__,__LINE__,ret);
	
	spi_out(port, (ICE_SEL_GPIO2 | ICE_SEL_GPIO_DIR), 0xf378, SEL_GPIO);
	ret = spi_in(port, (ICE_SEL_GPIO1 | ICE_SEL_GPIO_DIR), SEL_GPIO) & 0xffff;
	if(ret != 0xf378)
	DBG("%s:Line=%d,set=0xf378,ret=0x%x\n",__FUNCTION__,__LINE__,ret);

	spi_out(port, (ICE_SEL_GPIO3 | ICE_SEL_GPIO_DATA), 0x0000, SEL_GPIO);
	ret = spi_in(port, (ICE_SEL_GPIO3 | ICE_SEL_GPIO_DATA), SEL_GPIO) & 0xffff;
	if(ret != 0x0000)
	DBG("%s:Line=%d,set=0x0000,ret=0x%x\n",__FUNCTION__,__LINE__,ret);
	
	spi_out(port, (ICE_SEL_GPIO3 | ICE_SEL_GPIO_DIR), 0xffff, SEL_GPIO);
	ret = spi_in(port, (ICE_SEL_GPIO3 | ICE_SEL_GPIO_DIR), SEL_GPIO) & 0xffff;
	if(ret != 0xffff)
	DBG("%s:Line=%d,set=0xffff,ret=0x%x\n",__FUNCTION__,__LINE__,ret);

	spi_out(port, (ICE_SEL_GPIO4 | ICE_SEL_GPIO_DATA), 0x0000, SEL_GPIO);
	ret = spi_in(port, (ICE_SEL_GPIO4 | ICE_SEL_GPIO_DATA), SEL_GPIO) & 0xffff;
	if(ret != 0x0000)
	DBG("%s:Line=%d,set=0x0000,ret=0x%x\n",__FUNCTION__,__LINE__,ret);
	
	spi_out(port, (ICE_SEL_GPIO4 | ICE_SEL_GPIO_DIR), 0xffbf, SEL_GPIO);
	ret = spi_in(port, (ICE_SEL_GPIO4 | ICE_SEL_GPIO_DIR), SEL_GPIO) & 0xffff;
	if(ret != 0xffbf)
	DBG("%s:Line=%d,set=0xffbf,ret=0x%x\n",__FUNCTION__,__LINE__,ret);
	
	spi_out(port, (ICE_SEL_GPIO5 | ICE_SEL_GPIO_DATA), 0x0000, SEL_GPIO);
	ret = spi_in(port, (ICE_SEL_GPIO5 | ICE_SEL_GPIO_DATA), SEL_GPIO) & 0xffff;
	if(ret != 0x0000)
	DBG("%s:Line=%d,set=0x0000,ret=0x%x\n",__FUNCTION__,__LINE__,ret);
	
	spi_out(port, (ICE_SEL_GPIO5 | ICE_SEL_GPIO_DIR), 0xffff, SEL_GPIO);
	ret = spi_in(port, (ICE_SEL_GPIO5 | ICE_SEL_GPIO_DIR), SEL_GPIO) & 0xffff;
	if(ret != 0xffff)
	DBG("%s:Line=%d,set=0xffff,ret=0x%x\n",__FUNCTION__,__LINE__,ret);

#else
	spi_gpio_set_pinlevel(SPI_GPIO_P1_00, SPI_GPIO_HIGH);		//LCD_ON output//
	spi_gpio_set_pindirection(SPI_GPIO_P1_00, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P1_01, SPI_GPIO_HIGH);		//LCD_PWR_CTRL output
	spi_gpio_set_pindirection(SPI_GPIO_P1_01, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P1_02, SPI_GPIO_HIGH);		//SD_POW_ON output
	spi_gpio_set_pindirection(SPI_GPIO_P1_02, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P1_03, SPI_GPIO_LOW);		//WL_RST_N/WIFI_EN output
	spi_gpio_set_pindirection(SPI_GPIO_P1_03, SPI_GPIO_OUT);
	
	spi_gpio_set_pindirection(SPI_GPIO_P1_04, SPI_GPIO_IN);		//HARDO,input
	spi_gpio_set_pinlevel(SPI_GPIO_P1_05, SPI_GPIO_HIGH);		//SENSOR_PWDN output
	spi_gpio_set_pindirection(SPI_GPIO_P1_05, SPI_GPIO_OUT);	
	spi_gpio_set_pinlevel(SPI_GPIO_P1_06, SPI_GPIO_LOW);		//BT_PWR_EN output
	spi_gpio_set_pindirection(SPI_GPIO_P1_06, SPI_GPIO_OUT);	
	spi_gpio_set_pinlevel(SPI_GPIO_P1_07, SPI_GPIO_LOW);		//BT_RST output
	spi_gpio_set_pindirection(SPI_GPIO_P1_07, SPI_GPIO_OUT);
	
	spi_gpio_set_pinlevel(SPI_GPIO_P1_08, SPI_GPIO_LOW);		//BT_WAKE_B output
	spi_gpio_set_pindirection(SPI_GPIO_P1_08, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P1_09, SPI_GPIO_LOW);		//LCD_DISP_ON output
	spi_gpio_set_pindirection(SPI_GPIO_P1_09, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P1_10, SPI_GPIO_LOW);		//WM_PWR_EN output
	spi_gpio_set_pindirection(SPI_GPIO_P1_10, SPI_GPIO_OUT);
	spi_gpio_set_pindirection(SPI_GPIO_P1_11, SPI_GPIO_IN);		//HARD1,input
	
	spi_gpio_set_pinlevel(SPI_GPIO_P1_12, SPI_GPIO_LOW);		//VIB_MOTO output
	spi_gpio_set_pindirection(SPI_GPIO_P1_12, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P1_13, SPI_GPIO_HIGH);		//KEYLED_EN output
	spi_gpio_set_pindirection(SPI_GPIO_P1_13, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P1_14, SPI_GPIO_LOW);		//CAM_RST output
	spi_gpio_set_pindirection(SPI_GPIO_P1_14, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P1_15, SPI_GPIO_LOW);		//WL_WAKE_B output
	spi_gpio_set_pindirection(SPI_GPIO_P1_15, SPI_GPIO_OUT);

	spi_gpio_set_pindirection(SPI_GPIO_P2_00, SPI_GPIO_IN);			//Y+YD input
	spi_gpio_set_pindirection(SPI_GPIO_P2_01, SPI_GPIO_IN);			//Y-YU input
	spi_gpio_set_pindirection(SPI_GPIO_P2_02, SPI_GPIO_IN);			//AP_TD_UNDIFED input	
	spi_gpio_set_pinlevel(SPI_GPIO_P2_03, SPI_GPIO_HIGH);		//AP_PW_EN_TD output
	spi_gpio_set_pindirection(SPI_GPIO_P2_03, SPI_GPIO_OUT);
	
	spi_gpio_set_pinlevel(SPI_GPIO_P2_04, SPI_GPIO_LOW);		//AP_RESET_TD output
	spi_gpio_set_pindirection(SPI_GPIO_P2_04, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P2_05, SPI_GPIO_LOW);		//AP_SHUTDOWN_TD_PMU output
	spi_gpio_set_pindirection(SPI_GPIO_P2_05, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P2_06, SPI_GPIO_LOW);		//AP_RESET_CMMB output
	spi_gpio_set_pindirection(SPI_GPIO_P2_06, SPI_GPIO_OUT);
	spi_gpio_set_pindirection(SPI_GPIO_P2_07, SPI_GPIO_IN);		//AP_CHECK_TD_STATUS input
	
	spi_gpio_set_pinlevel(SPI_GPIO_P2_08, SPI_GPIO_LOW);		//CHARGE_CURRENT_SEL output
	spi_gpio_set_pindirection(SPI_GPIO_P2_08, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P2_09, SPI_GPIO_LOW);		//AP_PWD_CMMB output
	spi_gpio_set_pindirection(SPI_GPIO_P2_09, SPI_GPIO_OUT);
	spi_gpio_set_pindirection(SPI_GPIO_P2_10, SPI_GPIO_IN);		//X-XL input
	spi_gpio_set_pindirection(SPI_GPIO_P2_11, SPI_GPIO_IN);		//X+XR input
	
	spi_gpio_set_pinlevel(SPI_GPIO_P2_12, SPI_GPIO_HIGH);		//LCD_RESET output//
	spi_gpio_set_pindirection(SPI_GPIO_P2_12, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P2_13, SPI_GPIO_HIGH);		//USB_PWR_EN output
	spi_gpio_set_pindirection(SPI_GPIO_P2_13, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P2_14, SPI_GPIO_LOW);		//WL_HOST_WAKE_B output
	spi_gpio_set_pindirection(SPI_GPIO_P2_14, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P2_15, SPI_GPIO_HIGH);		//TOUCH_SCREEN_RST output//
	spi_gpio_set_pindirection(SPI_GPIO_P2_15, SPI_GPIO_OUT);

	spi_gpio_set_pindirection(SPI_GPIO_P4_06, SPI_GPIO_IN);		//CHARGER_INT_END input
	spi_gpio_set_pinlevel(SPI_GPIO_P4_07, SPI_GPIO_LOW);		//CM3605_PWD output
	spi_gpio_set_pindirection(SPI_GPIO_P4_07, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P4_08, SPI_GPIO_LOW);		//CM3605_PS_SHUTDOWN
	spi_gpio_set_pindirection(SPI_GPIO_P4_08, SPI_GPIO_OUT);
	
#endif

#if SPI_GPIO_TEST

	for(i=0;i<81;i++)
	{
		if(i<4)
		{
			switch(i)
			{
				case 0:
				spi_request_gpio_irq(i, (pSpiFunc)spi_gpio_int_test_0, SPI_GPIO_EDGE_FALLING, port);
				break;
				case 1:
				spi_request_gpio_irq(i, (pSpiFunc)spi_gpio_int_test_1, SPI_GPIO_EDGE_FALLING, port);
				break;
				case 2:
				spi_request_gpio_irq(i, (pSpiFunc)spi_gpio_int_test_2, SPI_GPIO_EDGE_FALLING, port);
				break;
				case 3:
				spi_request_gpio_irq(i, (pSpiFunc)spi_gpio_int_test_3, SPI_GPIO_EDGE_FALLING, port);
				break;
				
				default:
				break;
			}
			
		}
		else
		{
			//if(i<16)
			//spi_gpio_int_sel(i,SPI_GPIO0_IS_GPIO);
			spi_gpio_set_pindirection(i, SPI_GPIO_OUT);	
			ret = spi_gpio_get_pindirection(i);
			if(ret != SPI_GPIO_OUT)
			{
				#if SPI_FPGA_TEST_DEBUG
				spi_test_wrong_handle();
				#endif
				printk("err:PinNum=%d,set_pindirection=%d but get_pindirection=%d\n",i,SPI_GPIO_OUT,ret);	
			}
		}

	}
#endif


#endif

	return 0;

}

int spi_gpio_register(struct spi_fpga_port *port)
{
#if SPI_GPIO_TEST
	char b[20];
	sprintf(b, "spi_gpio_workqueue");
	port->gpio.spi_gpio_workqueue = create_freezeable_workqueue(b);
	if (!port->gpio.spi_gpio_workqueue) {
		printk("cannot create spi_gpio workqueue\n");
		return -EBUSY;
	}
	INIT_WORK(&port->gpio.spi_gpio_work, spi_gpio_work_handler);
	setup_timer(&port->gpio.gpio_timer, spi_testgpio_timer, (unsigned long)port);
	port->gpio.gpio_timer.expires  = jiffies+2000;//>1000ms
	add_timer(&port->gpio.gpio_timer);

#endif 

	spin_lock_init(&gpio_lock);
	spin_lock_init(&gpio_state_lock);
	spin_lock_init(&gpio_irq_lock);
	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);
	return 0;
}

int spi_gpio_unregister(struct spi_fpga_port *port)
{	
	return 0;
}

MODULE_DESCRIPTION("Driver for spi2gpio.");
MODULE_AUTHOR("luowei <lw@rock-chips.com>");
MODULE_LICENSE("GPL");


