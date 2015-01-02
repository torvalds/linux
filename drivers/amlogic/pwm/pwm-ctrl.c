//[*]--------------------------------------------------------------------------------------------------[*]
//
//  ODROID Board : PWM ctrl driver
//
//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/pwm.h>

#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>

struct pwm_ctrl {
	struct pwm_device 	*pwm0;
	struct pwm_device 	*pwm1;
	struct mutex		mutex;
	int	pwm0_status,pwm1_status;
	int freq0,freq1;
	int duty0,duty1;
};

//[*]------------------------------------------------------------------------------------------------------------------
//
// driver sysfs attribute define
//
//[*]------------------------------------------------------------------------------------------------------------------
static	ssize_t set_enable0	(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct pwm_ctrl *ctrl = dev_get_drvdata(dev);
	unsigned int	val;

    if(!(sscanf(buf, "%u\n", &val)))	return	-EINVAL;
	dev_info(dev, "PWM_0 : %s [%d] \n",__FUNCTION__,val);

	mutex_lock(&ctrl->mutex);
    if(val) {
    	ctrl->pwm0_status = 1;
		pwm_disable(ctrl->pwm0);
		pwm_config(ctrl->pwm0, ctrl->duty0, ctrl->freq0);
    	pwm_enable(ctrl->pwm0);
    }
    else {
    	pwm_disable(ctrl->pwm0);
		pwm_config(ctrl->pwm0, 0, ctrl->freq0);
    	ctrl->pwm0_status = 0;
    }
	mutex_unlock(&ctrl->mutex);

	return count;
}

static	ssize_t show_status0	(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pwm_ctrl *ctrl = dev_get_drvdata(dev);

	if(ctrl->pwm0_status)	return	sprintf(buf, "PWM_0 : %s\n", "on");
	else					return	sprintf(buf, "PWM_0 : %s\n", "off");
}

//[*]------------------------------------------------------------------------------------------------------------------
//[*]------------------------------------------------------------------------------------------------------------------
static	ssize_t set_duty0	(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct pwm_ctrl *ctrl = dev_get_drvdata(dev);
	unsigned int	val;

    if(!(sscanf(buf, "%u\n", &val)))	return	-EINVAL;

	if((val > 100)||(val < 0)){
		dev_err(dev, "PWM_0 : Invalid param. Duty cycle range is 0 to 100 \n");
		return count;
	}

	dev_info(dev, "PWM_0 : %s [%d] \n",__FUNCTION__,val);

	mutex_lock(&ctrl->mutex);
	ctrl->duty0 = val;

    if(ctrl->pwm0_status){
		pwm_disable(ctrl->pwm0);
		pwm_config(ctrl->pwm0, ctrl->duty0, ctrl->freq0);
    	pwm_enable(ctrl->pwm0);
    }
    else {
		pwm_disable(ctrl->pwm0);
		pwm_config(ctrl->pwm0, 0, ctrl->freq0);
    }
	mutex_unlock(&ctrl->mutex);
	
	return count;
}

static	ssize_t show_duty0	(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pwm_ctrl *ctrl = dev_get_drvdata(dev);

	return	sprintf(buf, "%d\n", ctrl->duty0);
}

//[*]------------------------------------------------------------------------------------------------------------------
//[*]------------------------------------------------------------------------------------------------------------------
static	ssize_t set_freq0	(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct pwm_ctrl *ctrl = dev_get_drvdata(dev);
	unsigned int	val;

    if(!(sscanf(buf, "%u\n", &val)))	return	-EINVAL;

	if((val < 0)){
		dev_err(dev, "PWM_0 : Invalid param. Duty cycle range is 0 to 100 \n");
		return count;
	}

	dev_info(dev, "PWM_0 : %s [%d] \n",__FUNCTION__,val);

	mutex_lock(&ctrl->mutex);
	ctrl->freq0 = val;

    if(ctrl->pwm0_status){
		pwm_disable(ctrl->pwm0);
		pwm_config(ctrl->pwm0, ctrl->duty0, ctrl->freq0);
    	pwm_enable(ctrl->pwm0);
    }
    else {
		pwm_disable(ctrl->pwm0);
		pwm_config(ctrl->pwm0, 0, ctrl->freq0);
    }
	mutex_unlock(&ctrl->mutex);
	
	return count;
}

static	ssize_t show_freq0	(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pwm_ctrl *ctrl = dev_get_drvdata(dev);

	return	sprintf(buf, "%d\n", ctrl->freq0);
}
//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static	DEVICE_ATTR(enable0, S_IRWXUGO, show_status0, set_enable0);
static	DEVICE_ATTR(freq0, S_IRWXUGO, show_freq0, set_freq0);
static	DEVICE_ATTR(duty0, S_IRWXUGO, show_duty0, set_duty0);

static struct attribute *pwm0_ctrl_sysfs_entries[] = {
	&dev_attr_enable0.attr,
	&dev_attr_freq0.attr,
	&dev_attr_duty0.attr,
	NULL
};

static struct attribute_group pwm0_ctrl_attr_group = {
	.name   = NULL,
	.attrs  = pwm0_ctrl_sysfs_entries,
};


//[*]------------------------------------------------------------------------------------------------------------------
static	ssize_t set_enable1	(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct pwm_ctrl *ctrl = dev_get_drvdata(dev);
	unsigned int	val;

    if(!(sscanf(buf, "%u\n", &val)))	return	-EINVAL;
	dev_info(dev, "PWM_1 : %s [%d] \n",__FUNCTION__,val);

	mutex_lock(&ctrl->mutex);
    if(val) {
    	ctrl->pwm1_status = 1;
		pwm_disable(ctrl->pwm1);
		pwm_config(ctrl->pwm1, ctrl->duty1, ctrl->freq1);
    	pwm_enable(ctrl->pwm1);
    }
    else {
    	pwm_disable(ctrl->pwm1);
		pwm_config(ctrl->pwm1, 0, ctrl->freq1);
    	ctrl->pwm1_status = 0;
    }
	mutex_unlock(&ctrl->mutex);

	return count;
}

static	ssize_t show_status1	(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pwm_ctrl *ctrl = dev_get_drvdata(dev);

	if(ctrl->pwm1_status)	return	sprintf(buf, "PWM_1 : %s\n", "on");
	else					return	sprintf(buf, "PWM_1 : %s\n", "off");
}

//[*]------------------------------------------------------------------------------------------------------------------
//[*]------------------------------------------------------------------------------------------------------------------
static	ssize_t set_duty1	(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct pwm_ctrl *ctrl = dev_get_drvdata(dev);
	unsigned int	val;

    if(!(sscanf(buf, "%u\n", &val)))	return	-EINVAL;

	if((val > 100)||(val < 0)){
		dev_err(dev, "PWM_1 : Invalid param. Duty cycle range is 0 to 100 \n");
		return count;
	}
	dev_info(dev, "PWM_1 : %s [%d] \n",__FUNCTION__,val);

	mutex_lock(&ctrl->mutex);
	ctrl->duty1 = val;

    if(ctrl->pwm1_status){
		pwm_disable(ctrl->pwm1);
		pwm_config(ctrl->pwm1, ctrl->duty1, ctrl->freq1);
    	pwm_enable(ctrl->pwm1);
    }
    else {
		pwm_disable(ctrl->pwm1);
		pwm_config(ctrl->pwm1, 0, ctrl->freq1);
    }
	mutex_unlock(&ctrl->mutex);
	
	return count;
}

static	ssize_t show_duty1	(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pwm_ctrl *ctrl = dev_get_drvdata(dev);

	return	sprintf(buf, "%d\n", ctrl->duty1);
}

//[*]------------------------------------------------------------------------------------------------------------------
//[*]------------------------------------------------------------------------------------------------------------------
static	ssize_t set_freq1	(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct pwm_ctrl *ctrl = dev_get_drvdata(dev);
	unsigned int	val;

    if(!(sscanf(buf, "%u\n", &val)))	return	-EINVAL;

	if((val < 10)||(val > 1000000)){
		dev_err(dev, "PWM_1 : Invalid param. Duty cycle range is 10 to 1MHz \n");
		return count;
	}
	dev_info(dev, "PWM_1 : %s [%d] \n",__FUNCTION__,val);
	mutex_lock(&ctrl->mutex);
	ctrl->freq1 = val;

    if(ctrl->pwm1_status){
		pwm_disable(ctrl->pwm1);
		pwm_config(ctrl->pwm1, ctrl->duty1, ctrl->freq1);
    	pwm_enable(ctrl->pwm1);
    }
    else {
		pwm_disable(ctrl->pwm1);
		pwm_config(ctrl->pwm1, 0, ctrl->freq1);
    }
	mutex_unlock(&ctrl->mutex);
	
	return count;
}

static	ssize_t show_freq1	(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pwm_ctrl *ctrl = dev_get_drvdata(dev);

	return	sprintf(buf, "%d\n", ctrl->freq1);
}
//[*]------------------------------------------------------------------------------------------------------------------
//[*]------------------------------------------------------------------------------------------------------------------
static	DEVICE_ATTR(enable1, S_IRWXUGO, show_status1, set_enable1);
static	DEVICE_ATTR(freq1, S_IRWXUGO, show_freq1, set_freq1);
static	DEVICE_ATTR(duty1, S_IRWXUGO, show_duty1, set_duty1);
static struct attribute *pwm1_ctrl_sysfs_entries[] = {
	&dev_attr_enable1.attr,
	&dev_attr_freq1.attr,
	&dev_attr_duty1.attr,
	NULL
};
static struct attribute_group pwm1_ctrl_attr_group = {
	.name   = NULL,
	.attrs  = pwm1_ctrl_sysfs_entries,
};

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static int	pwm_ctrl_resume(struct platform_device *dev)
{
	#if	defined(DEBUG_PM_MSG)
		dev_info(dev,"%s\n", __FUNCTION__);
	#endif

    return  0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int	pwm_ctrl_suspend(struct platform_device *dev, pm_message_t state)
{
	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	int		pwm_ctrl_probe		(struct platform_device *pdev)	
{
	struct pwm_ctrl *ctrl;
	struct device 	*dev = &pdev->dev;
	int ret=0;

	ctrl = devm_kzalloc(&pdev->dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl) {
		dev_err(&pdev->dev, "no memory for state\n");
		ret = -ENOMEM;
		goto err_alloc;
	}

	ctrl->pwm0 = pwm_request(0, "pwm-ctrl");
	if (IS_ERR(ctrl->pwm0)) {
		dev_err(&pdev->dev, "unable to request legacy PWM\n");
		ret = PTR_ERR(ctrl->pwm0);
		goto err_request;
	}

	ctrl->pwm1 = pwm_request(1, "pwm-ctrl");
	if (IS_ERR(ctrl->pwm1)) {
		dev_err(&pdev->dev, "unable to request legacy PWM\n");
		ctrl->pwm1=NULL;
	}

	mutex_init(&ctrl->mutex);
	dev_set_drvdata(dev, ctrl);

	ret =sysfs_create_group(&dev->kobj, &pwm0_ctrl_attr_group);
	if(ret < 0)	{
		dev_err(&pdev->dev, "failed to create sysfs group !!\n");
	}
    
    if(ctrl->pwm1 != NULL){
    	ret =sysfs_create_group(&dev->kobj, &pwm1_ctrl_attr_group);
    	if(ret < 0)	{
    		dev_err(&pdev->dev, "failed to create sysfs group !!\n");
    	}
    }

	return 0;

err_request:
    devm_kfree(&pdev->dev, ctrl);
    kfree(ctrl);
err_alloc:
	return ret;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	int		pwm_ctrl_remove		(struct platform_device *pdev)	
{
    struct device 	*dev = &pdev->dev;
	struct pwm_ctrl *ctrl = dev_get_drvdata(dev);


    if(ctrl->pwm1 != NULL){
    	sysfs_remove_group(&dev->kobj, &pwm1_ctrl_attr_group);
    }
   	sysfs_remove_group(&dev->kobj, &pwm0_ctrl_attr_group);
    
    if(ctrl->pwm1)
        pwm_free(ctrl->pwm1);

    if(ctrl->pwm0)
        pwm_free(ctrl->pwm0);

    devm_kfree(dev, ctrl);

    return	0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
#if defined(CONFIG_OF)
static const struct of_device_id pwm_ctrl_dt[] = {
	{ .compatible = "amlogic, pwm-ctrl" },
	{ },
};
#endif

//[*]--------------------------------------------------------------------------------------------------[*]
static struct platform_driver pwm_ctrl_driver = {
	.driver = {
		.name = "pwm-ctrl",
		.owner = THIS_MODULE,
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(pwm_ctrl_dt),
#endif
	},
	.probe 		= pwm_ctrl_probe,
	.remove 	= pwm_ctrl_remove,
	.suspend	= pwm_ctrl_suspend,
	.resume		= pwm_ctrl_resume,
};

//[*]--------------------------------------------------------------------------------------------------[*]
static int __init pwm_ctrl_init(void)
{
    return platform_driver_register(&pwm_ctrl_driver);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static void __exit pwm_ctrl_exit(void)
{
    platform_driver_unregister(&pwm_ctrl_driver);
}

//[*]--------------------------------------------------------------------------------------------------[*]
module_init(pwm_ctrl_init);
module_exit(pwm_ctrl_exit);

//[*]--------------------------------------------------------------------------------------------------[*]
MODULE_DESCRIPTION("PWM ctrl driver for odroid-Dev board");
MODULE_AUTHOR("HardKernel");
MODULE_LICENSE("GPL");

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
