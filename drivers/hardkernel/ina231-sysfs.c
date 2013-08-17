//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  I2C INA231(Sensor) driver
//  2013.07.17
// 
//
//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>

#include <linux/platform_data/ina231.h>

//[*]--------------------------------------------------------------------------------------------------[*]
#include "ina231-i2c.h"
#include "ina231-misc.h"

//[*]--------------------------------------------------------------------------------------------------[*]
//
//   sysfs function prototype define
//
//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_name	(struct device *dev, struct device_attribute *attr, char *buf);
static	DEVICE_ATTR(sensor_name, S_IRWXUGO, show_name, NULL);

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_power			(struct device *dev, struct device_attribute *attr, char *buf);
static	DEVICE_ATTR(sensor_W, S_IRWXUGO, show_power, NULL);

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_current		(struct device *dev, struct device_attribute *attr, char *buf);
static	DEVICE_ATTR(sensor_A, S_IRWXUGO, show_current, NULL);

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_voltage		(struct device *dev, struct device_attribute *attr, char *buf);
static	DEVICE_ATTR(sensor_V, S_IRWXUGO, show_voltage, NULL);

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_max_power		(struct device *dev, struct device_attribute *attr, char *buf);
static	DEVICE_ATTR(sensor_maxW, S_IRWXUGO, show_max_power, NULL);

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_max_current	(struct device *dev, struct device_attribute *attr, char *buf);
static	DEVICE_ATTR(sensor_maxA, S_IRWXUGO, show_max_current, NULL);

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_max_voltage	(struct device *dev, struct device_attribute *attr, char *buf);
static	DEVICE_ATTR(sensor_maxV, S_IRWXUGO, show_max_voltage, NULL);

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t show_enable         (struct device *dev, struct device_attribute *attr, char *buf);
static 	ssize_t set_enable          (struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static	DEVICE_ATTR(enable, S_IRWXUGO, show_enable, set_enable);

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_period 		(struct device *dev, struct device_attribute *attr, char *buf);
static	DEVICE_ATTR(update_period, S_IRWXUGO, show_period, NULL);

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static struct attribute *ina231_sysfs_entries[] = {
	&dev_attr_sensor_name.attr,
	&dev_attr_sensor_W.attr,
	&dev_attr_sensor_A.attr,
	&dev_attr_sensor_V.attr,
	&dev_attr_sensor_maxW.attr,
	&dev_attr_sensor_maxA.attr,
	&dev_attr_sensor_maxV.attr,
	&dev_attr_enable.attr,
	&dev_attr_update_period.attr,
	NULL
};

static struct attribute_group ina231_attr_group = {
	.name   = NULL,
	.attrs  = ina231_sysfs_entries,
};

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_name			(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ina231_sensor 	*sensor = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", sensor->pd->name);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_power			(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ina231_sensor 	*sensor = dev_get_drvdata(dev);
	unsigned int            value;
	
	mutex_lock(&sensor->mutex); value = sensor->cur_uW; mutex_unlock(&sensor->mutex);

	return sprintf(buf, "%d.%06d\n", (value/1000000), (value%1000000));
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_max_power		(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ina231_sensor 	*sensor = dev_get_drvdata(dev);
	unsigned int            value;
	
	mutex_lock(&sensor->mutex); value = sensor->max_uW; mutex_unlock(&sensor->mutex);

	return sprintf(buf, "%d.%06d\n", (value/1000000), (value%1000000));
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_current		(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ina231_sensor 	*sensor = dev_get_drvdata(dev);
	unsigned int            value;
	
	mutex_lock(&sensor->mutex); value = sensor->cur_uA; mutex_unlock(&sensor->mutex);

	return sprintf(buf, "%d.%06d\n", (value/1000000), (value%1000000));
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_max_current	(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ina231_sensor 	*sensor = dev_get_drvdata(dev);
	unsigned int            value;
	
	mutex_lock(&sensor->mutex); value = sensor->max_uA; mutex_unlock(&sensor->mutex);

	return sprintf(buf, "%d.%06d\n", (value/1000000), (value%1000000));
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_voltage		(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ina231_sensor 	*sensor = dev_get_drvdata(dev);
	unsigned int            value;
	
	mutex_lock(&sensor->mutex); value = sensor->cur_uV; mutex_unlock(&sensor->mutex);

	return sprintf(buf, "%d.%06d\n", (value/1000000), (value%1000000));
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_max_voltage	(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ina231_sensor 	*sensor = dev_get_drvdata(dev);
	unsigned int            value;
	
	mutex_lock(&sensor->mutex); value = sensor->max_uV; mutex_unlock(&sensor->mutex);

	return sprintf(buf, "%d.%06d\n", (value/1000000), (value%1000000));
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t show_enable         (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ina231_sensor 	*sensor = dev_get_drvdata(dev);
	
	return	sprintf(buf, "%d\n", sensor->pd->enable);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t set_enable          (struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct ina231_sensor 	*sensor = dev_get_drvdata(dev);
    
    if(simple_strtol(buf, NULL, 10) != 0)   {
        if(!sensor->pd->enable) {
            sensor->pd->enable = 1;     ina231_i2c_enable(sensor);
        }
    }
    else    {
        if(sensor->pd->enable)  {
            sensor->pd->enable = 0;     
        }   
    }
	return  count;
}
//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t show_period     (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ina231_sensor 	*sensor = dev_get_drvdata(dev);
	
	return	sprintf(buf, "%d usec\n", sensor->pd->update_period);
}

//[*]--------------------------------------------------------------------------------------------------[*]
int		ina231_sysfs_create		(struct device *dev)	
{
	return	sysfs_create_group(&dev->kobj, &ina231_attr_group);
}

//[*]--------------------------------------------------------------------------------------------------[*]
void	ina231_sysfs_remove		(struct device *dev)	
{
    sysfs_remove_group(&dev->kobj, &ina231_attr_group);
}

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
