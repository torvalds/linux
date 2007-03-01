#include <linux/delay.h>
#include <linux/if_ether.h>
#include <linux/ioport.h>
#include <linux/mv643xx.h>
#include <linux/platform_device.h>

#include "jaguar_atx_fpga.h"

#if defined(CONFIG_MV643XX_ETH) || defined(CONFIG_MV643XX_ETH_MODULE)

static struct resource mv643xx_eth_shared_resources[] = {
	[0] = {
		.name   = "ethernet shared base",
		.start  = 0xf1000000 + MV643XX_ETH_SHARED_REGS,
		.end    = 0xf1000000 + MV643XX_ETH_SHARED_REGS +
		                       MV643XX_ETH_SHARED_REGS_SIZE - 1,
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_device mv643xx_eth_shared_device = {
	.name		= MV643XX_ETH_SHARED_NAME,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(mv643xx_eth_shared_resources),
	.resource	= mv643xx_eth_shared_resources,
};

#define MV_SRAM_BASE			0xfe000000UL
#define MV_SRAM_SIZE			(256 * 1024)

#define MV_SRAM_RXRING_SIZE		(MV_SRAM_SIZE / 4)
#define MV_SRAM_TXRING_SIZE		(MV_SRAM_SIZE / 4)

#define MV_SRAM_BASE_ETH0		MV_SRAM_BASE
#define MV_SRAM_BASE_ETH1		(MV_SRAM_BASE + (MV_SRAM_SIZE / 2))

#define MV64x60_IRQ_ETH_0 48
#define MV64x60_IRQ_ETH_1 49
#define MV64x60_IRQ_ETH_2 50

static struct resource mv64x60_eth0_resources[] = {
	[0] = {
		.name	= "eth0 irq",
		.start	= MV64x60_IRQ_ETH_0,
		.end	= MV64x60_IRQ_ETH_0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv643xx_eth_platform_data eth0_pd = {
	.tx_sram_addr	= MV_SRAM_BASE_ETH0,
	.tx_sram_size	= MV_SRAM_TXRING_SIZE,
	.tx_queue_size	= MV_SRAM_TXRING_SIZE / 16,

	.rx_sram_addr	= MV_SRAM_BASE_ETH0 + MV_SRAM_TXRING_SIZE,
	.rx_sram_size	= MV_SRAM_RXRING_SIZE,
	.rx_queue_size	= MV_SRAM_RXRING_SIZE / 16,
};

static struct platform_device eth0_device = {
	.name		= MV643XX_ETH_NAME,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(mv64x60_eth0_resources),
	.resource	= mv64x60_eth0_resources,
	.dev = {
		.platform_data = &eth0_pd,
	},
};

static struct resource mv64x60_eth1_resources[] = {
	[0] = {
		.name	= "eth1 irq",
		.start	= MV64x60_IRQ_ETH_1,
		.end	= MV64x60_IRQ_ETH_1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv643xx_eth_platform_data eth1_pd = {
	.tx_sram_addr	= MV_SRAM_BASE_ETH1,
	.tx_sram_size	= MV_SRAM_TXRING_SIZE,
	.tx_queue_size	= MV_SRAM_TXRING_SIZE / 16,

	.rx_sram_addr	= MV_SRAM_BASE_ETH1 + MV_SRAM_TXRING_SIZE,
	.rx_sram_size	= MV_SRAM_RXRING_SIZE,
	.rx_queue_size	= MV_SRAM_RXRING_SIZE / 16,
};

static struct platform_device eth1_device = {
	.name		= MV643XX_ETH_NAME,
	.id		= 1,
	.num_resources	= ARRAY_SIZE(mv64x60_eth1_resources),
	.resource	= mv64x60_eth1_resources,
	.dev = {
		.platform_data = &eth1_pd,
	},
};

static struct resource mv64x60_eth2_resources[] = {
	[0] = {
		.name	= "eth2 irq",
		.start	= MV64x60_IRQ_ETH_2,
		.end	= MV64x60_IRQ_ETH_2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv643xx_eth_platform_data eth2_pd;

static struct platform_device eth2_device = {
	.name		= MV643XX_ETH_NAME,
	.id		= 2,
	.num_resources	= ARRAY_SIZE(mv64x60_eth2_resources),
	.resource	= mv64x60_eth2_resources,
	.dev = {
		.platform_data = &eth2_pd,
	},
};

static struct platform_device *mv643xx_eth_pd_devs[] __initdata = {
	&mv643xx_eth_shared_device,
	&eth0_device,
	&eth1_device,
	&eth2_device,
};

static u8 __init exchange_bit(u8 val, u8 cs)
{
	/* place the data */
	JAGUAR_FPGA_WRITE((val << 2) | cs, EEPROM_MODE);
	udelay(1);

	/* turn the clock on */
	JAGUAR_FPGA_WRITE((val << 2) | cs | 0x2, EEPROM_MODE);
	udelay(1);

	/* turn the clock off and read-strobe */
	JAGUAR_FPGA_WRITE((val << 2) | cs | 0x10, EEPROM_MODE);

	/* return the data */
	return (JAGUAR_FPGA_READ(EEPROM_MODE) >> 3) & 0x1;
}

static void __init get_mac(char dest[6])
{
	u8 read_opcode[12] = {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	int i,j;

	for (i = 0; i < 12; i++)
		exchange_bit(read_opcode[i], 1);

	for (j = 0; j < 6; j++) {
		dest[j] = 0;
		for (i = 0; i < 8; i++) {
			dest[j] <<= 1;
			dest[j] |= exchange_bit(0, 1);
		}
	}

	/* turn off CS */
	exchange_bit(0,0);
}

/*
 * Copy and increment ethernet MAC address by a small value.
 *
 * This is useful for systems where the only one MAC address is stored in
 * non-volatile memory for multiple ports.
 */
static inline void eth_mac_add(unsigned char *dst, unsigned char *src,
	unsigned int add)
{
	int i;

	BUG_ON(add >= 256);

	for (i = ETH_ALEN; i >= 0; i--) {
		dst[i] = src[i] + add;
		add = dst[i] < src[i];		/* compute carry */
	}

	WARN_ON(add);
}

static int __init mv643xx_eth_add_pds(void)
{
	unsigned char mac[ETH_ALEN];
	int ret;

	get_mac(mac);
	eth_mac_add(eth0_pd.mac_addr, mac, 0);
	eth_mac_add(eth1_pd.mac_addr, mac, 1);
	eth_mac_add(eth2_pd.mac_addr, mac, 2);
	ret = platform_add_devices(mv643xx_eth_pd_devs,
			ARRAY_SIZE(mv643xx_eth_pd_devs));

	return ret;
}

device_initcall(mv643xx_eth_add_pds);

#endif /* defined(CONFIG_MV643XX_ETH) || defined(CONFIG_MV643XX_ETH_MODULE) */
