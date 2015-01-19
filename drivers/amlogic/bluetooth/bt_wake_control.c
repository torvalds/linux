#include <linux/module.h>	/* kernel module definitions */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <mach/irqs.h>
#include <linux/irq.h>
#include <linux/param.h>
#include <linux/bitops.h>
#include <linux/termios.h>
#include <linux/wakelock.h>
#include <mach/gpio.h>
#include <mach/am_regs.h>
//#include <linux/gpio.h>
//#include <mach/msm_serial_hs.h>
#include <plat/wakeup.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h> /* event notifications */
#include <linux/serial_core.h>
#include "../../bluetooth/hci_uart.h"

#include <linux/amlogic/bluesleep.h>

//#define CONFIG_BT_HOST_WAKE
#define BT_SLEEP_DBG
#ifdef BT_DBG
#undef BT_DBG
#define BT_DBG(fmt, arg...) printk(KERN_INFO "%s " fmt "\n", __FUNCTION__, ##arg)
#endif
#ifndef BT_SLEEP_DBG
#define BT_DBG(fmt, arg...)
#endif
/*
 * Defines
 */

#define VERSION		"1.1"
#define PROC_DIR	"bluetooth/sleep"
#define BT_SLEEP "btwake_control"

static int btwake_control_start(void);
static void btwake_control_stop(void);

struct btwake_control_info {
	int gpio_host_wake;
	int gpio_ext_wake;
	int host_wake_irq_high;
	int host_wake_irq_low;
	struct wake_lock wake_lock;
	struct uart_port *uport;
};

static struct btwake_control_info *bsi;

#ifdef CONFIG_OF
static const struct of_device_id bt_sleep_match[]={
	{	.compatible = "amlogic,btwake_control",
	},
	{},
};
#else
#define bt_sleep_match NULL
#endif

/* 10 second timeout */
#define TX_TIMER_INTERVAL	10

static int ext_wake_active = 0;
static spinlock_t ext_wake_lock;

/* module usage */
static atomic_t open_count = ATOMIC_INIT(1);

/*
 * Global variables
 */

/** Transmission timer */
static struct timer_list tx_timer;

/** Lock for state transitions */
static spinlock_t rw_lock;

static struct bt_wake_ops btwake_control_ops;

struct proc_dir_entry *bluetooth_dir, *sleep_dir;

static int get_host_wake_value(void)
{
    if(bsi && bsi->gpio_host_wake)
        return amlogic_get_value(bsi->gpio_host_wake,BT_SLEEP);
    else
        return 0;
}
//EXPORT_SYMBOL(get_host_wake_value);
/*
 * Local functions
 */
static void set_bt_wake(int active)
{   
	unsigned long irq_flags;
			//gpio_out(bsi->ext_wake, 1);
	spin_lock_irqsave(&ext_wake_lock, irq_flags);
    if(bsi->gpio_ext_wake)
        amlogic_gpio_direction_output(bsi->gpio_ext_wake,active, BT_SLEEP);
	ext_wake_active = active;
	spin_unlock_irqrestore(&ext_wake_lock, irq_flags);
}	

/**
 * @return 1 if the Host can go to sleep, 0 otherwise.
 */
static int btwake_control_can_sleep(void)
{
	/* check if MSM_WAKE_BT_GPIO and BT_WAKE_MSM_GPIO are both deasserted */
	//return !ext_wake_active &&
     if(bsi->uport == NULL)
        BT_DBG("bsi->uport == NULL");
     else
        BT_DBG("bsi->uport != NULL");
	return !ext_wake_active && !get_host_wake_value();
}

/**
 * @brief@  main sleep work handling function which update the flags
 * and activate and deactivate UART ,check FIFO.
 */
extern unsigned int am_uart_tx_empty(struct uart_port *port);

/**
 * Handles proper timer action when outgoing data is delivered to the
 * HCI line discipline. Sets BT_TXDATA.
 */
static void btwake_control_outgoing_data(void)
{

	/* if the tx side is sleeping... */
	if (!ext_wake_active) {
		BT_DBG("tx was sleeping");
		set_bt_wake(1);
	}
    mod_timer(&tx_timer, jiffies + (TX_TIMER_INTERVAL * HZ));
}

static int btwake_control_setup_port(struct uart_port *port)
{
    if(bsi) {
        bsi->uport = port;
        printk("config bt uart port\n");
        return 0;
    }
    else {
        printk("btwake_control not init\n");
        return -1;
    }
}


static int btwake_control_show_proc_lpm(struct seq_file *m, void *v)
{
	return seq_printf(m, "unsupported to read\n");
}

static int btwake_control_open_proc_lpm(struct inode *inode, struct file *file)
{
    return single_open(file, btwake_control_show_proc_lpm, NULL);
}

static int btwake_control_write_proc_lpm(struct file *file, const char *buffer,
					size_t count, loff_t *data)
{
	char b;

	if (count < 1)
		return -EINVAL;

	if (copy_from_user(&b, buffer, 1))
		return -EFAULT;

	if (b == '0') {
		/* HCI_DEV_UNREG */
		btwake_control_stop();
        bsi->uport = NULL;
	} else {
		/* HCI_DEV_REG */
		//bsi->uport = btwake_control_get_uart_port();
		/* if bluetooth started, start btwake_control*/
		btwake_control_start();
	}
	return count;
}
static int btwake_control_show_proc_btwrite(struct seq_file *m, void *v)
{
	return seq_printf(m, "unsupported to read\n");
}

static int btwake_control_open_proc_btwrite(struct inode *inode, struct file *file)
{
    return single_open(file, btwake_control_show_proc_btwrite, NULL);
}

static int btwake_control_write_proc_btwrite(struct file *file, const char *buffer,
					size_t count, loff_t *data)
{
	char b;
	if (count < 1)
		return -EINVAL;
	if (copy_from_user(&b, buffer, 1))
		return -EFAULT;
	if (b != '0') {
		btwake_control_outgoing_data();
	}

	return count;
}

/**
 * Handles transmission timer expiration.
 * @param data Not used.
 */
static void btwake_control_tx_timer_expire(unsigned long data)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&rw_lock, irq_flags);

	BT_DBG("Tx timer expired");

    if(bsi != NULL && bsi->uport != NULL) {
    	if(am_uart_tx_empty(bsi->uport)) {
    		BT_DBG("BT go to sleep");
    		set_bt_wake(0);
    	}
    	else {
    		BT_DBG("TX not empty");
    		mod_timer(&tx_timer, jiffies + (TX_TIMER_INTERVAL * HZ));
    	}
    } else {
        printk("bsi is NULL or bsi->port is NULL");
    }

	spin_unlock_irqrestore(&rw_lock, irq_flags);
}

/**
 * Schedules a tasklet to run when receiving an interrupt on the
 * <code>HOST_WAKE</code> GPIO pin.
 * @param irq Not used.
 * @param dev_id Not used.
 */
static irqreturn_t btwake_control_hostwake_isr(int irq, void *dev_id)
{
	if(get_host_wake_value())
		wake_lock(&bsi->wake_lock);
	else
		 wake_lock_timeout(&bsi->wake_lock, HZ * 2);
	return IRQ_HANDLED;
}

/**
 * Starts the Sleep-Mode Protocol on the Host.
 * @return On success, 0. On error, -1, and <code>errno</code> is set
 * appropriately.
 */
static int btwake_control_start(void)
{
	int retval;

	if (!atomic_dec_and_test(&open_count)) {
		atomic_inc(&open_count);
		return -EBUSY;
	}

	/* assert BT_WAKE */
	set_bt_wake(1);	
	
    BT_DBG("request_irq\n");
    if(bsi->gpio_host_wake) {
        retval = request_irq((bsi->host_wake_irq_high + INT_GPIO_0) , btwake_control_hostwake_isr, IRQF_DISABLED , "bluetooth hostwake rise", bsi);
    	if (retval  < 0) {
    		BT_ERR("Couldn't acquire BT_HOST_WAKE RISE IRQ");
    		goto fail;
    	}
        retval = request_irq( (bsi->host_wake_irq_low + INT_GPIO_0) , btwake_control_hostwake_isr, IRQF_DISABLED ,"bluetooth hostwake fall", bsi);
    	if (retval  < 0) {
    		BT_ERR("Couldn't acquire BT_HOST_WAKE FALL IRQ");
    		goto fail2;
    	}
    }

	return 0;
fail2:
    if(bsi->host_wake_irq_high)
    free_irq(bsi->host_wake_irq_high + INT_GPIO_0, NULL);
fail:
	del_timer(&tx_timer);
	atomic_inc(&open_count);
	return retval;
}

/**
 * Stops the Sleep-Mode Protocol on the Host.
 */
static void btwake_control_stop(void)
{
	unsigned long irq_flags;

    spin_lock_irqsave(&rw_lock, irq_flags);

	del_timer(&tx_timer);

	atomic_inc(&open_count);

    spin_unlock_irqrestore(&rw_lock, irq_flags);

	//if (disable_irq_wake(bsi->host_wake_irq))
		//BT_ERR("Couldn't disable hostwake IRQ wakeup mode\n\n");
    BT_DBG("free_irq");
    if(bsi->gpio_host_wake)
	    free_irq(bsi->host_wake_irq_high + INT_GPIO_0, bsi);
        free_irq(bsi->host_wake_irq_low + INT_GPIO_0, bsi);

	wake_lock_timeout(&bsi->wake_lock, HZ / 2);
}

static int __init btwake_control_probe(struct platform_device *pdev)
{
	int ret;
	//struct resource *res;
    const char *str;

	if (!(pdev->dev.of_node)) {
        printk("btwake_control: pdev->dev.of_node == NULL!\n");
		ret = -ENODEV;
        return ret;
    }
//	bsi = bt_get_driver_data(pdev);	
	bsi = kzalloc(sizeof(struct btwake_control_info), GFP_KERNEL);
	if (!bsi)
	    return -ENOMEM;
    wake_lock_init(&bsi->wake_lock, WAKE_LOCK_SUSPEND, "wake_lock");
    
	BT_DBG("CONFIG_BT_HOST_WAKE"); 
	ret = of_property_read_string(pdev->dev.of_node,"gpio_host_wake",&str);
	if (ret) {
	    printk("couldn't find host_wake gpio\n");
    }
    else {
        bsi->gpio_host_wake = amlogic_gpio_name_map_num(str);	
        ret = amlogic_gpio_request(	bsi->gpio_host_wake, BT_SLEEP);
        if(ret) {
            BT_ERR("request bt host wake gpio failed\n");
            goto free_bsi;
        }
        ret = amlogic_disable_pullup(bsi->gpio_host_wake, BT_SLEEP);
        if(ret) {
            BT_ERR("disable bt host wake gpio pullup function failed\n");
            goto free_bt_host_wake;
        }
        ret = amlogic_gpio_direction_input(bsi->gpio_host_wake, BT_SLEEP);
        if(ret) {
            BT_ERR("set bt host wake gpio as input failed\n");
            goto free_bt_host_wake;
        }
    }

    BT_DBG("CONFIG_BT_WAKE"); 
	ret = of_property_read_string(pdev->dev.of_node,"gpio_ext_wake",&str);
	if (ret) {
		printk("couldn't find ext_wake gpio\n");
	}
    else {
        bsi->gpio_ext_wake = amlogic_gpio_name_map_num(str);
        ret = amlogic_gpio_request(bsi->gpio_ext_wake, BT_SLEEP);
        if(ret) {
            BT_ERR("request bt wake gpio failed\n");
            goto free_bt_host_wake;
        }
        ret = amlogic_gpio_direction_output(bsi->gpio_ext_wake, 1, BT_SLEEP);
        if (ret) {
            BT_ERR("set bt wake gpio as output failed\n");
            goto free_bt_ext_wake;
        }
    }
    
	spin_lock_init(&ext_wake_lock);
    set_bt_wake(1);			
    ret = of_property_read_u32(pdev->dev.of_node, "host_wake_irq_high", &bsi->host_wake_irq_high);	
	if(ret)
	   goto free_bsi;	
    ret = of_property_read_u32(pdev->dev.of_node, "host_wake_irq_low", &bsi->host_wake_irq_low);	
	if(ret)
	   goto free_bsi;	

    amlogic_gpio_to_irq(bsi->gpio_host_wake, BT_SLEEP,AML_GPIO_IRQ(bsi->host_wake_irq_high, FILTER_NUM7,GPIO_IRQ_RISING));
    amlogic_gpio_to_irq(bsi->gpio_host_wake, BT_SLEEP,AML_GPIO_IRQ(bsi->host_wake_irq_low, FILTER_NUM7,GPIO_IRQ_FALLING));		      

    btwake_control_ops.setup_bt_port = btwake_control_setup_port;
    btwake_control_ops.bt_can_sleep = btwake_control_can_sleep;
    register_bt_wake_ops(&btwake_control_ops); 
   	
    BT_DBG("---gpio_host_wake=%d,irq_num=%d, irq_type=%d,gpio_ext_wake=%d---",bsi->gpio_host_wake,bsi->host_wake_irq_low,bsi->host_wake_irq_high,bsi->gpio_ext_wake);
	return 0;

free_bt_ext_wake:
    if(bsi->gpio_ext_wake)
	    gpio_free(bsi->gpio_ext_wake);
free_bt_host_wake:
    if(bsi->gpio_host_wake)
	    gpio_free(bsi->gpio_host_wake);
free_bsi:
    bsi->gpio_host_wake = 0;
    bsi->gpio_ext_wake = 0;
    wake_lock_destroy(&bsi->wake_lock);
	kfree(bsi);
	return ret;
}

static int btwake_control_remove(struct platform_device *pdev)
{
	/* assert bt wake */
	//gpio_set_value(bsi->ext_wake, 1);
    set_bt_wake(1);

    free_irq(bsi->host_wake_irq_high + INT_GPIO_0, bsi);
    free_irq(bsi->host_wake_irq_low + INT_GPIO_0, bsi);
	del_timer(&tx_timer);

    if(bsi->gpio_host_wake)
	    gpio_free(bsi->gpio_host_wake);
    if(bsi->gpio_ext_wake)
	    gpio_free(bsi->gpio_ext_wake);
    bsi->gpio_host_wake = 0;
    bsi->gpio_ext_wake = 0;
	wake_lock_destroy(&bsi->wake_lock);
	kfree(bsi);
    unregister_bt_wake_ops(); 
	return 0;
}

static int btwake_control_suspend(struct platform_device *pdev, pm_message_t state)                                               
{
    return 0;
}

static int btwake_control_resume(struct platform_device *pdev)
{
    if (READ_AOBUS_REG(AO_RTI_STATUS_REG2) == FLAG_WAKEUP_BT) {
        wake_lock_timeout(&bsi->wake_lock, HZ * 5);
        WRITE_AOBUS_REG(AO_RTI_STATUS_REG2, 0);
    }
    return 0;
}




static struct platform_driver btwake_control_driver = {
	.remove = btwake_control_remove,
    .suspend = btwake_control_suspend,
    .resume = btwake_control_resume,
	.driver = {
		.name = "btwake_control",
		.owner = THIS_MODULE,
		.of_match_table = bt_sleep_match,
	},
};
/**
 * Initializes the module.
 * @return On success, 0. On error, -1, and <code>errno</code> is set
 * appropriately.
 */
static const struct file_operations proc_file_operations_lpm= {
    //.read       = btwake_control_read_proc_lpm,
    .open       = btwake_control_open_proc_lpm,
    .read       = seq_read,
    .write      = btwake_control_write_proc_lpm,
    .llseek     = seq_lseek,
	.release    = seq_release,
};

static const struct file_operations proc_file_operations_btwrite= {
    //.read       = btwake_control_read_proc_btwrite,
    .open       = btwake_control_open_proc_btwrite,
    .read       = seq_read,
    .write      = btwake_control_write_proc_btwrite,
    .llseek     = seq_lseek,
	.release    = seq_release,
};
 
static int __init btwake_control_init(void)
{
	int retval;
	struct proc_dir_entry *ent;
    BT_INFO("btwake_control_init Driver Ver %s",VERSION);
	retval = platform_driver_probe(&btwake_control_driver, btwake_control_probe);
	if (retval)
		return retval;

	bluetooth_dir = proc_mkdir("bluetooth", NULL);

	if (bluetooth_dir == NULL) {
		BT_ERR("Unable to create /proc/bluetooth directory");
		return -ENOMEM;
	}else
	BT_DBG("create /proc/bluetooth directory");

	sleep_dir = proc_mkdir("sleep", bluetooth_dir);
	if (sleep_dir == NULL) {
		BT_ERR("Unable to create /proc/%s directory", PROC_DIR);
		return -ENOMEM;
	}else
	BT_DBG("create /proc/%s directory", PROC_DIR);
	
	ent = proc_create("lpm",0777, sleep_dir,&proc_file_operations_lpm);
	BT_DBG("create /proc/%s/lpm entry", PROC_DIR);
	if (ent == NULL) {
		BT_ERR("Unable to create /proc/%s/lpm entry", PROC_DIR);
		retval = -ENOMEM;
		goto fail;
	}else
	BT_DBG("create /proc/%s/lpm entry", PROC_DIR);

	/* read/write proc entries */
	ent = proc_create("btwrite", 0777, sleep_dir,&proc_file_operations_btwrite);
	if (ent == NULL) {
		BT_ERR("Unable to create /proc/%s/btwrite entry", PROC_DIR);
		retval = -ENOMEM;
		goto fail;
	}else
	BT_DBG("create /proc/%s/btwrite entry", PROC_DIR);	

	/* Initialize spinlock. */
	spin_lock_init(&rw_lock);

	/* Initialize timer */
	init_timer(&tx_timer);
	tx_timer.function = btwake_control_tx_timer_expire;
	tx_timer.data = 0;

	return 0;

fail:
	remove_proc_entry("btwrite", sleep_dir);
	remove_proc_entry("lpm", sleep_dir);

	remove_proc_entry("sleep", bluetooth_dir);
	remove_proc_entry("bluetooth", 0);
	return retval;
}

/**
 * Cleans up the module.
 */
static void __exit btwake_control_exit(void)
{
	platform_driver_unregister(&btwake_control_driver);

	remove_proc_entry("btwrite", sleep_dir);
	remove_proc_entry("lpm", sleep_dir);

	remove_proc_entry("sleep", bluetooth_dir);
	remove_proc_entry("bluetooth", 0);
}

module_init(btwake_control_init);
module_exit(btwake_control_exit);

MODULE_DESCRIPTION("Bluetooth Wake Control Driver ver %s " VERSION);
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
