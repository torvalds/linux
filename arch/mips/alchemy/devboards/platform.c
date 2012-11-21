/*
 * devoard misc stuff.
 */

#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/physmap.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/pm.h>

#include <asm/bootinfo.h>
#include <asm/reboot.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-db1x00/bcsr.h>

#include <prom.h>

void __init prom_init(void)
{
	unsigned char *memsize_str;
	unsigned long memsize;

	prom_argc = (int)fw_arg0;
	prom_argv = (char **)fw_arg1;
	prom_envp = (char **)fw_arg2;

	prom_init_cmdline();
	memsize_str = prom_getenv("memsize");
	if (!memsize_str || kstrtoul(memsize_str, 0, &memsize))
		memsize = 64 << 20; /* all devboards have at least 64MB RAM */

	add_memory_region(0, memsize, BOOT_MEM_RAM);
}

void prom_putchar(unsigned char c)
{
	if (alchemy_get_cputype() == ALCHEMY_CPU_AU1300)
		alchemy_uart_putchar(AU1300_UART2_PHYS_ADDR, c);
	else
		alchemy_uart_putchar(AU1000_UART0_PHYS_ADDR, c);
}


static struct platform_device db1x00_rtc_dev = {
	.name	= "rtc-au1xxx",
	.id	= -1,
};


static void db1x_power_off(void)
{
	bcsr_write(BCSR_RESETS, 0);
	bcsr_write(BCSR_SYSTEM, BCSR_SYSTEM_PWROFF | BCSR_SYSTEM_RESET);
}

static void db1x_reset(char *c)
{
	bcsr_write(BCSR_RESETS, 0);
	bcsr_write(BCSR_SYSTEM, 0);
}

static int __init db1x_late_setup(void)
{
	if (!pm_power_off)
		pm_power_off = db1x_power_off;
	if (!_machine_halt)
		_machine_halt = db1x_power_off;
	if (!_machine_restart)
		_machine_restart = db1x_reset;

	platform_device_register(&db1x00_rtc_dev);

	return 0;
}
device_initcall(db1x_late_setup);

/* register a pcmcia socket */
int __init db1x_register_pcmcia_socket(phys_addr_t pcmcia_attr_start,
				       phys_addr_t pcmcia_attr_end,
				       phys_addr_t pcmcia_mem_start,
				       phys_addr_t pcmcia_mem_end,
				       phys_addr_t pcmcia_io_start,
				       phys_addr_t pcmcia_io_end,
				       int card_irq,
				       int cd_irq,
				       int stschg_irq,
				       int eject_irq,
				       int id)
{
	int cnt, i, ret;
	struct resource *sr;
	struct platform_device *pd;

	cnt = 5;
	if (eject_irq)
		cnt++;
	if (stschg_irq)
		cnt++;

	sr = kzalloc(sizeof(struct resource) * cnt, GFP_KERNEL);
	if (!sr)
		return -ENOMEM;

	pd = platform_device_alloc("db1xxx_pcmcia", id);
	if (!pd) {
		ret = -ENOMEM;
		goto out;
	}

	sr[0].name	= "pcmcia-attr";
	sr[0].flags	= IORESOURCE_MEM;
	sr[0].start	= pcmcia_attr_start;
	sr[0].end	= pcmcia_attr_end;

	sr[1].name	= "pcmcia-mem";
	sr[1].flags	= IORESOURCE_MEM;
	sr[1].start	= pcmcia_mem_start;
	sr[1].end	= pcmcia_mem_end;

	sr[2].name	= "pcmcia-io";
	sr[2].flags	= IORESOURCE_MEM;
	sr[2].start	= pcmcia_io_start;
	sr[2].end	= pcmcia_io_end;

	sr[3].name	= "insert";
	sr[3].flags	= IORESOURCE_IRQ;
	sr[3].start = sr[3].end = cd_irq;

	sr[4].name	= "card";
	sr[4].flags	= IORESOURCE_IRQ;
	sr[4].start = sr[4].end = card_irq;

	i = 5;
	if (stschg_irq) {
		sr[i].name	= "stschg";
		sr[i].flags	= IORESOURCE_IRQ;
		sr[i].start = sr[i].end = stschg_irq;
		i++;
	}
	if (eject_irq) {
		sr[i].name	= "eject";
		sr[i].flags	= IORESOURCE_IRQ;
		sr[i].start = sr[i].end = eject_irq;
	}

	pd->resource = sr;
	pd->num_resources = cnt;

	ret = platform_device_add(pd);
	if (!ret)
		return 0;

	platform_device_put(pd);
out:
	kfree(sr);
	return ret;
}

#define YAMON_SIZE	0x00100000
#define YAMON_ENV_SIZE	0x00040000

int __init db1x_register_norflash(unsigned long size, int width,
				  int swapped)
{
	struct physmap_flash_data *pfd;
	struct platform_device *pd;
	struct mtd_partition *parts;
	struct resource *res;
	int ret, i;

	if (size < (8 * 1024 * 1024))
		return -EINVAL;

	ret = -ENOMEM;
	parts = kzalloc(sizeof(struct mtd_partition) * 5, GFP_KERNEL);
	if (!parts)
		goto out;

	res = kzalloc(sizeof(struct resource), GFP_KERNEL);
	if (!res)
		goto out1;

	pfd = kzalloc(sizeof(struct physmap_flash_data), GFP_KERNEL);
	if (!pfd)
		goto out2;

	pd = platform_device_alloc("physmap-flash", 0);
	if (!pd)
		goto out3;

	/* NOR flash ends at 0x20000000, regardless of size */
	res->start = 0x20000000 - size;
	res->end = 0x20000000 - 1;
	res->flags = IORESOURCE_MEM;

	/* partition setup.  Most Develboards have a switch which allows
	 * to swap the physical locations of the 2 NOR flash banks.
	 */
	i = 0;
	if (!swapped) {
		/* first NOR chip */
		parts[i].offset = 0;
		parts[i].name = "User FS";
		parts[i].size = size / 2;
		i++;
	}

	parts[i].offset = MTDPART_OFS_APPEND;
	parts[i].name = "User FS 2";
	parts[i].size = (size / 2) - (0x20000000 - 0x1fc00000);
	i++;

	parts[i].offset = MTDPART_OFS_APPEND;
	parts[i].name = "YAMON";
	parts[i].size = YAMON_SIZE;
	parts[i].mask_flags = MTD_WRITEABLE;
	i++;

	parts[i].offset = MTDPART_OFS_APPEND;
	parts[i].name = "raw kernel";
	parts[i].size = 0x00400000 - YAMON_SIZE - YAMON_ENV_SIZE;
	i++;

	parts[i].offset = MTDPART_OFS_APPEND;
	parts[i].name = "YAMON Env";
	parts[i].size = YAMON_ENV_SIZE;
	parts[i].mask_flags = MTD_WRITEABLE;
	i++;

	if (swapped) {
		parts[i].offset = MTDPART_OFS_APPEND;
		parts[i].name = "User FS";
		parts[i].size = size / 2;
		i++;
	}

	pfd->width = width;
	pfd->parts = parts;
	pfd->nr_parts = 5;

	pd->dev.platform_data = pfd;
	pd->resource = res;
	pd->num_resources = 1;

	ret = platform_device_add(pd);
	if (!ret)
		return ret;

	platform_device_put(pd);
out3:
	kfree(pfd);
out2:
	kfree(res);
out1:
	kfree(parts);
out:
	return ret;
}
