	 
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>
#include <linux/reboot.h>
#include <asm/unaligned.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include <asm/uaccess.h>
#include <linux/power_supply.h>


#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif

//#define RK29_PLAY_ON_PIN RK29_PIN6_PA7
//#define MAX_PRE_CNT 2
//#define DET_CNT   5
#define PWR_ON_THRESHD 5       //power on threshd of capacity
//unsigned int   pre_cnt = 0;   //for long press counter 
//int charge_disp_mode = 0;
static int pwr_on_thrsd = 5;          //power on capcity threshold

//extern int boot_mode_init(char * s);

static int __init pwr_on_thrsd_setup(char *str)
{

	pwr_on_thrsd = simple_strtol(str,NULL,10);
	printk(KERN_INFO "power on threshold:%d",pwr_on_thrsd);
	return 0;
}

__setup("pwr_on_thrsd=", pwr_on_thrsd_setup);

static int usb_status;
static int ac_status;
static int __rk_get_system_battery_status(struct device *dev, void *data)
{
	union power_supply_propval val_status = {POWER_SUPPLY_STATUS_DISCHARGING};
	struct power_supply *psy = dev_get_drvdata(dev);

	psy->get_property(psy, POWER_SUPPLY_PROP_ONLINE, &val_status);

	if (val_status.intval != 0) {
		if (psy->type == POWER_SUPPLY_TYPE_USB)
			usb_status = POWER_SUPPLY_TYPE_USB;
		if (psy->type == POWER_SUPPLY_TYPE_MAINS)
			ac_status = POWER_SUPPLY_TYPE_MAINS;
	}

	return 0;
}

// POWER_SUPPLY_TYPE_BATTERY --- discharge
// POWER_SUPPLY_TYPE_USB     --- usb_charging
// POWER_SUPPLY_TYPE_MAINS   --- AC_charging
int rk_get_system_battery_status(void)
{
	class_for_each_device(power_supply_class, NULL, NULL, __rk_get_system_battery_status);

	if (ac_status == POWER_SUPPLY_TYPE_MAINS) {
		return POWER_SUPPLY_TYPE_MAINS;
	} else if (usb_status == POWER_SUPPLY_TYPE_USB) {
		return POWER_SUPPLY_TYPE_USB;
	}

	return POWER_SUPPLY_TYPE_BATTERY;
}
EXPORT_SYMBOL(rk_get_system_battery_status);

static union power_supply_propval battery_capacity = { 100 };
static int __rk_get_system_battery_capacity(struct device *dev, void *data)
{
	struct power_supply *psy = dev_get_drvdata(dev);

	psy->get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &battery_capacity);

	return 0;
}

int rk_get_system_battery_capacity(void)
{
	class_for_each_device(power_supply_class, NULL, NULL, __rk_get_system_battery_capacity);

	return battery_capacity.intval;
}
EXPORT_SYMBOL(rk_get_system_battery_capacity);

#ifdef CONFIG_POWER_ON_CHARGER_DISPLAY
//int charger_mode=0;	     	//1:charge,0:not charge
static void add_bootmode_charger_to_cmdline(void)
{
	char *pmode=" androidboot.mode=charger";
	//int off = strlen(saved_command_line);
	char *new_command_line = kzalloc(strlen(saved_command_line) + strlen(pmode) + 1, GFP_KERNEL);
	sprintf(new_command_line, "%s%s", saved_command_line, pmode);
	saved_command_line = new_command_line;
	//strcpy(saved_command_line+off,pmode);

	//int off = strlen(boot_command_line);
	//strcpy(boot_command_line+off,pmode);

	printk("Kernel command line: %s\n", saved_command_line);
}

//display charger logo in kernel CAPACITY


static int  __init start_charge_logo_display(void)
{
	union power_supply_propval val_status = {POWER_SUPPLY_STATUS_DISCHARGING};
	union power_supply_propval val_capacity ={ 100} ;

	printk("start_charge_logo_display\n");

	if(board_boot_mode() == BOOT_MODE_RECOVERY)  //recovery mode
	{
		printk("recovery mode \n");
		return 0;

	}

	if (rk_get_system_battery_status() != POWER_SUPPLY_TYPE_BATTERY)
		val_status.intval = POWER_SUPPLY_STATUS_CHARGING;

	val_capacity.intval = rk_get_system_battery_capacity();

	// low power   and  discharging
#if 0
	if((val_capacity.intval < pwr_on_thrsd )&&(val_status.intval != POWER_SUPPLY_STATUS_CHARGING))
	{
		printk("low power\n");
		kernel_power_off();
		while(1);
		return 0;
	}
#endif


	//low power and charging
#if 0
	if((val_capacity.intval < pwr_on_thrsd )&&(val_status.intval == POWER_SUPPLY_STATUS_CHARGING))
	{
		while((val_capacity.intval < pwr_on_thrsd ))
		{
			list_for_each_entry(psy, &rk_psy_head, rk_psy_node)
			{
				psy->get_property(psy,POWER_SUPPLY_PROP_CAPACITY,&val_capacity); 
			}

			//printk("charging ... \n");
		}
	}

#endif

	if(val_status.intval == POWER_SUPPLY_STATUS_CHARGING)
	{
		if ((board_boot_mode() != BOOT_MODE_REBOOT) || (val_capacity.intval <= pwr_on_thrsd))  //do not enter power on charge mode when soft  reset
	    {			
			add_bootmode_charger_to_cmdline();
			//boot_mode_init("charge");
			printk("power in charge mode\n");
		}
	}

	return 0;
} 

//subsys_initcall_sync(start_charge_logo_display);
module_init(start_charge_logo_display);
#endif
