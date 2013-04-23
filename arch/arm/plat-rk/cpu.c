#include <linux/kernel.h>
#include <linux/cpu.h>
#include <plat/cpu.h>

static ssize_t show_type(struct sysdev_class *dev, struct sysdev_class_attribute *attr, char *buf)
{
	const char *type;

	if (cpu_is_rk3188())
		type = "rk3188";
	else if (cpu_is_rk3066b())
		type = "rk3066b";
	else if (cpu_is_rk30xx())
		type = "rk30xx";
	else if (cpu_is_rk2928())
		type = "rk2928";
	else
		type = "";

	return sprintf(buf, "%s\n", type);
}

static SYSDEV_CLASS_ATTR(type, 0444, show_type, NULL);

static ssize_t show_soc(struct sysdev_class *dev, struct sysdev_class_attribute *attr, char *buf)
{
	const char *soc;

	if (soc_is_rk3188plus())
		soc = "rk3188+";
	else if (soc_is_rk3188())
		soc = "rk3188";
	else if (soc_is_rk3168())
		soc = "rk3168";
	else if (soc_is_rk3028())
		soc = "rk3028";
	else if (soc_is_rk3066b())
		soc = "rk3066b";
	else if (soc_is_rk2928g())
		soc = "rk2928g";
	else if (soc_is_rk2928l())
		soc = "rk2928l";
	else if (soc_is_rk2926())
		soc = "rk2926";
	else if (soc_is_rk3066())
		soc = "rk3066";
	else if (soc_is_rk3068())
		soc = "rk3068";
	else if (soc_is_rk3000())
		soc = "rk3000";
	else
		soc = "";

	return sprintf(buf, "%s\n", soc);
}

static SYSDEV_CLASS_ATTR(soc, 0444, show_soc, NULL);

static int __init rk_cpu_init(void)
{
	int err;

	err = sysfs_create_file(&cpu_sysdev_class.kset.kobj, &attr_type.attr);
	err = sysfs_create_file(&cpu_sysdev_class.kset.kobj, &attr_soc.attr);

	return err;
}
late_initcall(rk_cpu_init);
