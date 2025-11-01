// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/if_ether.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <asm/paccess.h>
#include <asm/sgi/ip22.h>
#include <asm/sgi/hpc3.h>
#include <asm/sgi/mc.h>
#include <asm/sgi/seeq.h>
#include <asm/sgi/wd.h>

static struct resource sgiwd93_0_resources[] = {
	{
		.name	= "eth0 irq",
		.start	= SGI_WD93_0_IRQ,
		.end	= SGI_WD93_0_IRQ,
		.flags	= IORESOURCE_IRQ
	}
};

static struct sgiwd93_platform_data sgiwd93_0_pd = {
	.unit	= 0,
	.irq	= SGI_WD93_0_IRQ,
};

static u64 sgiwd93_0_dma_mask = DMA_BIT_MASK(32);

static struct platform_device sgiwd93_0_device = {
	.name		= "sgiwd93",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(sgiwd93_0_resources),
	.resource	= sgiwd93_0_resources,
	.dev = {
		.platform_data = &sgiwd93_0_pd,
		.dma_mask = &sgiwd93_0_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct resource sgiwd93_1_resources[] = {
	{
		.name	= "eth0 irq",
		.start	= SGI_WD93_1_IRQ,
		.end	= SGI_WD93_1_IRQ,
		.flags	= IORESOURCE_IRQ
	}
};

static struct sgiwd93_platform_data sgiwd93_1_pd = {
	.unit	= 1,
	.irq	= SGI_WD93_1_IRQ,
};

static u64 sgiwd93_1_dma_mask = DMA_BIT_MASK(32);

static struct platform_device sgiwd93_1_device = {
	.name		= "sgiwd93",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(sgiwd93_1_resources),
	.resource	= sgiwd93_1_resources,
	.dev = {
		.platform_data = &sgiwd93_1_pd,
		.dma_mask = &sgiwd93_1_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

/*
 * Create a platform device for the GPI port that receives the
 * image data from the embedded camera.
 */
static int __init sgiwd93_devinit(void)
{
	int res;

	sgiwd93_0_pd.hregs	= &hpc3c0->scsi_chan0;
	sgiwd93_0_pd.wdregs	= (unsigned char *) hpc3c0->scsi0_ext;

	res = platform_device_register(&sgiwd93_0_device);
	if (res)
		return res;

	if (!ip22_is_fullhouse())
		return 0;

	sgiwd93_1_pd.hregs	= &hpc3c0->scsi_chan1;
	sgiwd93_1_pd.wdregs	= (unsigned char *) hpc3c0->scsi1_ext;

	return platform_device_register(&sgiwd93_1_device);
}

device_initcall(sgiwd93_devinit);

static struct resource sgiseeq_0_resources[] = {
	{
		.name	= "eth0 irq",
		.start	= SGI_ENET_IRQ,
		.end	= SGI_ENET_IRQ,
		.flags	= IORESOURCE_IRQ
	}
};

static struct sgiseeq_platform_data eth0_pd;

static u64 sgiseeq_dma_mask = DMA_BIT_MASK(32);

static struct platform_device eth0_device = {
	.name		= "sgiseeq",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(sgiseeq_0_resources),
	.resource	= sgiseeq_0_resources,
	.dev = {
		.platform_data = &eth0_pd,
		.dma_mask = &sgiseeq_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct resource sgiseeq_1_resources[] = {
	{
		.name	= "eth1 irq",
		.start	= SGI_GIO_0_IRQ,
		.end	= SGI_GIO_0_IRQ,
		.flags	= IORESOURCE_IRQ
	}
};

static struct sgiseeq_platform_data eth1_pd;

static struct platform_device eth1_device = {
	.name		= "sgiseeq",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(sgiseeq_1_resources),
	.resource	= sgiseeq_1_resources,
	.dev = {
		.platform_data = &eth1_pd,
	},
};

/*
 * Create a platform device for the GPI port that receives the
 * image data from the embedded camera.
 */
static int __init sgiseeq_devinit(void)
{
	unsigned int pbdma __maybe_unused;
	int res, i;

	eth0_pd.hpc = hpc3c0;
	eth0_pd.irq = SGI_ENET_IRQ;
#define EADDR_NVOFS	250
	for (i = 0; i < 3; i++) {
		unsigned short tmp = ip22_nvram_read(EADDR_NVOFS / 2 + i);

		eth0_pd.mac[2 * i]     = tmp >> 8;
		eth0_pd.mac[2 * i + 1] = tmp & 0xff;
	}

	res = platform_device_register(&eth0_device);
	if (res)
		return res;

	/* Second HPC is missing? */
	if (ip22_is_fullhouse() ||
	    get_dbe(pbdma, (unsigned int *)&hpc3c1->pbdma[1]))
		return 0;

	sgimc->giopar |= SGIMC_GIOPAR_MASTEREXP1 | SGIMC_GIOPAR_EXP164 |
			 SGIMC_GIOPAR_HPC264;
	hpc3c1->pbus_piocfg[0][0] = 0x3ffff;
	/* interrupt/config register on Challenge S Mezz board */
	hpc3c1->pbus_extregs[0][0] = 0x30;

	eth1_pd.hpc = hpc3c1;
	eth1_pd.irq = SGI_GIO_0_IRQ;
#define EADDR_NVOFS	250
	for (i = 0; i < 3; i++) {
		unsigned short tmp = ip22_eeprom_read(&hpc3c1->eeprom,
						      EADDR_NVOFS / 2 + i);

		eth1_pd.mac[2 * i]     = tmp >> 8;
		eth1_pd.mac[2 * i + 1] = tmp & 0xff;
	}

	return platform_device_register(&eth1_device);
}

device_initcall(sgiseeq_devinit);

static int __init sgi_hal2_devinit(void)
{
	return IS_ERR(platform_device_register_simple("sgihal2", 0, NULL, 0));
}

device_initcall(sgi_hal2_devinit);

static int __init sgi_button_devinit(void)
{
	if (ip22_is_fullhouse())
		return 0; /* full house has no volume buttons */

	return IS_ERR(platform_device_register_simple("sgibtns", -1, NULL, 0));
}

device_initcall(sgi_button_devinit);

static int __init sgi_ds1286_devinit(void)
{
	struct resource res;

	memset(&res, 0, sizeof(res));
	res.start = HPC3_CHIP0_BASE + offsetof(struct hpc3_regs, rtcregs);
	res.end = res.start + sizeof(hpc3c0->rtcregs) - 1;
	res.flags = IORESOURCE_MEM;

	return IS_ERR(platform_device_register_simple("rtc-ds1286", -1,
						      &res, 1));
}

device_initcall(sgi_ds1286_devinit);

#define SGI_ZILOG_BASE	(HPC3_CHIP0_BASE + \
			 offsetof(struct hpc3_regs, pbus_extregs[6]) + \
			 offsetof(struct sgioc_regs, uart))

static struct resource sgi_zilog_resources[] = {
	{
		.start	= SGI_ZILOG_BASE,
		.end	= SGI_ZILOG_BASE + 15,
		.flags	= IORESOURCE_MEM
	},
	{
		.start	= SGI_SERIAL_IRQ,
		.end	= SGI_SERIAL_IRQ,
		.flags	= IORESOURCE_IRQ
	}
};

static struct platform_device zilog_device = {
	.name		= "ip22zilog",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(sgi_zilog_resources),
	.resource	= sgi_zilog_resources,
};


static int __init sgi_zilog_devinit(void)
{
	return platform_device_register(&zilog_device);
}

device_initcall(sgi_zilog_devinit);
