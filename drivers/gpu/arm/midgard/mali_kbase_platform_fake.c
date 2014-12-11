/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#ifdef CONFIG_MALI_PLATFORM_FAKE

#include <linux/errno.h>
#include <linux/export.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#ifdef CONFIG_MACH_MANTA
#include <plat/devs.h>
#endif

/*
 * This file is included only for type definitions and functions belonging to
 * specific platform folders. Do not add dependencies with symbols that are
 * defined somewhere else.
 */
#include <mali_kbase_config.h>

#define PLATFORM_CONFIG_RESOURCE_COUNT 4
#define PLATFORM_CONFIG_IRQ_RES_COUNT  3

static struct platform_device *mali_device;

#ifndef CONFIG_OF
/**
 * @brief Convert data in struct kbase_io_resources struct to Linux-specific resources
 *
 * Function converts data in struct kbase_io_resources struct to an array of Linux resource structures. Note that function
 * assumes that size of linux_resource array is at least PLATFORM_CONFIG_RESOURCE_COUNT.
 * Resources are put in fixed order: I/O memory region, job IRQ, MMU IRQ, GPU IRQ.
 *
 * @param[in]  io_resource      Input IO resource data
 * @param[out] linux_resources  Pointer to output array of Linux resource structures
 */
static void kbasep_config_parse_io_resources(const struct kbase_io_resources *io_resources, struct resource *const linux_resources)
{
	if (!io_resources || !linux_resources) {
		pr_err("%s: couldn't find proper resources\n", __func__);
		return;
	}

	memset(linux_resources, 0, PLATFORM_CONFIG_RESOURCE_COUNT * sizeof(struct resource));

	linux_resources[0].start = io_resources->io_memory_region.start;
	linux_resources[0].end   = io_resources->io_memory_region.end;
	linux_resources[0].flags = IORESOURCE_MEM;
	linux_resources[1].start = io_resources->job_irq_number;
	linux_resources[1].end   = io_resources->job_irq_number;
	linux_resources[1].flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL;

	linux_resources[2].start = io_resources->mmu_irq_number;
	linux_resources[2].end   = io_resources->mmu_irq_number;
	linux_resources[2].flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL;

	linux_resources[3].start = io_resources->gpu_irq_number;
	linux_resources[3].end   = io_resources->gpu_irq_number;
	linux_resources[3].flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL;
}
#endif /* CONFIG_OF */

int kbase_platform_fake_register(void)
{
	struct kbase_platform_config *config;
	int attribute_count;
#ifndef CONFIG_OF
	struct resource resources[PLATFORM_CONFIG_RESOURCE_COUNT];
#endif
	int err;

	config = kbase_get_platform_config(); /* declared in midgard/mali_kbase_config.h but defined in platform folder */
	if (config == NULL) {
		pr_err("%s: couldn't get platform config\n", __func__);
		return -ENODEV;
	}

	attribute_count = kbasep_get_config_attribute_count(config->attributes);
#ifdef CONFIG_MACH_MANTA
	err = platform_device_add_data(&exynos5_device_g3d, config->attributes, attribute_count * sizeof(config->attributes[0]));
	if (err)
		return err;
#else

	mali_device = platform_device_alloc("mali", 0);
	if (mali_device == NULL)
		return -ENOMEM;

#ifndef CONFIG_OF
	kbasep_config_parse_io_resources(config->io_resources, resources);
	err = platform_device_add_resources(mali_device, resources, PLATFORM_CONFIG_RESOURCE_COUNT);
	if (err) {
		platform_device_put(mali_device);
		mali_device = NULL;
		return err;
	}
#endif /* CONFIG_OF */

	err = platform_device_add_data(mali_device, config->attributes, attribute_count * sizeof(config->attributes[0]));
	if (err) {
		platform_device_unregister(mali_device);
		mali_device = NULL;
		return err;
	}

	err = platform_device_add(mali_device);
	if (err) {
		platform_device_unregister(mali_device);
		mali_device = NULL;
		return err;
	}
#endif /* CONFIG_CONFIG_MACH_MANTA */

	return 0;
}
EXPORT_SYMBOL(kbase_platform_fake_register);

void kbase_platform_fake_unregister(void)
{
	if (mali_device)
		platform_device_unregister(mali_device);
}
EXPORT_SYMBOL(kbase_platform_fake_unregister);

#endif /* CONFIG_MALI_PLATFORM_FAKE */

