/*
 * drivers/mtd/maps/gpio-addr-flash.c
 *
 * Handle the case where a flash device is mostly addressed using physical
 * line and supplemented by GPIOs.  This way you can hook up say a 8MiB flash
 * to a 2MiB memory range and use the GPIOs to select a particular range.
 *
 * Copyright © 2000 Nicolas Pitre <nico@cam.org>
 * Copyright © 2005-2009 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>

#define pr_devinit(fmt, args...) ({ static const __devinitconst char __fmt[] = fmt; printk(__fmt, ## args); })

#define DRIVER_NAME "gpio-addr-flash"
#define PFX DRIVER_NAME ": "

/**
 * struct async_state - keep GPIO flash state
 *	@mtd:         MTD state for this mapping
 *	@map:         MTD map state for this flash
 *	@gpio_count:  number of GPIOs used to address
 *	@gpio_addrs:  array of GPIOs to twiddle
 *	@gpio_values: cached GPIO values
 *	@win_size:    dedicated memory size (if no GPIOs)
 */
struct async_state {
	struct mtd_info *mtd;
	struct map_info map;
	size_t gpio_count;
	unsigned *gpio_addrs;
	int *gpio_values;
	unsigned long win_size;
};
#define gf_map_info_to_state(mi) ((struct async_state *)(mi)->map_priv_1)

/**
 * gf_set_gpios() - set GPIO address lines to access specified flash offset
 *	@state: GPIO flash state
 *	@ofs:   desired offset to access
 *
 * Rather than call the GPIO framework every time, cache the last-programmed
 * value.  This speeds up sequential accesses (which are by far the most common
 * type).  We rely on the GPIO framework to treat non-zero value as high so
 * that we don't have to normalize the bits.
 */
static void gf_set_gpios(struct async_state *state, unsigned long ofs)
{
	size_t i = 0;
	int value;
	ofs /= state->win_size;
	do {
		value = ofs & (1 << i);
		if (state->gpio_values[i] != value) {
			gpio_set_value(state->gpio_addrs[i], value);
			state->gpio_values[i] = value;
		}
	} while (++i < state->gpio_count);
}

/**
 * gf_read() - read a word at the specified offset
 *	@map: MTD map state
 *	@ofs: desired offset to read
 */
static map_word gf_read(struct map_info *map, unsigned long ofs)
{
	struct async_state *state = gf_map_info_to_state(map);
	uint16_t word;
	map_word test;

	gf_set_gpios(state, ofs);

	word = readw(map->virt + (ofs % state->win_size));
	test.x[0] = word;
	return test;
}

/**
 * gf_copy_from() - copy a chunk of data from the flash
 *	@map:  MTD map state
 *	@to:   memory to copy to
 *	@from: flash offset to copy from
 *	@len:  how much to copy
 *
 * We rely on the MTD layer to chunk up copies such that a single request here
 * will not cross a window size.  This allows us to only wiggle the GPIOs once
 * before falling back to a normal memcpy.  Reading the higher layer code shows
 * that this is indeed the case, but add a BUG_ON() to future proof.
 */
static void gf_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	struct async_state *state = gf_map_info_to_state(map);

	gf_set_gpios(state, from);

	/* BUG if operation crosses the win_size */
	BUG_ON(!((from + len) % state->win_size <= (from + len)));

	/* operation does not cross the win_size, so one shot it */
	memcpy_fromio(to, map->virt + (from % state->win_size), len);
}

/**
 * gf_write() - write a word at the specified offset
 *	@map: MTD map state
 *	@ofs: desired offset to write
 */
static void gf_write(struct map_info *map, map_word d1, unsigned long ofs)
{
	struct async_state *state = gf_map_info_to_state(map);
	uint16_t d;

	gf_set_gpios(state, ofs);

	d = d1.x[0];
	writew(d, map->virt + (ofs % state->win_size));
}

/**
 * gf_copy_to() - copy a chunk of data to the flash
 *	@map:  MTD map state
 *	@to:   flash offset to copy to
 *	@from: memory to copy from
 *	@len:  how much to copy
 *
 * See gf_copy_from() caveat.
 */
static void gf_copy_to(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	struct async_state *state = gf_map_info_to_state(map);

	gf_set_gpios(state, to);

	/* BUG if operation crosses the win_size */
	BUG_ON(!((to + len) % state->win_size <= (to + len)));

	/* operation does not cross the win_size, so one shot it */
	memcpy_toio(map->virt + (to % state->win_size), from, len);
}

static const char *part_probe_types[] = { "cmdlinepart", "RedBoot", NULL };

/**
 * gpio_flash_probe() - setup a mapping for a GPIO assisted flash
 *	@pdev: platform device
 *
 * The platform resource layout expected looks something like:
 * struct mtd_partition partitions[] = { ... };
 * struct physmap_flash_data flash_data = { ... };
 * unsigned flash_gpios[] = { GPIO_XX, GPIO_XX, ... };
 * struct resource flash_resource[] = {
 *	{
 *		.name  = "cfi_probe",
 *		.start = 0x20000000,
 *		.end   = 0x201fffff,
 *		.flags = IORESOURCE_MEM,
 *	}, {
 *		.start = (unsigned long)flash_gpios,
 *		.end   = ARRAY_SIZE(flash_gpios),
 *		.flags = IORESOURCE_IRQ,
 *	}
 * };
 * struct platform_device flash_device = {
 *	.name          = "gpio-addr-flash",
 *	.dev           = { .platform_data = &flash_data, },
 *	.num_resources = ARRAY_SIZE(flash_resource),
 *	.resource      = flash_resource,
 *	...
 * };
 */
static int __devinit gpio_flash_probe(struct platform_device *pdev)
{
	size_t i, arr_size;
	struct physmap_flash_data *pdata;
	struct resource *memory;
	struct resource *gpios;
	struct async_state *state;

	pdata = pdev->dev.platform_data;
	memory = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	gpios = platform_get_resource(pdev, IORESOURCE_IRQ, 0);

	if (!memory || !gpios || !gpios->end)
		return -EINVAL;

	arr_size = sizeof(int) * gpios->end;
	state = kzalloc(sizeof(*state) + arr_size, GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	/*
	 * We cast start/end to known types in the boards file, so cast
	 * away their pointer types here to the known types (gpios->xxx).
	 */
	state->gpio_count     = gpios->end;
	state->gpio_addrs     = (void *)(unsigned long)gpios->start;
	state->gpio_values    = (void *)(state + 1);
	state->win_size       = resource_size(memory);
	memset(state->gpio_values, 0xff, arr_size);

	state->map.name       = DRIVER_NAME;
	state->map.read       = gf_read;
	state->map.copy_from  = gf_copy_from;
	state->map.write      = gf_write;
	state->map.copy_to    = gf_copy_to;
	state->map.bankwidth  = pdata->width;
	state->map.size       = state->win_size * (1 << state->gpio_count);
	state->map.virt       = ioremap_nocache(memory->start, state->map.size);
	state->map.phys       = NO_XIP;
	state->map.map_priv_1 = (unsigned long)state;

	platform_set_drvdata(pdev, state);

	i = 0;
	do {
		if (gpio_request(state->gpio_addrs[i], DRIVER_NAME)) {
			pr_devinit(KERN_ERR PFX "failed to request gpio %d\n",
				state->gpio_addrs[i]);
			while (i--)
				gpio_free(state->gpio_addrs[i]);
			kfree(state);
			return -EBUSY;
		}
		gpio_direction_output(state->gpio_addrs[i], 0);
	} while (++i < state->gpio_count);

	pr_devinit(KERN_NOTICE PFX "probing %d-bit flash bus\n",
		state->map.bankwidth * 8);
	state->mtd = do_map_probe(memory->name, &state->map);
	if (!state->mtd) {
		for (i = 0; i < state->gpio_count; ++i)
			gpio_free(state->gpio_addrs[i]);
		kfree(state);
		return -ENXIO;
	}


	mtd_device_parse_register(state->mtd, part_probe_types, 0,
			pdata->parts, pdata->nr_parts);

	return 0;
}

static int __devexit gpio_flash_remove(struct platform_device *pdev)
{
	struct async_state *state = platform_get_drvdata(pdev);
	size_t i = 0;
	do {
		gpio_free(state->gpio_addrs[i]);
	} while (++i < state->gpio_count);
	mtd_device_unregister(state->mtd);
	map_destroy(state->mtd);
	kfree(state);
	return 0;
}

static struct platform_driver gpio_flash_driver = {
	.probe		= gpio_flash_probe,
	.remove		= __devexit_p(gpio_flash_remove),
	.driver		= {
		.name	= DRIVER_NAME,
	},
};

module_platform_driver(gpio_flash_driver);

MODULE_AUTHOR("Mike Frysinger <vapier@gentoo.org>");
MODULE_DESCRIPTION("MTD map driver for flashes addressed physically and with gpios");
MODULE_LICENSE("GPL");
