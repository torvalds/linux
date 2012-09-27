#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/circ_buf.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <mach/gpio.h>
#include <asm/gpio.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/earlysuspend.h>
#include <linux/slab.h>
#include <mach/board.h>
#include <linux/gpio_exp_callpad.h>

//#define DEBUG_GPIO_EXP
#ifdef DEBUG_GPIO_EXP
#define DEBUGPRINT(x...) printk(x)
#else
#define DEBUGPRINT(x...)
#endif

struct gpioexp_data {
	struct i2c_client *client;
	int    irq;
	struct work_struct 	gpioexp_work;
	struct workqueue_struct *gpioexp_workqueue;
	u16 int_mask_bit;
	u16 pre_int_bit;
	gpioexp_int_handler_t interrupt_handlers [GPIOEXP_PIN_MAX];
	unsigned long interrupt_trigger[GPIOEXP_PIN_MAX];
};


static struct gpioexp_data* gpioexp_ptr=NULL;

int gpioexp_set_direction(unsigned gpio, int is_in)
{
	char buf[1];
	unsigned char reg, gpiobit;

	if(gpio >= GPIOEXP_PIN_MAX)
		return -EINVAL;

	reg = (gpio<8) ? 0x04 : 0x05;
	gpiobit = (gpio<8) ? (0x01<<gpio) : (0x01<<(gpio-8));

	buf[0] = 0;
	i2c_master_reg8_recv(gpioexp_ptr->client, reg, buf, 1, 400*1000);
	if(is_in)
		buf[0] |= gpiobit;
	else
		buf[0] &= ~gpiobit;

	i2c_master_reg8_send(gpioexp_ptr->client, reg, buf, 1, 400*1000);
	
	DEBUGPRINT("gpioexp_set_direction:gpio = %d, buf[0] = %d\n", gpio, buf[0]);

	return 0;

}	
EXPORT_SYMBOL(gpioexp_set_direction);

int gpioexp_set_output_level(unsigned gpio, int value)
{
    char buf[1];
    unsigned char reg, gpiobit;
    
	if(gpio >= GPIOEXP_PIN_MAX)
		return -EINVAL;

    reg = (gpio<8) ? 0x02 : 0x03;
    gpiobit = (gpio<8) ? (0x01<<gpio) : (0x01<<(gpio-8));

    buf[0] = 0;
    i2c_master_reg8_recv(gpioexp_ptr->client, reg, buf, 1, 400*1000);
    if(value)
     	buf[0] |= gpiobit;
   else
    	buf[0] &= ~gpiobit;
    	
    i2c_master_reg8_send(gpioexp_ptr->client, reg, buf, 1, 400*1000);
   	DEBUGPRINT("gpioexp_set_output_level:gpio = %d, buf[0] = %d\n", gpio, buf[0]);

	return 0;

}	
EXPORT_SYMBOL(gpioexp_set_output_level);


int gpioexp_read_input_level(unsigned gpio)
{
    char buf[1];
    unsigned char reg, gpiobit;
    int ret;
    
	if(gpio >= GPIOEXP_PIN_MAX)
		return -EINVAL;
	
    reg = (gpio<8) ? 0x00 : 0x01;
    gpiobit = (gpio<8)?(0x01<<gpio):(0x01<<(gpio-8));

    buf[0] = 0;
    i2c_master_reg8_recv(gpioexp_ptr->client, reg, buf, 1, 400*1000);
    
    ret = (buf[0] & gpiobit)?1:0;
    	
	DEBUGPRINT("gpioexp_read_input_level:gpio = %d, buf[0] = %d\n", gpio, buf[0]);
    return ret;

}
EXPORT_SYMBOL(gpioexp_read_input_level);

int gpioexp_request_irq(unsigned int irq, gpioexp_int_handler_t handler, unsigned long flags)
{
	char buf[1];
	
	if(irq > GPIOEXP_PIN_MAX || handler == NULL || !(flags & GPIOEXP_INT_TRIGGER_MASK))
		return -EINVAL;
	
	if(gpioexp_ptr->int_mask_bit & (0x01 << irq) == 0)
		return -EBUSY;
	gpioexp_ptr->interrupt_handlers[irq] = handler;
	gpioexp_ptr->interrupt_trigger[irq] = flags & GPIOEXP_INT_TRIGGER_MASK;
	gpioexp_ptr->int_mask_bit &= ~(0x01 << irq);

	if(irq >=8){
		buf[0] = (gpioexp_ptr->int_mask_bit >> 8) & 0xff;
		i2c_master_reg8_send(gpioexp_ptr->client, 0x07, buf, 1, 400*1000);
	}
	else{
		buf[0] = gpioexp_ptr->int_mask_bit & 0xff;
		i2c_master_reg8_send(gpioexp_ptr->client, 0x06, buf, 1, 400*1000);
	}

	return 0;
}
EXPORT_SYMBOL(gpioexp_request_irq);

int gpioexp_free_irq(unsigned int irq)
{
	char buf[1];

	if(irq > GPIOEXP_PIN_MAX)
		return -EINVAL;
	if(gpioexp_ptr->int_mask_bit & (0x01 << irq) == 0){
		gpioexp_ptr->int_mask_bit &= ~(0x01 << irq);
		if(irq >=8){
			buf[0] = (gpioexp_ptr->int_mask_bit >> 8) & 0xff;
			i2c_master_reg8_send(gpioexp_ptr->client, 0x07, buf, 1, 400*1000);
		}
		else{
			buf[0] = gpioexp_ptr->int_mask_bit & 0xff;
			i2c_master_reg8_send(gpioexp_ptr->client, 0x06, buf, 1, 400*1000);
		}
	}
	gpioexp_ptr->interrupt_handlers[irq] = NULL;
	gpioexp_ptr->interrupt_trigger[irq] = 0;
	
	return 0;
}EXPORT_SYMBOL(gpioexp_free_irq);

int gpioexp_enable_irq(unsigned int irq)
{
	char buf[1];
	
	if(irq > GPIOEXP_PIN_MAX)
		return -EINVAL;
	if(gpioexp_ptr->int_mask_bit & (0x01 << irq) == 0)
		return 0;
	gpioexp_ptr->int_mask_bit &= ~(0x01 << irq);
	if(irq >=8){
		buf[0] = (gpioexp_ptr->int_mask_bit >> 8) & 0xff;
		i2c_master_reg8_send(gpioexp_ptr->client, 0x07, buf, 1, 400*1000);
	}
	else{
		buf[0] = gpioexp_ptr->int_mask_bit & 0xff;
		i2c_master_reg8_send(gpioexp_ptr->client, 0x06, buf, 1, 400*1000);
	}
	return 0;
}
EXPORT_SYMBOL(gpioexp_enable_irq);

int gpioexp_disable_irq(unsigned int irq)
{
	char buf[1];
	
	if(irq > GPIOEXP_PIN_MAX)
		return -EINVAL;
	if(gpioexp_ptr->int_mask_bit & (0x01 << irq) == 1)
		return 0;
	gpioexp_ptr->int_mask_bit |= (0x01 << irq);
	if(irq >=8){
		buf[0] = (gpioexp_ptr->int_mask_bit >> 8) & 0xff;
		i2c_master_reg8_send(gpioexp_ptr->client, 0x07, buf, 1, 400*1000);
	}
	else{
		buf[0] = gpioexp_ptr->int_mask_bit & 0xff;
		i2c_master_reg8_send(gpioexp_ptr->client, 0x06, buf, 1, 400*1000);
	}
	return 0;
}
EXPORT_SYMBOL(gpioexp_disable_irq);

static inline void gpioexp_handle_interrupt(struct gpioexp_data *data, u16 int_pending_bit)
{
	int i;
	u16 pre_int_bit = data->pre_int_bit;

	for(i=0;i<GPIOEXP_PIN_MAX;i++){
		if(int_pending_bit & 0x01){
			if((pre_int_bit >> i) & 0x01){
				if(data->interrupt_trigger[i] && GPIOEXP_INT_TRIGGER_FALLING)\
					data->interrupt_handlers[i](data);
			}
			else{
				if(data->interrupt_trigger[i] && GPIOEXP_INT_TRIGGER_RISING)\
					data->interrupt_handlers[i](data);
			}
		}
		int_pending_bit = int_pending_bit >> 1;
	}
}

static inline u16 gpioexp_interrupt_pending(struct gpioexp_data *data,u16 cur_int_bit)
{
	u16 int_toggle_bit,int_pending_bit;
	
	int_toggle_bit = (data->pre_int_bit) ^ cur_int_bit;
	int_pending_bit = (~data->int_mask_bit) & int_toggle_bit;

	return int_pending_bit;
}

static void gpioexp_chip_init(void)
{
	char buf[1];

	buf[0] = 0x10; //set P0 Push-Pull
	i2c_master_reg8_send(gpioexp_ptr->client, 0x11, buf, 1, 400*1000);
	
	buf[0] = 0xff; //set interrupt disable
	i2c_master_reg8_send(gpioexp_ptr->client, 0x06, buf, 1, 400*1000);    
	buf[0] = 0xff; //set interrupt disable
	i2c_master_reg8_send(gpioexp_ptr->client, 0x07, buf, 1, 400*1000);    
	gpioexp_ptr->int_mask_bit = 0xffff;

	buf[0] = 0x00; //set input output P0 0b10111111
	i2c_master_reg8_send(gpioexp_ptr->client, 0x04, buf, 1, 400*1000);    
	buf[0] = 0x00;  //set input output P1 0b01000000
	i2c_master_reg8_send(gpioexp_ptr->client, 0x05, buf, 1, 400*1000);    

	buf[0] = 0x00; //set output value P0 0b00000000
	i2c_master_reg8_send(gpioexp_ptr->client, 0x02, buf, 1, 400*1000);    
	buf[0] = 0x80; //set output value P1 0b00000011
	i2c_master_reg8_send(gpioexp_ptr->client, 0x03, buf, 1, 400*1000);    
	gpioexp_ptr->pre_int_bit = 0x0000;
		
	DEBUGPRINT("gpioexp_init\n");
}	

static void gpioexp_work(struct work_struct *work)
{
	struct gpioexp_data *data = container_of(work, struct gpioexp_data, gpioexp_work);
	char buf[2] = {0};
	u16 cur_int_bit,int_pending_bit;
	
	DEBUGPRINT("gpioexp_work: read io status\n");
	
	i2c_master_reg8_recv(gpioexp_ptr->client, 0x00, buf, 1, 400*1000);		//read p0 value
	i2c_master_reg8_recv(gpioexp_ptr->client, 0x01, buf+1, 1, 400*1000);	//read p1 value
	cur_int_bit = (u16)(buf[1]) << 8 | buf[0];
	int_pending_bit = gpioexp_interrupt_pending(data,cur_int_bit);
	if(int_pending_bit){
		gpioexp_handle_interrupt(data,int_pending_bit);
	}
	data->pre_int_bit = cur_int_bit;

	enable_irq(data->irq);
}

static irqreturn_t gpioexp_interrupt(int irq, void *dev_id)
{
	struct gpioexp_data *gpioexp = dev_id;

	DEBUGPRINT("gpioexp_interrupt\n");
	disable_irq_nosync(gpioexp->irq);
	if (!work_pending(&gpioexp->gpioexp_work)) 
		queue_work(gpioexp->gpioexp_workqueue, &gpioexp->gpioexp_work);
	return IRQ_HANDLED;
}

static __devinit int gpioexp_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct gpio_exp_platform_data *pdata = i2c->dev.platform_data;
	struct gpioexp_data *gpioexp;
	int ret = 0;

	DEBUGPRINT("gpioexp_probe\n");
	if (!pdata) {
		dev_err(&i2c->dev, "platform data is required!\n");
		return -EINVAL;
	}

	gpioexp = kzalloc(sizeof(*gpioexp), GFP_KERNEL);
	if (!gpioexp)	{
		return -ENOMEM;
	}

	if (pdata->init_platform_hw)                              
		pdata->init_platform_hw();

	gpioexp->client = i2c;
	gpioexp->irq = gpio_to_irq(i2c->irq);

	INIT_WORK(&gpioexp->gpioexp_work, gpioexp_work);
	gpioexp->gpioexp_workqueue = create_singlethread_workqueue("gpioexp_workqueue");
	if (!gpioexp->gpioexp_workqueue) {
		ret = -ESRCH;
		goto gpioexp_wq_fail;
	}


	i2c_set_clientdata(gpioexp->client, gpioexp);
	gpioexp_ptr = gpioexp;

	gpioexp_chip_init();

	ret = request_irq(gpioexp->irq, gpioexp_interrupt, IRQF_TRIGGER_FALLING, gpioexp->client->dev.driver->name, gpioexp);
	if (ret < 0) {
		ret = 1;
		goto gpioexp_irq_fail;
	}

	DEBUGPRINT("gpioexp set modem power on!\n");
	return 0;

gpioexp_irq_fail:
	destroy_workqueue(gpioexp->gpioexp_workqueue);
	i2c_set_clientdata(gpioexp->client, NULL);
	gpioexp_ptr = NULL;
gpioexp_wq_fail:	
	pdata->exit_platform_hw();
	kfree(gpioexp);

	return ret;
}

static __devexit int gpioexp_remove(struct i2c_client *client)
{
	struct gpio_exp_platform_data *pdata = client->dev.platform_data;
	struct gpioexp_data *gpioexp = i2c_get_clientdata(client);
	
	DEBUGPRINT("gpioexp_remove");

	destroy_workqueue(gpioexp->gpioexp_workqueue);
	pdata->exit_platform_hw();
	kfree(gpioexp);
	gpioexp_ptr = NULL;
	
	return 0;
}

static const struct i2c_device_id gpioexp_i2c_id[] = {
	{ "gpioexp_aw9523b", 0 },{ }
};

static struct i2c_driver gpioexp_driver = {
	.probe		= gpioexp_probe,
	.remove		= __devexit_p(gpioexp_remove),
	.id_table	= gpioexp_i2c_id,
	.driver	= {
		.name	= "gpioexp_aw9523b",
		.owner	= THIS_MODULE,
	},
};

static int __init gpioexp_init(void)
{
    return i2c_add_driver(&gpioexp_driver);
}

static void __exit gpioexp_exit(void)
{
	i2c_del_driver(&gpioexp_driver);
}

subsys_initcall_sync(gpioexp_init);
module_exit(gpioexp_exit);

MODULE_LICENSE("GPL");


