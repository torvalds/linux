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
#include <mach/iomux.h>
#include <mach/gpio.h>
#include <asm/gpio.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/earlysuspend.h>

#include <linux/bp-auto.h>

#if 0
#define DBG(x...)  printk(x)
#else
#define DBG(x...)
#endif

struct bp_private_data *g_bp;
static struct class *g_bp_class;
static struct bp_operate *g_bp_ops[BP_ID_NUM]; 
struct class *bp_class = NULL; 

static void ap_wakeup_bp(struct bp_private_data *bp, int wake)
{
	if(bp->ops->ap_wake_bp)
		bp->ops->ap_wake_bp(bp, wake);	
	
}

static int bp_request_gpio(struct bp_private_data *bp)
{
	int result = 0;
	
	if(bp->pdata->gpio_valid)
	{
		if(bp->pdata->bp_power > 0)
		{
			bp->ops->bp_power = bp->pdata->bp_power;
		}

		if(bp->pdata->bp_en > 0)
		{
			bp->ops->bp_en = bp->pdata->bp_en;
		}

		if(bp->pdata->bp_reset > 0)
		{
			bp->ops->bp_reset = bp->pdata->bp_reset;
		}

		if(bp->pdata->ap_ready > 0)
		{
			bp->ops->ap_ready = bp->pdata->ap_ready;
		}

		if(bp->pdata->bp_ready > 0)
		{
			bp->ops->bp_ready = bp->pdata->bp_ready;
		}

		if(bp->pdata->ap_wakeup_bp > 0)
		{
			bp->ops->ap_wakeup_bp = bp->pdata->ap_wakeup_bp;
		}

		if(bp->pdata->bp_wakeup_ap > 0)
		{
			bp->ops->bp_wakeup_ap = bp->pdata->bp_wakeup_ap;
		}

		if(bp->pdata->bp_usb_en > 0)
		{
			bp->ops->bp_usb_en = bp->pdata->bp_usb_en;
		}
		
		if(bp->pdata->bp_uart_en > 0)
		{
			bp->ops->bp_uart_en = bp->pdata->bp_uart_en;
		}

	}
	
	if(bp->ops->bp_power != BP_UNKNOW_DATA)
	{
		result = gpio_request(bp->ops->bp_power, "bp_power");
		if(result)
		{
			printk("%s:fail to request gpio %d\n",__func__, bp->ops->bp_power);
			//return -1;
		}
	}
	
	if(bp->ops->bp_en != BP_UNKNOW_DATA)
	{
		result = gpio_request(bp->ops->bp_en, "bp_en");
		if(result)
		{
			printk("%s:fail to request gpio %d\n",__func__, bp->ops->bp_en);
			//return -1;
		}
	}


	if(bp->ops->bp_reset != BP_UNKNOW_DATA)
	{
		result = gpio_request(bp->ops->bp_reset, "bp_reset");
		if(result)
		{
			printk("%s:fail to request gpio %d\n",__func__, bp->ops->bp_reset);
			//return -1;
		}
	}


	if(bp->ops->ap_ready != BP_UNKNOW_DATA)
	{
		result = gpio_request(bp->ops->ap_ready, "ap_ready");
		if(result)
		{
			printk("%s:fail to request gpio %d\n",__func__, bp->ops->ap_ready);
			//return -1;
		}
	}


	if(bp->ops->bp_ready != BP_UNKNOW_DATA)
	{
		result = gpio_request(bp->ops->bp_ready, "bp_ready");
		if(result)
		{
			printk("%s:fail to request gpio %d\n",__func__, bp->ops->bp_ready);
			//return -1;
		}
	}


	if(bp->ops->ap_wakeup_bp != BP_UNKNOW_DATA)
	{
		result = gpio_request(bp->ops->ap_wakeup_bp, "ap_wakeup_bp");
		if(result)
		{
			printk("%s:fail to request gpio %d\n",__func__, bp->ops->ap_wakeup_bp);
			//return -1;
		}
	}

	if(bp->ops->bp_wakeup_ap != BP_UNKNOW_DATA)
	{
		result = gpio_request(bp->ops->bp_wakeup_ap, "bp_wakeup_ap");
		if(result)
		{
			printk("%s:fail to request gpio %d\n",__func__, bp->ops->bp_wakeup_ap);
			//return -1;
		}
	}

	if(bp->ops->bp_usb_en != BP_UNKNOW_DATA)
	{
		result = gpio_request(bp->ops->bp_usb_en, "bp_usb_en");
		if(result)
		{
			printk("%s:fail to request gpio %d\n",__func__, bp->ops->bp_usb_en);
			//return -1;
		}
	}

	if(bp->ops->bp_uart_en != BP_UNKNOW_DATA)
	{
		result = gpio_request(bp->ops->bp_uart_en, "bp_uart_en");
		if(result)
		{
			printk("%s:fail to request gpio %d\n",__func__, bp->ops->bp_uart_en);
			//return -1;
		}
	}
	
	return result;
}


static irqreturn_t bp_wake_up_irq(int irq, void *dev_id)
{
	
	struct bp_private_data *bp = dev_id;
	if(bp->ops->bp_wake_ap)
		bp->ops->bp_wake_ap(bp);
	
	return IRQ_HANDLED;
}

static int bp_id_open(struct inode *inode, struct file *file)
{
	struct bp_private_data *bp = g_bp;
	
	return 0;
}

static int bp_id_release(struct inode *inode, struct file *file)
{

	return 0;
}

static long bp_id_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct bp_private_data *bp = g_bp;
	void __user *argp = (void __user *)arg;
	int result = 0;

	switch(cmd)
	{	
		case BP_IOCTL_SET_PVID:
			
			break;
	
		case BP_IOCTL_GET_BPID:
			if (copy_to_user(argp, &bp->ops->bp_id, sizeof(bp->ops->bp_id)))
			{
	            		printk("%s:failed to copy status to user space.\n",__FUNCTION__);
				return -EFAULT;
			}
			
			break;
			
		default:
			break;
	}
	
	return 0;
}


static int bp_dev_open(struct inode *inode, struct file *file)
{
	struct bp_private_data *bp = g_bp;
	device_init_wakeup(bp->dev, 1);
	return 0;
}
static ssize_t bp_dev_write(struct file *file, const char __user *buf,size_t len, loff_t *off)
{	
	static char cmd[2];
	struct bp_private_data *bp = g_bp;
	
	int ret = 0;
	if (len > 2) 
	{
		return -EINVAL;
	}
	ret = copy_from_user(&cmd, buf, len);
	if (ret != 0) {
		return -EFAULT;
	}
	printk(" received cmd = %c\n",cmd[0]);
	switch(bp->ops->bp_id)
	{
		case BP_ID_MT6229:
			if (cmd[0] == '0')
			{
				gpio_direction_output(bp->ops->ap_ready, GPIO_LOW);
			}	
			if (cmd[0] == '1')
			{
				gpio_direction_output(bp->ops->ap_ready, GPIO_HIGH);
			}
			if (cmd[0] == '2')
			{
				gpio_direction_output(bp->ops->bp_uart_en, GPIO_LOW);
			}
			if (cmd[0] == '3')
			{
				gpio_direction_output(bp->ops->bp_uart_en, GPIO_HIGH);
			}
			if (cmd[0] == '4')
			{
				gpio_direction_output(bp->ops->bp_usb_en, GPIO_HIGH);
			}if (cmd[0] == '5')
			{
				gpio_direction_output(bp->ops->bp_usb_en, GPIO_LOW);
			}
			break;
		
		case BP_ID_MU509:
			break;

		default:
			break;

	}
	return len;
}
static int bp_dev_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long bp_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct bp_private_data *bp = g_bp;
	void __user *argp = (void __user *)arg;
	int result = 0;

	switch(cmd)
	{
		case BP_IOCTL_RESET:	
			if(bp->ops->reset)
			{
				bp->ops->reset(bp);
			}
			else if(bp->ops->active)
			{
				bp->ops->active(bp, 0);
				msleep(100);
				bp->ops->active(bp, 1);
			}
			break;
			
		case BP_IOCTL_POWON:
			if(bp->ops->active)
			bp->ops->active(bp, 1);
			break;
			
		case BP_IOCTL_POWOFF:
			if(bp->ops->active)
			bp->ops->active(bp, 0);
			break;
	
		case BP_IOCTL_WRITE_STATUS:
			
			break;
	
		case BP_IOCTL_GET_STATUS:
			
			break;

		case BP_IOCTL_SET_PVID:
			
			break;
	
		case BP_IOCTL_GET_BPID:
			if (copy_to_user(argp, &bp->ops->bp_id, sizeof(bp->ops->bp_id)))
			{
	            		printk("%s:failed to copy status to user space.\n",__FUNCTION__);
				return -EFAULT;
			}
			
			break;
			
		default:
			break;
	}
	
	return 0;
}

static ssize_t bp_status_read(struct class *cls, struct class_attribute *attr, char *_buf)
{
	struct bp_private_data *bp = g_bp;
	
	return sprintf(_buf, "%d\n", bp->status);
	
}

static ssize_t bp_status_write(struct class *cls, struct class_attribute *attr, const char *_buf, size_t _count)
{	
	struct bp_private_data *bp = g_bp;
	int result = 0;
	int status = 0;
	
	status = simple_strtoul(_buf, NULL, 16);
	if(status == bp->status) 
		return _count;
	
	bp->status = status;
	
	if(bp->ops->write_status)
		result = bp->ops->write_status(bp);	
	   
	return result; 
}
static CLASS_ATTR(bp_status, 0777, bp_status_read, bp_status_write);
static int bp_probe(struct platform_device *pdev)
{
	struct bp_platform_data *pdata = pdev->dev.platform_data;
	struct bp_private_data *bp = NULL;
	int i = 0, result;	

	if(!pdata)
		return -1;
	
	DBG("%s:init start\n",__func__);
	
	if(pdata->init_platform_hw)
		pdata->init_platform_hw();
	
	bp = kzalloc(sizeof(struct bp_private_data), GFP_KERNEL);
	if(bp == NULL)
	{
		printk("%s:fail malloc bp data\n",__func__);
		return -1;
	}

	bp->pdata = pdata;
	bp->dev = &pdev->dev;
	
	//select modem acccording to pdata defaultly
	if((pdata->bp_id > BP_ID_INVALID) && (pdata->bp_id < BP_ID_NUM))
	{
		if(g_bp_ops[pdata->bp_id])
		{
			bp->ops = g_bp_ops[pdata->bp_id];
			printk("%s:bp_id=%d\n",__func__,bp->ops->bp_id);
		}
		else
		{
			printk("%s:error:g_bp_ops[%d] = 0x%p\n",__func__, pdata->bp_id, g_bp_ops[pdata->bp_id]);
		}
		
	}
	else
	{
		printk("%s:bp_id=%d is out of range\n",__func__, pdata->bp_id);
	}
	
	bp_request_gpio(bp);
	
	if((bp->ops->bp_wakeup_ap) && (bp->ops->trig != BP_UNKNOW_DATA))
	{
		result = request_irq(bp->ops->bp_wakeup_ap, bp_wake_up_irq, bp->ops->trig, "bp_wakeup_ap", bp);
		if (result < 0) {
			printk("%s: request_irq(%d) failed\n", __func__, bp->ops->bp_wakeup_ap);
			gpio_free(pdata->bp_wakeup_ap);
			return result;
		}
	}

	if(bp->ops->init)
		bp->ops->init(bp);
	
	enable_irq_wake(bp->ops->bp_wakeup_ap);
	wake_lock_init(&bp->bp_wakelock, WAKE_LOCK_SUSPEND, "bp_wakelock");
	
	bp->status = BP_OFF;

	if(!bp->ops->private_miscdev)
	{
		bp->fops.owner = THIS_MODULE;
		bp->fops.open = bp_dev_open;
		bp->fops.write = bp_dev_write;
		bp->fops.release = bp_dev_release;	
		bp->fops.unlocked_ioctl = bp_dev_ioctl;

		bp->miscdev.minor = MISC_DYNAMIC_MINOR;
		if(bp->ops->misc_name)
		bp->miscdev.name = bp->ops->misc_name;
		else	
		bp->miscdev.name = "bp-auto";
		bp->miscdev.fops = &bp->fops;
	}
	else
	{
		memcpy(&bp->miscdev, bp->ops->private_miscdev, sizeof(*bp->ops->private_miscdev));

	}
	
	result = misc_register(&bp->miscdev);
	if (result < 0) {
		printk("misc_register err\n");
		return result;
	}

	bp->id_fops.owner = THIS_MODULE;
	bp->id_fops.open = bp_id_open;
	bp->id_fops.release = bp_id_release;	
	bp->id_fops.unlocked_ioctl = bp_id_ioctl;

	bp->id_miscdev.minor = MISC_DYNAMIC_MINOR;
	bp->id_miscdev.name = "bp_id";
	bp->id_miscdev.fops = &bp->id_fops;
	result = misc_register(&bp->id_miscdev);
	if (result < 0) {
		printk("misc_register err\n");
		return result;
	}
	
	g_bp = bp;

	platform_set_drvdata(pdev, bp);	
	
	printk("%s:init success\n",__func__);
	return result;

}

int bp_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct bp_private_data *bp = platform_get_drvdata(pdev);
	
	if(bp->ops->suspend)
		bp->ops->suspend(bp);
	
	return 0;
}

int bp_resume(struct platform_device *pdev)
{
	struct bp_private_data *bp = platform_get_drvdata(pdev);
	
	if(bp->ops->resume)
		bp->ops->resume(bp);

	return 0;
}

void bp_shutdown(struct platform_device *pdev)
{
	struct bp_private_data *bp = platform_get_drvdata(pdev);

	if(bp->ops->shutdown)
		bp->ops->shutdown(bp);
	
	if(bp->ops->bp_power != BP_UNKNOW_DATA)
	{
		gpio_free(bp->ops->bp_power);	
	}
	
	if(bp->ops->bp_en != BP_UNKNOW_DATA)
	{
		gpio_free(bp->ops->bp_en);
		
	}

	if(bp->ops->bp_reset != BP_UNKNOW_DATA)
	{
		gpio_free(bp->ops->bp_reset);	
	}
	
	if(bp->ops->ap_ready != BP_UNKNOW_DATA)
	{
		gpio_free(bp->ops->ap_ready);
		
	}
	
	if(bp->ops->bp_ready != BP_UNKNOW_DATA)
	{
		gpio_free(bp->ops->bp_ready);
		
	}
	
	if(bp->ops->ap_wakeup_bp != BP_UNKNOW_DATA)
	{
		gpio_free(bp->ops->ap_wakeup_bp);
		
	}
	
	if(bp->ops->bp_wakeup_ap != BP_UNKNOW_DATA)
	{
		gpio_free(bp->ops->bp_wakeup_ap);
		
	}
	
	if(bp->ops->bp_usb_en != BP_UNKNOW_DATA)
	{
		gpio_free(bp->ops->bp_usb_en);
		
	}
	
	if(bp->ops->bp_uart_en != BP_UNKNOW_DATA)
	{
		gpio_free(bp->ops->bp_uart_en);
		
	}
	
	if(bp->pdata->exit_platform_hw)
		bp->pdata->exit_platform_hw();
	
	kfree(bp);
	
}


int bp_register_slave(struct bp_private_data *bp,
			struct bp_platform_data *slave_pdata,
			struct bp_operate *(*get_bp_ops)(void))
{
	int result = 0;
	struct bp_operate *ops = get_bp_ops();
	if((ops->bp_id >= BP_ID_NUM) || (ops->bp_id <= BP_ID_INVALID))
	{	
		printk("%s:%s id is error %d\n", __func__, ops->name, ops->bp_id);
		return -1;	
	}
	g_bp_ops[ops->bp_id] = ops;
	printk("%s:%s,id=%d\n",__func__,g_bp_ops[ops->bp_id]->name, ops->bp_id);
	return result;
}


int bp_unregister_slave(struct bp_private_data *bp,
			struct bp_platform_data *slave_pdata,
			struct bp_operate *(*get_bp_ops)(void))
{
	int result = 0;
	struct bp_operate *ops = get_bp_ops();
	if((ops->bp_id >= BP_ID_NUM) || (ops->bp_id <= BP_ID_INVALID))
	{	
		printk("%s:%s id is error %d\n", __func__, ops->name, ops->bp_id);
		return -1;	
	}
	printk("%s:%s,id=%d\n",__func__,g_bp_ops[ops->bp_id]->name, ops->bp_id);
	g_bp_ops[ops->bp_id] = NULL;	
	return result;
}


static struct platform_driver bp_driver = {
	.probe		= bp_probe,
	.shutdown	= bp_shutdown,
	.suspend  	= bp_suspend,
	.resume		= bp_resume,
	.driver	= {
		.name	= "bp-auto",
		.owner	= THIS_MODULE,
	},
};

static int __init bp_init(void)
{
	int ret ;
	bp_class = class_create(THIS_MODULE, "bp-auto");
	ret =  class_create_file(bp_class, &class_attr_bp_status);
	if (ret)
	{
		printk("Fail to create class bp-auto\n");
	}
	return platform_driver_register(&bp_driver);
}

static void __exit bp_exit(void)
{
	platform_driver_unregister(&bp_driver);
	class_remove_file(bp_class, &class_attr_bp_status);
}

module_init(bp_init);
module_exit(bp_exit);

MODULE_AUTHOR("ROCKCHIP Corporation:lw@rock-chips.com");
MODULE_DESCRIPTION("device interface for auto modem driver");
MODULE_LICENSE("GPL");

