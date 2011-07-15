#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <mach/rk29_iomap.h>
#include <mach/irqs.h>

#include <mach/rk29-dma-pl330.h>

static u64 dma_dmamask = DMA_BIT_MASK(32);

static struct resource rk29_dmac0_resource[] = {
	[0] = {
		.start  = RK29_SDMAC0_PHYS,//RK29_DMAC0_PHYS,
		.end    = RK29_SDMAC0_PHYS + RK29_SDMAC0_SIZE -1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_DMAC0_0,
		.end	= IRQ_DMAC0_0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct rk29_pl330_platdata rk29_dmac0_pdata = {
	.peri = {
		[0] = DMACH_UART0_TX,
		[1] = DMACH_UART0_RX,
		[2] = DMACH_I2S_8CH_TX,
		[3] = DMACH_I2S_8CH_RX,
		[4] = DMACH_I2S_2CH_TX,
		[5] = DMACH_I2S_2CH_RX,
		[6] = DMACH_SPDIF,
                [7] = DMACH_MAX,
		[8] = DMACH_MAX,
		[9] = DMACH_MAX,
		[10] = DMACH_MAX,
		[11] = DMACH_MAX,
		[12] = DMACH_MAX,
		[13] = DMACH_MAX,
		[14] = DMACH_MAX,
		[15] = DMACH_MAX,
                [16] = DMACH_MAX,
		[17] = DMACH_MAX,
		[18] = DMACH_MAX,
		[19] = DMACH_MAX,
		[20] = DMACH_MAX,
		[21] = DMACH_MAX,
		[22] = DMACH_MAX,
		[23] = DMACH_MAX,
		[24] = DMACH_MAX,
		[25] = DMACH_MAX,
		[26] = DMACH_MAX,
		[27] = DMACH_MAX,
		[28] = DMACH_MAX,
		[29] = DMACH_MAX,
		[30] = DMACH_MAX,
		[31] = DMACH_MAX,	
	},
};

static struct platform_device rk29_device_dmac0 = {
	.name		= "rk29-pl330",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(rk29_dmac0_resource),
	.resource	= rk29_dmac0_resource,
	.dev		= {
		.dma_mask = &dma_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &rk29_dmac0_pdata,
	},
};

static struct resource rk29_dmac1_resource[] = {
	[0] = {
		.start  = RK29_DMAC1_PHYS,
		.end    = RK29_DMAC1_PHYS + RK29_DMAC1_SIZE - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_DMAC1_0,
		.end	= IRQ_DMAC1_0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct rk29_pl330_platdata rk29_dmac1_pdata = {
	.peri = {
		[0] = DMACH_HSADC,
		[1] = DMACH_SDMMC,
		[2] = DMACH_SDIO,
		[3] = DMACH_EMMC,
		[4] = DMACH_UART1_TX,
		[5] = DMACH_UART1_RX,
		[6] = DMACH_UART2_TX,
		[7] = DMACH_UART2_RX,
		[8] = DMACH_UART3_TX,
		[9] = DMACH_UART3_RX,
		[10] = DMACH_SPI0_TX,
		[11] = DMACH_SPI0_RX,
		[12] = DMACH_SPI1_TX,
		[13] = DMACH_SPI1_RX,
		[14] = DMACH_PID_FILTER, 
                [15] = DMACH_DMAC0_MEMTOMEM,
		[16] = DMACH_MAX,
		[17] = DMACH_MAX,
		[18] = DMACH_MAX,
		[19] = DMACH_MAX,
		[20] = DMACH_MAX,
		[21] = DMACH_MAX,
		[22] = DMACH_MAX,
		[23] = DMACH_MAX,
		[24] = DMACH_MAX,
		[25] = DMACH_MAX,
		[26] = DMACH_MAX,
		[27] = DMACH_MAX,
		[28] = DMACH_MAX,
		[29] = DMACH_MAX,
		[30] = DMACH_MAX,
		[31] = DMACH_MAX,	
	},
};

static struct platform_device rk29_device_dmac1 = {
	.name		= "rk29-pl330",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(rk29_dmac1_resource),
	.resource	= rk29_dmac1_resource,
	.dev		= {
		.dma_mask = &dma_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &rk29_dmac1_pdata,
	},
};

static struct platform_device *rk29_dmacs[] __initdata = {
	&rk29_device_dmac0,
	&rk29_device_dmac1,
};

static int __init rk29_dma_init(void)
{
	platform_add_devices(rk29_dmacs, ARRAY_SIZE(rk29_dmacs));

	return 0;
}
arch_initcall(rk29_dma_init);
