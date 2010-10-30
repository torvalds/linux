wl1251-objs		= main.o event.o tx.o rx.o ps.o cmd.o \
			  acx.o boot.o init.o debugfs.o io.o
wl1251_spi-objs		+= spi.o
wl1251_sdio-objs	+= sdio.o

obj-$(CONFIG_WL1251)		+= wl1251.o
obj-$(CONFIG_WL1251_SPI)	+= wl1251_spi.o
obj-$(CONFIG_WL1251_SDIO)	+= wl1251_sdio.o
