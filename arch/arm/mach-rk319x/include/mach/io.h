#ifndef __MACH_IO_H
#define __MACH_IO_H

#include <plat/io.h>

/*
 * RK319X IO memory map:
 *
 * Virt         Phys            Size    What
 * ---------------------------------------------------------------------------
 *              FDFF0000
 * FE900000     FE600000        1M      BB
 * FEA00000     FFA00000        1M
 *              FFB00000        1M      Peri AXI BUS
 * FEB00000     FFC00000        4M
 * FEC00000     FFD00000
 * FED00000     FFE00000
 * FEE00000     FFF00000
 * FEF00000     FFCD0000        32K     SRAM
 */

#define RK319X_IO_TO_VIRT0(pa)  IOMEM((pa) - 0xFE600000 + 0xFE900000)
#define RK319X_IO_TO_VIRT1(pa)  IOMEM((pa) - 0xFFA00000 + 0xFEA00000)
#define RK319X_IO_TO_VIRT2(pa)  IOMEM((pa) - 0xFFC00000 + 0xFEB00000)

#define IO_ADDRESS(pa)          IOMEM( \
	((pa) >= 0xFE600000 && (pa) <  0xFE700000) ? RK319X_IO_TO_VIRT0(pa) : \
	((pa) >= 0xFFA00000 && (pa) <  0xFFB00000) ? RK319X_IO_TO_VIRT1(pa) : \
	((pa) >= 0xFFC00000 && (pa) <= 0xFFFFFFFF) ? RK319X_IO_TO_VIRT2(pa) : \
	0)

#define RK319X_BOOT_RAM_PHYS            0xFDFF0000
#define RK319X_BOOT_RAM_SIZE            SZ_4K

#define RK319X_PMU_PHYS                 0xFE600000
#define RK319X_PMU_BASE                 IO_ADDRESS(RK319X_PMU_PHYS)
#define RK319X_PMU_SIZE                 SZ_64K

#define RK319X_BB_GRF_PHYS              0xFE620000
#define RK319X_BB_GRF_BASE              IO_ADDRESS(RK319X_BB_GRF_PHYS)
#define RK319X_BB_GRF_SIZE              SZ_64K

#define RK319X_MAILBOX_PHYS             0xFE650000
#define RK319X_MAILBOX_SIZE             SZ_64K

#define RK319X_GPIO0_PHYS               0xFE680000
#define RK319X_GPIO0_BASE               IO_ADDRESS(RK319X_GPIO0_PHYS)
#define RK319X_GPIO0_SIZE               SZ_8K

#define RK319X_SMC_BANK0_PHYS           0xFE800000
#define RK319X_SMC_BANK0_SIZE           SZ_8M
#define RK319X_SMC_BANK1_PHYS           0xFF000000
#define RK319X_SMC_BANK1_SIZE           SZ_8M
#define RK319X_USBOTG20_PHYS            0xFF800000
#define RK319X_USBOTG20_SIZE            SZ_256K
#define RK319X_USBHOST20_PHYS           0xFF840000
#define RK319X_USBHOST20_SIZE           SZ_256K
#define RK319X_HSIC_PHYS                0xFF880000
#define RK319X_HSIC_SIZE                SZ_256K
#define RK319X_NANDC_PHYS               0xFF8C0000
#define RK319X_NANDC_SIZE               SZ_32K
#define RK319X_CRYPTO_PHYS              0xFF8C8000
#define RK319X_CRYPTO_SIZE              SZ_32K

#define RK319X_EMMC_PHYS                0xFF900000
#define RK319X_EMMC_SIZE                SZ_64K
#define RK319X_SDIO_PHYS                0xFF910000
#define RK319X_SDIO_SIZE                SZ_64K
#define RK319X_SDMMC0_PHYS              0xFF920000
#define RK319X_SDMMC0_SIZE              SZ_64K

#define RK319X_HSADC_PHYS               0xFF980000
#define RK319X_HSADC_SIZE               SZ_64K
#define RK319X_GPS_PHYS                 0xFF990000
#define RK319X_GPS_SIZE                 SZ_64K

#define RK319X_SARADC_PHYS              0xFFA00000
#define RK319X_SARADC_SIZE              SZ_64K

#define RK319X_SPI0_PHYS                0xFFA10000
#define RK319X_SPI0_SIZE                SZ_64K
#define RK319X_SPI1_PHYS                0xFFA20000
#define RK319X_SPI1_SIZE                SZ_64K
#define RK319X_I2C2_PHYS                0xFFA30000
#define RK319X_I2C2_SIZE                SZ_64K
#define RK319X_I2C3_PHYS                0xFFA40000
#define RK319X_I2C3_SIZE                SZ_64K
#define RK319X_I2C4_PHYS                0xFFA50000
#define RK319X_I2C4_SIZE                SZ_64K
#define RK319X_UART0_PHYS               0xFFA60000
#define RK319X_UART0_BASE               RK319X_IO_TO_VIRT1(RK319X_UART0_PHYS)
#define RK319X_UART0_SIZE               SZ_64K
#define RK319X_UART1_PHYS               0xFFA70000
#define RK319X_UART1_BASE               RK319X_IO_TO_VIRT1(RK319X_UART1_PHYS)
#define RK319X_UART1_SIZE               SZ_64K
#define RK319X_UART2_PHYS               0xFFA80000
#define RK319X_UART2_BASE               RK319X_IO_TO_VIRT1(RK319X_UART2_PHYS)
#define RK319X_UART2_SIZE               SZ_64K
#define RK319X_UART3_PHYS               0xFFA90000
#define RK319X_UART3_BASE               RK319X_IO_TO_VIRT1(RK319X_UART3_PHYS)
#define RK319X_UART3_SIZE               SZ_64K
#define RK319X_WDT_PHYS                 0xFFAA0000
#define RK319X_WDT_SIZE                 SZ_64K

#define RK319X_DMAC2_PHYS               0xFFAD0000
#define RK319X_DMAC2_SIZE               SZ_64K
#define RK319X_SMC_PHYS                 0xFFAE0000
#define RK319X_SMC_SIZE                 SZ_64K
#define RK319X_TSADC_PHYS               0xFFAF0000
#define RK319X_TSADC_SIZE               SZ_64K
#define RK319X_PERI_AXI_BUS_PHYS        0xFFB00000
#define RK319X_PERI_AXI_BUS_SIZE        SZ_1M
#define RK319X_ROM_PHYS                 0xFFC00000
#define RK319X_ROM_BASE                 IO_ADDRESS(RK319X_ROM_PHYS)
#define RK319X_ROM_SIZE                 SZ_64K
#define RK319X_I2S1_2CH_PHYS            0xFFC10000
#define RK319X_I2S1_2CH_SIZE            SZ_64K
#define RK319X_SPDIF_PHYS               0xFFC20000
#define RK319X_SPDIF_SIZE               SZ_64K

#define RK319X_LCDC0_PHYS               0xFFC40000
#define RK319X_LCDC0_SIZE               SZ_64K
#define RK319X_LCDC1_PHYS               0xFFC50000
#define RK319X_LCDC1_SIZE               SZ_64K
#define RK319X_CIF0_PHYS                0xFFC60000
#define RK319X_CIF0_SIZE                SZ_64K
#define RK319X_RGA_PHYS                 0xFFC70000
#define RK319X_RGA_SIZE                 SZ_64K
#define RK319X_IEP_PHYS                 0xFFC80000
#define RK319X_IEP_SIZE                 SZ_64K
#define RK319X_MIPI_DSI_HOST_PHYS       0xFFC90000
#define RK319X_MIPI_DSI_HOST_SIZE       SZ_64K
#define RK319X_ISP_PHYS                 0xFFCA0000
#define RK319X_ISP_SIZE                 SZ_64K
#define RK319X_VCODEC_PHYS              0xFFCB0000
#define RK319X_VCODEC_SIZE              SZ_64K
#define RK319X_CPU_AXI_BUS_PHYS         0xFFCC0000
#define RK319X_CPU_AXI_BUS_BASE         IO_ADDRESS(RK319X_CPU_AXI_BUS_PHYS)
#define RK319X_CPU_AXI_BUS_SIZE         SZ_64K
#define RK319X_IMEM_PHYS                0xFFCD0000
#define RK319X_IMEM_BASE                IOMEM(0xFEF00000)
#define RK319X_IMEM_NONCACHED           IO_ADDRESS(RK319X_IMEM_PHYS)
#define RK319X_IMEM_SIZE                SZ_32K

#define RK319X_GPU_PHYS                 0xFFD00000
#define RK319X_GPU_SIZE                 SZ_64K

#define RK319X_TZPC_PHYS                0xFFD20000
#define RK319X_TZPC_SIZE                SZ_64K
#define RK319X_EFUSE_PHYS               0xFFD30000
#define RK319X_EFUSE_BASE               IO_ADDRESS(RK319X_EFUSE_PHYS)
#define RK319X_EFUSE_SIZE               SZ_64K
#define RK319X_DMACS1_PHYS              0xFFD40000
#define RK319X_DMACS1_SIZE              SZ_64K

#define RK319X_PWM_PHYS                 0xFFD60000
#define RK319X_PWM_BASE                 IO_ADDRESS(RK319X_PWM_PHYS)
#define RK319X_PWM_SIZE                 SZ_64K

#define RK319X_I2C0_PHYS                0xFFD80000
#define RK319X_I2C0_SIZE                SZ_64K
#define RK319X_I2C1_PHYS                0xFFD90000
#define RK319X_I2C1_BASE                IO_ADDRESS(RK319X_I2C1_PHYS)
#define RK319X_I2C1_SIZE                SZ_64K
#define RK319X_DMAC1_PHYS               0xFFDA0000
#define RK319X_DMAC1_SIZE               SZ_64K
#define RK319X_DDR_PCTL_PHYS            0xFFDB0000
#define RK319X_DDR_PCTL_BASE            IO_ADDRESS(RK319X_DDR_PCTL_PHYS)
#define RK319X_DDR_PCTL_SIZE            SZ_64K
#define RK319X_DDR_PUBL_PHYS            0xFFDC0000
#define RK319X_DDR_PUBL_BASE            IO_ADDRESS(RK319X_DDR_PUBL_PHYS)
#define RK319X_DDR_PUBL_SIZE            SZ_64K

#define RK319X_CPU_DEBUG_PHYS           0xFFDE0000
#define RK319X_CPU_DEBUG_SIZE           SZ_128K
#define RK319X_CRU_PHYS                 0xFFE00000
#define RK319X_CRU_BASE                 IO_ADDRESS(RK319X_CRU_PHYS)
#define RK319X_CRU_SIZE                 SZ_64K
#define RK319X_GRF_PHYS                 0xFFE10000
#define RK319X_GRF_BASE                 IO_ADDRESS(RK319X_GRF_PHYS)
#define RK319X_GRF_SIZE                 SZ_64K
#define RK319X_TIMER_PHYS               0xFFE20000
#define RK319X_TIMER_BASE               IO_ADDRESS(RK319X_TIMER_PHYS)
#define RK319X_TIMER_SIZE               SZ_64K
#define RK319X_ACODEC_PHYS              0xFFE30000
#define RK319X_ACODEC_SIZE              SZ_16K
#define RK319X_HDMI_PHYS                0xFFE34000
#define RK319X_HDMI_SIZE                SZ_16K
#define RK319X_MIPI_DSI_PHY_PHYS        0xFFE38000
#define RK319X_MIPI_DSI_PHY_SIZE        SZ_16K

#define RK319X_GPIO1_PHYS               0xFFE40000
#define RK319X_GPIO1_BASE               IO_ADDRESS(RK319X_GPIO1_PHYS)
#define RK319X_GPIO1_SIZE               SZ_64K
#define RK319X_GPIO2_PHYS               0xFFE50000
#define RK319X_GPIO2_BASE               IO_ADDRESS(RK319X_GPIO2_PHYS)
#define RK319X_GPIO2_SIZE               SZ_64K
#define RK319X_GPIO3_PHYS               0xFFE60000
#define RK319X_GPIO3_BASE               IO_ADDRESS(RK319X_GPIO3_PHYS)
#define RK319X_GPIO3_SIZE               SZ_64K
#define RK319X_GPIO4_PHYS               0xFFE70000
#define RK319X_GPIO4_BASE               IO_ADDRESS(RK319X_GPIO4_PHYS)
#define RK319X_GPIO4_SIZE               SZ_64K

#define RK319X_L2C_PHYS                 0xFFF00000
#define RK319X_L2C_BASE                 IO_ADDRESS(RK319X_L2C_PHYS)
#define RK319X_L2C_SIZE                 SZ_16K
#define RK319X_SCU_PHYS                 0xFFF04000
#define RK319X_SCU_BASE                 IO_ADDRESS(RK319X_SCU_PHYS)
#define RK319X_SCU_SIZE                 SZ_256
#define RK319X_GICC_PHYS                0xFFF04100
#define RK319X_GICC_BASE                RK319X_IO_TO_VIRT2(RK319X_GICC_PHYS)
#define RK319X_GICC_SIZE                SZ_256
#define RK319X_GTIMER_PHYS              0xFFF04200
#define RK319X_GTIMER_BASE              IO_ADDRESS(RK319X_GTIMER_PHYS)
#define RK319X_GTIMER_SIZE              SZ_1K
#define RK319X_PTIMER_PHYS              0xFFF04600
#define RK319X_PTIMER_BASE              IO_ADDRESS(RK319X_PTIMER_PHYS)
#define RK319X_PTIMER_SIZE              (SZ_2K + SZ_512)
#define RK319X_GICD_PHYS                0xFFF05000
#define RK319X_GICD_BASE                IO_ADDRESS(RK319X_GICD_PHYS)
#define RK319X_GICD_SIZE                SZ_2K

#define RK319X_BOOT_PHYS                0xFFFF0000
#define RK319X_BOOT_BASE                IO_ADDRESS(RK319X_BOOT_PHYS)

#define RK319X_CORE_PHYS                RK319X_L2C_PHYS
#define RK319X_CORE_BASE                RK319X_L2C_BASE
#define RK319X_CORE_SIZE                (RK319X_L2C_SIZE + SZ_8K)
#define GIC_DIST_BASE                   RK319X_GICD_BASE
#define GIC_CPU_BASE                    RK319X_GICC_BASE
#define SRAM_NONCACHED                  RK319X_IMEM_NONCACHED
#define SRAM_CACHED                     RK319X_IMEM_BASE
#define SRAM_PHYS                       RK319X_IMEM_PHYS
#define SRAM_SIZE                       RK319X_IMEM_SIZE

#define RK30_PMU_PHYS                   RK319X_PMU_PHYS
#define RK30_PMU_BASE                   RK319X_PMU_BASE
#define RK30_PMU_SIZE                   RK319X_PMU_SIZE
#define RK30_GPIO0_PHYS                 RK319X_GPIO0_PHYS
#define RK30_GPIO0_BASE                 RK319X_GPIO0_BASE
#define RK30_GPIO0_SIZE                 RK319X_GPIO0_SIZE
#define RK30_GPIO1_PHYS                 RK319X_GPIO1_PHYS
#define RK30_GPIO1_BASE                 RK319X_GPIO1_BASE
#define RK30_GPIO1_SIZE                 RK319X_GPIO1_SIZE
#define RK30_GPIO2_PHYS                 RK319X_GPIO2_PHYS
#define RK30_GPIO2_BASE                 RK319X_GPIO2_BASE
#define RK30_GPIO2_SIZE                 RK319X_GPIO2_SIZE
#define RK30_GPIO3_PHYS                 RK319X_GPIO3_PHYS
#define RK30_GPIO3_BASE                 RK319X_GPIO3_BASE
#define RK30_GPIO3_SIZE                 RK319X_GPIO3_SIZE
#define RK30_GPIO4_PHYS                 RK319X_GPIO4_PHYS
#define RK30_GPIO4_BASE                 RK319X_GPIO4_BASE
#define RK30_GPIO4_SIZE                 RK319X_GPIO4_SIZE
#define RK30_SMC_BANK0_PHYS             RK319X_SMC_BANK0_PHYS
#define RK30_SMC_BANK0_SIZE             RK319X_SMC_BANK0_SIZE
#define RK30_SMC_BANK1_PHYS             RK319X_SMC_BANK1_PHYS
#define RK30_SMC_BANK1_SIZE             RK319X_SMC_BANK1_SIZE
#define RK30_USBOTG20_PHYS              RK319X_USBOTG20_PHYS
#define RK30_USBOTG20_SIZE              RK319X_USBOTG20_SIZE
#define RK30_USBHOST20_PHYS             RK319X_USBHOST20_PHYS
#define RK30_USBHOST20_SIZE             RK319X_USBHOST20_SIZE
#define RK30_HSIC_PHYS                  RK319X_HSIC_PHYS
#define RK30_HSIC_SIZE                  RK319X_HSIC_SIZE
#define RK30_NANDC_PHYS                 RK319X_NANDC_PHYS
#define RK30_NANDC_SIZE                 RK319X_NANDC_SIZE
#define RK30_EMMC_PHYS                  RK319X_EMMC_PHYS
#define RK30_EMMC_SIZE                  RK319X_EMMC_SIZE
#define RK30_SDIO_PHYS                  RK319X_SDIO_PHYS
#define RK30_SDIO_SIZE                  RK319X_SDIO_SIZE
#define RK30_SDMMC0_PHYS                RK319X_SDMMC0_PHYS
#define RK30_SDMMC0_SIZE                RK319X_SDMMC0_SIZE
#define RK30_HSADC_PHYS                 RK319X_HSADC_PHYS
#define RK30_HSADC_SIZE                 RK319X_HSADC_SIZE
#define RK30_GPS_PHYS                   RK319X_GPS_PHYS
#define RK30_GPS_SIZE                   RK319X_GPS_SIZE
#define RK30_SARADC_PHYS                RK319X_SARADC_PHYS
#define RK30_SARADC_SIZE                RK319X_SARADC_SIZE
#define RK30_SPI0_PHYS                  RK319X_SPI0_PHYS
#define RK30_SPI0_SIZE                  RK319X_SPI0_SIZE
#define RK30_SPI1_PHYS                  RK319X_SPI1_PHYS
#define RK30_SPI1_SIZE                  RK319X_SPI1_SIZE
#define RK30_I2C0_PHYS                  RK319X_I2C0_PHYS
#define RK30_I2C0_SIZE                  RK319X_I2C0_SIZE
#define RK30_I2C1_PHYS                  RK319X_I2C1_PHYS
#define RK30_I2C1_BASE                  RK319X_I2C1_BASE
#define RK30_I2C1_SIZE                  RK319X_I2C1_SIZE
#define RK30_I2C2_PHYS                  RK319X_I2C2_PHYS
#define RK30_I2C2_SIZE                  RK319X_I2C2_SIZE
#define RK30_I2C3_PHYS                  RK319X_I2C3_PHYS
#define RK30_I2C3_SIZE                  RK319X_I2C3_SIZE
#define RK30_I2C4_PHYS                  RK319X_I2C4_PHYS
#define RK30_I2C4_SIZE                  RK319X_I2C4_SIZE
#define RK30_UART0_PHYS                 RK319X_UART0_PHYS
#define RK30_UART0_BASE                 RK319X_UART0_BASE
#define RK30_UART0_SIZE                 RK319X_UART0_SIZE
#define RK30_UART1_PHYS                 RK319X_UART1_PHYS
#define RK30_UART1_BASE                 RK319X_UART1_BASE
#define RK30_UART1_SIZE                 RK319X_UART1_SIZE
#define RK30_UART2_PHYS                 RK319X_UART2_PHYS
#define RK30_UART2_BASE                 RK319X_UART2_BASE
#define RK30_UART2_SIZE                 RK319X_UART2_SIZE
#define RK30_UART3_PHYS                 RK319X_UART3_PHYS
#define RK30_UART3_BASE                 RK319X_UART3_BASE
#define RK30_UART3_SIZE                 RK319X_UART3_SIZE
#define RK30_WDT_PHYS                   RK319X_WDT_PHYS
#define RK30_WDT_SIZE                   RK319X_WDT_SIZE
#define RK30_DMAC2_PHYS                 RK319X_DMAC2_PHYS
#define RK30_DMAC2_SIZE                 RK319X_DMAC2_SIZE
#define RK30_SMC_PHYS                   RK319X_SMC_PHYS
#define RK30_SMC_SIZE                   RK319X_SMC_SIZE
#define RK30_TSADC_PHYS                 RK319X_TSADC_PHYS
#define RK30_TSADC_SIZE                 RK319X_TSADC_SIZE
#define RK30_PERI_AXI_BUS_PHYS          RK319X_PERI_AXI_BUS_PHYS
#define RK30_PERI_AXI_BUS_SIZE          RK319X_PERI_AXI_BUS_SIZE
#define RK30_ROM_PHYS                   RK319X_ROM_PHYS
#define RK30_ROM_BASE                   RK319X_ROM_BASE
#define RK30_ROM_SIZE                   RK319X_ROM_SIZE
#define RK30_I2S1_2CH_PHYS              RK319X_I2S1_2CH_PHYS
#define RK30_I2S1_2CH_SIZE              RK319X_I2S1_2CH_SIZE
#define RK30_SPDIF_PHYS                 RK319X_SPDIF_PHYS
#define RK30_SPDIF_SIZE                 RK319X_SPDIF_SIZE
#define RK30_LCDC0_PHYS                 RK319X_LCDC0_PHYS
#define RK30_LCDC0_SIZE                 RK319X_LCDC0_SIZE
#define RK30_LCDC1_PHYS                 RK319X_LCDC1_PHYS
#define RK30_LCDC1_SIZE                 RK319X_LCDC1_SIZE
#define RK30_CIF0_PHYS                  RK319X_CIF0_PHYS
#define RK30_CIF0_SIZE                  RK319X_CIF0_SIZE
#define RK30_RGA_PHYS                   RK319X_RGA_PHYS
#define RK30_RGA_SIZE                   RK319X_RGA_SIZE
#define RK30_VCODEC_PHYS                RK319X_VCODEC_PHYS
#define RK30_VCODEC_SIZE                RK319X_VCODEC_SIZE
#define RK30_CPU_AXI_BUS_PHYS           RK319X_CPU_AXI_BUS_PHYS
#define RK30_CPU_AXI_BUS_BASE           RK319X_CPU_AXI_BUS_BASE
#define RK30_CPU_AXI_BUS_SIZE           RK319X_CPU_AXI_BUS_SIZE
#define RK30_IMEM_PHYS                  RK319X_IMEM_PHYS
#define RK30_IMEM_BASE                  RK319X_IMEM_BASE
#define RK30_IMEM_NONCACHED             RK319X_IMEM_NONCACHED
#define RK30_IMEM_SIZE                  RK319X_IMEM_SIZE
#define RK3188_IMEM_SIZE                RK319X_IMEM_SIZE
#define RK30_GPU_PHYS                   RK319X_GPU_PHYS
#define RK30_GPU_SIZE                   RK319X_GPU_SIZE
#define RK30_TZPC_PHYS                  RK319X_TZPC_PHYS
#define RK30_TZPC_SIZE                  RK319X_TZPC_SIZE
#define RK30_EFUSE_PHYS                 RK319X_EFUSE_PHYS
#define RK30_EFUSE_BASE                 RK319X_EFUSE_BASE
#define RK30_EFUSE_SIZE                 RK319X_EFUSE_SIZE
#define RK30_DMACS1_PHYS                RK319X_DMACS1_PHYS
#define RK30_DMACS1_SIZE                RK319X_DMACS1_SIZE
#define RK30_PWM_PHYS                   RK319X_PWM_PHYS
#define RK30_PWM_BASE                   RK319X_PWM_BASE
#define RK30_PWM_SIZE                   RK319X_PWM_SIZE
#define RK30_DMAC1_PHYS                 RK319X_DMAC1_PHYS
#define RK30_DMAC1_SIZE                 RK319X_DMAC1_SIZE
#define RK30_DDR_PCTL_PHYS              RK319X_DDR_PCTL_PHYS
#define RK30_DDR_PCTL_BASE              RK319X_DDR_PCTL_BASE
#define RK30_DDR_PCTL_SIZE              RK319X_DDR_PCTL_SIZE
#define RK30_DDR_PUBL_PHYS              RK319X_DDR_PUBL_PHYS
#define RK30_DDR_PUBL_BASE              RK319X_DDR_PUBL_BASE
#define RK30_DDR_PUBL_SIZE              RK319X_DDR_PUBL_SIZE
#define RK30_CPU_DEBUG_PHYS             RK319X_CPU_DEBUG_PHYS
#define RK30_CPU_DEBUG_SIZE             RK319X_CPU_DEBUG_SIZE
#define RK30_CRU_PHYS                   RK319X_CRU_PHYS
#define RK30_CRU_BASE                   RK319X_CRU_BASE
#define RK30_CRU_SIZE                   RK319X_CRU_SIZE
#define RK30_GRF_PHYS                   RK319X_GRF_PHYS
#define RK30_GRF_BASE                   RK319X_GRF_BASE
#define RK30_GRF_SIZE                   RK319X_GRF_SIZE
#define RK30_HDMI_PHYS                  RK319X_HDMI_PHYS
#define RK30_HDMI_SIZE                  RK319X_HDMI_SIZE
#define RK30_L2C_PHYS                   RK319X_L2C_PHYS
#define RK30_L2C_BASE                   RK319X_L2C_BASE
#define RK30_L2C_SIZE                   RK319X_L2C_SIZE
#define RK30_SCU_PHYS                   RK319X_SCU_PHYS
#define RK30_SCU_BASE                   RK319X_SCU_BASE
#define RK30_SCU_SIZE                   RK319X_SCU_SIZE
#define RK30_GICC_PHYS                  RK319X_GICC_PHYS
#define RK30_GICC_BASE                  RK319X_GICC_BASE
#define RK30_GICC_SIZE                  RK319X_GICC_SIZE
#define RK30_GTIMER_PHYS                RK319X_GTIMER_PHYS
#define RK30_GTIMER_BASE                RK319X_GTIMER_BASE
#define RK30_GTIMER_SIZE                RK319X_GTIMER_SIZE
#define RK30_PTIMER_PHYS                RK319X_PTIMER_PHYS
#define RK30_PTIMER_BASE                RK319X_PTIMER_BASE
#define RK30_PTIMER_SIZE                RK319X_PTIMER_SIZE
#define RK30_GICD_PHYS                  RK319X_GICD_PHYS
#define RK30_GICD_BASE                  RK319X_GICD_BASE
#define RK30_GICD_SIZE                  RK319X_GICD_SIZE
#define RK30_CORE_PHYS                  RK319X_CORE_PHYS
#define RK30_CORE_BASE                  RK319X_CORE_BASE
#define RK30_CORE_SIZE                  RK319X_CORE_SIZE

#endif
