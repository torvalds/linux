#
# (C) COPYRIGHT 2011 ARM Limited. All rights reserved.
#
# This program is free software and is provided to you under the terms of the GNU General Public License version 2
# as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
#
# A copy of the licence is included with the program, and can also be obtained from Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA. 
#
#



# default to building for the host
ARCH ?= $(shell uname -m)

# linux build system integration

#ifneq ($(KERNELRELEASE),)
ifneq ($(VITHAR_ROOT),)
# Inside the kernel build system

EXTRA_CFLAGS += -I$(KBUILD_EXTMOD) -I$(KBUILD_EXTMOD)/../../../../..
KBUILD_EXTRA_SYMBOLS += "$(KBUILD_EXTMOD)/../../Module.symvers"

SRC += imports/ion/ump_kernel_import_ion.c

#MODULE:=ump_ion_import.ko

#obj-m := $(MODULE:.ko=.o)
#$(MODULE:.ko=-y) := $(SRC:.c=.o)
#$(MODULE:.ko=-objs) := $(SRC:.c=.o)

else
# Outside the kernel build system
#
#

ifeq ($(KDIR),)
$(error Must specify KDIR to point to the kernel to target))
endif

all:
	$(MAKE) ARCH=$(ARCH) -C $(KDIR) M=$(CURDIR)

clean:
	$(MAKE) ARCH=$(ARCH) -C $(KDIR) M=$(CURDIR) clean

endif

