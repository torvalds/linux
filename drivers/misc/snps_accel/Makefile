# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_SNPS_ARCSYNC) += snps_arcsync.o

obj-$(CONFIG_SNPS_ACCEL_APP) += snps_accel_app.o
snps_accel_app-y := \
		snps_accel_drv.o \
		snps_accel_mem.o

ccflags-y += -DDEBUG
