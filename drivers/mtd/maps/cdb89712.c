/*
 * Flash on Cirrus CDB89712
 *
 * $Id: cdb89712.c,v 1.11 2005/11/07 11:14:26 gleixner Exp $
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/arch/hardware.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>




static struct mtd_info *flash_mtd;

struct map_info cdb89712_flash_map = {
	.name = "flash",
	.size = FLASH_SIZE,
	.bankwidth = FLASH_WIDTH,
	.phys = FLASH_START,
};

struct resource cdb89712_flash_resource = {
	.name =   "Flash",
	.start =  FLASH_START,
	.end =    FLASH_START + FLASH_SIZE - 1,
	.flags =  IORESOURCE_IO | IORESOURCE_BUSY,
};

static int __init init_cdb89712_flash (void)
{
	int err;

	if (request_resource (&ioport_resource, &cdb89712_flash_resource)) {
		printk(KERN_NOTICE "Failed to reserve Cdb89712 FLASH space\n");
		err = -EBUSY;
		goto out;
	}

	cdb89712_flash_map.virt = ioremap(FLASH_START, FLASH_SIZE);
	if (!cdb89712_flash_map.virt) {
		printk(KERN_NOTICE "Failed to ioremap Cdb89712 FLASH space\n");
		err = -EIO;
		goto out_resource;
	}
	simple_map_init(&cdb89712_flash_map);
	flash_mtd = do_map_probe("cfi_probe", &cdb89712_flash_map);
	if (!flash_mtd) {
		flash_mtd = do_map_probe("map_rom", &cdb89712_flash_map);
		if (flash_mtd)
			flash_mtd->erasesize = 0x10000;
	}
	if (!flash_mtd) {
		printk("FLASH probe failed\n");
		err = -ENXIO;
		goto out_ioremap;
	}

	flash_mtd->owner = THIS_MODULE;

	if (add_mtd_device(flash_mtd)) {
		printk("FLASH device addition failed\n");
		err = -ENOMEM;
		goto out_probe;
	}

	return 0;

out_probe:
	map_destroy(flash_mtd);
	flash_mtd = 0;
out_ioremap:
	iounmap((void *)cdb89712_flash_map.virt);
out_resource:
	release_resource (&cdb89712_flash_resource);
out:
	return err;
}





static struct mtd_info *sram_mtd;

struct map_info cdb89712_sram_map = {
	.name = "SRAM",
	.size = SRAM_SIZE,
	.bankwidth = SRAM_WIDTH,
	.phys = SRAM_START,
};

struct resource cdb89712_sram_resource = {
	.name =   "SRAM",
	.start =  SRAM_START,
	.end =    SRAM_START + SRAM_SIZE - 1,
	.flags =  IORESOURCE_IO | IORESOURCE_BUSY,
};

static int __init init_cdb89712_sram (void)
{
	int err;

	if (request_resource (&ioport_resource, &cdb89712_sram_resource)) {
		printk(KERN_NOTICE "Failed to reserve Cdb89712 SRAM space\n");
		err = -EBUSY;
		goto out;
	}

	cdb89712_sram_map.virt = ioremap(SRAM_START, SRAM_SIZE);
	if (!cdb89712_sram_map.virt) {
		printk(KERN_NOTICE "Failed to ioremap Cdb89712 SRAM space\n");
		err = -EIO;
		goto out_resource;
	}
	simple_map_init(&cdb89712_sram_map);
	sram_mtd = do_map_probe("map_ram", &cdb89712_sram_map);
	if (!sram_mtd) {
		printk("SRAM probe failed\n");
		err = -ENXIO;
		goto out_ioremap;
	}

	sram_mtd->owner = THIS_MODULE;
	sram_mtd->erasesize = 16;

	if (add_mtd_device(sram_mtd)) {
		printk("SRAM device addition failed\n");
		err = -ENOMEM;
		goto out_probe;
	}

	return 0;

out_probe:
	map_destroy(sram_mtd);
	sram_mtd = 0;
out_ioremap:
	iounmap((void *)cdb89712_sram_map.virt);
out_resource:
	release_resource (&cdb89712_sram_resource);
out:
	return err;
}







static struct mtd_info *bootrom_mtd;

struct map_info cdb89712_bootrom_map = {
	.name = "BootROM",
	.size = BOOTROM_SIZE,
	.bankwidth = BOOTROM_WIDTH,
	.phys = BOOTROM_START,
};

struct resource cdb89712_bootrom_resource = {
	.name =   "BootROM",
	.start =  BOOTROM_START,
	.end =    BOOTROM_START + BOOTROM_SIZE - 1,
	.flags =  IORESOURCE_IO | IORESOURCE_BUSY,
};

static int __init init_cdb89712_bootrom (void)
{
	int err;

	if (request_resource (&ioport_resource, &cdb89712_bootrom_resource)) {
		printk(KERN_NOTICE "Failed to reserve Cdb89712 BOOTROM space\n");
		err = -EBUSY;
		goto out;
	}

	cdb89712_bootrom_map.virt = ioremap(BOOTROM_START, BOOTROM_SIZE);
	if (!cdb89712_bootrom_map.virt) {
		printk(KERN_NOTICE "Failed to ioremap Cdb89712 BootROM space\n");
		err = -EIO;
		goto out_resource;
	}
	simple_map_init(&cdb89712_bootrom_map);
	bootrom_mtd = do_map_probe("map_rom", &cdb89712_bootrom_map);
	if (!bootrom_mtd) {
		printk("BootROM probe failed\n");
		err = -ENXIO;
		goto out_ioremap;
	}

	bootrom_mtd->owner = THIS_MODULE;
	bootrom_mtd->erasesize = 0x10000;

	if (add_mtd_device(bootrom_mtd)) {
		printk("BootROM device addition failed\n");
		err = -ENOMEM;
		goto out_probe;
	}

	return 0;

out_probe:
	map_destroy(bootrom_mtd);
	bootrom_mtd = 0;
out_ioremap:
	iounmap((void *)cdb89712_bootrom_map.virt);
out_resource:
	release_resource (&cdb89712_bootrom_resource);
out:
	return err;
}





static int __init init_cdb89712_maps(void)
{

       	printk(KERN_INFO "Cirrus CDB89712 MTD mappings:\n  Flash 0x%x at 0x%x\n  SRAM 0x%x at 0x%x\n  BootROM 0x%x at 0x%x\n",
	       FLASH_SIZE, FLASH_START, SRAM_SIZE, SRAM_START, BOOTROM_SIZE, BOOTROM_START);

	init_cdb89712_flash();
	init_cdb89712_sram();
	init_cdb89712_bootrom();

	return 0;
}


static void __exit cleanup_cdb89712_maps(void)
{
	if (sram_mtd) {
		del_mtd_device(sram_mtd);
		map_destroy(sram_mtd);
		iounmap((void *)cdb89712_sram_map.virt);
		release_resource (&cdb89712_sram_resource);
	}

	if (flash_mtd) {
		del_mtd_device(flash_mtd);
		map_destroy(flash_mtd);
		iounmap((void *)cdb89712_flash_map.virt);
		release_resource (&cdb89712_flash_resource);
	}

	if (bootrom_mtd) {
		del_mtd_device(bootrom_mtd);
		map_destroy(bootrom_mtd);
		iounmap((void *)cdb89712_bootrom_map.virt);
		release_resource (&cdb89712_bootrom_resource);
	}
}

module_init(init_cdb89712_maps);
module_exit(cleanup_cdb89712_maps);

MODULE_AUTHOR("Ray L");
MODULE_DESCRIPTION("ARM CDB89712 map driver");
MODULE_LICENSE("GPL");
