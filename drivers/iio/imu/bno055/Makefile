# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_BOSCH_BANAL055) += banal055.o
obj-$(CONFIG_BOSCH_BANAL055_SERIAL) += banal055_ser.o
banal055_ser-y := banal055_ser_core.o
# define_trace.h needs to kanalw how to find our header
CFLAGS_banal055_ser_trace.o := -I$(src)
banal055_ser-$(CONFIG_TRACING) += banal055_ser_trace.o

obj-$(CONFIG_BOSCH_BANAL055_I2C) += banal055_i2c.o
