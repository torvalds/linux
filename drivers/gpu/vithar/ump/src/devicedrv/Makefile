#
# (C) COPYRIGHT 2008-2012 ARM Limited. All rights reserved.
#
# This program is free software and is provided to you under the terms of the GNU General Public License version 2
# as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
#
# A copy of the licence is included with the program, and can also be obtained from Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA. 
#
#


ifneq ($(KBUILD_EXTMOD),)
OS=linux
include $(KBUILD_EXTMOD)/Makefile.common
else
include $(VITHAR_ROOT)/$(UMP_PATH)/Makefile.common
endif

ifdef CONFIG_ION
include $(VITHAR_ROOT)/$(UMP_PATH)/imports/ion/Makefile
endif

# default to building for the host
ARCH ?= $(shell uname -m)

# linux build system integration

######################################################################
#******************************************************************###
#TODO: remove OSK references for Linux Kernel mainline release:  **###
#***** I.E. for each ifeq ($(MALI_UNIT_TEST),1) leave only the   **###
#####* body in the else branch and remove the rest               **###
#####**************************************************************###
######################################################################

ifeq ($(MALI_UNIT_TEST),1)
OS=linux
RELATIVE_ROOT=../../..
ROOT = $(PWD)/../../..
OSK_PATH = $(ROOT)/osk
include $(OSK_PATH)/src/$(OS)/Makefile.osk
EXTRA_CFLAGS += -DMALI_UNIT_TEST=$(MALI_UNIT_TEST) -DMALI_DEBUG=$(MALI_DEBUG) -DMALI_LICENSE_IS_GPL=$(MALI_LICENSE_IS_GPL) -DMALI_BASE_TRACK_MEMLEAK=$(MALI_BASE_TRACK_MEMLEAK)
endif

#ifneq ($(KERNELRELEASE),)
ifneq ($(VITHAR_ROOT),)
# Inside the kernel build system

EXTRA_CFLAGS += -I$(KBUILD_EXTMOD) -I$(KBUILD_EXTMOD)/common -I$(KBUILD_EXTMOD)/linux  -I$(KBUILD_EXTMOD)/../.. -I$(KBUILD_EXTMOD)/../../.. -DUMP_LICENSE_IS_GPL=$(MALI_LICENSE_IS_GPL)

# For customer releases the Linux Device Drivers will be provided as ARM proprietary and GPL releases:
# The ARM proprietary product will only include the license/proprietary directory
# The GPL product will only include the license/gpl directory

ifeq ($(wildcard $(KBUILD_EXTMOD)/linux/license/gpl/*),)
EXTRA_CFLAGS += -I$(KBUILD_EXTMOD)/linux/license/proprietary
else
EXTRA_CFLAGS += -I$(KBUILD_EXTMOD)/linux/license/gpl
endif

SRC +=	linux/ump_kernel_linux.c linux/ump_kernel_linux_mem.c

#MODULE:=ump.ko

#obj-m := $(MODULE:.ko=.o)
#$(MODULE:.ko=-y) := $(SRC:.c=.o)

ifeq ($(MALI_UNIT_TEST),1)
$(MODULE:.ko=-objs) := $(SRC:.c=.o) $(RELATIVE_ROOT)/osk/src/linux/lib.a
else
#$(MODULE:.ko=-objs) := $(SRC:.c=.o) 
endif

else
# Outside the kernel build system
#
#

# Get any user defined KDIR-<names> or maybe even a hardcoded KDIR
-include KDIR_CONFIGURATION

# Define host system directory
KDIR-$(shell uname -m):=/lib/modules/$(shell uname -r)/build

ifeq ($(ARCH), arm)
	# when compiling for ARM we're cross compiling
	export CROSS_COMPILE ?= arm-none-linux-gnueabi-
	CONFIG ?= arm
else
	# Compiling for the host
	CONFIG ?= $(shell uname -m)
endif

# default cpu to select
CPU ?= $(shell uname -m)

# look up KDIR based om CPU selection
KDIR ?= $(KDIR-$(CPU))

ifeq ($(KDIR),)
$(error No KDIR found for platform $(CPU))
endif

# Validate selected config
ifneq ($(shell [ -d arch-$(CONFIG) ] && [ -f arch-$(CONFIG)/config.h ] && echo "OK"), OK)
$(warning Current directory is $(shell pwd))
$(error No configuration found for config $(CONFIG). Check that arch-$(CONFIG)/config.h exists)
else
# Link arch to the selected arch-config directory
$(shell [ -L arch ] && rm arch)
$(shell ln -sf arch-$(CONFIG) arch)
$(shell touch arch/config.h)
endif

all:
ifeq ($(MALI_UNIT_TEST),1)
	$(MAKE) ARCH=$(ARCH) -C $(KDIR) M=$(CURDIR) KBUILD_EXTRA_SYMBOLS="$(ROOT)/uk/src/ukk/linux/Module.symvers"
else
	$(MAKE) ARCH=$(ARCH) -C $(KDIR) M=$(CURDIR)
endif

kernelrelease:
ifeq ($(MALI_UNIT_TEST),1)
	$(MAKE) -C $(KDIR) KBUILD_EXTRA_SYMBOLS="$(ROOT)/uk/src/ukk/linux/Module.symvers" kernelrelease
else
	$(MAKE) -C $(KDIR) kernelrelease
endif

clean:
	$(MAKE) ARCH=$(ARCH) -C $(KDIR) M=$(CURDIR) clean

endif
