#
# Makefile for the kernel mmc device drivers.
#

subdir-ccflags-$(CONFIG_MMC_DEBUG) := -DDEBUG

obj-$(CONFIG_MMC)		+= core/
obj-$(CONFIG_MMC)		+= card/
obj-$(subst m,y,$(CONFIG_MMC))	+= host/
obj-$(CONFIG_MMC)		+= mmc-pm/
obj-$(CONFIG_MMC)		+= sunxi-host/
