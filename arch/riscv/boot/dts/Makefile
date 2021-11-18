# SPDX-License-Identifier: GPL-2.0
subdir-y += sifive
subdir-$(CONFIG_SOC_CANAAN_K210_DTB_BUILTIN) += canaan
subdir-y += microchip
subdir-y += starfive

obj-$(CONFIG_BUILTIN_DTB) := $(addsuffix /, $(subdir-y))
