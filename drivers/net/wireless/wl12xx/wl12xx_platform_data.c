#include <linux/module.h>
#include <linux/err.h>
#include <linux/wl12xx.h>

static const struct wl12xx_platform_data *platform_data;

int __init wl12xx_set_platform_data(const struct wl12xx_platform_data *data)
{
	if (platform_data)
		return -EBUSY;
	if (!data)
		return -EINVAL;

	platform_data = kmemdup(data, sizeof(*data), GFP_KERNEL);
	if (!platform_data)
		return -ENOMEM;

	return 0;
}

const struct wl12xx_platform_data *wl12xx_get_platform_data(void)
{
	if (!platform_data)
		return ERR_PTR(-ENODEV);

	return platform_data;
}
EXPORT_SYMBOL(wl12xx_get_platform_data);
