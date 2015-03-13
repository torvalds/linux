# copyright (c) 2011 - 2012 espressif system
# linux_sdio
#ifneq ($(KERNELRELEASE),)
# kbuild part of makefile
ARCH := arm
CROSS_COMPILE := $(TOOLCHAIN_PREFIX)
KSRC := $(KERNEL_DIR)
MODPATH := $(shell pwd)
DRIVER_NAME := esp_prealloc

####################### NORMAL OPTION ########################################
EXTRA_CFLAGS += -DESP_PRE_MEM              # normal mode, support SDIO
#EXTRA_CFLAGS += -DESP_PRE_MEM -DESP_SPI   # normal mode, support SPI
##############################################################################

####################### ADVANCE OPTION ###########################
#EXTRA_CFLAGS += -DESP_SLAB                # ex mode, most users unlikely use this mode, if someone use, please add this macro extractly
##############################################################################
obj-y:= $(DRIVER_NAME).o
$(DRIVER_NAME)-y += esp_mem.o
$(DRIVER_NAME)-y += esp_slab.o
$(DRIVER_NAME)-y += esp_mem_init.o

