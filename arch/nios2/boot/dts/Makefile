# SPDX-License-Identifier: GPL-2.0

dtb-y := $(addsuffix .dtb, $(CONFIG_BUILTIN_DTB_NAME))

dtb-$(CONFIG_OF_ALL_DTBS) += $(patsubst $(src)/%.dts,%.dtb, $(wildcard $(src)/*.dts))
