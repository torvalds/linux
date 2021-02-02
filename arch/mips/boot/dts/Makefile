# SPDX-License-Identifier: GPL-2.0
subdir-$(CONFIG_BMIPS_GENERIC)		+= brcm
subdir-$(CONFIG_CAVIUM_OCTEON_SOC)	+= cavium-octeon
subdir-$(CONFIG_MACH_PISTACHIO)		+= img
subdir-$(CONFIG_FIT_IMAGE_FDT_BOSTON)	+= img
subdir-$(CONFIG_MACH_INGENIC)		+= ingenic
subdir-$(CONFIG_LANTIQ)			+= lantiq
subdir-$(CONFIG_MACH_LOONGSON64)	+= loongson
subdir-$(CONFIG_SOC_VCOREIII)		+= mscc
subdir-$(CONFIG_MIPS_MALTA)		+= mti
subdir-$(CONFIG_LEGACY_BOARD_SEAD3)	+= mti
subdir-$(CONFIG_NLM_XLP_BOARD)		+= netlogic
subdir-$(CONFIG_FIT_IMAGE_FDT_NI169445)	+= ni
subdir-$(CONFIG_MACH_PIC32)		+= pic32
subdir-$(CONFIG_ATH79)			+= qca
subdir-$(CONFIG_RALINK)			+= ralink
subdir-$(CONFIG_FIT_IMAGE_FDT_XILFPGA)	+= xilfpga

obj-$(CONFIG_BUILTIN_DTB)	:= $(addsuffix /, $(subdir-y))
