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
#export MALI_KBASE_SCHEDULING_POLICY?=cfs
#export PLATFORM?=dummy
#export MALI_BACKEND_KERNEL?=1
#export MALI_NO_MALI?=0
#export MALI_USE_UMP?=1
#export MALI_DEBUG?=0
#export MALI_ERROR_INJECT_ON?=0
#export MALI_ANDROID?=0
#export MALI_BASE_TRACK_MEMLEAK?=0
#export MALI_KERNEL_TEST_API?=0
#export MALI_UNIT_TEST?=0
#export MALI_INFINITE_CACHE?=0
#export MALI_LICENSE_IS_GPL?=1
#export MALI_FAKE_PLATFORM_DEVICE?=0
#export MALI_MOCK_TEST?=0
#export MALI_HW_VERSION?=r0p0
#export MALI_UNCACHED?=0
#export MALI_INSTRUMENTATION_LEVEL?=0

KDIR ?= /lib/modules/$(shell uname -r)/build

# MROOT defines root of kernel device driver components
# This makefile may reside in container directory of the kernel device driver components or within the
# 'kbase' kernel driver component itself, so we set MROOT accordingly.
#
ifeq ($(wildcard $(PWD)/kbase/src/Makefile),)
MROOT=$(PWD)/..
else
MROOT=$(PWD)
endif

all:
	$(MAKE) -C $(KDIR) M=$(MROOT)/osk/src/linux
	$(MAKE) -C $(KDIR) M=$(MROOT)/uk/src/ukk/linux
	$(MAKE) -C $(KDIR) M=$(MROOT)/ump/src/devicedrv
	$(MAKE) -C $(KDIR) M=$(MROOT)/kbase/src KBUILD_EXTRA_SYMBOLS="$(MROOT)/uk/src/ukk/linux/Module.symvers $(MROOT)/ump/src/devicedrv/Module.symvers"

clean:
	$(MAKE) -C $(KDIR) M=$(MROOT)/osk/src/linux clean
	$(MAKE) -C $(KDIR) M=$(MROOT)/uk/src/ukk/linux clean
	$(MAKE) -C $(KDIR) M=$(MROOT)/ump/src/devicedrv clean
	$(MAKE) -C $(KDIR) M=$(MROOT)/kbase/src clean

