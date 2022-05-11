/*
 * controller's IR driver for the nordic52832 SoCs
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/string.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <asm-generic/gpio.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/kdev_t.h>

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/dma-buf.h>

#include <linux/of.h>
#include <linux/of_gpio.h>

#include <linux/of_device.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>

#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/timekeeping.h>
#include <linux/kthread.h>

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/spi/spi.h>
#include <linux/of_irq.h>

#include <linux/regulator/consumer.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/hrtimer.h>  
#include <linux/ktime.h>  


struct rled_chip_io {
    	
	int torch_en;
	int flash_en;
	int charge_en;
	int flag_pin; /*rled chip flag pin from chip to cpu,indicate chip state*/
	int irq;   
};



struct local_rled_chip {
	struct mutex      change_lock;/*rled state chagen lock*/
	struct pinctrl *pinctrl;
	struct pinctrl_state *active;
	struct pinctrl_state *suspend;
	int rled_trig_gpio;
	int  irq; /*from the camera sensor trig the irq*/
	struct rled_chip_io chip1;  
	struct rled_chip_io chip2;
    struct hrtimer hr_timer; 
    ktime_t ktime;  
    int  grled_chipenabled;
    int  grled_swflag;/*Every other frame the trig led will use ,not all frame */
    
};


static enum hrtimer_restart timercallback( struct hrtimer *timer )  
{

    struct local_rled_chip   *rled_data;
	rled_data = container_of(timer, struct local_rled_chip,hr_timer);

    gpio_set_value(rled_data->chip1.flash_en, 0);
    gpio_set_value(rled_data->chip2.flash_en, 0);

    //enable_irq(rled_data->irq);
    //pr_err("local_rled timercallback\n");

    return HRTIMER_NORESTART;  
}  


static irqreturn_t  rled_irq_handler(int irq, void *dev_id)
{
    int val=0;
	struct local_rled_chip   *rled_data=(struct local_rled_chip *)dev_id;

    if(rled_data->grled_chipenabled==0)
    {
    	gpio_set_value(rled_data->chip1.flash_en,0);
		gpio_set_value(rled_data->chip2.flash_en,0);
        return IRQ_HANDLED;
    }

	val=gpio_get_value(rled_data->rled_trig_gpio);
	if(val==1)
	{
         if(rled_data->grled_swflag)
         {
                gpio_set_value(rled_data->chip1.flash_en,1);
                gpio_set_value(rled_data->chip2.flash_en,1);
         }

        //disable_irq_nosync(rled_data->irq);
        //hrtimer_start( &rled_data->hr_timer, rled_data->ktime, HRTIMER_MODE_REL ); 
	}
    else
    {
		gpio_set_value(rled_data->chip1.flash_en,0);
		gpio_set_value(rled_data->chip2.flash_en,0);
    }   
    
	//pr_err("rled_irq_handler val=%d\n",val);
	
	return IRQ_HANDLED;
}




/*just test code for the rled chip */

static int  grled_enabled=0;
static int  grled_mode=0;// 0 flash 1,torch  ,0x80 charge enable
static struct local_rled_chip   *gprled_data=NULL;


static ssize_t rledtest_show(struct device *dev,struct device_attribute *attr, char *buf)
{

 	return sprintf(buf, "mode[%d]enable[%d]\n",grled_mode,grled_enabled );
}


static ssize_t rledtest_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)  
{

   if(gprled_data==NULL)return size;

   if(sscanf(buf,"%d:%d",&grled_mode,&grled_enabled)==2)
   {
		
		pr_err("local_rled mode =%d, rled = %d \n",grled_mode,grled_enabled);
        
        disable_irq_nosync(gprled_data->irq);// 

		if((grled_mode&0x80)==0x80){ //TODO 
			if(grled_enabled){
	            gpio_set_value(gprled_data->chip1.charge_en,1);
 	            gpio_set_value(gprled_data->chip2.charge_en,1);
			}
			else
	           {
	            gpio_set_value(gprled_data->chip1.charge_en,0);
 	            gpio_set_value(gprled_data->chip2.charge_en,0);
				}
			   
		}


		if((grled_mode&0x7f)==0){
			if(grled_enabled){
	            gpio_set_value(gprled_data->chip1.flash_en,1);
 	            gpio_set_value(gprled_data->chip2.flash_en,1);
			}
			else
	           {
	            gpio_set_value(gprled_data->chip1.flash_en,0);
 	            gpio_set_value(gprled_data->chip2.flash_en,0);
				}
			   
		}

		if((grled_mode&0x7f)==1){
			if(grled_enabled){
	            gpio_set_value(gprled_data->chip1.torch_en,1);
 	            gpio_set_value(gprled_data->chip2.torch_en,1);
			}
			else
	          {
	            gpio_set_value(gprled_data->chip1.torch_en,0);
 	            gpio_set_value(gprled_data->chip2.torch_en,0);
			 }
			   
		}


		
            
   }
      
   return size;
}
/*just test code for the rled chip  end*/


static ssize_t rledchipenable_show(struct device *dev,struct device_attribute *attr, char *buf)
{

 	return sprintf(buf, "rledchipenable[%d]\n",gprled_data->grled_chipenabled );
}


static ssize_t rledchipenable_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)  
{
   int  enableflag=0;

   if(gprled_data==NULL)return size;

   if(sscanf(buf,"%d",&enableflag)==1)
   {
		pr_err("local_rled enableflag = %d \n",enableflag);

        mutex_lock(&gprled_data->change_lock);
        if(enableflag==1){

            if(gprled_data->grled_chipenabled==0){
                gprled_data->grled_chipenabled=1;
                enable_irq(gprled_data->irq);
            }
        }
        else{

            if(gprled_data->grled_chipenabled==1){
                gprled_data->grled_chipenabled=0;
                disable_irq_nosync(gprled_data->irq);
            }
            //if irq is not trig when led is on  ,disable here .
            gpio_set_value(gprled_data->chip1.flash_en,0);
		    gpio_set_value(gprled_data->chip2.flash_en,0);
        }
        
        mutex_unlock(&gprled_data->change_lock);
   }
      
   return size;
}



extern  void  external_ctl_gpio(u8 mask);

static ssize_t grledswflag_show(struct device *dev,struct device_attribute *attr, char *buf)
{
 	return sprintf(buf, "rledchipenable[%d]\n",gprled_data->grled_swflag );
}


/*used to ctl V02A joystick */
static ssize_t grledswflag_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)  
{
    int  swflag=0;


   if(gprled_data==NULL)return size;

   if(sscanf(buf,"%d",&swflag)==1)
   {
       
        if(swflag){
            external_ctl_gpio((u8)swflag);
            gprled_data->grled_swflag=1;
        }
        else{
            external_ctl_gpio(0);
            gprled_data->grled_swflag=0;
            
        } 
        
	//	pr_err("local_rled grledswflag  = %d \n",gprled_data->grled_swflag);
        
   }
      
   return size;
}



static DEVICE_ATTR(rledtest, S_IRUGO|S_IWUSR|S_IWGRP, rledtest_show, rledtest_store);
static DEVICE_ATTR(rledchipenable, S_IRUGO|S_IWUSR|S_IWGRP, rledchipenable_show, rledchipenable_store);
static DEVICE_ATTR(grledswflag, S_IRUGO|S_IWUSR|S_IWGRP, grledswflag_show, grledswflag_store);


static int rled_local_probe(struct platform_device *pdev)
{

	struct device *dev = &pdev->dev;
	struct device_node *of_node = dev->of_node;

	int rc = 0;

	struct local_rled_chip   *rled_data;

	if(of_node==NULL){
	   pr_err("failed to check of_node \n");
	   return -ENOMEM;
	}

	rled_data = kzalloc(sizeof(*rled_data), GFP_KERNEL);
	if (!rled_data) {
		return -ENOMEM;
	}

	rled_data->pinctrl= devm_pinctrl_get(dev);
	
	if (IS_ERR_OR_NULL(rled_data->pinctrl)) {
		rc = PTR_ERR(rled_data->pinctrl);
		pr_err("failed  pinctrl, rc=%d\n", rc);
		goto data_free;
	}

	rled_data->active = pinctrl_lookup_state(rled_data->pinctrl, "pin_default");
	if (IS_ERR_OR_NULL(rled_data->active)) {
		rc = PTR_ERR(rled_data->active);
		pr_err("failed  pinctrl active state, rc=%d\n", rc);
		goto data_free;
	}

	rled_data->suspend =pinctrl_lookup_state(rled_data->pinctrl, "pin_sleep");

	if (IS_ERR_OR_NULL(rled_data->suspend)) {
		rc = PTR_ERR(rled_data->suspend);
		pr_err("failed  pinctrl suspend state, rc=%d\n", rc);
		goto data_free;
	}
	pr_err("rled_pinctrl t ok \n");

        
    rled_data->rled_trig_gpio= of_get_named_gpio(of_node,"yc,irq-gpio", 0);
    if (!gpio_is_valid(rled_data->rled_trig_gpio)) {
            pr_err("failed get   rled_trig_gpio gpio, rc=%d\n", rc);
            rc = -EINVAL;
            goto data_free;
     }

	
    // 1
    
    rled_data->chip1.flag_pin= of_get_named_gpio(of_node,"yc,flag1_gpio", 0);
    if (!gpio_is_valid(rled_data->chip1.flag_pin)) {
            pr_err("failed get   flag1_gpio gpio, rc=%d\n", rc);
            rc = -EINVAL;
            goto data_free;
     }

    rled_data->chip1.charge_en= of_get_named_gpio(of_node,"yc,chgen1_gpio", 0);
    if (!gpio_is_valid(rled_data->chip1.charge_en)) {
            pr_err("failed get   chgen1_gpio gpio, rc=%d\n", rc);
            rc = -EINVAL;
            goto data_free;
     }

    rled_data->chip1.flash_en= of_get_named_gpio(of_node,"yc,flash1_gpio", 0);
    if (!gpio_is_valid(rled_data->chip1.flash_en)) {
            pr_err("failed get   flash1_gpio gpio, rc=%d\n", rc);
            rc = -EINVAL;
            goto data_free;
     }

    rled_data->chip1.torch_en= of_get_named_gpio(of_node,"yc,torch1_gpio", 0);
    if (!gpio_is_valid(rled_data->chip1.torch_en)) {
            pr_err("failed get   torch1_gpio gpio, rc=%d\n", rc);
            rc = -EINVAL;
            goto data_free;
     }
 
	
    // 2
    rled_data->chip2.flag_pin= of_get_named_gpio(of_node,"yc,flag2_gpio", 0);
    if (!gpio_is_valid(rled_data->chip2.flag_pin)) {
            pr_err("failed get   flag2_gpio gpio, rc=%d\n", rc);
            rc = -EINVAL;
            goto data_free;
     }

    rled_data->chip2.charge_en= of_get_named_gpio(of_node,"yc,chgen2_gpio", 0);
    if (!gpio_is_valid(rled_data->chip2.charge_en)) {
            pr_err("failed get   chgen2_gpio gpio, rc=%d\n", rc);
            rc = -EINVAL;
            goto data_free;
     }

    rled_data->chip2.flash_en= of_get_named_gpio(of_node,"yc,flash2_gpio", 0);
    if (!gpio_is_valid(rled_data->chip2.flash_en)) {
            pr_err("failed get  flash2_gpio gpio, rc=%d\n", rc);
            rc = -EINVAL;
            goto data_free;
     }

    rled_data->chip2.torch_en= of_get_named_gpio(of_node,"yc,torch2_gpio", 0);
    if (!gpio_is_valid(rled_data->chip2.torch_en)) {
            pr_err("failed get   torch2_gpio gpio, rc=%d\n", rc);
            rc = -EINVAL;
            goto data_free;
     }

	

	pr_err("rled_get_gpio  ok \n");

//----------------------------------------------------------
	if (gpio_is_valid(rled_data->rled_trig_gpio)) {
		
		pr_err("request for rled_trig_gpio  =%d ", rled_data->rled_trig_gpio);
		rc = devm_gpio_request(dev,rled_data->rled_trig_gpio, "rled_trig_gpio");
		if (rc) {
			pr_err("request for rled_trig_gpio failed, rc=%d\n", rc);
            goto data_free;
		}
        
	}
	

	if (gpio_is_valid(rled_data->chip1.flag_pin)) {
		
		rc = devm_gpio_request(dev,rled_data->chip1.flag_pin, "rled1_flag_pin");
		if (rc) {
			pr_err("request for rled1_flag_pin failed, rc=%d\n", rc);
            goto data_free;
		}
	}

	if (gpio_is_valid(rled_data->chip1.charge_en)) {
		
		rc = devm_gpio_request(dev,rled_data->chip1.charge_en, "rled1_charge_en");
		if (rc) {
			pr_err("request for rled1_charge_en failed, rc=%d\n", rc);
            goto data_free;
		}
	}

	if (gpio_is_valid(rled_data->chip1.flash_en)) {
		
		rc = devm_gpio_request(dev,rled_data->chip1.flash_en, "rled1_flash_en");
		if (rc) {
			pr_err("request for rled1_flash_en failed, rc=%d\n", rc);
            goto data_free;
		}
	}

	if (gpio_is_valid(rled_data->chip1.torch_en)) {
		
		rc = devm_gpio_request(dev,rled_data->chip1.torch_en, "rled1_torch_en");
		if (rc) {
			pr_err("request for rled1_torch_en failed, rc=%d\n", rc);
            goto data_free;
		}
	}

//
	if (gpio_is_valid(rled_data->chip2.flag_pin)) {
		
		rc = devm_gpio_request(dev,rled_data->chip2.flag_pin, "rled2_flag_pin");
		if (rc) {
			pr_err("request for rled2_flag_pin failed, rc=%d\n", rc);
            goto data_free;
		}
	}

	if (gpio_is_valid(rled_data->chip2.charge_en)) {
		
		rc = devm_gpio_request(dev,rled_data->chip2.charge_en, "rled2_charge_en");
		if (rc) {
			pr_err("request for rled2_charge_en failed, rc=%d\n", rc);
            goto data_free;
		}
	}

	if (gpio_is_valid(rled_data->chip2.flash_en)) {
		
		rc = devm_gpio_request(dev,rled_data->chip2.flash_en, "rled2_flash_en");
		if (rc) {
			pr_err("request for rled2_flash_en failed, rc=%d\n", rc);
            goto data_free;
		}
	}

	if (gpio_is_valid(rled_data->chip2.torch_en)) {
		
		rc = devm_gpio_request(dev,rled_data->chip2.torch_en, "rled2_torch_en");
		if (rc) {
			pr_err("request for rled2_torch_en failed, rc=%d\n", rc);
            goto data_free;
		}
	}


    pr_err("rled_gpio_request ok \n");

	

//------------
	rc=pinctrl_select_state(rled_data->pinctrl ,rled_data->active);
	if (rc)
       pr_err("rled failed to set pin state, rc=%d\n",rc);


	gpio_direction_output(rled_data->chip1.charge_en, 0);
	gpio_direction_output(rled_data->chip1.flash_en, 0);
	gpio_direction_output(rled_data->chip1.torch_en, 0);
    gpio_direction_input(rled_data->chip1.flag_pin);
	
	gpio_direction_output(rled_data->chip2.charge_en, 0);
	gpio_direction_output(rled_data->chip2.flash_en, 0);
	gpio_direction_output(rled_data->chip2.torch_en, 0);
    gpio_direction_input(rled_data->chip2.flag_pin);

    gpio_direction_input(rled_data->rled_trig_gpio);

	rled_data->irq = gpio_to_irq(rled_data->rled_trig_gpio);
	
	if (rled_data->irq < 0) {
		rled_data->irq=-1;
		pr_err(" rled_irq err\n");
	}
	else{
	    rled_data->grled_chipenabled=1;
        rled_data->grled_swflag=1;
		rc = devm_request_irq(dev,rled_data->irq, rled_irq_handler,IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING , "rled", rled_data);//IRQF_TRIGGER_FALLING 
		//disable_irq(rled_data->irq);
		if(rc<0)
			pr_err("rled request_irq err   =%d  \n",rled_data->irq);
		
		pr_err(" yc request_irq =%d\n",rled_data->irq);


        rled_data->ktime = ktime_set(0, 5000000);  

        hrtimer_init( &rled_data->hr_timer, CLOCK_BOOTTIME, HRTIMER_MODE_REL);  

        rled_data->hr_timer.function = timercallback;  
 
        gpio_direction_output(rled_data->chip2.charge_en, 1);
		gpio_direction_output(rled_data->chip1.charge_en, 1);
	}


    device_create_file(dev, &dev_attr_rledtest); 
    device_create_file(dev, &dev_attr_rledchipenable); 
    device_create_file(dev, &dev_attr_grledswflag); 
    
	gprled_data=rled_data;

	mutex_init(&rled_data->change_lock);

	return rc;

data_free:
	kfree(rled_data);
	return rc;
  
}



static const struct of_device_id rled_test_match_table[] = {
	{	.compatible = "yc,rled_test",
	},
	{}
};

static struct platform_driver  local_rled_driver = {
	.driver		= {
		.name	= "yc,rled_test",
		.of_match_table = rled_test_match_table,
	},
	.probe		= rled_local_probe,
};



static int __init rled_local_init(void)
{
	return platform_driver_register(&local_rled_driver);

}

static void __exit rled_local_exit(void)
{
	return platform_driver_unregister(&local_rled_driver);
}


module_init(rled_local_init);
module_exit(rled_local_exit);
MODULE_DESCRIPTION("nordic52832 rled driver");
MODULE_LICENSE("GPL v2");
