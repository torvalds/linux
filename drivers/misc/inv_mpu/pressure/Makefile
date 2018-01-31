# SPDX-License-Identifier: GPL-2.0
#
# Pressure Slaves to MPUxxxx
#
obj-$(CONFIG_MPU_SENSORS_BMA085) += inv_mpu_bma085.o
inv_mpu_bma085-objs +=	bma085.o

EXTRA_CFLAGS += -Idrivers/misc/inv_mpu
EXTRA_CFLAGS += -D__C99_DESIGNATED_INITIALIZER
