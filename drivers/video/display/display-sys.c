#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/idr.h>
#include <linux/err.h>
#include <linux/kdev_t.h>
#include <linux/display-sys.h>

static struct list_head display_device_list;

static ssize_t display_show_name(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	return snprintf(buf, PAGE_SIZE, "%s\n", dsp->name);
}

static ssize_t display_show_type(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	return snprintf(buf, PAGE_SIZE, "%s\n", dsp->type);
}

static ssize_t display_show_enable(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	int enable;
	if(dsp->ops && dsp->ops->getenable)
		enable = dsp->ops->getenable(dsp);
	else
		return 0;
	return snprintf(buf, PAGE_SIZE, "%d\n", enable);
}

static ssize_t display_store_enable(struct device *dev, 
						struct device_attribute *attr,
			 			const char *buf, size_t size)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	int enable;
	
	sscanf(buf, "%d", &enable);
	if(dsp->ops && dsp->ops->setenable)
		dsp->ops->setenable(dsp, enable);
	return size;
}

static ssize_t display_show_connect(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	int connect;
	if(dsp->ops && dsp->ops->getstatus)
		connect = dsp->ops->getstatus(dsp);
	else
		return 0;
	return snprintf(buf, PAGE_SIZE, "%d\n", connect);
}

static int mode_string(char *buf, unsigned int offset,
		       const struct fb_videomode *mode)
{
//	char m = 'U';
	char v = 'p';

//	if (mode->flag & FB_MODE_IS_DETAILED)
//		m = 'D';
//	if (mode->flag & FB_MODE_IS_VESA)
//		m = 'V';
//	if (mode->flag & FB_MODE_IS_STANDARD)
//		m = 'S';

	if (mode->vmode & FB_VMODE_INTERLACED)
		v = 'i';
	if (mode->vmode & FB_VMODE_DOUBLE)
		v = 'd';

	return snprintf(&buf[offset], PAGE_SIZE - offset, "%dx%d%c-%d\n",
	                mode->xres, mode->yres, v, mode->refresh);
}
static ssize_t display_show_modes(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	struct list_head *modelist, *pos;
	struct fb_modelist *fb_modelist;
	const struct fb_videomode *mode;
	int i;
	if(dsp->ops && dsp->ops->getmodelist)
	{
		if(dsp->ops->getmodelist(dsp, &modelist))
			return -EINVAL;
	}
	else
		return 0;

	i = 0;
	list_for_each(pos, modelist) {
		fb_modelist = list_entry(pos, struct fb_modelist, list);
		mode = &fb_modelist->mode;
		i += mode_string(buf, i, mode);
	}
	return i;
}

static ssize_t display_show_mode(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	struct fb_videomode mode;
	
	if(dsp->ops && dsp->ops->getmode)
		if(dsp->ops->getmode(dsp, &mode) == 0)
			return mode_string(buf, 0, &mode);
	return 0;
}

static ssize_t display_store_mode(struct device *dev, 
						struct device_attribute *attr,
			 			const char *buf, size_t count)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	char mstr[100];
	struct list_head *modelist, *pos;
	struct fb_modelist *fb_modelist;
	struct fb_videomode *mode;     
	size_t i;                   

	if(dsp->ops && dsp->ops->getmodelist)
	{
		if(dsp->ops && dsp->ops->getmodelist)
		{
			if(dsp->ops->getmodelist(dsp, &modelist))
				return -EINVAL;
		}
		list_for_each(pos, modelist) {
			fb_modelist = list_entry(pos, struct fb_modelist, list);
			mode = &fb_modelist->mode;
			i = mode_string(mstr, 0, mode);
			if (strncmp(mstr, buf, max(count, i)) == 0) {
				if(dsp->ops && dsp->ops->setmode)
					dsp->ops->setmode(dsp, mode);
				return count;
			}
		}
	}
	return -EINVAL;
}

static struct device_attribute display_attrs[] = {
	__ATTR(name, S_IRUGO, display_show_name, NULL),
	__ATTR(type, S_IRUGO, display_show_type, NULL),
	__ATTR(enable, S_IRUGO | /*S_IWUGO*/S_IWUSR, display_show_enable, display_store_enable),
	__ATTR(connect, S_IRUGO, display_show_connect, NULL),
	__ATTR(modes, S_IRUGO, display_show_modes, NULL),
	__ATTR(mode, S_IRUGO | /*S_IWUGO*/S_IWUSR, display_show_mode, display_store_mode)
};

static int display_suspend(struct device *dev, pm_message_t state)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);

	mutex_lock(&dsp->lock);
	if (likely(dsp->driver->suspend))
		dsp->driver->suspend(dsp, state);
	mutex_unlock(&dsp->lock);
	return 0;
};

static int display_resume(struct device *dev)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);

	mutex_lock(&dsp->lock);
	if (likely(dsp->driver->resume))
		dsp->driver->resume(dsp);
	mutex_unlock(&dsp->lock);
	return 0;
};

void rk_display_device_enable(struct rk_display_device *ddev)
{
#ifndef CONFIG_DISPLAY_AUTO_SWITCH	
	return;
#else
	struct list_head *pos, *head = &display_device_list;
	struct rk_display_device *dev = NULL, *dev_enabled = NULL, *dev_enable = NULL;
	int enable = 0,connect, has_connect = 0;
	
	list_for_each(pos, head) {
		dev = list_entry(pos, struct rk_display_device, list);
		enable = dev->ops->getenable(dev);
		connect = dev->ops->getstatus(dev);
		if(connect)
			dev_enable = dev;
		if(enable == 1)
			dev_enabled = dev;
	}
	// If no device is connected, enable highest priority device.
	if(dev_enable == NULL) {
		dev->ops->setenable(dev, 1);
		return;
	}
	
	if(dev_enable == dev_enabled) {
		if(dev_enable != ddev)
			ddev->ops->setenable(ddev, 0);
	}
	else {
		if(dev_enabled)
			dev_enabled->ops->setenable(dev_enabled, 0);
		dev_enable->ops->setenable(dev_enable, 1);
	}
		

#endif
}
EXPORT_SYMBOL(rk_display_device_enable);

void rk_display_device_enable_other(struct rk_display_device *ddev)
{
#ifndef CONFIG_DISPLAY_AUTO_SWITCH	
	return;
#else
	struct list_head *pos, *head = &display_device_list;
	struct rk_display_device *dev;	
	int connect = 0;
	
	list_for_each_prev(pos, head) {
		dev = list_entry(pos, struct rk_display_device, list);
		if(dev != ddev)
		{
			connect = dev->ops->getstatus(dev);
			if(connect)
			{
				dev->ops->setenable(dev, 1);
				return;
			}
		}
	}
#endif
}
EXPORT_SYMBOL(rk_display_device_enable_other);

void rk_display_device_disable_other(struct rk_display_device *ddev)
{
#ifndef CONFIG_DISPLAY_AUTO_SWITCH
	return;
#else
	struct list_head *pos, *head = &display_device_list;
	struct rk_display_device *dev;	
	int enable = 0;
	
	list_for_each(pos, head) {
		dev = list_entry(pos, struct rk_display_device, list);
		if(dev != ddev)
		{
			enable = dev->ops->getenable(dev);
			if(enable)
				dev->ops->setenable(dev, 0);
		}
	}
	ddev->ops->setenable(ddev, 1);
#endif
}
EXPORT_SYMBOL(rk_display_device_disable_other);

void rk_display_device_select(int priority)
{
	struct list_head *pos, *head = &display_device_list;
	struct rk_display_device *dev;
	int enable, found = 0;
	
	list_for_each(pos, head) {
		dev = list_entry(pos, struct rk_display_device, list);
		if(dev->priority == priority)
			found = 1;
	}
	
	if(!found)
	{
		printk("[%s] select display interface %d not exist\n", __FUNCTION__, priority);
		return;
	}
	
	list_for_each(pos, head) {
		dev = list_entry(pos, struct rk_display_device, list);
		enable = dev->ops->getenable(dev);
		if(dev->priority == priority)
		{
			if(!enable)	
				dev->ops->setenable(dev, 1);
		}
		else if(enable) 
			dev->ops->setenable(dev, 0);
	}
}
EXPORT_SYMBOL(rk_display_device_select);
static struct mutex allocated_dsp_lock;
static DEFINE_IDR(allocated_dsp);
static struct class *display_class;

struct rk_display_device *rk_display_device_register(struct rk_display_driver *driver,
						struct device *parent, void *devdata)
{
	struct rk_display_device *new_dev = NULL;
	int ret = -EINVAL;

	if (unlikely(!driver))
		return ERR_PTR(ret);

	mutex_lock(&allocated_dsp_lock);
	ret = idr_pre_get(&allocated_dsp, GFP_KERNEL);
	mutex_unlock(&allocated_dsp_lock);
	if (!ret)
		return ERR_PTR(ret);

	new_dev = kzalloc(sizeof(struct rk_display_device), GFP_KERNEL);
	if (likely(new_dev) && unlikely(driver->probe(new_dev, devdata))) {
		// Reserve the index for this display
		mutex_lock(&allocated_dsp_lock);
		ret = idr_get_new(&allocated_dsp, new_dev, &new_dev->idx);
		mutex_unlock(&allocated_dsp_lock);

		if (!ret) {
			new_dev->dev = device_create(display_class, parent,
						     MKDEV(0, 0), new_dev,
						     "%s", new_dev->type);
			if (!IS_ERR(new_dev->dev)) {
				new_dev->parent = parent;
				new_dev->driver = driver;
				new_dev->dev->driver = parent->driver;
				mutex_init(&new_dev->lock);
				// Add new device to display device list.
				{
					struct list_head *pos, *head = &display_device_list;
					struct rk_display_device *dev;
					
					list_for_each(pos, head) {
						dev = list_entry(pos, struct rk_display_device, list);
						if(dev->priority > new_dev->priority)
							break;
					}
					list_add_tail(&new_dev->list, pos);
				}
				return new_dev;
			}
			mutex_lock(&allocated_dsp_lock);
			idr_remove(&allocated_dsp, new_dev->idx);
			mutex_unlock(&allocated_dsp_lock);
			ret = -EINVAL;
		}
	}
	kfree(new_dev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(rk_display_device_register);

void rk_display_device_unregister(struct rk_display_device *ddev)
{
	if (!ddev)
		return;
	// Free device
	mutex_lock(&ddev->lock);
	device_unregister(ddev->dev);
	mutex_unlock(&ddev->lock);
	// Mark device index as avaliable
	mutex_lock(&allocated_dsp_lock);
	idr_remove(&allocated_dsp, ddev->idx);
	mutex_unlock(&allocated_dsp_lock);
	list_del(&ddev->list);
	kfree(ddev);
}
EXPORT_SYMBOL(rk_display_device_unregister);

static int __init rk_display_class_init(void)
{
	display_class = class_create(THIS_MODULE, "display");
	if (IS_ERR(display_class)) {
		printk(KERN_ERR "Failed to create display class\n");
		display_class = NULL;
		return -EINVAL;
	}
	display_class->dev_attrs = display_attrs;
	display_class->suspend = display_suspend;
	display_class->resume = display_resume;
	mutex_init(&allocated_dsp_lock);
	INIT_LIST_HEAD(&display_device_list);
	return 0;
}

static void __exit rk_display_class_exit(void)
{
	class_destroy(display_class);
}

subsys_initcall(rk_display_class_init);
module_exit(rk_display_class_exit);


MODULE_AUTHOR("zhengyang@rock-chips.com");
MODULE_DESCRIPTION("Driver for rk display device");
MODULE_LICENSE("GPL");
