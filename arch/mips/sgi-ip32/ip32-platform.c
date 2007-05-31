#include <linux/init.h>
#include <linux/platform_device.h>

static __init int meth_devinit(void)
{
	struct platform_device *pd;
	int ret;

	pd = platform_device_alloc("meth", -1);
	if (!pd)
		return -ENOMEM;

	ret = platform_device_add(pd);
	if (ret)
		platform_device_put(pd);

	return ret;
}

device_initcall(meth_devinit);
