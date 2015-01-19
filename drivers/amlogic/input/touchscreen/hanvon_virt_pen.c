#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/version.h>
#include <asm/uaccess.h>

struct hanvon_char_dev 
{ 
    struct cdev cdev;
};

struct hanvon_pen_data
{
    u16 x;
    u16 y;
    u16 pressure;
    bool isLeave;
};
//#define HANVON_PEN_DEBUG
#define MAX_EVENTS		600

int isPenOn = 0;
static int global_major = 0;
static int global_minor = 0;
static struct input_dev* pen_idev = NULL;
static struct hanvon_char_dev *p_char_dev = NULL;
static struct class *hanvon_class;

static int hanvon_cdev_open (struct inode* inode, struct file* filp)
{
    return 0;
}

static int hanvon_cdev_release (struct inode* inode, struct file* filp)
{
    return 0;
}

static ssize_t hanvon_cdev_read (struct file *filp, char __user *buf, size_t count, loff_t *f_ops)
{
    printk (KERN_INFO "%s\n",__func__);
    return count;
}

static ssize_t hanvon_cdev_write (struct file *filp, const char __user *buf, size_t count, loff_t *offset)
{
#ifdef HANVON_PEN_DEBUG
    printk (KERN_INFO "%s\n",__func__);
#endif
    struct hanvon_pen_data data = {0};
    if (copy_from_user(&data,buf,sizeof(struct hanvon_pen_data)))
    {
        return -EFAULT;
    }
    do {
	isPenOn = 1;  // indicate that pen is on.
        input_report_abs(pen_idev, ABS_MT_TRACKING_ID, 0);
	input_report_abs(pen_idev, ABS_MT_TOUCH_MAJOR, data.pressure);
	input_report_abs(pen_idev, ABS_MT_POSITION_X, data.x);
	input_report_abs(pen_idev, ABS_MT_POSITION_Y, data.y);
	input_report_abs(pen_idev, ABS_MT_WIDTH_MAJOR, 0);
	input_mt_sync(pen_idev);
        input_sync(pen_idev);
    }while(false);
    if (data.isLeave == true)
        isPenOn = 0;
#ifdef HANVON_PEN_DEBUG
    printk(KERN_INFO "(pressure)=%d,(x)=%d,(y)=%d\n",data.pressure,data.x,data.y);
#endif
    return count;
}

static const struct file_operations hanvon_cdev_fops = {
    .owner     = THIS_MODULE,
    .open      = hanvon_cdev_open,
    .read      = hanvon_cdev_read,
    .write     = hanvon_cdev_write,
    .release   = hanvon_cdev_release,    
};

static struct hanvon_char_dev *hanvon_setup_cdev (dev_t dev)
{
    int result;
    struct hanvon_char_dev *pcdev = NULL;
    pcdev = kmalloc(1*sizeof(struct hanvon_char_dev),GFP_KERNEL);
    if (pcdev == NULL)
        printk(KERN_NOTICE "%s: malloc memmory failed!\n",__func__);
    memset(pcdev, 0, sizeof(struct hanvon_char_dev));
    cdev_init(&pcdev->cdev, &hanvon_cdev_fops);
    pcdev->cdev.owner = THIS_MODULE;
    result = cdev_add(&pcdev->cdev, dev, 1);
    if (result)
    {
        printk(KERN_NOTICE "%s: add cdev failed. \n", __func__);
    }
    return pcdev;
}

static struct input_dev * allocate_Input_Dev(void)
{
    int ret;
    struct input_dev *pInputDev=NULL;

    pInputDev = input_allocate_device();
    if(pInputDev == NULL)
    {
	printk(KERN_NOTICE, " Failed to allocate input device\n");
	return NULL;//-ENOMEM;
    }

    pInputDev->name = "Hanvon-Pen";
    pInputDev->phys = "UART";
    //pInputDev->id.bustype = BUS_I2C;
    pInputDev->id.vendor = 0x0F0F;
    pInputDev->id.product = 0x0099;
    pInputDev->id.version = 0x001;
	
    set_bit(EV_ABS, pInputDev->evbit);
#if 0
    set_bit(EV_KEY, pInputDev->evbit);
    __set_bit(BTN_LEFT, pInputDev->keybit);
    input_set_abs_params(pInputDev, ABS_X, 0, 2047, 0, 0);
    input_set_abs_params(pInputDev, ABS_Y, 0, 2047, 0, 0);
#endif
    input_set_abs_params(pInputDev, ABS_MT_POSITION_X, 0, 2047, 0, 0);
    input_set_abs_params(pInputDev, ABS_MT_POSITION_Y, 0, 2047, 0, 0);
    input_set_abs_params(pInputDev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(pInputDev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(pInputDev, ABS_MT_TRACKING_ID, 0, 1, 0, 0);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)
    input_set_events_per_packet(pInputDev, MAX_EVENTS);
#endif

    ret = input_register_device(pInputDev);
    if(ret) 
    {
	printk(KERN_NOTICE, " Unable to register input device.\n");
	input_free_device(pInputDev);
	pInputDev = NULL;
    }
    return pInputDev;
}

static int hanvon_pen_init (void)
{
    int result;
    dev_t devno = 0;
    if (global_major)
    {
        devno = MKDEV(global_major,global_minor);
	result = register_chrdev_region (devno,1,"hanvon_pen");
    }
    else
    {
        result = alloc_chrdev_region (&devno, global_minor, 1, "hanvon_pen");
	global_major = MAJOR(devno);
    }

    if (result < 0)
    {
        return 0;
    }
    
    p_char_dev = hanvon_setup_cdev (devno);
    if (!p_char_dev)
    {
        printk(KERN_NOTICE "%s: hanvon_setup_cdev failed .\n",__func__);
        result = -ENOMEM;
    }
    
    hanvon_class = class_create(THIS_MODULE, "hanvon_pen");
    if (IS_ERR(hanvon_class))
    {
	printk(KERN_NOTICE "%s: class_create failed .\n",__func__);
        return -EFAULT;
    }
    device_create(hanvon_class, NULL, devno, NULL, "hanvon_pen");
    printk(KERN_INFO "Registor hanvon pen char device done, major: %d\n",global_major);

    // Register hanvon input device
    pen_idev = allocate_Input_Dev();
    if (!pen_idev)
    {
        printk(KERN_NOTICE, "allocate input device failed.\n");
        return -EINVAL;
    }
    printk(KERN_INFO "Registor hanvon input device done.\n");

    return 0;
}

static void hanvon_pen_exit (void)
{
    dev_t devno = MKDEV(global_major, global_minor);
    if (p_char_dev)
    {
        cdev_del(&p_char_dev->cdev);
        kfree(p_char_dev);
        p_char_dev = NULL;
    }
    unregister_chrdev_region (devno, 1);
    if (!IS_ERR(hanvon_class))
    {
        device_destroy(hanvon_class, devno);
        class_destroy(hanvon_class);
    }
    printk (KERN_NOTICE "hanvon pen exit. \n");
}

#ifdef CONFIG_DEFERRED_MODULE_INIT
deferred_module_init(hanvon_pen_init);
#else
module_init(hanvon_pen_init);
#endif
module_exit(hanvon_pen_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hanvon Tec.");
MODULE_DESCRIPTION("pen driver for Hanvon");
MODULE_ALIAS("platform:virtual");
