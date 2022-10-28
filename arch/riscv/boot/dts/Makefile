# SPDX-License-Identifier: GPL-2.0
subdir-y += sifive
subdir-y += starfive
subdir-$(CONFIG_SOC_CANAAN_K210_DTB_BUILTIN) += canaan
subdir-y += microchip
subdir-y += renesas

obj-$(CONFIG_BUILTIN_DTB) := $(addsuffix /, $(subdir-y))
