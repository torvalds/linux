#
# (C) COPYRIGHT ARM Limited. All rights reserved.
#
# This program is free software and is provided to you under the terms of the GNU General Public License version 2
# as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
#
# A copy of the licence is included with the program, and can also be obtained from Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA. 
#
#

#ifneq ($(KERNELRELEASE),)
ifneq ($(VITHAR_ROOT),)

OS=linux
RELATIVE_ROOT=../../../..
ROOT = $(KBUILD_EXTMOD)/$(RELATIVE_ROOT)
ifneq ($(KBUILD_EXTMOD),)
OSK_PATH = $(ROOT)/osk
include $(OSK_PATH)/src/$(OS)/Makefile.osk
else
OSK_PATH = osk/src/linux
include $(VITHAR_ROOT)/$(OSK_PATH)/Makefile.osk
endif

SRC=\
	../mali_ukk.c \
	mali_ukk_os.c 

# add osk kernel test functions to uk module to make it visible to other clients (mali_kbase.ko, mali_kernel_*_test.ko) 
ifeq ($(MALI_UNIT_TEST), 1)
	SRC+=$(RELATIVE_ROOT)/osk/src/$(OS)/mali_osk_kernel_test.c
endif

ifeq ($(MALI_LICENSE_IS_GPL), 1)
	# ensure GPL version of malisw gets pulled in
	EXTRA_CFLAGS += -I$(ROOT)/kbase
endif

#MODULE := ukk.ko

#obj-m := $(MODULE:.ko=.o)

#$(MODULE:.ko=-y) := $(SRC:.c=.o)

#$(MODULE:.ko=-objs) := $(SRC:.c=.o) $(RELATIVE_ROOT)/osk/src/$(OS)/lib.a

EXTRA_CFLAGS += -I$(ROOT)/uk -I$(ROOT)/uk/platform_$(PLATFORM) -DMALI_DEBUG=$(MALI_DEBUG) -DMALI_BACKEND_KERNEL=$(MALI_BACKEND_KERNEL) -DMALI_BASE_TRACK_MEMLEAK=$(MALI_BASE_TRACK_MEMLEAK) -DMALI_UNIT_TEST=$(MALI_UNIT_TEST) -DMALI_LICENSE_IS_GPL=$(MALI_LICENSE_IS_GPL)

else

KDIR ?= /lib/modules/$(shell uname -r)/build

all: 
	$(MAKE) -C $(KDIR) M=$(PWD) modules


endif
