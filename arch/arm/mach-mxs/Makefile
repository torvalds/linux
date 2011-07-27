# Common support
obj-y := clock.o devices.o gpio.o icoll.o iomux.o system.o timer.o

obj-$(CONFIG_MXS_OCOTP) += ocotp.o
obj-$(CONFIG_PM) += pm.o

obj-$(CONFIG_SOC_IMX23) += clock-mx23.o mm-mx23.o
obj-$(CONFIG_SOC_IMX28) += clock-mx28.o mm-mx28.o

obj-$(CONFIG_MACH_STMP378X_DEVB) += mach-stmp378x_devb.o
obj-$(CONFIG_MACH_MX23EVK) += mach-mx23evk.o
obj-$(CONFIG_MACH_MX28EVK) += mach-mx28evk.o
obj-$(CONFIG_MODULE_TX28) += module-tx28.o
obj-$(CONFIG_MACH_TX28)    += mach-tx28.o

obj-y += devices/
