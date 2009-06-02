/*
 * Toshiba RBTX4939 setup routines.
 * Based on linux/arch/mips/txx9/rbtx4938/setup.c,
 *	    and RBTX49xx patch from CELF patch archive.
 *
 * Copyright (C) 2000-2001,2005-2007 Toshiba Corporation
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/interrupt.h>
#include <linux/smc91x.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/map.h>
#include <asm/reboot.h>
#include <asm/txx9/generic.h>
#include <asm/txx9/pci.h>
#include <asm/txx9/rbtx4939.h>

static void rbtx4939_machine_restart(char *command)
{
	local_irq_disable();
	writeb(1, rbtx4939_reseten_addr);
	writeb(1, rbtx4939_softreset_addr);
	while (1)
		;
}

static void __init rbtx4939_time_init(void)
{
	tx4939_time_init(0);
}

#if defined(__BIG_ENDIAN) && \
	(defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE))
#define HAVE_RBTX4939_IOSWAB
#define IS_CE1_ADDR(addr) \
	((((unsigned long)(addr) - IO_BASE) & 0xfff00000) == TXX9_CE(1))
static u16 rbtx4939_ioswabw(volatile u16 *a, u16 x)
{
	return IS_CE1_ADDR(a) ? x : le16_to_cpu(x);
}
static u16 rbtx4939_mem_ioswabw(volatile u16 *a, u16 x)
{
	return !IS_CE1_ADDR(a) ? x : le16_to_cpu(x);
}
#endif /* __BIG_ENDIAN && CONFIG_SMC91X */

static void __init rbtx4939_pci_setup(void)
{
#ifdef CONFIG_PCI
	int extarb = !(__raw_readq(&tx4939_ccfgptr->ccfg) & TX4939_CCFG_PCIARB);
	struct pci_controller *c = &txx9_primary_pcic;

	register_pci_controller(c);

	tx4939_report_pciclk();
	tx4927_pcic_setup(tx4939_pcicptr, c, extarb);
	if (!(__raw_readq(&tx4939_ccfgptr->pcfg) & TX4939_PCFG_ATA1MODE) &&
	    (__raw_readq(&tx4939_ccfgptr->pcfg) &
	     (TX4939_PCFG_ET0MODE | TX4939_PCFG_ET1MODE))) {
		tx4939_report_pci1clk();

		/* mem:64K(max), io:64K(max) (enough for ETH0,ETH1) */
		c = txx9_alloc_pci_controller(NULL, 0, 0x10000, 0, 0x10000);
		register_pci_controller(c);
		tx4927_pcic_setup(tx4939_pcic1ptr, c, 0);
	}

	tx4939_setup_pcierr_irq();
#endif /* CONFIG_PCI */
}

static unsigned long long default_ebccr[] __initdata = {
	0x01c0000000007608ULL, /* 64M ROM */
	0x017f000000007049ULL, /* 1M IOC */
	0x0180000000408608ULL, /* ISA */
	0,
};

static void __init rbtx4939_ebusc_setup(void)
{
	int i;
	unsigned int sp;

	/* use user-configured speed */
	sp = TX4939_EBUSC_CR(0) & 0x30;
	default_ebccr[0] |= sp;
	default_ebccr[1] |= sp;
	default_ebccr[2] |= sp;
	/* initialise by myself */
	for (i = 0; i < ARRAY_SIZE(default_ebccr); i++) {
		if (default_ebccr[i])
			____raw_writeq(default_ebccr[i],
				       &tx4939_ebuscptr->cr[i]);
		else
			____raw_writeq(____raw_readq(&tx4939_ebuscptr->cr[i])
				       & ~8,
				       &tx4939_ebuscptr->cr[i]);
	}
}

static void __init rbtx4939_update_ioc_pen(void)
{
	__u64 pcfg = ____raw_readq(&tx4939_ccfgptr->pcfg);
	__u64 ccfg = ____raw_readq(&tx4939_ccfgptr->ccfg);
	__u8 pe1 = readb(rbtx4939_pe1_addr);
	__u8 pe2 = readb(rbtx4939_pe2_addr);
	__u8 pe3 = readb(rbtx4939_pe3_addr);
	if (pcfg & TX4939_PCFG_ATA0MODE)
		pe1 |= RBTX4939_PE1_ATA(0);
	else
		pe1 &= ~RBTX4939_PE1_ATA(0);
	if (pcfg & TX4939_PCFG_ATA1MODE) {
		pe1 |= RBTX4939_PE1_ATA(1);
		pe1 &= ~(RBTX4939_PE1_RMII(0) | RBTX4939_PE1_RMII(1));
	} else {
		pe1 &= ~RBTX4939_PE1_ATA(1);
		if (pcfg & TX4939_PCFG_ET0MODE)
			pe1 |= RBTX4939_PE1_RMII(0);
		else
			pe1 &= ~RBTX4939_PE1_RMII(0);
		if (pcfg & TX4939_PCFG_ET1MODE)
			pe1 |= RBTX4939_PE1_RMII(1);
		else
			pe1 &= ~RBTX4939_PE1_RMII(1);
	}
	if (ccfg & TX4939_CCFG_PTSEL)
		pe3 &= ~(RBTX4939_PE3_VP | RBTX4939_PE3_VP_P |
			 RBTX4939_PE3_VP_S);
	else {
		__u64 vmode = pcfg &
			(TX4939_PCFG_VSSMODE | TX4939_PCFG_VPSMODE);
		if (vmode == 0)
			pe3 &= ~(RBTX4939_PE3_VP | RBTX4939_PE3_VP_P |
				 RBTX4939_PE3_VP_S);
		else if (vmode == TX4939_PCFG_VPSMODE) {
			pe3 |= RBTX4939_PE3_VP_P;
			pe3 &= ~(RBTX4939_PE3_VP | RBTX4939_PE3_VP_S);
		} else if (vmode == TX4939_PCFG_VSSMODE) {
			pe3 |= RBTX4939_PE3_VP | RBTX4939_PE3_VP_S;
			pe3 &= ~RBTX4939_PE3_VP_P;
		} else {
			pe3 |= RBTX4939_PE3_VP | RBTX4939_PE3_VP_P;
			pe3 &= ~RBTX4939_PE3_VP_S;
		}
	}
	if (pcfg & TX4939_PCFG_SPIMODE) {
		if (pcfg & TX4939_PCFG_SIO2MODE_GPIO)
			pe2 &= ~(RBTX4939_PE2_SIO2 | RBTX4939_PE2_SIO0);
		else {
			if (pcfg & TX4939_PCFG_SIO2MODE_SIO2) {
				pe2 |= RBTX4939_PE2_SIO2;
				pe2 &= ~RBTX4939_PE2_SIO0;
			} else {
				pe2 |= RBTX4939_PE2_SIO0;
				pe2 &= ~RBTX4939_PE2_SIO2;
			}
		}
		if (pcfg & TX4939_PCFG_SIO3MODE)
			pe2 |= RBTX4939_PE2_SIO3;
		else
			pe2 &= ~RBTX4939_PE2_SIO3;
		pe2 &= ~RBTX4939_PE2_SPI;
	} else {
		pe2 |= RBTX4939_PE2_SPI;
		pe2 &= ~(RBTX4939_PE2_SIO3 | RBTX4939_PE2_SIO2 |
			 RBTX4939_PE2_SIO0);
	}
	if ((pcfg & TX4939_PCFG_I2SMODE_MASK) == TX4939_PCFG_I2SMODE_GPIO)
		pe2 |= RBTX4939_PE2_GPIO;
	else
		pe2 &= ~RBTX4939_PE2_GPIO;
	writeb(pe1, rbtx4939_pe1_addr);
	writeb(pe2, rbtx4939_pe2_addr);
	writeb(pe3, rbtx4939_pe3_addr);
}

#define RBTX4939_MAX_7SEGLEDS	8

#if defined(CONFIG_LEDS_CLASS) || defined(CONFIG_LEDS_CLASS_MODULE)
static u8 led_val[RBTX4939_MAX_7SEGLEDS];
struct rbtx4939_led_data {
	struct led_classdev cdev;
	char name[32];
	unsigned int num;
};

/* Use "dot" in 7seg LEDs */
static void rbtx4939_led_brightness_set(struct led_classdev *led_cdev,
					enum led_brightness value)
{
	struct rbtx4939_led_data *led_dat =
		container_of(led_cdev, struct rbtx4939_led_data, cdev);
	unsigned int num = led_dat->num;
	unsigned long flags;

	local_irq_save(flags);
	led_val[num] = (led_val[num] & 0x7f) | (value ? 0x80 : 0);
	writeb(led_val[num], rbtx4939_7seg_addr(num / 4, num % 4));
	local_irq_restore(flags);
}

static int __init rbtx4939_led_probe(struct platform_device *pdev)
{
	struct rbtx4939_led_data *leds_data;
	int i;
	static char *default_triggers[] __initdata = {
		"heartbeat",
		"ide-disk",
		"nand-disk",
	};

	leds_data = kzalloc(sizeof(*leds_data) * RBTX4939_MAX_7SEGLEDS,
			    GFP_KERNEL);
	if (!leds_data)
		return -ENOMEM;
	for (i = 0; i < RBTX4939_MAX_7SEGLEDS; i++) {
		int rc;
		struct rbtx4939_led_data *led_dat = &leds_data[i];

		led_dat->num = i;
		led_dat->cdev.brightness_set = rbtx4939_led_brightness_set;
		sprintf(led_dat->name, "rbtx4939:amber:%u", i);
		led_dat->cdev.name = led_dat->name;
		if (i < ARRAY_SIZE(default_triggers))
			led_dat->cdev.default_trigger = default_triggers[i];
		rc = led_classdev_register(&pdev->dev, &led_dat->cdev);
		if (rc < 0)
			return rc;
		led_dat->cdev.brightness_set(&led_dat->cdev, 0);
	}
	return 0;

}

static struct platform_driver rbtx4939_led_driver = {
	.driver  = {
		.name = "rbtx4939-led",
		.owner = THIS_MODULE,
	},
};

static void __init rbtx4939_led_setup(void)
{
	platform_device_register_simple("rbtx4939-led", -1, NULL, 0);
	platform_driver_probe(&rbtx4939_led_driver, rbtx4939_led_probe);
}
#else
static inline void rbtx4939_led_setup(void)
{
}
#endif

static void __rbtx4939_7segled_putc(unsigned int pos, unsigned char val)
{
#if defined(CONFIG_LEDS_CLASS) || defined(CONFIG_LEDS_CLASS_MODULE)
	unsigned long flags;
	local_irq_save(flags);
	/* bit7: reserved for LED class */
	led_val[pos] = (led_val[pos] & 0x80) | (val & 0x7f);
	val = led_val[pos];
	local_irq_restore(flags);
#endif
	writeb(val, rbtx4939_7seg_addr(pos / 4, pos % 4));
}

static void rbtx4939_7segled_putc(unsigned int pos, unsigned char val)
{
	/* convert from map_to_seg7() notation */
	val = (val & 0x88) |
		((val & 0x40) >> 6) |
		((val & 0x20) >> 4) |
		((val & 0x10) >> 2) |
		((val & 0x04) << 2) |
		((val & 0x02) << 4) |
		((val & 0x01) << 6);
	__rbtx4939_7segled_putc(pos, val);
}

#if defined(CONFIG_MTD_RBTX4939) || defined(CONFIG_MTD_RBTX4939_MODULE)
/* special mapping for boot rom */
static unsigned long rbtx4939_flash_fixup_ofs(unsigned long ofs)
{
	u8 bdipsw = readb(rbtx4939_bdipsw_addr) & 0x0f;
	unsigned char shift;

	if (bdipsw & 8) {
		/* BOOT Mode: USER ROM1 / USER ROM2 */
		shift = bdipsw & 3;
		/* rotate A[23:22] */
		return (ofs & ~0xc00000) | ((((ofs >> 22) + shift) & 3) << 22);
	}
#ifdef __BIG_ENDIAN
	if (bdipsw == 0)
		/* BOOT Mode: Monitor ROM */
		ofs ^= 0x400000;	/* swap A[22] */
#endif
	return ofs;
}

static map_word rbtx4939_flash_read16(struct map_info *map, unsigned long ofs)
{
	map_word r;

	ofs = rbtx4939_flash_fixup_ofs(ofs);
	r.x[0] = __raw_readw(map->virt + ofs);
	return r;
}

static void rbtx4939_flash_write16(struct map_info *map, const map_word datum,
				   unsigned long ofs)
{
	ofs = rbtx4939_flash_fixup_ofs(ofs);
	__raw_writew(datum.x[0], map->virt + ofs);
	mb();	/* see inline_map_write() in mtd/map.h */
}

static void rbtx4939_flash_copy_from(struct map_info *map, void *to,
				     unsigned long from, ssize_t len)
{
	u8 bdipsw = readb(rbtx4939_bdipsw_addr) & 0x0f;
	unsigned char shift;
	ssize_t curlen;

	from += (unsigned long)map->virt;
	if (bdipsw & 8) {
		/* BOOT Mode: USER ROM1 / USER ROM2 */
		shift = bdipsw & 3;
		while (len) {
			curlen = min_t(unsigned long, len,
				     0x400000 -	(from & (0x400000 - 1)));
			memcpy(to,
			       (void *)((from & ~0xc00000) |
					((((from >> 22) + shift) & 3) << 22)),
			       curlen);
			len -= curlen;
			from += curlen;
			to += curlen;
		}
		return;
	}
#ifdef __BIG_ENDIAN
	if (bdipsw == 0) {
		/* BOOT Mode: Monitor ROM */
		while (len) {
			curlen = min_t(unsigned long, len,
				     0x400000 - (from & (0x400000 - 1)));
			memcpy(to, (void *)(from ^ 0x400000), curlen);
			len -= curlen;
			from += curlen;
			to += curlen;
		}
		return;
	}
#endif
	memcpy(to, (void *)from, len);
}

static void rbtx4939_flash_map_init(struct map_info *map)
{
	map->read = rbtx4939_flash_read16;
	map->write = rbtx4939_flash_write16;
	map->copy_from = rbtx4939_flash_copy_from;
}

static void __init rbtx4939_mtd_init(void)
{
	static struct {
		struct platform_device dev;
		struct resource res;
		struct rbtx4939_flash_data data;
	} pdevs[4];
	int i;
	static char names[4][8];
	static struct mtd_partition parts[4];
	struct rbtx4939_flash_data *boot_pdata = &pdevs[0].data;
	u8 bdipsw = readb(rbtx4939_bdipsw_addr) & 0x0f;

	if (bdipsw & 8) {
		/* BOOT Mode: USER ROM1 / USER ROM2 */
		boot_pdata->nr_parts = 4;
		for (i = 0; i < boot_pdata->nr_parts; i++) {
			sprintf(names[i], "img%d", 4 - i);
			parts[i].name = names[i];
			parts[i].size = 0x400000;
			parts[i].offset = MTDPART_OFS_NXTBLK;
		}
	} else if (bdipsw == 0) {
		/* BOOT Mode: Monitor ROM */
		boot_pdata->nr_parts = 2;
		strcpy(names[0], "big");
		strcpy(names[1], "little");
		for (i = 0; i < boot_pdata->nr_parts; i++) {
			parts[i].name = names[i];
			parts[i].size = 0x400000;
			parts[i].offset = MTDPART_OFS_NXTBLK;
		}
	} else {
		/* BOOT Mode: ROM Emulator */
		boot_pdata->nr_parts = 2;
		parts[0].name = "boot";
		parts[0].offset = 0xc00000;
		parts[0].size = 0x400000;
		parts[1].name = "user";
		parts[1].offset = 0;
		parts[1].size = 0xc00000;
	}
	boot_pdata->parts = parts;
	boot_pdata->map_init = rbtx4939_flash_map_init;

	for (i = 0; i < ARRAY_SIZE(pdevs); i++) {
		struct resource *r = &pdevs[i].res;
		struct platform_device *dev = &pdevs[i].dev;

		r->start = 0x1f000000 - i * 0x1000000;
		r->end = r->start + 0x1000000 - 1;
		r->flags = IORESOURCE_MEM;
		pdevs[i].data.width = 2;
		dev->num_resources = 1;
		dev->resource = r;
		dev->id = i;
		dev->name = "rbtx4939-flash";
		dev->dev.platform_data = &pdevs[i].data;
		platform_device_register(dev);
	}
}
#else
static void __init rbtx4939_mtd_init(void)
{
}
#endif

static void __init rbtx4939_arch_init(void)
{
	rbtx4939_pci_setup();
}

static void __init rbtx4939_device_init(void)
{
	unsigned long smc_addr = RBTX4939_ETHER_ADDR - IO_BASE;
	struct resource smc_res[] = {
		{
			.start	= smc_addr,
			.end	= smc_addr + 0x10 - 1,
			.flags	= IORESOURCE_MEM,
		}, {
			.start	= RBTX4939_IRQ_ETHER,
			/* override default irq flag defined in smc91x.h */
			.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_LOW,
		},
	};
	struct smc91x_platdata smc_pdata = {
		.flags = SMC91X_USE_16BIT,
	};
	struct platform_device *pdev;
#if defined(CONFIG_TC35815) || defined(CONFIG_TC35815_MODULE)
	int i, j;
	unsigned char ethaddr[2][6];
	u8 bdipsw = readb(rbtx4939_bdipsw_addr) & 0x0f;

	for (i = 0; i < 2; i++) {
		unsigned long area = CKSEG1 + 0x1fff0000 + (i * 0x10);
		if (bdipsw == 0)
			memcpy(ethaddr[i], (void *)area, 6);
		else {
			u16 buf[3];
			if (bdipsw & 8)
				area -= 0x03000000;
			else
				area -= 0x01000000;
			for (j = 0; j < 3; j++)
				buf[j] = le16_to_cpup((u16 *)(area + j * 2));
			memcpy(ethaddr[i], buf, 6);
		}
	}
	tx4939_ethaddr_init(ethaddr[0], ethaddr[1]);
#endif
	pdev = platform_device_alloc("smc91x", -1);
	if (!pdev ||
	    platform_device_add_resources(pdev, smc_res, ARRAY_SIZE(smc_res)) ||
	    platform_device_add_data(pdev, &smc_pdata, sizeof(smc_pdata)) ||
	    platform_device_add(pdev))
		platform_device_put(pdev);
	rbtx4939_mtd_init();
	/* TC58DVM82A1FT: tDH=10ns, tWP=tRP=tREADID=35ns */
	tx4939_ndfmc_init(10, 35,
			  (1 << 1) | (1 << 2),
			  (1 << 2)); /* ch1:8bit, ch2:16bit */
	rbtx4939_led_setup();
	tx4939_wdt_init();
	tx4939_ata_init();
	tx4939_rtc_init();
	tx4939_dmac_init(0, 2);
	tx4939_aclc_init();
	platform_device_register_simple("txx9aclc-generic", -1, NULL, 0);
	tx4939_sramc_init();
	tx4939_rng_init();
}

static void __init rbtx4939_setup(void)
{
	int i;

	rbtx4939_ebusc_setup();
	/* always enable ATA0 */
	txx9_set64(&tx4939_ccfgptr->pcfg, TX4939_PCFG_ATA0MODE);
	rbtx4939_update_ioc_pen();
	if (txx9_master_clock == 0)
		txx9_master_clock = 20000000;
	tx4939_setup();
#ifdef HAVE_RBTX4939_IOSWAB
	ioswabw = rbtx4939_ioswabw;
	__mem_ioswabw = rbtx4939_mem_ioswabw;
#endif

	_machine_restart = rbtx4939_machine_restart;

	txx9_7segled_init(RBTX4939_MAX_7SEGLEDS, rbtx4939_7segled_putc);
	for (i = 0; i < RBTX4939_MAX_7SEGLEDS; i++)
		txx9_7segled_putc(i, '-');
	pr_info("RBTX4939 (Rev %02x) --- FPGA(Rev %02x) DIPSW:%02x,%02x\n",
		readb(rbtx4939_board_rev_addr), readb(rbtx4939_ioc_rev_addr),
		readb(rbtx4939_udipsw_addr), readb(rbtx4939_bdipsw_addr));

#ifdef CONFIG_PCI
	txx9_alloc_pci_controller(&txx9_primary_pcic, 0, 0, 0, 0);
	txx9_board_pcibios_setup = tx4927_pcibios_setup;
#else
	set_io_port_base(RBTX4939_ETHER_BASE);
#endif

	tx4939_sio_init(TX4939_SCLK0(txx9_master_clock), 0);
}

struct txx9_board_vec rbtx4939_vec __initdata = {
	.system = "Toshiba RBTX4939",
	.prom_init = rbtx4939_prom_init,
	.mem_setup = rbtx4939_setup,
	.irq_setup = rbtx4939_irq_setup,
	.time_init = rbtx4939_time_init,
	.device_init = rbtx4939_device_init,
	.arch_init = rbtx4939_arch_init,
#ifdef CONFIG_PCI
	.pci_map_irq = tx4939_pci_map_irq,
#endif
};
