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
#include <linux/gpio/consumer.h>
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

#define win_mask(x) ((BIT(x)) - 1)

#define DRIVER_NAME "gpio-addr-flash"

/**
 * struct async_state - keep GPIO flash state
 *	@mtd:         MTD state for this mapping
 *	@map:         MTD map state for this flash
 *	@gpios:       Struct containing the array of GPIO descriptors
 *	@gpio_values: cached GPIO values
 *	@win_order:   dedicated memory size (if no GPIOs)
 */
struct async_state {
	struct mtd_info *mtd;
	struct map_info map;
	struct gpio_descs *gpios;
	unsigned int gpio_values;
	unsigned int win_order;
};
#define gf_map_info_to_state(mi) ((struct async_state *)(mi)->map_priv_1)

/**
 * gf_set_gpios() - set GPIO address lines to access specified flash offset
 *	@state: GPIO flash state
 *	@ofs:   desired offset to access
 *
 * Rather than call the GPIO framework every time, cache the last-programmed
 * value.  This speeds up sequential accesses (which are by far the most common
 * type).
 */
static void gf_set_gpios(struct async_state *state, unsigned long ofs)
{
	int i;

	ofs >>= state->win_order;

	if (ofs == state->gpio_values)
		return;

	for (i = 0; i < state->gpios->ndescs; i++) {
		if ((ofs & BIT(i)) == (state->gpio_values & BIT(i)))
			continue;

		gpiod_set_value(state->gpios->desc[i], !!(ofs & BIT(i)));
	}

	state->gpio_values = ofs;
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

	word = readw(map->virt + (ofs & win_mask(state->win_order)));
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
 * The "from" region may straddle more than one window, so toggle the GPIOs for
 * each window region before reading its data.
 */
static void gf_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	struct async_state *state = gf_map_info_to_state(map);

	int this_len;

	while (len) {
		this_len = from & win_mask(state->win_order);
		this_len = BIT(state->win_order) - this_len;
		this_len = min_t(int, len, this_len);

		gf_set_gpios(state, from);
		memcpy_fromio(to,
			      map->virt + (from & win_mask(state->win_order)),
			      this_len);
		len -= this_len;
		from += this_len;
		to += this_len;
	}
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
	writew(d, map->virt + (ofs & win_mask(state->win_order)));
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
static void gf_copy_to(struct map_info *map, unsigned long to,
		       const void *from, ssize_t len)
{
	struct async_state *state = gf_map_info_to_state(map);

	int this_len;

	while (len) {
		this_len = to & win_mask(state->win_order);
		this_len = BIT(state->win_order) - this_len;
		this_len = min_t(int, len, this_len);

		gf_set_gpios(state, to);
		memcpy_toio(map->virt + (to & win_mask(state->win_order)),
			    from, len);

		len -= this_len;
		to += this_len;
		from += this_len;
	}
}

static const char * const part_probe_types[] = {
	"cmdlinepart", "RedBoot", NULL };

/**
 * gpio_flash_probe() - setup a mapping for a GPIO assisted flash
 *	@pdev: platform device
 *
 * The platform resource layout expected looks something like:
 * struct mtd_partition partitions[] = { ... };
 * struct physmap_flash_data flash_data = { ... };
 * static struct gpiod_lookup_table addr_flash_gpios = {
 *		.dev_id = "gpio-addr-flash.0",
 *		.table = {
 *		GPIO_LOOKUP_IDX("gpio.0", 15, "addr", 0, GPIO_ACTIVE_HIGH),
 *		GPIO_LOOKUP_IDX("gpio.0", 16, "addr", 1, GPIO_ACTIVE_HIGH),
 *		);
 * };
 * gpiod_add_lookup_table(&addr_flash_gpios);
 *
 * struct resource flash_resource[] = {
 *	{
 *		.name  = "cfi_probe",
 *		.start = 0x20000000,
 *		.end   = 0x201fffff,
 *		.flags = IORESOURCE_MEM,
 *	},
 * };
 * struct platform_device flash_device = {
 *	.name          = "gpio-addr-flash",
 *	.dev           = { .platform_data = &flash_data, },
 *	.num_resources = ARRAY_SIZE(flash_resource),
 *	.resource      = flash_resource,
 *	...
 * };
 */
static int gpio_flash_probe(struct platform_device *pdev)
{
	struct physmap_flash_data *pdata;
	struct resource *memory;
	struct async_state *state;

	pdata = dev_get_platdata(&pdev->dev);
	memory = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!memory)
		return -EINVAL;

	state = devm_kzalloc(&pdev->dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->gpios = devm_gpiod_get_array(&pdev->dev, "addr", GPIOD_OUT_LOW);
	if (IS_ERR(state->gpios))
		return PTR_ERR(state->gpios);

	state->win_order      = get_bitmask_order(resource_size(memory)) - 1;

	state->map.name       = DRIVER_NAME;
	state->map.read       = gf_read;
	state->map.copy_from  = gf_copy_from;
	state->map.write      = gf_write;
	state->map.copy_to    = gf_copy_to;
	state->map.bankwidth  = pdata->width;
	state->map.size       = BIT(state->win_order + state->gpios->ndescs);
	state->map.virt	      = devm_ioremap_resource(&pdev->dev, memory);
	if (IS_ERR(state->map.virt))
		return PTR_ERR(state->map.virt);

	state->map.phys       = NO_XIP;
	state->map.map_priv_1 = (unsigned long)state;

	platform_set_drvdata(pdev, state);

	dev_notice(&pdev->dev, "probing %d-bit flash bus\n",
		   state->map.bankwidth * 8);
	state->mtd = do_map_probe(memory->name, &state->map);
	if (!state->mtd)
		return -ENXIO;
	state->mtd->dev.parent = &pdev->dev;

	mtd_device_parse_register(state->mtd, part_probe_types, NULL,
				  pdata->parts, pdata->nr_parts);

	return 0;
}

static int gpio_flash_remove(struct platform_device *pdev)
{
	struct async_state *state = platform_get_drvdata(pdev);

	mtd_device_unregister(state->mtd);
	map_destroy(state->mtd);
	return 0;
}

static struct platform_driver gpio_flash_driver = {
	.probe		= gpio_flash_probe,
	.remove		= gpio_flash_remove,
	.driver		= {
		.name	= DRIVER_NAME,
	},
};

module_platform_driver(gpio_flash_driver);

MODULE_AUTHOR("Mike Frysinger <vapier@gentoo.org>");
MODULE_DESCRIPTION("MTD map driver for flashes addressed physically and with gpios");
MODULE_LICENSE("GPL");
