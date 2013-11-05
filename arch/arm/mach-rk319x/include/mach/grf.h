#ifndef __MACH_GRF_H
#define __MACH_GRF_H

#include <asm/io.h>

#define GRF_GPIO1A_IOMUX        0x0010
#define GRF_GPIO1B_IOMUX        0x0014
#define GRF_GPIO1C_IOMUX        0x0018
#define GRF_GPIO1D_IOMUX        0x001c
#define GRF_GPIO2A_IOMUX        0x0020
#define GRF_GPIO2B_IOMUX        0x0024
#define GRF_GPIO2C_IOMUX        0x0028
#define GRF_GPIO2D_IOMUX        0x002c
#define GRF_GPIO3A_IOMUX        0x0030
#define GRF_GPIO3B_IOMUX        0x0034
#define GRF_GPIO3C_IOMUX        0x0038
#define GRF_GPIO3D_IOMUX        0x003c
#define GRF_GPIO4A_IOMUX        0x0040
#define GRF_GPIO4B_IOMUX        0x0044
#define GRF_GPIO4C_IOMUX        0x0048
#define GRF_GPIO4D_IOMUX        0x004c

#define GRF_SOC_CON0            0x0060
#define GRF_SOC_CON1            0x0064
#define GRF_SOC_CON2            0x0068
#define GRF_SOC_CON3            0x006c
#define GRF_SOC_CON4            0x0070
#define GRF_SOC_STATUS0         0x0074
#define GRF_SOC_STATUS1         0x0078
#define GRF_SOC_STATUS2         0x007c
#define GRF_DMAC1_CON0          0x0080
#define GRF_DMAC1_CON1          0x0084
#define GRF_DMAC1_CON2          0x0088
#define GRF_DMAC2_CON0          0x008c
#define GRF_DMAC2_CON1          0x0090
#define GRF_DMAC2_CON2          0x0094
#define GRF_DMAC2_CON3          0x0098
#define GRF_CPU_CON0            0x009c
#define GRF_CPU_CON1            0x00a0
#define GRF_CPU_CON2            0x00a4
#define GRF_CPU_CON3            0x00a8
#define GRF_CPU_CON4            0x00ac
#define GRF_CPU_CON5            0x00b0
#define GRF_CPU_STATUS0         0x00b4
#define GRF_CPU_STATUS1         0x00b8
#define GRF_DDRC_CON0           0x00bc
#define GRF_DDRC_STAT           0x00c0
#define GRF_UOC0_CON0           0x00c4
#define GRF_UOC0_CON1           0x00c8
#define GRF_UOC0_CON2           0x00cc
#define GRF_UOC0_CON3           0x00d0
#define GRF_UOC1_CON0           0x00d4
#define GRF_UOC1_CON1           0x00d8
#define GRF_UOC2_CON0           0x00e4
#define GRF_UOC2_CON1           0x00e8
#define GRF_UOC3_CON0           0x00ec
#define GRF_UOC3_CON1           0x00f0
#define GRF_PVTM_CON0           0x00f4
#define GRF_PVTM_CON1           0x00f8
#define GRF_PVTM_CON2           0x00fc
#define GRF_PVTM_STATUS0        0x0100
#define GRF_PVTM_STATUS1        0x0104
#define GRF_PVTM_STATUS2        0x0108

#define GRF_NIF_FIFO0           0x0110
#define GRF_NIF_FIFO1           0x0114
#define GRF_NIF_FIFO2           0x0118
#define GRF_NIF_FIFO3           0x011c
#define GRF_OS_REG0             0x0120
#define GRF_OS_REG1             0x0124
#define GRF_OS_REG2             0x0128
#define GRF_OS_REG3             0x012c
#define GRF_SOC_CON5            0x0130
#define GRF_SOC_CON6            0x0134
#define GRF_SOC_CON7            0x0138
#define GRF_SOC_CON8            0x013c

#define GRF_GPIO1A_PULL         0x0144
#define GRF_GPIO1B_PULL         0x0148
#define GRF_GPIO1C_PULL         0x014c
#define GRF_GPIO1D_PULL         0x0150
#define GRF_GPIO2A_PULL         0x0154
#define GRF_GPIO2B_PULL         0x0158
#define GRF_GPIO2C_PULL         0x015c
#define GRF_GPIO2D_PULL         0x0160
#define GRF_GPIO3A_PULL         0x0164
#define GRF_GPIO3B_PULL         0x0168
#define GRF_GPIO3C_PULL         0x016c
#define GRF_GPIO3D_PULL         0x0170
#define GRF_GPIO4A_PULL         0x0174
#define GRF_GPIO4B_PULL         0x0178
#define GRF_GPIO4C_PULL         0x017c
#define GRF_GPIO4D_PULL         0x0180

#define GRF_IO_VSEL             0x018c

#define GRF_GPIO1L_SR           0x0198
#define GRF_GPIO1H_SR           0x019c
#define GRF_GPIO2L_SR           0x01a0
#define GRF_GPIO2H_SR           0x01a4
#define GRF_GPIO3L_SR           0x01a8
#define GRF_GPIO3H_SR           0x01ac
#define GRF_GPIO4L_SR           0x01b0
#define GRF_GPIO4H_SR           0x01b4

#define GRF_GPIO1A_E            0x01c8
#define GRF_GPIO1B_E            0x01cc
#define GRF_GPIO1C_E            0x01d0
#define GRF_GPIO1D_E            0x01d4
#define GRF_GPIO2A_E            0x01d8
#define GRF_GPIO2B_E            0x01dc
#define GRF_GPIO2C_E            0x01e0
#define GRF_GPIO2D_E            0x01e4
#define GRF_GPIO3A_E            0x01e8
#define GRF_GPIO3B_E            0x01ec
#define GRF_GPIO3C_E            0x01f0
#define GRF_GPIO3D_E            0x01f4
#define GRF_GPIO4A_E            0x01f8
#define GRF_GPIO4B_E            0x01fc
#define GRF_GPIO4C_E            0x0200
#define GRF_GPIO4D_E            0x0204

#define GRF_FLASH_DATA_PULL     0x0210
#define GRF_FLASH_DATA_E        0x0214
#define GRF_FLASH_DATA_SR       0x0218

#define GRF_USBPHY_CON0         0x0220
#define GRF_USBPHY_CON1         0x0224
#define GRF_USBPHY_CON2         0x0228
#define GRF_USBPHY_CON3         0x022c
#define GRF_USBPHY_CON4         0x0230
#define GRF_USBPHY_CON5         0x0234
#define GRF_USBPHY_CON6         0x0238
#define GRF_USBPHY_CON7         0x023c
#define GRF_USBPHY_CON8         0x0240
#define GRF_USBPHY_CON9         0x0244
#define GRF_USBPHY_CON10        0x0248
#define GRF_USBPHY_CON11        0x024c
#define GRF_DFI_STAT0           0x0250
#define GRF_DFI_STAT1           0x0254
#define GRF_DFI_STAT2           0x0258
#define GRF_DFI_STAT3           0x025c

#define GRF_SECURE_BOOT_STATUS  0x0300

#define BB_GRF_GPIO0A_IOMUX     0x0000
#define BB_GRF_GPIO0B_IOMUX     0x0004
#define BB_GRF_GPIO0C_IOMUX     0x0008
#define BB_GRF_GPIO0D_IOMUX     0x000c
#define BB_GRF_GPIO0A_DRV       0x0010
#define BB_GRF_GPIO0B_DRV       0x0014
#define BB_GRF_GPIO0C_DRV       0x0018
#define BB_GRF_GPIO0D_DRV       0x001c
#define BB_GRF_GPIO0A_PULL      0x0020
#define BB_GRF_GPIO0B_PULL      0x0024
#define BB_GRF_GPIO0C_PULL      0x0028
#define BB_GRF_GPIO0D_PULL      0x002c
#define BB_GRF_GPIO0L_SR        0x0030
#define BB_GRF_GPIO0H_SR        0x0034

#define BB_GRF_GLB_CON0         0x0040
#define BB_GRF_GLB_CON1         0x0044
#define BB_GRF_GLB_CON2         0x0048
#define BB_GRF_GLB_CON3         0x004c
#define BB_GRF_GLB_CON4         0x0050

#define BB_GRF_GLB_STS0         0x0060

enum grf_io_power_domain_voltage {
	IO_PD_VOLTAGE_3_3V = 0,
	IO_PD_VOLTAGE_2_5V = IO_PD_VOLTAGE_3_3V,
	IO_PD_VOLTAGE_1_8V = 1,
};

enum grf_io_power_domain {
	IO_PD_LCDC = 0,
	IO_PD_CIF,
	IO_PD_FLASH,
	IO_PD_WIFI_BT,
	IO_PD_AUDIO,
	IO_PD_GPIO0,
	IO_PD_GPIO1,
};

static inline void grf_set_io_power_domain_voltage(enum grf_io_power_domain pd, enum grf_io_power_domain_voltage volt)
{
	writel_relaxed((0x10000 + volt) << pd, RK30_GRF_BASE + GRF_IO_VSEL);
	dsb();
}

static inline enum grf_io_power_domain_voltage grf_get_io_power_domain_voltage(enum grf_io_power_domain pd)
{
	return (readl_relaxed(RK30_GRF_BASE + GRF_IO_VSEL) >> pd) & 1;
}

#endif
