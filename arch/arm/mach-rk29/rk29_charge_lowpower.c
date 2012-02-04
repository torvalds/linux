#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/suspend.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/suspend.h>
#include <linux/power_supply.h>
#include <linux/earlysuspend.h>
#include <mach/rk29_iomap.h>
#include <linux/io.h>
#include <mach/gpio.h>



#define CHARGE_EARLYSUSPEND 0
#define CHARGE_EARLYRESUME  1

/******************************************************************/
#ifdef CONFIG_RK29_CHARGE_EARLYSUSPEND

static DEFINE_MUTEX(power_suspend_lock);

static struct early_suspend charge_lowerpower = {

	.level = -0xff,
	.suspend = NULL,
	.resume = NULL,
};


void charge_earlysuspend_enter(int status) //xsf
{

#ifdef CONFIG_HAS_EARLYSUSPEND  

	struct early_suspend *pos;
	struct early_suspend *earlysuspend_temp;
	struct list_head *list_head;
	struct list_head *list_temp;


	mutex_lock(&power_suspend_lock);

	earlysuspend_temp = &charge_lowerpower;
	list_head =  earlysuspend_temp->link.prev;

	if(status == 0)
	{
		list_for_each_entry(pos, list_head, link)
		{
//			printk("earlysuspend-level = %d --\n", pos->level);
			if (pos->suspend != NULL)
				pos->suspend(pos);
		}	
	}
	
	if(status == 1)
	{
		list_for_each_entry_reverse(pos, list_head, link)
		{
//			printk("earlysuspend-level = %d --\n", pos->level);
			if (pos->resume != NULL)
				pos->resume(pos);
		}	
	}

	mutex_unlock(&power_suspend_lock);
#endif

}
int  rk29_charge_judge(void)
{
	return readl(RK29_GPIO4_BASE + GPIO_INT_STATUS);

}
extern int rk29_pm_enter(suspend_state_t state);

int charger_suspend(void)
{

#ifdef CONFIG_RK29_CHARGE_EARLYSUSPEND

	charge_earlysuspend_enter(0); 
	while(1)
	{
		local_irq_disable();
		rk29_pm_enter(PM_SUSPEND_MEM);

		if((rk29_charge_judge() &&(0x01000000)))
		{	
			local_irq_enable();
			break;
		}
		else
			local_irq_enable();
	}
	charge_earlysuspend_enter(1); //xsf
	return 0;
#endif
}



static int __devinit charge_lowerpower_probe(struct platform_device *pdev)
{


	printk("%s\n",__FUNCTION__);
#ifdef CONFIG_HAS_EARLYSUSPEND  
	register_early_suspend(&charge_lowerpower);//xsf
#endif

}

static struct platform_driver charge_lowerpower_driver = {
	.probe		= charge_lowerpower_probe,
	.driver		= {
		.name	= "charge_lowerpower",
		.owner	= THIS_MODULE,
	},
};
 
static int __init charge_lowerpower_init(void)
{
	return platform_driver_register(&charge_lowerpower_driver);
}
module_init(charge_lowerpower_init);

static void __exit charge_lowerpower_exit(void)
{
	platform_driver_unregister(&charge_lowerpower_driver);
}
module_exit(charge_lowerpower_exit);



MODULE_LICENSE("GPL");
MODULE_AUTHOR("xsf<xsf@rock-chips.com>");
MODULE_DESCRIPTION("charger lowerpower");


#endif
/***************************************************************/






















