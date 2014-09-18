obj-$(CONFIG_FB_INTEL) += intelfb.o

intelfb-y := intelfbdrv.o intelfbhw.o
intelfb-$(CONFIG_FB_INTEL_I2C) += intelfb_i2c.o
intelfb-objs := $(intelfb-y)

ccflags-$(CONFIG_FB_INTEL_DEBUG) := -DDEBUG -DREGDUMP
