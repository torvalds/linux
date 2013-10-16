#include <linux/device.h>
#include <linux/init.h>
#include <linux/scaler-core.h>
#include <linux/slab.h>
extern const char const *scaler_input_name[];
static ssize_t scaler_iport_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int iports = 0;
	struct scaler_chip_dev *chip = NULL;
	struct scaler_input_port *in = NULL;
	struct scaler_device *sdev = dev_get_drvdata(dev);

	list_for_each_entry(chip, &sdev->chips, next) {

		list_for_each_entry(in, &chip->iports, next) {
			iports++;
			printk("id = %d type = %s gpio = %d\n", in->id, 
					    scaler_input_name[in->type], in->led_gpio);
		}
	}

	return sprintf(buf, "%d\n", iports);
}

static ssize_t scaler_iport_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	return 0;
}
static DEVICE_ATTR(iports, 0664, scaler_iport_show, NULL);

static ssize_t scaler_cur_iport_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct scaler_chip_dev *chip = NULL;
	struct scaler_input_port *in = NULL;
	struct scaler_device *sdev = dev_get_drvdata(dev);

	list_for_each_entry(chip, &sdev->chips, next) {

		printk("id = %d type = %s\n", chip->cur_inport_id, 
				scaler_input_name[chip->cur_in_type]);
	}
	return 0;
}
static DEVICE_ATTR(cur_iport, 0664, scaler_cur_iport_show, NULL);

static ssize_t scaler_oport_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	printk("%s: scaler sysfs test.\n", __func__);
	return 0;
}

static ssize_t scaler_oport_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	return 0;
}
static DEVICE_ATTR(oports, 0664, scaler_oport_show, NULL);

extern void scaler_test_read_vga_edid(void);
static ssize_t scaler_edid_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	scaler_test_read_vga_edid();
	return 0;
}
static DEVICE_ATTR(edid, 0664, scaler_edid_show, NULL);

static struct attribute *scaler_attributes[] = {
	&dev_attr_iports.attr,
	&dev_attr_cur_iport.attr,
	&dev_attr_edid.attr,
	&dev_attr_oports.attr,
	NULL
};

static mode_t scaler_attr_is_visible(struct kobject *kobj,
				     struct attribute *attr, int n)
{
	mode_t mode = attr->mode;

	return mode;
}

static const struct attribute_group scaler_attr_group = {
	.is_visible	= scaler_attr_is_visible,
	.attrs		= scaler_attributes,
};


int scaler_sysfs_create(struct scaler_device *sdev)
{
	int err;

	//sdev->kobj = kobject_create_and_add("attr", &sdev->dev->kobj);
	//err = sysfs_create_group(sdev->kobj, &scaler_attr_group);
	err = sysfs_create_group(&sdev->dev->kobj, &scaler_attr_group);

	return err;
}

int scaler_sysfs_remove(struct scaler_device *sdev)
{
	sysfs_remove_group(&sdev->dev->kobj, &scaler_attr_group);
	//dev_set_drvdata(sdev->dev, NULL);
	//kobject_put(sdev->kobj);
	return 0;
}
