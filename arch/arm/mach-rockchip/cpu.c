#include <linux/kernel.h>
#include <linux/cpu.h>
#include <linux/rockchip/cpu.h>

unsigned long rockchip_soc_id;
EXPORT_SYMBOL(rockchip_soc_id);

static ssize_t type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	const char *type;

	if (cpu_is_rk3288())
		type = "rk3288";
	else if (cpu_is_rk319x())
		type = "rk319x";
	else if (cpu_is_rk3188())
		type = "rk3188";
	else if (cpu_is_rk3066b())
		type = "rk3066b";
	else if (cpu_is_rk3026())
		type = "rk3026";
	else if (cpu_is_rk30xx())
		type = "rk30xx";
	else if (cpu_is_rk2928())
		type = "rk2928";
	else if (cpu_is_rk312x())
		type = "rk312x";
	else
		type = "";

	if (rockchip_get_cpu_version())
		return sprintf(buf, "%sv%lu\n", type,
			       rockchip_get_cpu_version());

	return sprintf(buf, "%s\n", type);
}

static struct device_attribute type_attr = __ATTR_RO(type);

static ssize_t soc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	const char *soc;

	if (soc_is_rk3288())
		soc = "rk3288";
	else if (soc_is_rk3190())
		soc = "rk3190";
	else if (soc_is_rk3188plus())
		soc = "rk3188+";
	else if (soc_is_rk3188())
		soc = "rk3188";
	else if (soc_is_rk3168())
		soc = "rk3168";
	else if (soc_is_rk3028())
		soc = "rk3028";
	else if (soc_is_rk3066b())
		soc = "rk3066b";
	else if (soc_is_rk3028a())
		soc = "rk3028a";
	else if (soc_is_rk3026())
		soc = "rk3026";
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
	else if (soc_is_rk3126() || soc_is_rk3126b())
		soc = "rk3126";
	else if (soc_is_rk3128())
		soc = "rk3128";
	else
		soc = "";

	return sprintf(buf, "%s\n", soc);
}

static struct device_attribute soc_attr = __ATTR_RO(soc);

static int __init rockchip_cpu_lateinit(void)
{
	int err;

	err = device_create_file(cpu_subsys.dev_root, &type_attr);
	err = device_create_file(cpu_subsys.dev_root, &soc_attr);

	return err;
}
late_initcall(rockchip_cpu_lateinit);
