#ifndef __MACH_IO_H
#define __MACH_IO_H

#include <plat/io.h>

/*
 * RK3188 IO memory map:
 *
 * Virt         Phys            Size    What
 * ---------------------------------------------------------------------------
 * FEA00000     10000000        1M
 * FEB00000     10100000        1M
 * FEC00000     10200000        176K
 *              10300000        1M      Peri AXI BUS
 * FEC80000     10500000        16K     NANDC
 * FECE0000     1FFE0000        128K    CPU Debug
 * FED00000     20000000        640K
 * FEF00000     10080000/0      32K     SRAM
 */

#define RK30_IO_TO_VIRT0(pa)    IOMEM(pa + (0xFEA00000 - 0x10000000))
#define RK30_IO_TO_VIRT1(pa)    IOMEM(pa + (0xFED00000 - 0x20000000))

#define RK30_IMEM_PHYS          0x10080000
#define RK30_IMEM_BASE          IOMEM(0xFEF00000)
#define RK30_IMEM_NONCACHED     RK30_IO_TO_VIRT0(RK30_IMEM_PHYS)
#define RK3188_IMEM_SIZE        SZ_32K
#define RK30_GPU_PHYS           0x10090000
#define RK30_GPU_SIZE           SZ_64K

#define RK3188_ROM_PHYS         0x10120000
#define RK30_ROM_BASE           IOMEM(0xFEB00000)
#define RK30_ROM_SIZE           SZ_16K

#define RK30_VCODEC_PHYS        0x10104000
#define RK30_VCODEC_SIZE        SZ_16K
#define RK30_CIF0_PHYS          0x10108000
#define RK30_CIF0_SIZE          SZ_8K

#define RK30_LCDC0_PHYS         0x1010c000
#define RK30_LCDC0_SIZE         SZ_8K
#define RK30_LCDC1_PHYS         0x1010e000
#define RK30_LCDC1_SIZE         SZ_8K
#define RK30_IPP_PHYS           0x10110000
#define RK30_IPP_SIZE           SZ_16K
#define RK30_RGA_PHYS           0x10114000
#define RK30_RGA_SIZE           SZ_8K

#define RK30_I2S1_2CH_PHYS      0x1011a000
#define RK30_I2S1_2CH_SIZE      SZ_8K
#define RK30_SPDIF_PHYS         0x1011e000
#define RK30_SPDIF_SIZE         SZ_8K

#define RK30_UART0_PHYS         0x10124000
#define RK30_UART0_BASE         RK30_IO_TO_VIRT0(RK30_UART0_PHYS)
#define RK30_UART0_SIZE         SZ_8K
#define RK30_UART1_PHYS         0x10126000
#define RK30_UART1_BASE         RK30_IO_TO_VIRT0(RK30_UART1_PHYS)
#define RK30_UART1_SIZE         SZ_8K
#define RK30_CPU_AXI_BUS_PHYS   0x10128000
#define RK30_CPU_AXI_BUS_BASE   RK30_IO_TO_VIRT0(RK30_CPU_AXI_BUS_PHYS)
#define RK30_CPU_AXI_BUS_SIZE   SZ_32K

#define RK30_L2C_PHYS           0x10138000
#define RK30_L2C_BASE           RK30_IO_TO_VIRT0(RK30_L2C_PHYS)
#define RK30_L2C_SIZE           SZ_16K
#define RK30_SCU_PHYS           0x1013c000
#define RK30_SCU_BASE           RK30_IO_TO_VIRT0(RK30_SCU_PHYS)
#define RK30_SCU_SIZE           SZ_256
#define RK30_GICC_PHYS          0x1013c100
#define RK30_GICC_BASE          RK30_IO_TO_VIRT0(RK30_GICC_PHYS)
#define RK30_GICC_SIZE          SZ_256
#define RK30_GTIMER_PHYS        0x1013c200
#define RK30_GTIMER_BASE        RK30_IO_TO_VIRT0(RK30_GTIMER_PHYS)
#define RK30_GTIMER_SIZE        SZ_1K
#define RK30_PTIMER_PHYS        0x1013c600
#define RK30_PTIMER_BASE        RK30_IO_TO_VIRT0(RK30_PTIMER_PHYS)
#define RK30_PTIMER_SIZE        (SZ_2K + SZ_512)
#define RK30_GICD_PHYS          0x1013d000
#define RK30_GICD_BASE          RK30_IO_TO_VIRT0(RK30_GICD_PHYS)
#define RK30_GICD_SIZE          SZ_2K

#define RK30_CORE_PHYS        RK30_L2C_PHYS
#define RK30_CORE_BASE          RK30_IO_TO_VIRT0(RK30_CORE_PHYS)
#define RK30_CORE_SIZE          (RK30_L2C_SIZE + SZ_8K)

#define RK30_USBHOST11_PHYS     0x10140000
#define RK30_USBHOST11_SIZE     SZ_256K
#define RK30_USBOTG20_PHYS      0x10180000
#define RK30_USBOTG20_SIZE      SZ_256K
#define RK30_USBHOST20_PHYS     0x101c0000
#define RK30_USBHOST20_SIZE     SZ_256K

#define RK30_MAC_PHYS           0x10204000
#define RK30_MAC_SIZE           SZ_16K

#define RK30_HSADC_PHYS         0x10210000
#define RK30_HSADC_SIZE         SZ_16K
#define RK30_SDMMC0_PHYS        0x10214000
#define RK30_SDMMC0_SIZE        SZ_16K
#define RK30_SDIO_PHYS          0x10218000
#define RK30_SDIO_SIZE          SZ_16K
#define RK30_EMMC_PHYS          0x1021c000
#define RK30_EMMC_SIZE          SZ_16K
#define RK30_PIDF_PHYS          0x10220000
#define RK30_PIDF_SIZE          SZ_16K

#define RK30_HSIC_PHYS          0x10240000
#define RK30_HSIC_SIZE          SZ_256K

#define RK30_PERI_AXI_BUS_PHYS  0x10300000
#define RK30_PERI_AXI_BUS_SIZE  SZ_1M

#define RK30_GPS_PHYS           0x10400000
#define RK30_GPS_SIZE           SZ_1M
#define RK30_NANDC_PHYS         0x10500000
#define RK30_NANDC_SIZE         SZ_16K

#define RK30_SMC_BANK0_PHYS     0x11000000
#define RK30_SMC_BANK0_SIZE     SZ_16M
#define RK30_SMC_BANK1_PHYS     0x12000000
#define RK30_SMC_BANK1_SIZE     SZ_16M

#define RK30_CPU_DEBUG_PHYS     0x1FFE0000
#define RK30_CPU_DEBUG_SIZE     SZ_128K
#define RK30_CRU_PHYS           0x20000000
#define RK30_CRU_BASE           RK30_IO_TO_VIRT1(RK30_CRU_PHYS)
#define RK30_CRU_SIZE           SZ_16K
#define RK30_PMU_PHYS           0x20004000
#define RK30_PMU_BASE           RK30_IO_TO_VIRT1(RK30_PMU_PHYS)
#define RK30_PMU_SIZE           SZ_16K
#define RK30_GRF_PHYS           0x20008000
#define RK30_GRF_BASE           RK30_IO_TO_VIRT1(RK30_GRF_PHYS)
#define RK30_GRF_SIZE           SZ_8K
#define RK30_GPIO0_PHYS         0x2000a000
#define RK30_GPIO0_BASE         RK30_IO_TO_VIRT1(RK30_GPIO0_PHYS)
#define RK30_GPIO0_SIZE         SZ_8K

#define RK3188_TIMER3_PHYS      0x2000e000
#define RK3188_TIMER3_BASE      RK30_IO_TO_VIRT1(RK3188_TIMER3_PHYS)
#define RK3188_TIMER3_SIZE      SZ_8K
#define RK30_EFUSE_PHYS         0x20010000
#define RK30_EFUSE_SIZE         SZ_16K
#define RK30_TZPC_PHYS          0x20014000
#define RK30_TZPC_SIZE          SZ_16K
#define RK30_DMACS1_PHYS        0x20018000
#define RK30_DMACS1_SIZE        SZ_16K
#define RK30_DMAC1_PHYS         0x2001c000
#define RK30_DMAC1_SIZE         SZ_16K
#define RK30_DDR_PCTL_PHYS      0x20020000
#define RK30_DDR_PCTL_BASE      RK30_IO_TO_VIRT1(RK30_DDR_PCTL_PHYS)
#define RK30_DDR_PCTL_SIZE      SZ_16K

#define RK30_I2C0_PHYS          0x2002c000
#define RK30_I2C0_SIZE          SZ_8K
#define RK30_I2C1_PHYS          0x2002e000
#define RK30_I2C1_BASE         RK30_IO_TO_VIRT1(RK30_I2C1_PHYS)
#define RK30_I2C1_SIZE          SZ_8K
#define RK30_PWM01_PHYS         0x20030000
#define RK30_PWM01_BASE         RK30_IO_TO_VIRT1(RK30_PWM01_PHYS)
#define RK30_PWM01_SIZE         SZ_16K

#define RK30_TIMER0_PHYS        0x20038000
#define RK30_TIMER0_BASE        RK30_IO_TO_VIRT1(RK30_TIMER0_PHYS)
#define RK30_TIMER0_SIZE        SZ_8K

#define RK30_GPIO1_PHYS         0x2003c000
#define RK30_GPIO1_BASE         RK30_IO_TO_VIRT1(RK30_GPIO1_PHYS)
#define RK30_GPIO1_SIZE         SZ_8K
#define RK30_GPIO2_PHYS         0x2003e000
#define RK30_GPIO2_BASE         RK30_IO_TO_VIRT1(RK30_GPIO2_PHYS)
#define RK30_GPIO2_SIZE         SZ_8K
#define RK30_DDR_PUBL_PHYS      0x20040000
#define RK30_DDR_PUBL_BASE      RK30_IO_TO_VIRT1(RK30_DDR_PUBL_PHYS)
#define RK30_DDR_PUBL_SIZE      SZ_16K

#define RK30_WDT_PHYS           0x2004c000
#define RK30_WDT_SIZE           SZ_16K
#define RK30_PWM23_PHYS         0x20050000
#define RK30_PWM23_BASE         RK30_IO_TO_VIRT1(RK30_PWM23_PHYS)
#define RK30_PWM23_SIZE         SZ_16K
#define RK30_I2C2_PHYS          0x20054000
#define RK30_I2C2_SIZE          SZ_16K
#define RK30_I2C3_PHYS          0x20058000
#define RK30_I2C3_SIZE          SZ_16K
#define RK30_I2C4_PHYS          0x2005c000
#define RK30_I2C4_SIZE          SZ_16K
#define RK30_TSADC_PHYS         0x20060000
#define RK30_TSADC_SIZE         SZ_16K
#define RK30_UART2_PHYS         0x20064000
#define RK30_UART2_BASE         RK30_IO_TO_VIRT1(RK30_UART2_PHYS)
#define RK30_UART2_SIZE         SZ_16K
#define RK30_UART3_PHYS         0x20068000
#define RK30_UART3_BASE         RK30_IO_TO_VIRT1(RK30_UART3_PHYS)
#define RK30_UART3_SIZE         SZ_16K
#define RK30_SARADC_PHYS        0x2006c000
#define RK30_SARADC_SIZE        SZ_16K
#define RK30_SPI0_PHYS          0x20070000
#define RK30_SPI0_SIZE          SZ_16K
#define RK30_SPI1_PHYS          0x20074000
#define RK30_SPI1_SIZE          SZ_16K
#define RK30_DMAC2_PHYS         0x20078000
#define RK30_DMAC2_SIZE         SZ_16K
#define RK30_SMC_PHYS           0x2007c000
#define RK30_SMC_SIZE           SZ_16K
#define RK30_GPIO3_PHYS         0x20080000
#define RK30_GPIO3_BASE         RK30_IO_TO_VIRT1(RK30_GPIO3_PHYS)
#define RK30_GPIO3_SIZE         SZ_16K

#define GIC_DIST_BASE           RK30_GICD_BASE
#define GIC_CPU_BASE            RK30_GICC_BASE

#endif
