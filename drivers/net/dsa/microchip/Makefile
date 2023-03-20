# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_NET_DSA_MICROCHIP_KSZ_COMMON)	+= ksz_switch.o
ksz_switch-objs := ksz_common.o
ksz_switch-objs += ksz9477.o
ksz_switch-objs += ksz8795.o
ksz_switch-objs += lan937x_main.o

ifdef CONFIG_NET_DSA_MICROCHIP_KSZ_PTP
ksz_switch-objs += ksz_ptp.o
endif

obj-$(CONFIG_NET_DSA_MICROCHIP_KSZ9477_I2C)	+= ksz9477_i2c.o
obj-$(CONFIG_NET_DSA_MICROCHIP_KSZ_SPI)		+= ksz_spi.o
obj-$(CONFIG_NET_DSA_MICROCHIP_KSZ8863_SMI)	+= ksz8863_smi.o
