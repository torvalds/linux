
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>

#include <mach/rk29-dma-pl330.h>
#include <mach/rk29_iomap.h>
#include <mach/irqs.h>

static struct resource rk29_iis_2ch_resource[] = {
        [0] = {
                .start = RK29_I2S_2CH_PHYS,
                .end   = RK29_I2S_2CH_PHYS + RK29_I2S_2CH_SIZE,
                .flags = IORESOURCE_MEM,
        },
        [1] = {
                .start = DMACH_I2S_2CH_TX,
                .end   = DMACH_I2S_2CH_TX,
                .flags = IORESOURCE_DMA,
        },
        [2] = {
                .start = DMACH_I2S_2CH_RX,
                .end   = DMACH_I2S_2CH_RX,
                .flags = IORESOURCE_DMA,
        },
        [3] = {
                .start = IRQ_I2S_2CH,
                .end   = IRQ_I2S_2CH,
                .flags = IORESOURCE_IRQ,        
        },
};

struct platform_device rk29_device_iis_2ch = {
        .name           = "rk29-i2s",
        .id             = 0,
        .num_resources  = ARRAY_SIZE(rk29_iis_2ch_resource),
        .resource       = rk29_iis_2ch_resource,
};

static struct resource rk29_iis_8ch_resource[] = {
        [0] = {
                .start = RK29_I2S_8CH_PHYS,
                .end   = RK29_I2S_8CH_PHYS + RK29_I2S_8CH_SIZE,
                .flags = IORESOURCE_MEM,
        },
        [1] = {
                .start = DMACH_I2S_8CH_TX,
                .end   = DMACH_I2S_8CH_TX,
                .flags = IORESOURCE_DMA,
        },
        [2] = {
                .start = DMACH_I2S_8CH_RX,
                .end   = DMACH_I2S_8CH_RX,
                .flags = IORESOURCE_DMA,
        },
        [3] = {
                .start = IRQ_I2S_8CH,
                .end   = IRQ_I2S_8CH,
                .flags = IORESOURCE_IRQ,        
        },
};

struct platform_device rk29_device_iis_8ch = {
        .name           = "rk29-i2s",
        .id             = 1,
        .num_resources  = ARRAY_SIZE(rk29_iis_8ch_resource),
        .resource       = rk29_iis_8ch_resource,
};
