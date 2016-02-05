//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  ODROID-C Board : GPIO IRQ driver for AMLogic.(charles.park)
//  2015.05.15
// 
//
//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/kdev_t.h>
#include <linux/idr.h>
#include <asm/irq.h>

#if defined(CONFIG_OF)
    #include <linux/of.h>
    #include <linux/of_gpio.h>
    #include <linux/of_device.h>
#endif

#if defined(CONFIG_MACH_MESON8B_ODROIDC)
    #include <linux/amlogic/aml_gpio_consumer.h>
#endif

//[*]--------------------------------------------------------------------------------------------------[*]
#define FLAG_TRUG_NONE  0
#define FLAG_TRIG_FALL	1	/* trigger on falling edge */
#define FLAG_TRIG_RISE	2	/* trigger on rising edge */

// define trigger type for edge
static const struct {
	const char      *name;
	unsigned long   flags;
}   trigger_types[] = {
	{ "none",    0 },
	{ "falling", (FLAG_TRIG_FALL) },
	{ "rising",  (FLAG_TRIG_RISE) },
	{ "both",    (FLAG_TRIG_FALL | FLAG_TRIG_RISE) },
};

const char  aml_gpio_str[] = "aml_gpio";

//[*]--------------------------------------------------------------------------------------------------[*]
#define NUM_OF_IRQ          (8)     // AMLogic IRQ count
#define NUM_OF_GPIO_DESC    (140)   // AMLogic GPIO count

struct   aml_gpio_desc    {
    bool                is_use;     // use flag
    struct  mutex       lock;       // sysfs lock
    int                 id;         // idr id
    int                 gpio;       // gpio number
    int                 num;        // desc num
    int                 direction;  // gpio direction
    int                 edge;       // irq trigger
    int                 irq_num[2]; // irq number (high edge and low edge)
};

struct   aml_gpio    {
    struct platform_device  *pdev;
    struct mutex            lock;
    struct idr              idr;
    struct aml_gpio_desc    desc[NUM_OF_GPIO_DESC];
    unsigned char           irq_use[NUM_OF_IRQ];
};

static  struct  aml_gpio    *aml_gpio;
//[*]--------------------------------------------------------------------------------------------------[*]
// function define
//[*]--------------------------------------------------------------------------------------------------[*]
static irqreturn_t aml_gpio_sysfs_irq   (int irq, void *priv);
static int match_export                 (struct device *dev, const void *data);
static int find_free_irq                (void);
static int aml_gpio_irq_setup           (struct device *dev, int desc, int edge);

static 	ssize_t show_direction  (struct device *dev, struct device_attribute *attr, char *buf);
static 	ssize_t show_edge	    (struct device *dev, struct device_attribute *attr, char *buf);
static 	ssize_t show_value	    (struct device *dev, struct device_attribute *attr, char *buf);
static 	ssize_t store_direction (struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static 	ssize_t store_value     (struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static 	ssize_t store_edge      (struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

//[*]--------------------------------------------------------------------------------------------------[*]
static DEVICE_ATTR(direction,   0644, show_direction,   store_direction);
static DEVICE_ATTR(value,       0644, show_value,       store_value);
static DEVICE_ATTR(edge,        0644, show_edge,        store_edge);

//[*]--------------------------------------------------------------------------------------------------[*]
static struct attribute *aml_gpio_sysfs_entries[] = {
	&dev_attr_direction.attr,
	&dev_attr_value.attr,
	&dev_attr_edge.attr,
	NULL
};

static struct attribute_group aml_gpio_sysfs_attr_group = {
	.name   = NULL,
	.attrs  = aml_gpio_sysfs_entries,
};

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static ssize_t store_export     (struct class *class, struct class_attribute *attr,
				const char *buf, size_t len);
static ssize_t store_unexport   (struct class *class, struct class_attribute *attr,
				const char *buf, size_t len);

//[*]--------------------------------------------------------------------------------------------------[*]
static struct class_attribute aml_gpio_class_attrs[] = {
	__ATTR(export,      0200, NULL, store_export),
	__ATTR(unexport,    0200, NULL, store_unexport),
	__ATTR_NULL,
};

static struct class aml_gpio_class = {
	.name   =	"aml_gpio",
	.owner  =   THIS_MODULE,

	.class_attrs =	aml_gpio_class_attrs,
};

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static irqreturn_t aml_gpio_sysfs_irq   (int irq, void *priv)
{
	struct sysfs_dirent	*value_sd = priv;

	sysfs_notify_dirent(value_sd);

	return IRQ_HANDLED;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int match_export     (struct device *dev, const void *data)
{
	return dev_get_drvdata(dev) == data;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int find_free_irq   (void)
{
    int     i;

    for(i = 0; i < NUM_OF_IRQ; i++) {
        if(!aml_gpio->irq_use[i])  {
            aml_gpio->irq_use[i] = true;
            return   (i);
        }
    }

    pr_err("%s : can't allocation gpio irq!\n", __func__);
    return  -1;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int aml_gpio_irq_setup   (struct device *dev, int desc, int edge)
{
	struct sysfs_dirent	*value_sd;
	int    ret = 0;

    if((value_sd = idr_find(&aml_gpio->idr, aml_gpio->desc[desc].id)))    {
        if(aml_gpio->desc[desc].edge & FLAG_TRIG_RISE)  {
            free_irq(INT_GPIO_0 + aml_gpio->desc[desc].irq_num[0], value_sd);
            aml_gpio->irq_use[aml_gpio->desc[desc].irq_num[0]] = false;
        }

        if(aml_gpio->desc[desc].edge & FLAG_TRIG_FALL)  {
            free_irq(INT_GPIO_0 + aml_gpio->desc[desc].irq_num[1], value_sd);
            aml_gpio->irq_use[aml_gpio->desc[desc].irq_num[1]] = false;
        }
    }
    else    {
    	value_sd = sysfs_get_dirent(dev->kobj.sd, NULL, "value");

		if (!value_sd)      return  -ENODEV;

		ret = idr_alloc(&aml_gpio->idr, value_sd, 1, 0, GFP_KERNEL);

		if (ret < 0)        goto    free_sd;

        aml_gpio->desc[desc].id = ret;
    }

    if(edge)    {
        // gpio input check
        if(aml_gpio->desc[desc].direction)  {
    		pr_err("%s : gpio direction error!(%d) fail!\n", __func__, aml_gpio->desc[desc].gpio);
            goto    free_sd;
        }

        if(edge & FLAG_TRIG_RISE)   {
            if((aml_gpio->desc[desc].irq_num[0] = find_free_irq()) != -1)    {

                ret = amlogic_gpio_to_irq(  aml_gpio->desc[desc].gpio,
                                            aml_gpio_str,
                                            AML_GPIO_IRQ(aml_gpio->desc[desc].irq_num[0], 0, 2));
                if(ret) {
                    aml_gpio->irq_use[aml_gpio->desc[desc].irq_num[0]] = false;
            		pr_err("%s : amlogic_gpio_to_irq(%d) fail!\n", __func__, aml_gpio->desc[desc].gpio);
            		goto    free_sd;
                }
                else    {
                	ret = request_any_context_irq(  INT_GPIO_0 + aml_gpio->desc[desc].irq_num[0],
                	                                aml_gpio_sysfs_irq,
                	                                IRQF_DISABLED,
                				                    aml_gpio_str,
                				                    value_sd);
                    if (ret < 0) {
                        aml_gpio->irq_use[aml_gpio->desc[desc].irq_num[0]] = false;
                		pr_err("%s : request_any_context_irq(%d) fail!\n", __func__, aml_gpio->desc[desc].gpio);
                		goto    free_sd;
                    }
                    
                    #if defined(CONFIG_ALLOC_CPU_FOR_IRQ)
                        if (irq_set_affinity(INT_GPIO_0 + aml_gpio->desc[desc].irq_num[0], cpumask_of(CONFIG_ALLOC_CPU_FOR_IRQ))) {
                            pr_warning("unable to set irq affinity (irq=%d, cpu=%d)\n",
                                    INT_GPIO_0 + aml_gpio->desc[desc].irq_num[0], CONFIG_ALLOC_CPU_FOR_IRQ);
                        }
                        else    {
                            pr_warning("allocation to set irq affinity (irq=%d, cpu=%d)\n",
                                    INT_GPIO_0 + aml_gpio->desc[desc].irq_num[0], CONFIG_ALLOC_CPU_FOR_IRQ);
                        }
                    #endif
                }
                aml_gpio->desc[desc].edge |= FLAG_TRIG_RISE;
            }
            else    {
                pr_err("%s (FLAG_TRIG_RISE) : can't allocation gpio(%d) irq!\n", __func__, aml_gpio->desc[desc].gpio);
                ret = -ENOMEM;
            }
        }

        if(edge & FLAG_TRIG_FALL)   {
            if((aml_gpio->desc[desc].irq_num[1] = find_free_irq()) != -1)    {

                ret = amlogic_gpio_to_irq(  aml_gpio->desc[desc].gpio,
                                            aml_gpio_str,
                                            AML_GPIO_IRQ(aml_gpio->desc[desc].irq_num[1], 0, 3));
                if(ret) {
                    aml_gpio->irq_use[aml_gpio->desc[desc].irq_num[1]] = false;
            		pr_err("%s : amlogic_gpio_to_irq(%d) fail!\n", __func__, aml_gpio->desc[desc].gpio);
            		goto    free_sd;
                }
                else    {
                	ret = request_any_context_irq(  INT_GPIO_0 + aml_gpio->desc[desc].irq_num[1],
                	                                aml_gpio_sysfs_irq,
                	                                IRQF_DISABLED,
                				                    aml_gpio_str,
                				                    value_sd);
                    if (ret < 0) {
                        aml_gpio->irq_use[aml_gpio->desc[desc].irq_num[1]] = false;
                		pr_err("%s : request_any_context_irq(%d) fail!\n", __func__, aml_gpio->desc[desc].gpio);
                		goto    free_sd;
                    }
                    #if defined(CONFIG_ALLOC_CPU_FOR_IRQ)
                        if (irq_set_affinity(INT_GPIO_0 + aml_gpio->desc[desc].irq_num[1], cpumask_of(CONFIG_ALLOC_CPU_FOR_IRQ))) {
                            pr_warning("unable to set irq affinity (irq=%d, cpu=%d)\n",
                                    INT_GPIO_0 + aml_gpio->desc[desc].irq_num[1], CONFIG_ALLOC_CPU_FOR_IRQ);
                        }
                        else    {
                            pr_warning("allocation to set irq affinity (irq=%d, cpu=%d)\n",
                                    INT_GPIO_0 + aml_gpio->desc[desc].irq_num[1], CONFIG_ALLOC_CPU_FOR_IRQ);
                        }
                    #endif
                }
                aml_gpio->desc[desc].edge |= FLAG_TRIG_FALL;
            }
            else    {
                pr_err("%s (FLAG_TRIG_FALL) : can't allocation gpio(%d) irq!\n", __func__, aml_gpio->desc[desc].gpio);
                ret = -ENOMEM;
            }
        }
        if(ret)     return  ret;

        aml_gpio->desc[desc].edge  = edge;

        return  0;
    }
    else    {
        if(aml_gpio->desc[desc].edge & FLAG_TRIG_RISE)  {
            aml_gpio->irq_use[aml_gpio->desc[desc].irq_num[0]] = false;
        }

        if(aml_gpio->desc[desc].edge & FLAG_TRIG_FALL)  {
            aml_gpio->irq_use[aml_gpio->desc[desc].irq_num[1]] = false;
        }

        ret = 0;
    }

free_sd:
    if(value_sd)    {
        idr_remove(&aml_gpio->idr, aml_gpio->desc[desc].id);
        aml_gpio->desc[desc].id     = 0;
        aml_gpio->desc[desc].edge   = 0;
    }

    return  ret;
}

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t show_direction  (struct device *dev, struct device_attribute *attr, char *buf)
{
    struct aml_gpio_desc *aml_gpio_desc = dev_get_drvdata(dev);

	return	sprintf(buf, "%s\n", aml_gpio_desc->direction ? "out":"in");
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t show_value	    (struct device *dev, struct device_attribute *attr, char *buf)
{
    struct aml_gpio_desc *aml_gpio_desc = dev_get_drvdata(dev);

	return	sprintf(buf, "%d\n", amlogic_get_value(aml_gpio_desc->gpio, aml_gpio_str));
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t show_edge	    (struct device *dev, struct device_attribute *attr, char *buf)
{
    struct aml_gpio_desc *aml_gpio_desc = dev_get_drvdata(dev);
    int     i;
	ssize_t status = -EINVAL;

    mutex_lock(&aml_gpio_desc->lock);

	for (i = 0; i < ARRAY_SIZE(trigger_types); i++) {
	    if(aml_gpio_desc->edge == trigger_types[i].flags)    {
			status = sprintf(buf, "%s\n", trigger_types[i].name);
			break;
	    }
	}

    mutex_unlock(&aml_gpio_desc->lock);

	return	status;
}

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t store_direction (struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct aml_gpio_desc *aml_gpio_desc = dev_get_drvdata(dev);
	ssize_t     status;

    mutex_lock(&aml_gpio_desc->lock);
    if      (sysfs_streq(buf, "high"))  {
        status = amlogic_gpio_direction_output(aml_gpio_desc->gpio, 1, aml_gpio_str);
        if(!status)     aml_gpio_desc->direction = 1;
    }
    else if (sysfs_streq(buf, "out") || sysfs_streq(buf, "low"))    {
        status = amlogic_gpio_direction_output(aml_gpio_desc->gpio, 0, aml_gpio_str);
        if(!status)     aml_gpio_desc->direction = 1;
    }
    else if (sysfs_streq(buf, "in"))    {
        status = amlogic_gpio_direction_input(aml_gpio_desc->gpio, aml_gpio_str);
        if(!status)     aml_gpio_desc->direction = 0;
    }
    else
        status = -EINVAL;

    mutex_unlock(&aml_gpio_desc->lock);

    return status ? : count;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t store_value     (struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct aml_gpio_desc *aml_gpio_desc = dev_get_drvdata(dev);
    long    val;

    if(strict_strtol(buf, 0, &val) < 0)     goto done;

    mutex_lock(&aml_gpio_desc->lock);

    if(aml_gpio_desc->direction)
        amlogic_set_value(aml_gpio_desc->gpio, val ? 1 : 0, aml_gpio_str);

    mutex_unlock(&aml_gpio_desc->lock);

done:
    return count;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t store_edge      (struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct aml_gpio_desc *aml_gpio_desc = dev_get_drvdata(dev);
	ssize_t status;
	int			i;

	for (i = 0; i < ARRAY_SIZE(trigger_types); i++)
		if (sysfs_streq(trigger_types[i].name, buf))
			goto done;
	return -EINVAL;

done:
    mutex_lock(&aml_gpio_desc->lock);

    if((status = aml_gpio_irq_setup(dev, aml_gpio_desc->num, trigger_types[i].flags)))
        aml_gpio_irq_setup(dev, aml_gpio_desc->num, 0);

    mutex_unlock(&aml_gpio_desc->lock);

    if(status)  pr_err("%s : status %d\n", __func__, status);

    return status ? : count;
}

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static ssize_t store_export     (struct class *class, struct class_attribute *attr,
				const char *buf, size_t len)
{
    long            gpio;
    int             status, i;
	struct device	*dev = NULL;

    if((status = strict_strtol(buf, 0, &gpio)) < 0)     goto done;

	if((status = amlogic_gpio_request_one(gpio, GPIOF_IN, aml_gpio_str)))	{
		pr_err("%s : %ld gpio reqest err!\n", __FUNCTION__, gpio);
		goto    done;
	}
	else    {
    	amlogic_disable_pullup(gpio, aml_gpio_str);
	}

    // mutex_lock
    mutex_lock(&aml_gpio->lock);

    // find free gpio desc
    for(i = 0; i < NUM_OF_GPIO_DESC; i++) {
        if(!aml_gpio->desc[i].is_use)   break;
    }

    if(i == NUM_OF_GPIO_DESC) {
        pr_err("%s : Can't find free gpio desc!\n", __func__);
        status = -ENOMEM;
        goto    unlock_done;
    }

    // device create
    dev = device_create(&aml_gpio_class,
                        &aml_gpio->pdev->dev,
                        MKDEV(0, 0),
                        &aml_gpio->desc[i],
                        "gpio%ld", gpio);

    if (IS_ERR(dev))    {
        status = PTR_ERR(dev);
        goto    unlock_done;
    }

    // gpio sysfs create
    if ((status = sysfs_create_group(&dev->kobj, &aml_gpio_sysfs_attr_group)))
        goto    unregister_device;

    memset(&aml_gpio->desc[i], 0x00, sizeof(struct aml_gpio_desc));

    mutex_init(&aml_gpio->desc[i].lock);

    aml_gpio->desc[i].is_use    = true;

    aml_gpio->desc[i].gpio      = gpio;

    aml_gpio->desc[i].num       = i;

    dev_set_drvdata(dev, &aml_gpio->desc[i]);

    goto unlock_done;

unregister_device:
	device_unregister(dev);

unlock_done:
    // mutex_unlock
    mutex_unlock(&aml_gpio->lock);

done:
    if(status)  pr_err("%s : status %d\n", __func__, status);

    return  status ? : len;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static ssize_t store_unexport   (struct class *class, struct class_attribute *attr,
				const char *buf, size_t len)
{
    long            gpio;
    int             status, i;
	struct device	*dev = NULL;
    
    if((status = strict_strtol(buf, 0, &gpio)) < 0)     goto done;

    for(i = 0; i < NUM_OF_GPIO_DESC; i++) {
        if(gpio == aml_gpio->desc[i].gpio)  break;
    }

    if(i == NUM_OF_GPIO_DESC) {
        pr_err("%s : Can't find gpio%ld desc!\n", __func__, gpio);
        status = -ENODEV;
        goto done;
    }

    // mutex_lock
    mutex_lock(&aml_gpio->lock);

	dev = class_find_device(&aml_gpio_class, NULL, &aml_gpio->desc[i], match_export);

	if(dev) {
        aml_gpio_irq_setup(dev, i, 0);

    	sysfs_remove_group(&dev->kobj, &aml_gpio_sysfs_attr_group);
    	device_unregister(dev);     put_device(dev);
	}

    amlogic_gpio_free(aml_gpio->desc[i].gpio, aml_gpio_str);
    memset(&aml_gpio->desc[i], 0x00, sizeof(struct aml_gpio_desc));

    // mutex_unlock
    mutex_unlock(&aml_gpio->lock);

done:
    if(status)  pr_err("%s : status %d\n", __func__, status);

    return  status ? : len;
}

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static int aml_gpio_probe   (struct platform_device *pdev)	
{
	int status;

	if ((status = class_register(&aml_gpio_class)) < 0)
		return status;

    if(!(aml_gpio = devm_kzalloc(&pdev->dev, sizeof(struct aml_gpio), GFP_KERNEL)))  {
        pr_err("memory not allocated for aml_gpio(%s).\n", __func__);
        return  -ENOMEM;
    }

	idr_init(&aml_gpio->idr);

    aml_gpio->pdev = pdev;

	dev_set_drvdata(&pdev->dev, aml_gpio);

    mutex_init(&aml_gpio->lock);

    pr_info("%s : success\n", __func__);

	return status;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int aml_gpio_remove  (struct platform_device *pdev)	
{
	struct device	*dev = NULL;
	int     i;

    // mutex_lock
    mutex_lock(&aml_gpio->lock);

    for(i = 0; i < NUM_OF_GPIO_DESC; i++) {
        if(aml_gpio->desc[i].is_use)    {
            // gpio class remove
        	dev = class_find_device(&aml_gpio_class, NULL, &aml_gpio->desc[i], match_export);
        
        	if(dev) {
                aml_gpio_irq_setup(dev, i, 0);
        
            	sysfs_remove_group(&dev->kobj, &aml_gpio_sysfs_attr_group);
            	device_unregister(dev);     put_device(dev);
        	}
            amlogic_gpio_free(aml_gpio->desc[i].gpio, aml_gpio_str);
        }
    }
    // mutex_unlock
    mutex_unlock(&aml_gpio->lock);

    class_unregister(&aml_gpio_class);

	idr_destroy(&aml_gpio->idr);

    pr_info("%s : success\n", __func__);

    return	0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
#if defined(CONFIG_OF)
static const struct of_device_id aml_gpio_dt[] = {
	{ .compatible = "aml-gpio" },
	{ },
};
MODULE_DEVICE_TABLE(of, aml_gpio_dt);
#endif

static struct platform_driver aml_gpio_driver = {
	.driver = {
		.name = "aml-gpio",
		.owner = THIS_MODULE,
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(aml_gpio_dt),
#endif
	},
	.probe 		= aml_gpio_probe,
	.remove 	= aml_gpio_remove,
};

//[*]--------------------------------------------------------------------------------------------------[*]
static int __init aml_gpio_init     (void)
{	
    return platform_driver_register(&aml_gpio_driver);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static void __exit aml_gpio_exit    (void)
{
    platform_driver_unregister(&aml_gpio_driver);
}

//[*]--------------------------------------------------------------------------------------------------[*]
module_init(aml_gpio_init);
module_exit(aml_gpio_exit);

//[*]--------------------------------------------------------------------------------------------------[*]
MODULE_DESCRIPTION("AMLoogic GPIO LIB for GPIO IRQ");
MODULE_AUTHOR("Hard-Kernel");
MODULE_LICENSE("GPL");

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
