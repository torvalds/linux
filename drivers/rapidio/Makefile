#
# Makefile for RapidIO interconnect services
#
obj-y += rio.o rio-access.o rio-driver.o rio-scan.o rio-sysfs.o

obj-$(CONFIG_RAPIDIO)		+= switches/
obj-$(CONFIG_RAPIDIO)		+= devices/

subdir-ccflags-$(CONFIG_RAPIDIO_DEBUG) := -DDEBUG
