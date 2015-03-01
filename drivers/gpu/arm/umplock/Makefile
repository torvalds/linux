#
# Copyright (C) 2012, 2014 ARM Limited. All rights reserved.
# 
# This program is free software and is provided to you under the terms of the GNU General Public License version 2
# as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
# 
# A copy of the licence is included with the program, and can also be obtained from Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#

# default to building for the host
ARCH ?= $(shell uname -m)

# linux build system integration

ifneq ($(KERNELRELEASE),)
# Inside the kernel build system

EXTRA_CFLAGS += -I$(KBUILD_EXTMOD)

SRC =	umplock_driver.c

MODULE:=umplock.ko

obj-m := $(MODULE:.ko=.o)
$(MODULE:.ko=-y) := $(SRC:.c=.o)

$(MODULE:.ko=-objs) := $(SRC:.c=.o) 

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

all:
	$(MAKE) ARCH=$(ARCH) -C $(KDIR) M=$(CURDIR)

kernelrelease:
	$(MAKE) -C $(KDIR) kernelrelease

clean:
	$(MAKE) ARCH=$(ARCH) -C $(KDIR) M=$(CURDIR) clean

endif
