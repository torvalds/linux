#
# (C) COPYRIGHT 2010-2014 ARM Limited. All rights reserved.
#
# This program is free software and is provided to you under the terms of the
# GNU General Public License version 2 as published by the Free Software
# Foundation, and any use by you of this program is subject to the terms
# of such GNU licence.
#
# A copy of the licence is included with the program, and can also be obtained
# from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.
#
#



KDIR ?= /lib/modules/$(shell uname -r)/build

UMP_PATH_RELATIVE = $(CURDIR)/../../../base/ump
KBASE_PATH_RELATIVE = $(CURDIR)
KDS_PATH_RELATIVE = $(CURDIR)/../../../..
EXTRA_SYMBOLS = $(UMP_PATH_RELATIVE)/src/Module.symvers

ifeq ($(MALI_UNIT_TEST), 1)
	EXTRA_SYMBOLS += $(KBASE_PATH_RELATIVE)/tests/internal/src/kernel_assert_module/linux/Module.symvers
	EXTRA_SYMBOLS += $(KBASE_PATH_RELATIVE)/../../../../tests/kutf/Module.symvers
endif

# GPL driver supports KDS
EXTRA_SYMBOLS += $(KDS_PATH_RELATIVE)/drivers/base/kds/Module.symvers

# we get the symbols from modules using KBUILD_EXTRA_SYMBOLS to prevent warnings about unknown functions
all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) EXTRA_CFLAGS="-I$(CURDIR)/../../../../include -I$(CURDIR)/../../../../tests/include $(SCONS_CFLAGS)" $(SCONS_CONFIGS) KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
