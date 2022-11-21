#
# Copyright (C) 2012-2015 Broadcom Corporation
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation version 2.
#
# This program is distributed "as is" WITHOUT ANY WARRANTY of any
# kind, whether express or implied; without even the implied warranty
# of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# Cygnus
obj-$(CONFIG_ARCH_BCM_CYGNUS) +=  bcm_cygnus.o

# Hurricane 2
obj-$(CONFIG_ARCH_BCM_HR2)	+= bcm_hr2.o

# Northstar Plus
obj-$(CONFIG_ARCH_BCM_NSP)	+= bcm_nsp.o

ifeq ($(CONFIG_ARCH_BCM_NSP),y)
obj-$(CONFIG_SMP)		+= platsmp.o
endif

# BCM281XX
obj-$(CONFIG_ARCH_BCM_281XX)	+= board_bcm281xx.o

# BCM21664
obj-$(CONFIG_ARCH_BCM_21664)	+= board_bcm21664.o

# BCM23550
obj-$(CONFIG_ARCH_BCM_23550)	+= board_bcm23550.o

# BCM281XX, BCM21664 and BCM23550 SMP support
obj-$(CONFIG_ARCH_BCM_MOBILE_SMP) += platsmp.o

# BCM281XX and BCM21664 L2 cache control
obj-$(CONFIG_ARCH_BCM_MOBILE_L2_CACHE) += kona_l2_cache.o

# Support for secure monitor traps
obj-$(CONFIG_ARCH_BCM_MOBILE_SMC) += bcm_kona_smc.o
CFLAGS_REMOVE_bcm_kona_smc.o += $(CC_FLAGS_FTRACE)

# BCM2835
ifeq ($(CONFIG_ARCH_BCM2835),y)
obj-y				+= board_bcm2835.o
obj-y				+= bcm2711.o
ifeq ($(CONFIG_ARM),y)
obj-$(CONFIG_SMP)		+= platsmp.o
endif
endif

# BCM5301X
obj-$(CONFIG_ARCH_BCM_5301X)	+= bcm_5301x.o
ifeq ($(CONFIG_ARCH_BCM_5301X),y)
obj-$(CONFIG_SMP)		+= platsmp.o
endif

# BCM63XXx
ifeq ($(CONFIG_ARCH_BCM_63XX),y)
obj-y				+= bcm63xx.o
obj-$(CONFIG_SMP)		+= bcm63xx_smp.o bcm63xx_pmb.o
endif

ifeq ($(CONFIG_ARCH_BRCMSTB),y)
CFLAGS_platsmp-brcmstb.o	+= -march=armv7-a
obj-y				+= brcmstb.o
obj-$(CONFIG_SMP)		+= platsmp-brcmstb.o
endif
