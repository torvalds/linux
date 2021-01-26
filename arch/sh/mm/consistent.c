/*
 * Copyright (C) 2004 - 2007  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>

static int __init memchunk_setup(char *str)
{
	return 1; /* accept anything that begins with "memchunk." */
}
__setup("memchunk.", memchunk_setup);

static void __init memchunk_cmdline_override(char *name, unsigned long *sizep)
{
	char *p = boot_command_line;
	int k = strlen(name);

	while ((p = strstr(p, "memchunk."))) {
		p += 9; /* strlen("memchunk.") */
		if (!strncmp(name, p, k) && p[k] == '=') {
			p += k + 1;
			*sizep = memparse(p, NULL);
			pr_info("%s: forcing memory chunk size to 0x%08lx\n",
				name, *sizep);
			break;
		}
	}
}

int __init platform_resource_setup_memory(struct platform_device *pdev,
					  char *name, unsigned long memsize)
{
	struct resource *r;
	dma_addr_t dma_handle;
	void *buf;

	r = pdev->resource + pdev->num_resources - 1;
	if (r->flags) {
		pr_warn("%s: unable to find empty space for resource\n", name);
		return -EINVAL;
	}

	memchunk_cmdline_override(name, &memsize);
	if (!memsize)
		return 0;

	buf = dma_alloc_coherent(&pdev->dev, memsize, &dma_handle, GFP_KERNEL);
	if (!buf) {
		pr_warn("%s: unable to allocate memory\n", name);
		return -ENOMEM;
	}

	r->flags = IORESOURCE_MEM;
	r->start = dma_handle;
	r->end = r->start + memsize - 1;
	r->name = name;
	return 0;
}
