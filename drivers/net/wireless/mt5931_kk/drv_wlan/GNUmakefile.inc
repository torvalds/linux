# --------------------------------------------------------------------
# Copyright (c) 2007 MediaTek Inc.
#
# All rights reserved. Copying, compilation, modification, distribution
# or any other use whatsoever of this material is strictly prohibited
# except in accordance with a Software License Agreement with
# MediaTek Inc.
# --------------------------------------------------------------------

# --------------------------------------------------------------------
# This file is used for identifying version of Linux Kernel
# --------------------------------------------------------------------

KCHECK_DIR := $(DRV_SRC_DIR)/os/linux/.kcheck

ifndef LINUX_SRC
$(error LINUX_SRC must be defined - check config.mk !)
endif 

FULL_PATH_CC := $(shell which $(CC) 2> /dev/null)
ifeq ($(FULL_PATH_CC), )
$(error $(CC) is missing - check config.mk !)
endif

ifeq ($(shell cd $(LINUX_SRC) 2> /dev/null && pwd), )
$(error $(LINUX_SRC) is missing - check config.mk !)
endif

ifndef KERNEL_RELEASE
KERNEL_RELEASE := $(shell $(CC) -E -I $(LINUX_SRC)/include $(KCHECK_DIR)/.kversion.c | grep uts_release | cut -f2 -d'"')
endif

ifeq ($(filter-out 2.4%, $(KERNEL_RELEASE)), )
LINUX24 = yes
endif

ifndef LINUX24
LINUX26_CC := $(shell $(MAKE) -n -C $(LINUX_SRC) M=$(KCHECK_DIR) 2> /dev/null | grep cc)
LINUX26_CC := $(filter-out -% '% "%,$(LINUX26_CC))
LINUX26_CC := $(sort $(filter %cc,$(LINUX26_CC)))
FULL_PATH_LINUX26_CC := $(shell which $(LINUX26_CC) 2> /dev/null)
ifeq ($(FULL_PATH_LINUX26_CC), )
LINUX26_CC := "\"$(LINUX26_CC)\" not found, check $(LINUX_SRC)/Makefile or \$$PATH in current SHELL !"
else
LINUX26_CC := $(FULL_PATH_LINUX26_CC)
endif
endif

ifneq ($(INSTALL_PATH), )
ifeq ($(shell cd $(INSTALL_PATH) 2> /dev/null && pwd), )
INSTALL_PATH :=
endif
endif
