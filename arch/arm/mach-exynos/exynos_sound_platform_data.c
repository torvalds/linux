#include <linux/module.h>
#include <linux/err.h>
#include <linux/exynos_audio.h>

static const struct exynos_sound_platform_data *platform_data;

int __init exynos_sound_set_platform_data(
		const struct exynos_sound_platform_data *data)
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

const struct exynos_sound_platform_data *exynos_sound_get_platform_data(void)
{
	if (!platform_data)
		return ERR_PTR(-ENODEV);

	return platform_data;
}
EXPORT_SYMBOL(exynos_sound_get_platform_data);
