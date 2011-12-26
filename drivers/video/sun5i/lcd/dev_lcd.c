#include "dev_lcd.h"

static struct cdev *my_cdev;
static dev_t devid ;
static struct class *lcd_class;


int lcd_open(struct inode *inode, struct file *file)
{
	return 0;
}

int lcd_release(struct inode *inode, struct file *file)
{
	return 0;
}


ssize_t lcd_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

ssize_t lcd_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    return -EINVAL;
}

int lcd_mmap(struct file *file, struct vm_area_struct * vma)
{
	return 0;
}

long lcd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static const struct file_operations lcd_fops = 
{
	.owner		      = THIS_MODULE,
	.open		        = lcd_open,
	.release        = lcd_release,
	.write          = lcd_write,
	.read		        = lcd_read,
	.unlocked_ioctl	= lcd_ioctl,
	.mmap           = lcd_mmap,
};

int lcd_init(void)
{
	static __lcd_panel_fun_t lcd0_cfg;
	static __lcd_panel_fun_t lcd1_cfg;

	memset(&lcd0_cfg, 0, sizeof(__lcd_panel_fun_t));
	memset(&lcd1_cfg, 0, sizeof(__lcd_panel_fun_t));

    LCD_get_panel_funs_0(&lcd0_cfg);
	LCD_get_panel_funs_1(&lcd1_cfg);
	LCD_set_panel_funs(&lcd0_cfg, &lcd1_cfg);

    DRV_DISP_Init();

	Fb_Init(0);
	
	return 0;
}

int __init lcd_module_init(void)
{
	int ret = 0, err;
	
	__inf("lcd_module_init\n");

	 alloc_chrdev_region(&devid, 0, 1, "lcd");
	 my_cdev = cdev_alloc();
	 cdev_init(my_cdev, &lcd_fops);
	 my_cdev->owner = THIS_MODULE;
	 err = cdev_add(my_cdev, devid, 1);
	 if (err)
	 {
		  __wrn("cdev_add fail.\n");
		  return -1;
	 }

    lcd_class = class_create(THIS_MODULE, "lcd");
    if (IS_ERR(lcd_class))
    {
        __wrn("class_create fail\n");
        return -1;
    }
    
	device_create(lcd_class, NULL, devid, NULL, "lcd");
	
	lcd_init();
	
	return ret;
}

static void __exit lcd_module_exit(void)
{
	__inf("lcd_module_exit\n");
		
		device_destroy(lcd_class,  devid);
		
    class_destroy(lcd_class);

    cdev_del(my_cdev);
}

late_initcall(lcd_module_init);
module_exit(lcd_module_exit);

MODULE_AUTHOR("danling_xiao");
MODULE_DESCRIPTION("lcd driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:lcd");

