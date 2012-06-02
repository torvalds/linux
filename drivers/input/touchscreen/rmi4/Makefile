obj-$(CONFIG_RMI4_BUS) += rmi_bus.o
obj-$(CONFIG_RMI4_I2C) += rmi_i2c.o
obj-$(CONFIG_RMI4_SPI) += rmi_spi.o
obj-$(CONFIG_RMI4_GENERIC) += rmi_driver.o rmi_f01.o
obj-$(CONFIG_RMI4_F09) += rmi_f09.o
obj-$(CONFIG_RMI4_F1A) += rmi_f1a.o
obj-$(CONFIG_RMI4_F11) += rmi_f11.o
obj-$(CONFIG_RMI4_F17) += rmi_f17.o
obj-$(CONFIG_RMI4_F19) += rmi_f19.o
obj-$(CONFIG_RMI4_F21) += rmi_f21.o
obj-$(CONFIG_RMI4_F34) += rmi_f34.o
obj-$(CONFIG_RMI4_F54) += rmi_f54.o
obj-$(CONFIG_RMI4_DEV) += rmi_dev.o
obj-y += rmi_reflash.o

ifeq ($(KERNELRELEASE),)

# KERNELDIR ?= /home/<AndroidKernelDirectory>
PWD := $(shell pwd)

.PHONY: build clean

build:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c
else

$(info Building with KERNELRELEASE = ${KERNELRELEASE})
obj-m +=    rmi_dev.o

endif

