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

static void __init rbtx4939_arch_init(void)
{
	rbtx4939_pci_setup();
}

static void __init rbtx4939_device_init(void)
{
#if defined(CONFIG_TC35815) || defined(CONFIG_TC35815_MODULE)
	int i, j;
	unsigned char ethaddr[2][6];
	for (i = 0; i < 2; i++) {
		unsigned long area = CKSEG1 + 0x1fff0000 + (i * 0x10);
		if (readb(rbtx4939_bdipsw_addr) & 8) {
			u16 buf[3];
			area -= 0x03000000;
			for (j = 0; j < 3; j++)
				buf[j] = le16_to_cpup((u16 *)(area + j * 2));
			memcpy(ethaddr[i], buf, 6);
		} else
			memcpy(ethaddr[i], (void *)area, 6);
	}
	tx4939_ethaddr_init(ethaddr[0], ethaddr[1]);
#endif
	rbtx4939_led_setup();
	tx4939_wdt_init();
	tx4939_ata_init();
}

static void __init rbtx4939_setup(void)
{
	rbtx4939_ebusc_setup();
	/* always enable ATA0 */
	txx9_set64(&tx4939_ccfgptr->pcfg, TX4939_PCFG_ATA0MODE);
	rbtx4939_update_ioc_pen();
	if (txx9_master_clock == 0)
		txx9_master_clock = 20000000;
	tx4939_setup();

	_machine_restart = rbtx4939_machine_restart;

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
	.system = "Tothiba RBTX4939",
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
