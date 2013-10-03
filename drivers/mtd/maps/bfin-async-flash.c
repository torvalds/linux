/*
 * drivers/mtd/maps/bfin-async-flash.c
 *
 * Handle the case where flash memory and ethernet mac/phy are
 * mapped onto the same async bank.  The BF533-STAMP does this
 * for example.  All board-specific configuration goes in your
 * board resources file.
 *
 * Copyright 2000 Nicolas Pitre <nico@fluxnic.net>
 * Copyright 2005-2008 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <asm/blackfin.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <asm/unaligned.h>

#define pr_devinit(fmt, args...) \
		({ static const char __fmt[] = fmt; printk(__fmt, ## args); })

#define DRIVER_NAME "bfin-async-flash"

struct async_state {
	struct mtd_info *mtd;
	struct map_info map;
	int enet_flash_pin;
	uint32_t flash_ambctl0, flash_ambctl1;
	uint32_t save_ambctl0, save_ambctl1;
	unsigned long irq_flags;
};

static void switch_to_flash(struct async_state *state)
{
	local_irq_save(state->irq_flags);

	gpio_set_value(state->enet_flash_pin, 0);

	state->save_ambctl0 = bfin_read_EBIU_AMBCTL0();
	state->save_ambctl1 = bfin_read_EBIU_AMBCTL1();
	bfin_write_EBIU_AMBCTL0(state->flash_ambctl0);
	bfin_write_EBIU_AMBCTL1(state->flash_ambctl1);
	SSYNC();
}

static void switch_back(struct async_state *state)
{
	bfin_write_EBIU_AMBCTL0(state->save_ambctl0);
	bfin_write_EBIU_AMBCTL1(state->save_ambctl1);
	SSYNC();

	gpio_set_value(state->enet_flash_pin, 1);

	local_irq_restore(state->irq_flags);
}

static map_word bfin_flash_read(struct map_info *map, unsigned long ofs)
{
	struct async_state *state = (struct async_state *)map->map_priv_1;
	uint16_t word;
	map_word test;

	switch_to_flash(state);

	word = readw(map->virt + ofs);

	switch_back(state);

	test.x[0] = word;
	return test;
}

static void bfin_flash_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	struct async_state *state = (struct async_state *)map->map_priv_1;

	switch_to_flash(state);

	memcpy(to, map->virt + from, len);

	switch_back(state);
}

static void bfin_flash_write(struct map_info *map, map_word d1, unsigned long ofs)
{
	struct async_state *state = (struct async_state *)map->map_priv_1;
	uint16_t d;

	d = d1.x[0];

	switch_to_flash(state);

	writew(d, map->virt + ofs);
	SSYNC();

	switch_back(state);
}

static void bfin_flash_copy_to(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	struct async_state *state = (struct async_state *)map->map_priv_1;

	switch_to_flash(state);

	memcpy(map->virt + to, from, len);
	SSYNC();

	switch_back(state);
}

static const char * const part_probe_types[] = {
	"cmdlinepart", "RedBoot", NULL };

static int bfin_flash_probe(struct platform_device *pdev)
{
	int ret;
	struct physmap_flash_data *pdata = dev_get_platdata(&pdev->dev);
	struct resource *memory = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct resource *flash_ambctl = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	struct async_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->map.name       = DRIVER_NAME;
	state->map.read       = bfin_flash_read;
	state->map.copy_from  = bfin_flash_copy_from;
	state->map.write      = bfin_flash_write;
	state->map.copy_to    = bfin_flash_copy_to;
	state->map.bankwidth  = pdata->width;
	state->map.size       = resource_size(memory);
	state->map.virt       = (void __iomem *)memory->start;
	state->map.phys       = memory->start;
	state->map.map_priv_1 = (unsigned long)state;
	state->enet_flash_pin = platform_get_irq(pdev, 0);
	state->flash_ambctl0  = flash_ambctl->start;
	state->flash_ambctl1  = flash_ambctl->end;

	if (gpio_request(state->enet_flash_pin, DRIVER_NAME)) {
		pr_devinit(KERN_ERR DRIVER_NAME ": Failed to request gpio %d\n", state->enet_flash_pin);
		kfree(state);
		return -EBUSY;
	}
	gpio_direction_output(state->enet_flash_pin, 1);

	pr_devinit(KERN_NOTICE DRIVER_NAME ": probing %d-bit flash bus\n", state->map.bankwidth * 8);
	state->mtd = do_map_probe(memory->name, &state->map);
	if (!state->mtd) {
		gpio_free(state->enet_flash_pin);
		kfree(state);
		return -ENXIO;
	}

	mtd_device_parse_register(state->mtd, part_probe_types, NULL,
				  pdata->parts, pdata->nr_parts);

	platform_set_drvdata(pdev, state);

	return 0;
}

static int bfin_flash_remove(struct platform_device *pdev)
{
	struct async_state *state = platform_get_drvdata(pdev);
	gpio_free(state->enet_flash_pin);
	mtd_device_unregister(state->mtd);
	map_destroy(state->mtd);
	kfree(state);
	return 0;
}

static struct platform_driver bfin_flash_driver = {
	.probe		= bfin_flash_probe,
	.remove		= bfin_flash_remove,
	.driver		= {
		.name	= DRIVER_NAME,
	},
};

module_platform_driver(bfin_flash_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MTD map driver for Blackfins with flash/ethernet on same async bank");
