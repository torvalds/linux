#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <asm/types.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <linux/wait.h>
#include <mach/board.h>




#if 1
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif


#define NEWTON_GPIO_R     RK29_PIN4_PB0
#define NEWTON_GPIO_G     RK29_PIN4_PB1
#define NEWTON_GPIO_B     RK29_PIN4_PB2
#define NEWTON_GET_UID    0x6001
typedef struct{
uint16_t SN_Size;		//0-1
char SN[30];			//2-31
char Reserved[419];		//32-450
char IMEI_Size;			//451
char IMEI_Data[15];		//452-466
char UID_Size;			//467
char UID_Data[30];		//468-497
char BT_Size;			//498
char BlueTooth[6];		//499-504
char Mac_Size;			//505
char Mac_Data[6];		//506-511
}IdbSector3;
static int led_state = 0;
char GetSNSectorInfo(char * pbuf);

int newton_print_buf(char *buf,int size)
{
	int i,j,mo=size%16,line = size/16;
	char *pbuf = buf;

	if(line>0)
	{
		for(i=0;i<line;i++)
		{
			for(j=0;j<16;j++)
			{
				printk("0x%02x ",*pbuf);
				pbuf++;
			}
			printk("\n");
		}
	}
	
	for(j=0;j<mo;j++)
	{
		printk("0x%02x ",*pbuf);
		pbuf++;
	}
}

int rk29_newton_open(struct inode *inode, struct file *filp)
{
    DBG("%s\n",__FUNCTION__);
    return 0;
}

ssize_t rk29_newton_read(struct file *filp, char __user *ptr, size_t size, loff_t *pos)
{
    DBG("%s\n",__FUNCTION__);
	return sizeof(int);
}

ssize_t rk29_newton_write(struct file *filp, char __user *ptr, size_t size, loff_t *pos)
{
    return sizeof(int);
}

int rk29_newton_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
    void __user *argp = (void __user *)arg;
    DBG("%s\n",__FUNCTION__);
	
    switch(cmd)
    {
    case NEWTON_GET_UID:
		{
			IdbSector3 sn;
			memset(&sn,0,sizeof(IdbSector3));
			GetSNSectorInfo(&sn);
			//newton_print_buf(&sn.UID_Data, sizeof(sn.UID_Data));
            if(copy_to_user(argp, &sn.UID_Data, sizeof(sn.UID_Data)))  return -EFAULT;
    	}
		break;
	default:
		break;
	}
	return 0;
}


int rk29_newton_release(struct inode *inode, struct file *filp)
{
    DBG("%s\n",__FUNCTION__);
    
	return 0;
}


static struct file_operations rk29_newton_fops = {
	.owner   = THIS_MODULE,
	.open    = rk29_newton_open,
	//.read    = rk29_newton_read,
	//.write	 = rk29_newton_write,
	.ioctl   = rk29_newton_ioctl,
	//.release = rk29_newton_release,
};


static struct miscdevice rk29_newton_dev = 
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = "newton",
    .fops = &rk29_newton_fops,
};


static int rk29_newton_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct rk29_newton_data *pdata = pdev->dev.platform_data;
	if(!pdata)
		return -1;
	DBG("%s",__FUNCTION__);
	ret = misc_register(&rk29_newton_dev);
	if (ret < 0){
		printk("rk29 newton register err!\n");
		return ret;
	}
#if 0
    if(gpio_request(NEWTON_GPIO_R,NULL) != 0){
      gpio_free(NEWTON_GPIO_R);
      printk("rk29_newton_probe gpio_request NEWTON_GPIO_R error\n");
      return -EIO;
    }
	
    if(gpio_request(NEWTON_GPIO_G,NULL) != 0){
      gpio_free(NEWTON_GPIO_G);
      printk("rk29_newton_probe gpio_request NEWTON_GPIO_G error\n");
      return -EIO;
    }
	
    if(gpio_request(NEWTON_GPIO_B,NULL) != 0){
      gpio_free(NEWTON_GPIO_B);
      printk("rk29_newton_probe gpio_request NEWTON_GPIO_B error\n");
      return -EIO;
    }
	
    gpio_direction_output(NEWTON_GPIO_R, 0);
	gpio_set_value(NEWTON_GPIO_R,GPIO_LOW);
	gpio_direction_output(NEWTON_GPIO_G, 0);
	gpio_set_value(NEWTON_GPIO_G,GPIO_LOW);
	gpio_direction_output(NEWTON_GPIO_B, 0);
	gpio_set_value(NEWTON_GPIO_B,GPIO_LOW);
#endif
	DBG("%s:rk29 newton initialized\n",__FUNCTION__);

	return ret;
}

static int rk29_newton_remove(struct platform_device *pdev)
{
	misc_deregister(&rk29_newton_dev);
	return 0;
}


int rk29_newton_suspend(struct platform_device *pdev,  pm_message_t state)
{
	return 0;	
}

int rk29_newton_resume(struct platform_device *pdev)
{
	return 0;
}


static struct platform_driver rk29_newton_driver = {
	.probe	    = rk29_newton_probe,
	.remove     = rk29_newton_remove,
	.suspend  	= rk29_newton_suspend,
	.resume		= rk29_newton_resume,
	.driver	    = {
		.name	= "rk29_newton",
		.owner	= THIS_MODULE,
	},
};

static int __init rk29_newton_init(void)
{
	return platform_driver_register(&rk29_newton_driver);
}

static void __exit rk29_newton_exit(void)
{
	platform_driver_unregister(&rk29_newton_driver);
}

module_init(rk29_newton_init);
module_exit(rk29_newton_exit);
MODULE_DESCRIPTION ("rk29 newton misc driver");
MODULE_LICENSE("GPL");

