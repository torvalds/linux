# SPDX-License-Identifier: GPL-2.0
vfio-platform-base-y := vfio_platform_common.o vfio_platform_irq.o
vfio-platform-y := vfio_platform.o

obj-$(CONFIG_VFIO_PLATFORM) += vfio-platform.o
obj-$(CONFIG_VFIO_PLATFORM) += vfio-platform-base.o
obj-$(CONFIG_VFIO_PLATFORM) += reset/

vfio-amba-y := vfio_amba.o

obj-$(CONFIG_VFIO_AMBA) += vfio-amba.o
obj-$(CONFIG_VFIO_AMBA) += vfio-platform-base.o
obj-$(CONFIG_VFIO_AMBA) += reset/
