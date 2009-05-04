libertas-objs := main.o wext.o rx.o tx.o cmd.o cmdresp.o scan.o 11d.o	\
		 debugfs.o persistcfg.o ethtool.o assoc.o

usb8xxx-objs += if_usb.o
libertas_cs-objs += if_cs.o
libertas_sdio-objs += if_sdio.o
libertas_spi-objs += if_spi.o

obj-$(CONFIG_LIBERTAS)     += libertas.o
obj-$(CONFIG_LIBERTAS_USB) += usb8xxx.o
obj-$(CONFIG_LIBERTAS_CS)  += libertas_cs.o
obj-$(CONFIG_LIBERTAS_SDIO) += libertas_sdio.o
obj-$(CONFIG_LIBERTAS_SPI) += libertas_spi.o
