# --------------------------------------------------------------------
# Copyright (c) 2010 MediaTek Inc.
#
# All rights reserved. Copying, compilation, modification, distribution
# or any other use whatsoever of this material is strictly prohibited
# except in accordance with a Software License Agreement with
# MediaTek Inc.
# --------------------------------------------------------------------

# --------------------------------------------------------------------
# This Makefile is major entrance for compiling driver module for 
# Linux Kernel 2.6
# --------------------------------------------------------------------

DRV_SRC_DIR := $(shell pwd)
export DRV_SRC_DIR

include $(DRV_SRC_DIR)/config.mk
include $(DRV_SRC_DIR)/GNUmakefile.inc

# Objects List for Linux 2.6
COMMON_OBJS   := $(patsubst %.o,$(COMMON_DIR)%.o,$(strip $(COMMON_OBJS)))
NIC_OBJS      := $(patsubst %.o,$(NIC_DIR)%.o,$(strip $(NIC_OBJS)))
OS_OBJS       := $(patsubst %.o,$(OS_DIR)%.o,$(strip $(OS_OBJS)))
HIF_OBJS      := $(patsubst %.o,$(HIF_DIR)%.o,$(strip $(HIF_OBJS)))
MGMT_OBJS     := $(patsubst %.o,$(MGMT_DIR)%.o,$(strip $(MGMT_OBJS)))


.PHONY: all clean install info build
all: build install

build: info
	@$(MAKE) -C $(LINUX_SRC) M=$(DRV_SRC_DIR) modules
	@echo
	@$(SIZE) -d $(patsubst %.o,%.ko,$(MTK_WLAN_MODULE))
	@echo


clean:
	@$(MAKE) -C $(LINUX_SRC) M=$(DRV_SRC_DIR) clean


install:
ifneq ($(INSTALL_PATH),)
	@echo Install $(patsubst %.o,%.ko,$(MTK_WLAN_MODULE)) to $(INSTALL_PATH)/$(INSTALL_MODULE_NAME)
	@cp $(patsubst %.o,%.ko,$(MTK_WLAN_MODULE)) $(INSTALL_PATH)/$(INSTALL_MODULE_NAME)
else
	@echo Warning INSTALL_PATH must be defined for 'make install' - check config.mk !
endif
	@echo


info:
	@echo ""
	@echo "The following settings will be used for compilation:"
	@echo "----------------------------------------------------"
	@echo "PLATFORM       : $(ARCH)"
	@echo "COMPILE        : $(LINUX26_CC)"
	@echo "KERNEL PATH    : $(LINUX_SRC)"
	@echo "KERNEL VERSION : $(KERNEL_RELEASE)"
	@echo "INSTALL PATH   : $(INSTALL_PATH)"
	@echo "INSTALL MODULE NAME : $(INSTALL_MODULE_NAME)"	
	@echo "----------------------------------------------------"
	@echo ""
