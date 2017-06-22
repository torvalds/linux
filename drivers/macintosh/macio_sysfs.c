#include <linux/kernel.h>
#include <linux/stat.h>
#include <asm/macio.h>


#define macio_config_of_attr(field, format_string)			\
static ssize_t								\
field##_show (struct device *dev, struct device_attribute *attr,	\
              char *buf)						\
{									\
	struct macio_dev *mdev = to_macio_device (dev);			\
	return sprintf (buf, format_string, mdev->ofdev.dev.of_node->field); \
}

static ssize_t
compatible_show (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *of;
	const char *compat;
	int cplen;
	int length = 0;

	of = &to_macio_device (dev)->ofdev;
	compat = of_get_property(of->dev.of_node, "compatible", &cplen);
	if (!compat) {
		*buf = '\0';
		return 0;
	}
	while (cplen > 0) {
		int l;
		length += sprintf (buf, "%s\n", compat);
		buf += length;
		l = strlen (compat) + 1;
		compat += l;
		cplen -= l;
	}

	return length;
}

static ssize_t modalias_show (struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return of_device_modalias(dev, buf, PAGE_SIZE);
}

static ssize_t devspec_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct platform_device *ofdev;

	ofdev = to_platform_device(dev);
	return sprintf(buf, "%s\n", ofdev->dev.of_node->full_name);
}

macio_config_of_attr (name, "%s\n");
macio_config_of_attr (type, "%s\n");

struct device_attribute macio_dev_attrs[] = {
	__ATTR_RO(name),
	__ATTR_RO(type),
	__ATTR_RO(compatible),
	__ATTR_RO(modalias),
	__ATTR_RO(devspec),
	__ATTR_NULL
};
