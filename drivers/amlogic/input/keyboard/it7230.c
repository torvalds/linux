/*
 * linux/drivers/input/it7230.c
 *
 * it7230 Keypad Driver
 *
 * Copyright (C) 2010 Amlogic Corporation
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * author : X
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/i2c/it7230.h>

#define DRIVER_NAME "it7230"

/* periodic polling delay and period */
#define KP_POLL_DELAY       (1 * 1000000)
#define KP_POLL_PERIOD      (5 * 1000000)

//#define _DEBUG_IT7230_I2C
//#define _DEBUG_IT7230_READ_
//#define _DEBUG_IT7230_INIT_
int it7230_read_reg(unsigned char page, unsigned char addr_byte);
int it7230_write_reg(unsigned char page, unsigned char addr_byte, unsigned short data_word);

struct it7230 {
    spinlock_t lock;
    struct i2c_client *client;
    struct input_dev *input;
    struct hrtimer timer;
    int config_major;
    char config_name[20];
    struct class *config_class;
    struct device *config_dev;
    struct work_struct work;
    struct workqueue_struct *workqueue;
    int (*get_irq_level)(void);
    struct cap_key *key;
    int key_num;
    int pending_keys;
    int last_read;
};

static struct it7230 *gp_kp = NULL;
static int work_count=0;
static int timer_count=0;
static int int_count=0;
static int err_count = 0;
static int low_count=0;

const sInitCapSReg asInitCapSReg[] = {  
{ PAGE_1,  CAPS_PCR,        0x0001},
{ PAGE_1,  CAPS_PSR,        0x0001},
{ PAGE_1,  CAPS_PSR,        0x0001},
{ PAGE_1,  CAPS_PSR,        0x0001},
{ PAGE_1,  CAPS_PMR,        0x0000},
{ PAGE_1,  CAPS_RTR,        0x0000},
{ PAGE_1,  CAPS_CTR,        0x0000},
{ PAGE_1,  CAPS_CFER,       0x4000},
{ PAGE_1,  CAPS_CRMR,       0x0020},
{ PAGE_1,  CAPS_PDR,        0x1FFF},//default:
{ PAGE_1,  CAPS_DR,         0x0050},
{ PAGE_1,  CAPS_S0CR,       0xC013},
{ PAGE_1,  CAPS_S1CR,       0xC023},
{ PAGE_1,  CAPS_S2CR,       0xC049},
{ PAGE_1,  CAPS_S3CR,       0xC079},
{ PAGE_1,  CAPS_C1COR,      0x68D1},//
{ PAGE_1,  CAPS_C2COR,      0x68D2},//
{ PAGE_1,  CAPS_C3COR,      0x68C0},
{ PAGE_1,  CAPS_C4COR,      0x68D3},//
{ PAGE_1,  CAPS_C7COR,      0x68D1},//
{ PAGE_1,  CAPS_C9COR,      0x68C0},
{ PAGE_1,  CAPS_ICR0,       0xFFBF},
{ PAGE_1,  CAPS_ICR1,       0x0FFB},
{ PAGE_1,  CAPS_COER0,      0xFFFF},
{ PAGE_1,  CAPS_COER1,      0x03FF},
{ PAGE_1,  CAPS_CGCR,       0x0001},
{ PAGE_1,  CAPS_LEDBR,      0x0000},
{ PAGE_1,  CAPS_GPIODR,     0x0000},
{ PAGE_1,  CAPS_GPIOOR,     0x0000},
{ PAGE_1,  CAPS_GPIOMR,     0x0000},
{ PAGE_1,  CAPS_GPIOLR,     0x009C},
{ PAGE_1,  CAPS_GPIOER,     0x0000},
{ PAGE_1,  CAPS_LEDCMR0,    0xffDD},
{ PAGE_1,  CAPS_LEDCMR1,    0xfDDf},
{ PAGE_1,  CAPS_LEDCMR2,    0xDDDD},
{ PAGE_1,  CAPS_LEDCMR3,    0x0DDD},
{ PAGE_1,  CAPS_LEDRPR,     0x3030},
{ PAGE_1,  CAPS_LEDBR,      0x001F},
{ PAGE_1,  CAPS_LEDCGCR,    0x009C},
{ PAGE_1,  CAPS_LEDPR0,     0x2244},
{ PAGE_1,  CAPS_LEDPR1,     0x2442},
{ PAGE_1,  CAPS_LEDPR2,     0x4444},
{ PAGE_1,  CAPS_LEDPR3,     0x0444},
{ PAGE_1,  CAPS_GPIOMSR,    0x009C},
{ PAGE_0,  CAPS_S0DLR,      0x8000},
{ PAGE_0,  CAPS_S0OHCR,     0x0600},//
{ PAGE_0,  CAPS_S0OLCR,     0x7000},
{ PAGE_0,  CAPS_S0SR,       0xCC88},
{ PAGE_0,  CAPS_S1DLR,      0x8000},
{ PAGE_0,  CAPS_S1OHCR,     0x0600},//
{ PAGE_0,  CAPS_S1OLCR,     0x7000},
{ PAGE_0,  CAPS_S1SR,       0xCC88},
{ PAGE_0,  CAPS_S2DLR,      0x8000},
{ PAGE_0,  CAPS_S2OHCR,     0x0600},//
{ PAGE_0,  CAPS_S2OLCR,     0x7000},
{ PAGE_0,  CAPS_S2SR,       0xCC88},
{ PAGE_0,  CAPS_S3DLR,      0x8000},
{ PAGE_0,  CAPS_S3OHCR,     0x0600},//
{ PAGE_0,  CAPS_S3OLCR,     0x7000},
{ PAGE_0,  CAPS_S3SR,       0xCC88},
{ PAGE_0,  CAPS_SXCHAIER,   0x0000},
{ PAGE_0,  CAPS_SXCHRIER,   0x0000},
{ PAGE_0,  CAPS_SXCLAIER,   0x0000},
{ PAGE_0,  CAPS_SXCLRIER,   0x0000},
{ PAGE_1,  CAPS_GPIONPCR,   0x1FFF},
{ PAGE_1,  CAPS_PCR,        0x3C06}
};

/*****************************************
  initial the IT7230
***************************************/
static int it7230_power_on_init(void)
{
    int ret = 0;
    int ret1 = 0;
    int ret2 = 0;
    unsigned char temp = 0x00;
    #ifdef _DEBUG_IT7230_INIT_
    unsigned short read_buff = 0xaa;
    #endif
    while (temp < (sizeof(asInitCapSReg)/sizeof(sInitCapSReg))) {

        #ifdef _DEBUG_IT7230_INIT_
        printk(KERN_INFO "\n");
        #endif
        ret1 = it7230_write_reg(asInitCapSReg[temp].page, asInitCapSReg[temp].reg, asInitCapSReg[temp].value);
        #ifdef _DEBUG_IT7230_INIT_
        if (ret1 < 0)
            printk(KERN_INFO "***Write asInitCapSReg[%d] failed***, ret = %d\n", temp, ret1);
        else
            printk(KERN_INFO "Write: asInitCapSReg[%d], .page = %d, .reg = 0x%x, .value = 0x%x\n", temp, asInitCapSReg[temp].page, asInitCapSReg[temp].reg, asInitCapSReg[temp].value);
        #endif
        temp++;
        if ( temp<=4) {
            msleep(20);//delay 5ms
        }
        #ifdef _DEBUG_IT7230_INIT_
        read_buff = it7230_read_reg(asInitCapSReg[temp-1].page, asInitCapSReg[temp-1].reg);
        printk(KERN_INFO "Read asInitCapSReg[%d], .page = %d, .reg = 0x%x, .value = 0x%x\n", temp-1, asInitCapSReg[temp-1].page, asInitCapSReg[temp-1].reg, read_buff);
        printk(KERN_INFO "\n");
        if (read_buff != asInitCapSReg[temp-1].value)
            ret1 = it7230_write_reg(asInitCapSReg[temp-1].page, asInitCapSReg[temp-1].reg, asInitCapSReg[temp-1].value);
        #endif
    }

    msleep(200);//delay the time is 50 to 200 ms

    ret2 = it7230_write_reg(PAGE_1, CAPS_RTR, 0x05f);//set the cab  time.
    if( it7230_read_reg(PAGE_1, CAPS_RTR)!=0x05f){
       ret2 = it7230_write_reg(PAGE_1, CAPS_RTR, 0x05f);//set the cab  time.
        }
    #ifdef _DEBUG_IT7230_INIT_
    if (ret2 < 0)
        printk(KERN_INFO "***it7230_write_reg(PAGE_1, CAPS_RTR, 0x05f) failed*** ret = %d\n", ret2);
    #endif
    ret |= ret2;
    ret = it7230_write_reg(PAGE_1, CAPS_CTR, 0x001f);
       if( it7230_read_reg(PAGE_1, CAPS_CTR)!=0x01f){
       ret2 = it7230_write_reg(PAGE_1, CAPS_RTR, 0x01f);//set the cab  time.
        }
    #ifdef _DEBUG_IT7230_INIT_
    if (ret2 < 0)
        printk(KERN_INFO "***it7230_write_reg(PAGE_1, CAPS_CTR, 0x001f) failed ret = %d***\n", ret2);
    #endif
    ret |= ret2;
    ret2 = it7230_write_reg(PAGE_1, CAPS_CFER, 0xC000);
        if( it7230_read_reg(PAGE_1, CAPS_CFER)!=0xC000){
       ret2 = it7230_write_reg(PAGE_1, CAPS_CFER, 0xC000);//set the cab  time.
        }
    #ifdef _DEBUG_IT7230_INIT_
    ret2 = it7230_write_reg(PAGE_1, CAPS_CFER, 0xC000);    
    if (ret2 < 0)
        printk(KERN_INFO "***it7230_write_reg(PAGE_1, CAPS_CFER, 0xC000) failed ret = %d***\n", ret2);
    #endif
    ret2 = it7230_write_reg(PAGE_1, CAPS_LEDCMR0, 0x10DD);
    #ifdef _DEBUG_IT7230_INIT_
    if (ret2 < 0)
        printk(KERN_INFO "***it7230_write_reg(PAGE_1, CAPS_LEDCMR0, 0x10DD) failed ret = %d***\n", ret2);
    #endif
    ret2 = it7230_write_reg(PAGE_1, CAPS_LEDCMR1, 0x3DD2);
    #ifdef _DEBUG_IT7230_INIT_
    if (ret2 < 0)
        printk(KERN_INFO "***it7230_write_reg(PAGE_1, CAPS_LEDCMR1, 0x3DD2) failed ret = %d***\n", ret2);
    #endif
    ret |= ret2;
    ret1 = it7230_read_reg(PAGE_0, CAPS_SIR);//to clear contact interrupts if any.
    #ifdef _DEBUG_IT7230_INIT_
    printk(KERN_INFO "***it7230_read_reg(PAGE_0, CAPS_SIR) = 0x%x\n", ret2);
    #endif
    ret2 = it7230_write_reg(PAGE_0, CAPS_SXCHAIER, 0x000f);//set the any key to have interrupts(contact high)
            if( it7230_read_reg(PAGE_1, CAPS_SXCHAIER)!=0x000f){
       ret2 = it7230_write_reg(PAGE_1, CAPS_SXCHAIER, 0x000f);//set the cab  time.
        }
    ret |= ret2;
    return ret;
}


/******************************************
check page and caps read write function
*******************************************/
unsigned char current_page = 0;
int it7230_read_reg(unsigned char page, unsigned char addr_byte)
{
    int ret = -1;
    static int count = 0;
    struct i2c_client *client = gp_kp->client;
    u8 check_page_buf[3] = {CAPS_PSR, page, 0};
    u8 buf_reg[2] = { addr_byte, 0};
    u8 buf[2] = { 0, 0};
    struct i2c_msg msg[3] = {
        [0] = {
            .addr = client->addr,
            .flags = !I2C_M_RD,
            .len = 3,
            .buf = check_page_buf,
        },
        [1] = {
            .addr = client->addr,
            .flags = !I2C_M_RD,
            .len = 1,
            .buf = buf_reg,
        },
        [2] = {
            .addr = client->addr,
            .flags = I2C_M_RD,
            .len = 2,
            .buf = buf,
        },
    };

    #ifdef _DEBUG_IT7230_I2C
    printk(KERN_INFO " read address = 0x%x\n", addr_byte);
    #endif
    if (page != current_page ) {
        ret = i2c_transfer(client->adapter, &msg[0], 1);
        if (ret != 1) {
    			printk( "it7230 read(1) failed, address = 0x%x\n", addr_byte);
        	return -1;
       	}
        current_page = page;
        msleep(10);
    }
    ret = i2c_transfer(client->adapter, &msg[1], 2);
    if (ret == 2) {
        ret = (buf[1] << 8) | buf[0];
    }
    else{
    		printk( "it7230 read(2) failed, address = 0x%x\n", addr_byte);
    		return -1;
        count ++;
        if(count < 10){ 
            printk("count = %d\n", count++);    
        }
        else{
            printk("power on init the it7230!\n");
            count = 0;
            it7230_power_on_init();  
        }
    }
    return ret;
}

int it7230_write_reg(unsigned char page, unsigned char addr_byte, unsigned short data_word)
{
    int ret = -1;
    u8 check_page_buf[3] = { CAPS_PSR, page, 0};
    u8 buf[3] = { addr_byte, data_word&0xff, data_word>>8};
    struct i2c_client *client = gp_kp->client;
    struct i2c_msg msg[2] = {
        [0] = {
            .addr = client->addr,
            .flags = !I2C_M_RD,
            .len = 3,
            .buf = check_page_buf,
        },

        [1] = {
            .addr = client->addr,
            .flags = !I2C_M_RD,
            .len = 3,
            .buf = buf,
        }
    };
    #ifdef _DEBUG_IT7230_I2C
    printk(KERN_INFO " write address = 0x%x\n", addr_byte);
    printk(KERN_INFO " current_page = %d\n", current_page);
    printk(KERN_INFO " page = %d\n", page);
    #endif
    if (page != current_page) {
        ret = i2c_transfer(client->adapter, &msg[0], 1);
            current_page = page;
        msleep(10);
    }
    ret = i2c_transfer(client->adapter, &msg[1], 1);
    //When the "reset" bit of PCR register is 1, current_page is set to 0.
    if ((CAPS_PCR == addr_byte) && (1 == page) && (data_word & 0x0001))
        current_page = 0;
    return ret;
}



static ssize_t it7230_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	  struct i2c_client *client = container_of(dev, struct i2c_client, dev);
    u16 reg_addr, reg_data;
   
		printk("echo %s: buf[0]=%x\n", attr->attr.name, buf[0]);
    if (!strcmp(attr->attr.name, "reg")) {
			sscanf(buf+2, "%x", &reg_addr);
			sscanf(buf+9, "%x", &reg_data);
			if (buf[0]== 'w') {
				printk("write register(0x%x)= 0x%x\n",reg_addr, reg_data);
				it7230_write_reg(reg_addr>>8, reg_addr&0xff, reg_data);
			}
			else if (buf[0]== 'r') {
				reg_data = it7230_read_reg(reg_addr>>8, reg_addr&0xff);
				printk(KERN_ALERT"read register(0x%x)= 0x%x\n",reg_addr, reg_data);
			}
 		}

    if (!strcmp(attr->attr.name, "state")) {
    	printk("interrupt count = %d\n", int_count);
    	printk("timer count = %d\n", timer_count);
    	printk("work count = %d\n", work_count);
    	printk("gpioA3 mode=%d(0=output, 1=input)\n", get_gpio_mode(GPIOA_bank_bit0_14(3), GPIOA_bit_bit0_14(3)));
    	printk("gpioA3 value=%d(0=low, 1=high)\n", get_gpio_val(GPIOA_bank_bit0_14(3), GPIOA_bit_bit0_14(3)));
 		}

    if (!strcmp(attr->attr.name, "init")) {
    	printk("it7230 initialize echo\n");
    	it7230_power_on_init();  
 		}
		
		return 1;
}

static DEVICE_ATTR(reg, S_IRWXUGO, 0, it7230_write);
static DEVICE_ATTR(state, S_IRWXUGO, 0, it7230_write);
static DEVICE_ATTR(init, S_IRWXUGO, 0, it7230_write);

static struct attribute *it7230_attr[] = {
    &dev_attr_reg.attr,
    &dev_attr_state.attr,
    &dev_attr_init.attr,
    NULL
};

static struct attribute_group it7230_attr_group = {
    .name = NULL,
    .attrs = it7230_attr,
};



/*
static int it7230_reset(void)
{
    return 0;
}

static int it7230_sleep(void)
{
    return 0;
}
*/

static void it7230_work(struct work_struct *work)
{
    int i;
    #ifdef _DEBUG_IT7230_READ_
    int gpio_val = 0;
    #endif
    int button_val = 0;
    struct it7230 *kp;
    struct cap_key *key;

    kp = (struct it7230 *)container_of(work, struct it7230, work);
	//*************************************************************
    work_count++;
	//***************************************************************
    if ((!kp->get_irq_level()) || (kp->pending_keys)) 
    {
    		if (!kp->get_irq_level()) {
    			if (++low_count > 30) {
    				low_count = 0;
    				it7230_power_on_init();
    				printk("interrupt pin low time more than 300ms\n");
    				goto restart;
    			}
    		}
        button_val = it7230_read_reg(PAGE_0, CAPS_SXCHSR);
        if ((button_val >> 4) || (button_val < 0)){ //note, just use 5 keys currently, if you have more key define, you need change this value.
            #ifdef _DEBUG_IT7230_READ_
            printk(KERN_INFO "Error! Invalid touch key 0x%04x!\n", button_val);
            #endif
            kp->pending_keys = 0;
            if (++err_count > 50) {
	    				err_count = 0;
	    				it7230_power_on_init();
	    				printk("error data more than 50\n");    	
            }
            goto restart;
        }
        #ifdef _DEBUG_IT7230_READ_
        printk(KERN_INFO "button_val = 0x%04x\n", button_val);
        #endif
        err_count = 0;
        key = kp->key;
        for (i = 0; i < kp->key_num; i++) {
            if (button_val & key->mask) {
                if (!(kp->pending_keys & key->mask)) {
                    kp->pending_keys |= key->mask;
                    input_report_key(kp->input, key->code, 1);
                    #ifdef _DEBUG_IT7230_READ_
                    printk(KERN_INFO"%s key(%d) pressed\n", key->name, key->code);
                    #endif
                }
            }
            else if (kp->pending_keys & key->mask) {
                input_report_key(kp->input, key->code, 0);
                kp->pending_keys &= ~(key->mask);
                #ifdef _DEBUG_IT7230_READ_
                printk(KERN_INFO"%s key(%d) released\n", key->name, key->code);
                #endif
            }
            key++;
        }
restart:
        hrtimer_start(&kp->timer, ktime_set(0, KP_POLL_PERIOD), HRTIMER_MODE_REL);
    }
    else
    {
    	  low_count = 0;
        enable_irq(kp->client->irq);
    }
}

static struct input_dev* it7230_register_input(struct cap_key *key, int key_num)
{
    struct input_dev *input;
    int i;
    
    input = input_allocate_device();
    if (input) {
        /* setup input device */
        set_bit(EV_KEY, input->evbit);
        set_bit(EV_REP, input->evbit);

        for (i=0; i<key_num; i++) {
            set_bit(key->code, input->keybit);
            printk(KERN_INFO "%s it7230 touch key(%d) registed.\n", key->name, key->code);
            key++;
        }

        input->name = DRIVER_NAME;
        input->phys = "it7230/input0";
        input->id.bustype = BUS_ISA;
        input->id.vendor = 0x0001;
        input->id.product = 0x0001;
        input->id.version = 0x0100; 
        input->rep[REP_DELAY]=0xffffffff;
        input->rep[REP_PERIOD]=0xffffffff;
        input->keycodesize = sizeof(unsigned short);
        input->keycodemax = 0x1ff;

        if (input_register_device(input) < 0) {
            printk(KERN_ERR "it7230 register input device failed\n");
            input_free_device(input);
            input = 0;
        }
        else {
            printk("it7230 register input device completed\n");
        }
    }
    return input;
}

/**
 * it7230_timer() - timer callback function
 * @timer: timer that caused this function call
 */
static enum hrtimer_restart it7230_timer(struct hrtimer *timer)
{
    struct it7230 *kp = (struct it7230*)container_of(timer, struct it7230, timer);
    unsigned long flags = 0;

    timer_count++;
    spin_lock_irqsave(&kp->lock, flags);
    queue_work(kp->workqueue, &kp->work);
    spin_unlock_irqrestore(&kp->lock, flags);
    return HRTIMER_NORESTART;
}

static irqreturn_t it7230_interrupt(int irq, void *context)
{
    struct it7230 *kp = (struct it7230 *)context;
    unsigned long flags;

    int_count++;
    spin_lock_irqsave(&kp->lock, flags);
    /* if the attn low or data not clear, disable IRQ and start timer chain */
//    if ((!kp->get_irq_level()) || (kp->pending_keys)) {
    if (!kp->get_irq_level()) {
        disable_irq_nosync(kp->client->irq);
        hrtimer_start(&kp->timer, ktime_set(0, KP_POLL_DELAY), HRTIMER_MODE_REL);
    }
    spin_unlock_irqrestore(&kp->lock, flags);
    return IRQ_HANDLED;
}

static int it7230_config_open(struct inode *inode, struct file *file)
{
    file->private_data = gp_kp;
    return 0;
}

static int it7230_config_release(struct inode *inode, struct file *file)
{
    file->private_data=NULL;
    return 0;
}

static const struct file_operations it7230_fops = {
    .owner      = THIS_MODULE,
    .open       = it7230_config_open,
    .ioctl      = NULL,
    .release    = it7230_config_release,
};

static int it7230_register_device(struct it7230 *kp)
{
    int ret=0;
    strcpy(kp->config_name,DRIVER_NAME);
    ret=register_chrdev(0, kp->config_name, &it7230_fops);
    if(ret<=0) {
        printk("register char device error\r\n");
        return  ret ;
    }
    kp->config_major=ret;
    printk("it7230 major:%d\r\n",ret);
    kp->config_class=class_create(THIS_MODULE,kp->config_name);
    kp->config_dev=device_create(kp->config_class,  NULL,
    MKDEV(kp->config_major,0),NULL,kp->config_name);
    return ret;
}

static int it7230_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int err = 0;
    struct it7230 *kp;
    struct it7230_platform_data *pdata;

    pdata = client->dev.platform_data;
    if (!pdata) {
        dev_err(&client->dev, "it7230 require platform data!\n");
        return  -EINVAL;
    }

    kp = kzalloc(sizeof(struct it7230), GFP_KERNEL);
    if (!kp) {
        dev_err(&client->dev, "it7230 alloc data failed!\n");
        return -ENOMEM;
    }
    kp->last_read = 0;
    kp->client = client;
    kp->key = pdata->key;
    kp->key_num = pdata->key_num;
    kp->pending_keys = 0;
    kp->input = it7230_register_input(kp->key, kp->key_num);
    if (!kp->input) {
        err =  -EINVAL;
        goto fail_irq;
    }

    if (!kp->client)
        printk(KERN_INFO "it7230 kp->client failed\n");
    gp_kp=kp;
    if (!i2c_check_functionality(kp->client->adapter, I2C_FUNC_I2C) ){
       printk(KERN_INFO "i2c client failed\n");
       err = -1;
       goto fail_irq;
    }
    if(it7230_read_reg(PAGE_1, CAPS_PCR)!=0x3C06){
    err = it7230_power_on_init();
        }
    if (err < 0)
        printk(KERN_INFO "it7230 regs init failed\n");
    else
        printk(KERN_INFO "it7230 regs init successful\n");
    hrtimer_init(&kp->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    kp->timer.function = it7230_timer;
    INIT_WORK(&kp->work, it7230_work);
    kp->workqueue = create_singlethread_workqueue(DRIVER_NAME);
    if (kp->workqueue == NULL) {
        dev_err(&client->dev, "it7230 can't create work queue\n");
        err = -ENOMEM;
        goto fail;
    }

    if (!pdata->init_irq || !pdata->get_irq_level) {
        err = -ENOMEM;
        goto fail;
    }
    printk(KERN_INFO "it7230 kp->get_irq_level\n");
    kp->get_irq_level = pdata->get_irq_level;
    pdata->init_irq();
    printk(KERN_INFO "it7230 pdata->init_irq()\n");
    err = request_irq(client->irq, it7230_interrupt, IRQF_TRIGGER_FALLING,
        client->dev.driver->name, kp);
    if (err) {
        dev_err(&client->dev, "failed to request IRQ#%d: %d\n", client->irq, err);
        goto fail_irq;
    }
    printk(KERN_INFO "it7230 request_irq\n");

    i2c_set_clientdata(client, kp);
    it7230_register_device(gp_kp);

	struct device *dev = &client->dev;
	sysfs_create_group(&dev->kobj, &it7230_attr_group);

    return 0;

fail_irq:
    free_irq(client->irq, client);

fail:
    kfree(kp);
    return err;
}

static int it7230_remove(struct i2c_client *client)
{
    struct it7230 *kp = i2c_get_clientdata(client);

    free_irq(client->irq, client);
    i2c_set_clientdata(client, NULL);
    input_unregister_device(kp->input);
    input_free_device(kp->input);
    unregister_chrdev(kp->config_major,kp->config_name);
    if(kp->config_class)
    {
        if(kp->config_dev)
            device_destroy(kp->config_class,MKDEV(kp->config_major,0));
        class_destroy(kp->config_class);
    }
    kfree(kp);
    gp_kp=NULL ;
    return 0;
}

static const struct i2c_device_id it7230_ids[] = {
       {DRIVER_NAME, 0 },
       { }
};

static struct i2c_driver it7230_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .owner = THIS_MODULE,
    },
    .probe = it7230_probe,
    .remove = it7230_remove,
//  .suspend = it7230_suspend,
//  .resume = it7230_resume,
    .id_table = it7230_ids,
};

static int __init it7230_init(void)
{
    printk(KERN_INFO "it7230 init\n");
    return i2c_add_driver(&it7230_driver);
}

static void __exit it7230_exit(void)
{
    printk(KERN_INFO "it7230 exit\n");
    i2c_del_driver(&it7230_driver);
}

late_initcall(it7230_init);
module_exit(it7230_exit);

MODULE_AUTHOR("");
MODULE_DESCRIPTION("it7230 driver");
MODULE_LICENSE("GPL");




