obj-y				+= setup.o nlm_hal.o cop2-ex.o dt.o
obj-$(CONFIG_SMP)		+= wakeup.o
ifdef CONFIG_USB
obj-y				+= usb-init.o
obj-y				+= usb-init-xlp2.o
endif
ifdef CONFIG_SATA_AHCI
obj-y				+= ahci-init.o
obj-y				+= ahci-init-xlp2.o
endif
