/*
 *
 * (C) COPYRIGHT 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */


#include <kbase/src/linux/mali_kbase_config_linux.h>
#include <osk/mali_osk.h>

#ifdef CONFIG_MALI_PLATFORM_FAKE

void kbasep_config_parse_io_resources(const kbase_io_resources *io_resources, struct resource *linux_resources)
{
	OSK_ASSERT(io_resources != NULL);
	OSK_ASSERT(linux_resources != NULL);

	memset(linux_resources, 0, PLATFORM_CONFIG_RESOURCE_COUNT * sizeof(struct resource));

	linux_resources[0].start = io_resources->io_memory_region.start;
	linux_resources[0].end   = io_resources->io_memory_region.end;
	linux_resources[0].flags = IORESOURCE_MEM;

	linux_resources[1].start = linux_resources[1].end = io_resources->job_irq_number;
	linux_resources[1].flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL;

	linux_resources[2].start = linux_resources[2].end = io_resources->mmu_irq_number;
	linux_resources[2].flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL;

	linux_resources[3].start = linux_resources[3].end = io_resources->gpu_irq_number;
	linux_resources[3].flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL;
}

#endif /* CONFIG_MALI_PLATFORM_FAKE */
