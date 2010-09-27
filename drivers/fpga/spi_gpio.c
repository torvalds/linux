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
#include <linux/irq.h>
#include <asm/gpio.h>
#include <mach/spi_fpga.h>

#if defined(CONFIG_SPI_GPIO_DEBUG)
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

#define SPI_GPIO_TEST 0
#define HIGH_SPI_TEST 1

static DEFINE_SPINLOCK(gpio_state_lock);
//static DEFINE_SPINLOCK(gpio_irq_lock);
static unsigned short int gGpio0State = 0;		
#define SPI_GPIO_IRQ_NUM 16
static SPI_GPIO_PDATA g_spiGpioVectorTable[SPI_GPIO_IRQ_NUM] = \
{{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}; 

/************FPGA 操作GPIO的函数实现************************/

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
	DBG("%s:PinNum=%d,type=%d\n",__FUNCTION__,PinNum,type);
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
		//if((state & (1 << PinNum )) != 0)
		//{
		//	printk("Fail to get PinLevel because it is int pin!\n");
		//	return SPI_GPIO_LEVEL_ERR;
		//}	
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
			printk("%s:Fail to enable int because it is gpio pin!\n",__FUNCTION__);
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
			printk("%s:Fail to enable int because it is gpio pin!\n",__FUNCTION__);
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


int spi_gpio_set_int_trigger(eSpiGpioPinNum_t PinNum,eSpiGpioIntTri_t IntTri)
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
			printk("%s:Fail to enable int because it is gpio pin!\n",__FUNCTION__);
			return -1;
		}
	
		DBG("%s,PinNum=%d,IntTri=%d\n",__FUNCTION__,PinNum,IntTri);	
		
		spi_gpio_write_reg(reg, PinNum, IntTri);
		
	}
	else
	{
		printk("%s:error\n",__FUNCTION__);
		return -1;
	}

	return 0;
}


int spi_gpio_set_int_type(eSpiGpioPinNum_t PinNum,eSpiGpioIntType_t IntType)
{
	int reg = get_gpio_addr(PinNum);
	//struct spi_fpga_port *port = pFpgaPort;
	int state;
	spin_lock(&gpio_state_lock);
	state = gGpio0State;
	spin_unlock(&gpio_state_lock);
	
	if(ICE_SEL_GPIO0 == reg)
	{
		reg |= ICE_SEL_GPIO0_INT_TYPE;

		if((state & (1 << PinNum )) == 0)
		{
			printk("%s:Fail to enable int because it is gpio pin!\n",__FUNCTION__);
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

int spi_request_gpio_irq(eSpiGpioPinNum_t PinNum, pSpiFunc Routine, eSpiGpioIntTri_t IntType,void *dev_id)
{			
#if 0
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
#endif	
	return 0;
}

int spi_free_gpio_irq(eSpiGpioPinNum_t PinNum)
{	
	spi_gpio_disable_int(PinNum);
//	spin_lock(&gpio_irq_lock);
	g_spiGpioVectorTable[PinNum].gpio_vector = NULL;
	g_spiGpioVectorTable[PinNum].gpio_devid= NULL;
//	spin_unlock(&gpio_irq_lock);

	return 0;
}
	

int spi_gpio_handle_irq(struct spi_device *spi)
{
	int gpio_iir, i;
	int state;
	int irq;
	struct irq_desc *desc;
	spin_lock(&gpio_state_lock);
	state = gGpio0State;
	spin_unlock(&gpio_state_lock);

	gpio_iir = spi_gpio_read_iir() & 0xffff;	
	if(gpio_iir == 0xffff)
		return -1;

	printk("%s:gpio_iir=%d\n",__FUNCTION__,gpio_iir);
	for(i=0; i<SPI_GPIO_IRQ_NUM; i++)
	{
		if(((gpio_iir & (1 << i)) == 0) && ((state & (1 << i)) != 0))
		{
			irq = i + SPI_FPGA_EXPANDER_BASE;
			desc = irq_to_desc(irq);
			if(desc->action->handler)
			desc->action->handler(irq,desc->action->dev_id);
			printk("%s:pin=%d,irq=%d\n",__FUNCTION__,i,irq);
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


static volatile int TestGpioPinLevel = 0;
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
	gpio_direction_output(RK2818_PIN_PE0,0);
	udelay(2);
	gpio_direction_output(RK2818_PIN_PE0,1);
	for(i=4;i<81;i++)
	{
		gpio_direction_output(SPI_FPGA_EXPANDER_BASE+i,TestGpioPinLevel);
		//ret = gpio_direction_input(GPIOS_EXPANDER_BASE+i);
		//if (ret) {
		//	printk("%s:failed to set GPIO[%d] input\n",__FUNCTION__,GPIOS_EXPANDER_BASE+i);
		//}
		udelay(1);
		ret = gpio_get_value (SPI_FPGA_EXPANDER_BASE+i);
		if(ret != TestGpioPinLevel)
		{
			#if SPI_FPGA_TEST_DEBUG
			spi_test_wrong_handle();
			#endif
			printk("err:%s:PinNum=%d,set_pinlevel=%d but get_pinlevel=%d\n",__FUNCTION__,i,TestGpioPinLevel,ret);	
		}
	}

	DBG("%s:LINE=%d\n",__FUNCTION__,__LINE__);
#endif

}

static void spi_testgpio_timer(unsigned long data)
{
	struct spi_fpga_port *port = (struct spi_fpga_port *)data;
	port->gpio.gpio_timer.expires  = jiffies + msecs_to_jiffies(3000);
	add_timer(&port->gpio.gpio_timer);
	queue_work(port->gpio.spi_gpio_workqueue, &port->gpio.spi_gpio_work);
}

#endif

int spi_gpio_init_first(void)
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

	for(i=16; i<32; i++)
	{
		spi_gpio_set_pinlevel(i, SPI_GPIO_LOW);
		spi_gpio_set_pindirection(i, SPI_GPIO_OUT);
	}
		
#elif (FPGA_TYPE == ICE_CC196)

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
	spi_gpio_set_pinlevel(SPI_GPIO_P1_10, SPI_GPIO_HIGH);		//WM_PWR_EN output
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
	
	spi_gpio_set_pinlevel(SPI_GPIO_P2_04, SPI_GPIO_HIGH);		//AP_RESET_TD output
	spi_gpio_set_pindirection(SPI_GPIO_P2_04, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P2_05, SPI_GPIO_HIGH);		//AP_SHUTDOWN_TD_PMU output
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
	
	spi_gpio_set_pinlevel(SPI_GPIO_P2_12, SPI_GPIO_LOW);		//LCD_RESET output//
	spi_gpio_set_pindirection(SPI_GPIO_P2_12, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P2_12, SPI_GPIO_HIGH);
	spi_gpio_set_pinlevel(SPI_GPIO_P2_13, SPI_GPIO_HIGH);		//USB_PWR_EN output
	spi_gpio_set_pindirection(SPI_GPIO_P2_13, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P2_14, SPI_GPIO_LOW);		//WL_HOST_WAKE_B output
	spi_gpio_set_pindirection(SPI_GPIO_P2_14, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P2_15, SPI_GPIO_HIGH);		//TOUCH_SCREEN_RST output//
	spi_gpio_set_pindirection(SPI_GPIO_P2_15, SPI_GPIO_OUT);

	//spi_gpio_set_pindirection(SPI_GPIO_P4_06, SPI_GPIO_IN);		//CHARGER_INT_END input
	spi_gpio_set_pinlevel(SPI_GPIO_P4_06, SPI_GPIO_LOW);		//CM3605_PWD low
	spi_gpio_set_pindirection(SPI_GPIO_P4_06, SPI_GPIO_OUT);		//CHARGER_INT_END output
	

	spi_gpio_set_pinlevel(SPI_GPIO_P4_07, SPI_GPIO_LOW);		//CM3605_PWD output
	spi_gpio_set_pindirection(SPI_GPIO_P4_07, SPI_GPIO_OUT);
	spi_gpio_set_pinlevel(SPI_GPIO_P4_08, SPI_GPIO_LOW);		//CM3605_PS_SHUTDOWN
	spi_gpio_set_pindirection(SPI_GPIO_P4_08, SPI_GPIO_OUT);
	
#endif

	return 0;

}

static void spi_gpio_irq_work_handler(struct work_struct *work);

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

	INIT_LIST_HEAD(&port->gpio.msg_queue);
	port->gpio.spi_gpio_irq_workqueue = create_freezeable_workqueue("spi_gpio_irq_workqueue");
	if (!port->gpio.spi_gpio_irq_workqueue) {
		printk("cannot create spi_gpio_irq workqueue\n");
		return -EBUSY;
	}
	INIT_WORK(&port->gpio.spi_gpio_irq_work, spi_gpio_irq_work_handler);

	DBG("%s:line=%d,port=0x%x\n",__FUNCTION__,__LINE__,(int)port);
	return 0;
}

int spi_gpio_unregister(struct spi_fpga_port *port)
{	
	return 0;
}

#if 1

/************抽象GPIO接口函数实现部分************************/
static int spi_gpiolib_direction_input(struct gpio_chip *chip, unsigned offset)
{
	int pinnum = offset + chip->base -SPI_FPGA_EXPANDER_BASE;
	DBG("%s:pinnum=%d\n",__FUNCTION__,pinnum);
	if(pinnum < 16)
	spi_gpio_int_sel(pinnum,SPI_GPIO0_IS_GPIO);
	return spi_gpio_set_pindirection(pinnum, SPI_GPIO_IN);
}

static int spi_gpiolib_direction_output(struct gpio_chip *chip, unsigned offset, int val)
{
	int pinnum = offset + chip->base -SPI_FPGA_EXPANDER_BASE;
	if(pinnum < 16)
	spi_gpio_int_sel(pinnum,SPI_GPIO0_IS_GPIO);
	DBG("%s:pinnum=%d\n",__FUNCTION__,pinnum);
	if(GPIO_HIGH == val)
	spi_gpio_set_pinlevel(pinnum,SPI_GPIO_HIGH);
	else
	spi_gpio_set_pinlevel(pinnum,SPI_GPIO_LOW);
	return spi_gpio_set_pindirection(pinnum, SPI_GPIO_OUT);
}

static int spi_gpiolib_get_pinlevel(struct gpio_chip *chip, unsigned int offset)
{
	int pinnum = offset + chip->base -SPI_FPGA_EXPANDER_BASE;
	DBG("%s:pinnum=%d\n",__FUNCTION__,pinnum);
	return spi_gpio_get_pinlevel(pinnum);
}

static void spi_gpiolib_set_pinlevel(struct gpio_chip *chip, unsigned int offset, int val)
{
	int pinnum = offset + chip->base -SPI_FPGA_EXPANDER_BASE;
	DBG("%s:pinnum=%d\n",__FUNCTION__,pinnum);
	if(GPIO_HIGH == val)
	spi_gpio_set_pinlevel(pinnum,SPI_GPIO_HIGH);
	else
	spi_gpio_set_pinlevel(pinnum,SPI_GPIO_LOW);
}

static int spi_gpiolib_pull_updown(struct gpio_chip *chip, unsigned offset, unsigned val)
{
	return 0;	//FPGA do not support pull up or down
}

static void spi_gpiolib_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	//to do	
}

static int spi_gpiolib_to_irq(struct gpio_chip *chip, unsigned offset)
{
	if(offset < CONFIG_EXPANDED_GPIO_IRQ_NUM)
	return offset + NR_AIC_IRQS + CONFIG_RK28_GPIO_IRQ;
	else
	return -EINVAL;
}

struct fpga_gpio_bank {
	unsigned short id;			//GPIO寄存器组的ID识别号
	unsigned long offset;		//GPIO0或GPIO1的基地址
	struct clk *clock;		/* associated clock */
};

struct fpga_gpio_chip {
	struct gpio_chip	chip;
	struct fpga_gpio_chip	*next;		/* Bank sharing same clock */
	struct fpga_gpio_bank	*bank;		/* Bank definition */
	void __iomem		*regbase;	/* Base of register bank */
};

#define SPI_GPIO_CHIP_DEF(name, base_gpio, nr_gpio)			\
	{								\
		.chip = {						\
			.label		  = name,			\
			.direction_input  = spi_gpiolib_direction_input, \
			.direction_output = spi_gpiolib_direction_output, \
			.get		  = spi_gpiolib_get_pinlevel,		\
			.set		  = spi_gpiolib_set_pinlevel,		\
			.pull_updown  = spi_gpiolib_pull_updown,         \
			.dbg_show	  = spi_gpiolib_dbg_show,	\
			.to_irq       = spi_gpiolib_to_irq,     \
			.base		  = base_gpio,			\
			.ngpio		  = nr_gpio,			\
		},							\
	}

static struct fpga_gpio_chip spi_gpio_chip[] = {
	SPI_GPIO_CHIP_DEF("PIO0", SPI_FPGA_EXPANDER_BASE+0*NUM_GROUP*2, NUM_GROUP<<1),
	SPI_GPIO_CHIP_DEF("PIO1", SPI_FPGA_EXPANDER_BASE+1*NUM_GROUP*2, NUM_GROUP<<1),
	SPI_GPIO_CHIP_DEF("PIO2", SPI_FPGA_EXPANDER_BASE+2*NUM_GROUP*2, NUM_GROUP<<1),
	SPI_GPIO_CHIP_DEF("PIO3", SPI_FPGA_EXPANDER_BASE+3*NUM_GROUP*2, NUM_GROUP<<1),
	SPI_GPIO_CHIP_DEF("PIO4", SPI_FPGA_EXPANDER_BASE+4*NUM_GROUP*2, NUM_GROUP<<1),
	SPI_GPIO_CHIP_DEF("PIO5", SPI_FPGA_EXPANDER_BASE+5*NUM_GROUP*2, NUM_GROUP<<1),
};


#define ID_SPI_GPIO_IRQ_ENABLE		1
#define ID_SPI_GPIO_IRQ_DISABLE		2
#define ID_SPI_GPIO_IRQ_SET_TYPE	3
#define ID_SPI_GPIO_IRQ_SET_WAKE	4

struct spi_gpio_irq_transfer
{
	unsigned int irq;
	unsigned int type;
	unsigned int state;
	unsigned int id;
	struct list_head	queue;
};

static void _spi_gpio_irq_enable(unsigned irq)
{
	int gpio = irq_to_gpio(irq) - SPI_FPGA_EXPANDER_BASE;
	DBG("%s:line=%d,irq=%d,gpio=%d\n",__FUNCTION__,__LINE__,irq,gpio);
	if(gpio < 16)
	spi_gpio_int_sel(gpio,SPI_GPIO0_IS_INT);
	else
	{
		printk("err:%s:pin %d don't support gpio irq!\n",__FUNCTION__,gpio);
		return;
	}
	spi_gpio_enable_int(gpio);	
}

static void _spi_gpio_irq_disable(unsigned irq)
{
	int gpio = irq_to_gpio(irq) - SPI_FPGA_EXPANDER_BASE;
	DBG("%s:line=%d,irq=%d,gpio=%d\n",__FUNCTION__,__LINE__,irq,gpio);
	if(gpio < 16)
	spi_gpio_int_sel(gpio,SPI_GPIO0_IS_INT);
	else
	{
		printk("err:%s:pin %d don't support gpio irq!\n",__FUNCTION__,gpio);
		return;
	}
	spi_gpio_disable_int(gpio);

}

static int _spi_gpio_irq_set_type(unsigned int irq, unsigned int type)
{
	int gpio = irq_to_gpio(irq) - SPI_FPGA_EXPANDER_BASE;
	int int_tri = 0, int_type = 0;
	DBG("%s:line=%d,irq=%d,type=%d,gpio=%d\n",__FUNCTION__,__LINE__,irq,type,gpio);
	if(gpio < 16)
	spi_gpio_int_sel(gpio,SPI_GPIO0_IS_INT);
	else
	{
		printk("err:%s:pin %d don't support gpio irq!\n",__FUNCTION__,gpio);
		return -1;
	}
	switch(type)
	{
		case IRQF_TRIGGER_FALLING:
			int_type = SPI_GPIO_EDGE;
			int_tri = SPI_GPIO_EDGE_FALLING;
			break;
		case IRQF_TRIGGER_RISING:
			int_type = SPI_GPIO_EDGE;
			int_tri = SPI_GPIO_EDGE_RISING;
			break;
		case (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING):
			int_type = SPI_GPIO_LEVEL;
			int_tri = SPI_GPIO_EDGE_FALLING;
			break;
		default:
			printk("err:%s:FPGA don't support this intterupt type!\n",__FUNCTION__);	
			break;
	}
	spi_gpio_set_int_type(gpio, int_type);
	spi_gpio_set_int_trigger(gpio, int_tri);
	return 0;
}

static int _spi_gpio_irq_set_wake(unsigned irq, unsigned state)
{
	//unsigned int pin = irq_to_gpio(irq);
	set_irq_wake(irq, state);
	return 0;
}

static void spi_gpio_irq_work_handler(struct work_struct *work)
{
	struct spi_fpga_port *port =
		container_of(work, struct spi_fpga_port, gpio.spi_gpio_irq_work);

	while (1) {
		struct spi_gpio_irq_transfer *t = NULL;
		unsigned long flags;
		unsigned int irq;
		unsigned int type;
		unsigned int state;
		unsigned int id;

		spin_lock_irqsave(&port->work_lock, flags);
		if (!list_empty(&port->gpio.msg_queue)) {
			t = list_first_entry(&port->gpio.msg_queue, struct spi_gpio_irq_transfer, queue);
			list_del(&t->queue);
		}
		spin_unlock_irqrestore(&port->work_lock, flags);

		if (!t)	// msg_queue empty
			break;

		irq = t->irq;
		type = t->type;
		state = t->state;
		id = t->id;
		kfree(t);

		if ((irq == 0) || (id == 0))
			continue;
		printk("%s:irq=%d,type=%d,state=%d,id=%d\n",__FUNCTION__,irq,type,state,id);
		switch (id) {
			case ID_SPI_GPIO_IRQ_ENABLE:
				_spi_gpio_irq_enable(irq);
				break;
			case ID_SPI_GPIO_IRQ_DISABLE:
				_spi_gpio_irq_disable(irq);
				break;
			case ID_SPI_GPIO_IRQ_SET_TYPE:
				_spi_gpio_irq_set_type(irq, type);
				break;
			case ID_SPI_GPIO_IRQ_SET_WAKE:
				_spi_gpio_irq_set_wake(irq, state);
				break;
			default:
				break;
		}
	}
}


static void spi_gpio_irq_enable(unsigned irq)
{
	struct spi_fpga_port *port = pFpgaPort;
	struct spi_gpio_irq_transfer *t;
	unsigned long flags;
	t = kzalloc(sizeof(struct spi_gpio_irq_transfer), GFP_ATOMIC);
	if (!t)
	{
		printk("err:%s:ENOMEM\n",__FUNCTION__);
		return ;
	}
	t->irq = irq;
	t->id = ID_SPI_GPIO_IRQ_ENABLE;
	
	spin_lock_irqsave(&port->work_lock, flags);
	list_add_tail(&t->queue, &port->gpio.msg_queue);
	spin_unlock_irqrestore(&port->work_lock, flags);

	queue_work(port->gpio.spi_gpio_irq_workqueue, &port->gpio.spi_gpio_irq_work);
}

static void spi_gpio_irq_disable(unsigned irq)
{
	struct spi_fpga_port *port = pFpgaPort;
	struct spi_gpio_irq_transfer *t;
	unsigned long flags;
	t = kzalloc(sizeof(struct spi_gpio_irq_transfer), GFP_ATOMIC);
	if (!t)
	{
		printk("err:%s:ENOMEM\n",__FUNCTION__);
		return ;
	}
	t->irq = irq;
	t->id = ID_SPI_GPIO_IRQ_DISABLE;
	
	spin_lock_irqsave(&port->work_lock, flags);
	list_add_tail(&t->queue, &port->gpio.msg_queue);
	spin_unlock_irqrestore(&port->work_lock, flags);

	queue_work(port->gpio.spi_gpio_irq_workqueue, &port->gpio.spi_gpio_irq_work);
}

static void spi_gpio_irq_mask(unsigned int irq)
{
	//FPGA do not support irq mask
}

static void spi_gpio_irq_unmask(unsigned int irq)
{
	//FPGA do not support irq unmask
}

static int spi_gpio_irq_set_type(unsigned int irq, unsigned int type)
{
	struct spi_fpga_port *port = pFpgaPort;
	struct spi_gpio_irq_transfer *t;
	unsigned long flags;
	t = kzalloc(sizeof(struct spi_gpio_irq_transfer), GFP_KERNEL);
	if (!t)
	{
		printk("err:%s:ENOMEM\n",__FUNCTION__);
		return -ENOMEM;
	}
	t->irq = irq;
	t->id = ID_SPI_GPIO_IRQ_SET_TYPE;
	t->type = type;
	
	spin_lock_irqsave(&port->work_lock, flags);
	list_add_tail(&t->queue, &port->gpio.msg_queue);
	spin_unlock_irqrestore(&port->work_lock, flags);

	queue_work(port->gpio.spi_gpio_irq_workqueue, &port->gpio.spi_gpio_irq_work);

	return 0;
}

static int spi_gpio_irq_set_wake(unsigned irq, unsigned state)
{
	struct spi_fpga_port *port = pFpgaPort;
	struct spi_gpio_irq_transfer *t;
	unsigned long flags;
	t = kzalloc(sizeof(struct spi_gpio_irq_transfer), GFP_KERNEL);
	if (!t)
	{
		printk("err:%s:ENOMEM\n",__FUNCTION__);
		return -ENOMEM;
	}
	t->irq = irq;
	t->id = ID_SPI_GPIO_IRQ_SET_WAKE;
	t->state = state;
	
	spin_lock_irqsave(&port->work_lock, flags);
	list_add_tail(&t->queue, &port->gpio.msg_queue);
	spin_unlock_irqrestore(&port->work_lock, flags);

	queue_work(port->gpio.spi_gpio_irq_workqueue, &port->gpio.spi_gpio_irq_work);

	return 0;
}


static struct irq_chip spi_gpio_irq_chip = {
	.name		= "SPI_GPIO_IRQ",
	.enable 		= spi_gpio_irq_enable,
	.disable		= spi_gpio_irq_disable,
	.mask		= spi_gpio_irq_mask,
	.unmask		= spi_gpio_irq_unmask,
	.set_type		= spi_gpio_irq_set_type,
	.set_wake	= spi_gpio_irq_set_wake,
};

void spi_gpio_test_gpio_irq_init(void)
{
#if SPI_GPIO_TEST
	struct spi_fpga_port *port = pFpgaPort;
	int i,gpio,ret,irq;

	for(i=0;i<81;i++)
	{
		gpio = SPI_FPGA_EXPANDER_BASE+i;
		ret = gpio_request(gpio, NULL);
		if (ret) {
			printk("%s:failed to request GPIO[%d]\n",__FUNCTION__,gpio);
		}
	}

	for(i=0;i<4;i++)
	{
		gpio = SPI_FPGA_EXPANDER_BASE+i;
		irq = gpio_to_irq(gpio);
		printk("%s:line=%d,irq=%d,gpio=%d\n",__FUNCTION__,__LINE__,irq,gpio);
		switch(i)
		{
			case 0:
			ret = request_irq(irq ,spi_gpio_int_test_0,IRQF_TRIGGER_FALLING,NULL,port);
			if(ret)
			{
				printk("%s:unable to request GPIO[%d] irq\n",__FUNCTION__,gpio);
				gpio_free(gpio);
			}	
			break;

			case 1:
			ret = request_irq(irq ,spi_gpio_int_test_1,IRQF_TRIGGER_FALLING,NULL,port);
			if(ret)
			{
				printk("%s:unable to request GPIO[%d] irq\n",__FUNCTION__,gpio);
				gpio_free(gpio);
			}	
			break;

			case 2:
			ret = request_irq(irq ,spi_gpio_int_test_2,IRQF_TRIGGER_FALLING,NULL,port);
			if(ret)
			{
				printk("%s:unable to request GPIO[%d] irq\n",__FUNCTION__,gpio);
				gpio_free(gpio);
			}	
			break;

			case 3:
			ret = request_irq(irq ,spi_gpio_int_test_3,(IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING),NULL,port);
			if(ret)
			{
				printk("%s:unable to request GPIO[%d] irq\n",__FUNCTION__,gpio);
				gpio_free(gpio);
			}	
			break;
			
			default:
			break;
		}
	
	}

#endif

}

static int spi_gpio_banks;
static struct lock_class_key gpio_lock_class;

/*
 * Called from the processor-specific init to enable GPIO interrupt support.
 */
void spi_gpio_irq_setup(void)
{
	unsigned	int	j, pin;
	struct fpga_gpio_chip *this;
	
	this = spi_gpio_chip;
	pin = NR_AIC_IRQS + CONFIG_RK28_GPIO_IRQ;

	for (j = 0; j < 16; j++) 
	{
		lockdep_set_class(&irq_desc[pin+j].lock, &gpio_lock_class);
		/*
		 * Can use the "simple" and not "edge" handler since it's
		 * shorter, and the AIC handles interrupts sanely.
		 */
		set_irq_chip(pin+j, &spi_gpio_irq_chip);
		//set_irq_handler(pin+j, handle_simple_irq);
		set_irq_flags(pin+j, IRQF_VALID);
	}

	printk("%s: %d gpio irqs in %d banks\n", __FUNCTION__, pin+j-SPI_FPGA_EXPANDER_BASE, spi_gpio_banks);

	spi_gpio_test_gpio_irq_init();

}


int spi_gpio_init(void)
{
	unsigned		i;
	struct fpga_gpio_chip *fpga_gpio_chip;
	spi_gpio_banks = 6;	
	spi_gpio_init_first();
	for (i = 0; i < 6; i++) 
	{	
		fpga_gpio_chip = &spi_gpio_chip[i];
		gpiochip_add(&fpga_gpio_chip->chip);
	}

	spi_gpio_irq_setup();
	
	return 0;
}

#endif

MODULE_DESCRIPTION("Driver for spi2gpio.");
MODULE_AUTHOR("luowei <lw@rock-chips.com>");
MODULE_LICENSE("GPL");
