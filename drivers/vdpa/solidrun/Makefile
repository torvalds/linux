# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_SNET_VDPA) += snet_vdpa.o
snet_vdpa-$(CONFIG_SNET_VDPA) += snet_main.o
ifdef CONFIG_HWMON
snet_vdpa-$(CONFIG_SNET_VDPA) += snet_hwmon.o
endif
