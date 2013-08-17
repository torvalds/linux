#
# (C) COPYRIGHT 2011-2012 ARM Limited. All rights reserved.
#
# This program is free software and is provided to you under the terms of the GNU General Public License version 2
# as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
#
# A copy of the licence is included with the program, and can also be obtained from Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
#


# Defaults
export PLATFORM?=dummy
export MALI_ANDROID?=0
export MALI_UNIT_TEST?=0
export MALI_KERNEL_TEST_API?=0
export MALI_MOCK_TEST?=0
export MALI_INSTRUMENTATION_LEVEL?=0
export MALI_CUSTOMER_RELEASE?=1
export MALI_COVERAGE?=0
export CONFIG_MALI_NO_MALI?=n
export CONFIG_UMP?=m
export CONFIG_MALI_DEBUG?=y
export CONFIG_MALI_ERROR_INJECT?=n
export CONFIG_MALI_QA_LEAK?=n
export CONFIG_MALI_QA_RESFAIL?=n
export CONFIG_MALI_PLATFORM_FAKE?=y
export CONFIG_MALI_PLATFORM_VEXPRESS?=y
export CONFIG_MALI_UNCACHED?=y
export CONFIG_MALI_GATOR_SUPPORT?=y
export CONFIG_MALI_T6XX?=m
export CONFIG_KDS?=m

ifeq ($(CONFIG_MALI_NO_MALI),y)
	SCONS_CFLAGS+=-DCONFIG_MALI_NO_MALI
endif
ifeq ($(CONFIG_UMP),m)
	SCONS_CFLAGS+=-DCONFIG_UMP
endif
ifeq ($(CONFIG_MALI_DEBUG),y)
	SCONS_CFLAGS+=-DCONFIG_MALI_DEBUG
endif
ifeq ($(CONFIG_MALI_ERROR_INJECT),y)
	SCONS_CFLAGS+=-DCONFIG_MALI_ERROR_INJECT
endif
ifeq ($(CONFIG_MALI_QA_LEAK),y)
	SCONS_CFLAGS+=-DCONFIG_MALI_QA_LEAK
endif
ifeq ($(CONFIG_MALI_QA_RESFAIL),y)
	SCONS_CFLAGS+=-DCONFIG_MALI_QA_RESFAIL
endif
ifeq ($(CONFIG_MALI_PLATFORM_FAKE),y)
	SCONS_CFLAGS+=-DCONFIG_MALI_PLATFORM_FAKE
endif
ifeq ($(CONFIG_MALI_PLATFORM_VEXPRESS),y)
	SCONS_CFLAGS+=-DCONFIG_MALI_PLATFORM_VEXPRESS
endif
ifeq ($(CONFIG_MALI_UNCACHED),y)
	SCONS_CFLAGS+=-DCONFIG_MALI_UNCACHED
endif
ifeq ($(CONFIG_MALI_GATOR_SUPPORT),y)
	SCONS_CFLAGS+=-DCONFIG_MALI_GATOR_SUPPORT
endif
ifeq ($(CONFIG_KDS),m)
	SCONS_CFLAGS+=-DCONFIG_KDS
endif
export SCONS_CFLAGS

KDIR ?= /lib/modules/$(shell uname -r)/build

# MROOT defines root of kernel device driver components
# This makefile may reside in container directory of the kernel device driver components or within the
# 'kbase' kernel driver component itself, so we set MROOT accordingly.
#
ifeq ($(wildcard $(PWD)/kbase/src/Makefile),)
MROOT=$(PWD)/../../../../../..
else
MROOT=$(PWD)
endif

KBASE_PATH=$(MROOT)/kernel/drivers/gpu/arm/t6xx/kbase
UMP_PATH=$(MROOT)/kernel/drivers/base/ump
KDS_PATH=$(MROOT)/kernel/drivers/base/kds
OSK_PATH=$(MROOT)/kernel/drivers/gpu/arm/t6xx/kbase/osk

all:
	$(MAKE) -C $(OSK_PATH)/src/linux
	$(MAKE) -C $(KDS_PATH)
	$(MAKE) -C $(UMP_PATH)/src
	$(MAKE) -C $(KBASE_PATH)/src

clean:
	$(MAKE) -C $(OSK_PATH)/src/linux clean
	$(MAKE) -C $(KDS_PATH) clean
	$(MAKE) -C $(UMP_PATH)/src clean
	$(MAKE) -C $(KBASE_PATH)/src clean
