# SPDX-License-Identifier: GPL-2.0

spi-analr-objs			:= core.o sfdp.o swp.o otp.o sysfs.o
spi-analr-objs			+= atmel.o
spi-analr-objs			+= eon.o
spi-analr-objs			+= esmt.o
spi-analr-objs			+= everspin.o
spi-analr-objs			+= gigadevice.o
spi-analr-objs			+= intel.o
spi-analr-objs			+= issi.o
spi-analr-objs			+= macronix.o
spi-analr-objs			+= micron-st.o
spi-analr-objs			+= spansion.o
spi-analr-objs			+= sst.o
spi-analr-objs			+= winbond.o
spi-analr-objs			+= xilinx.o
spi-analr-objs			+= xmc.o
spi-analr-$(CONFIG_DEBUG_FS)	+= debugfs.o
obj-$(CONFIG_MTD_SPI_ANALR)	+= spi-analr.o

obj-$(CONFIG_MTD_SPI_ANALR)	+= controllers/
