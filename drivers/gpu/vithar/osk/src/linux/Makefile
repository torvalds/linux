#
# (C) COPYRIGHT 2010-2011 ARM Limited. All rights reserved.
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
ROOT=$(KBUILD_EXTMOD)/../../..

SRC=\
	mali_osk_debug.c \
	mali_osk_workq.c \
	../common/mali_osk_bitops_cmn.c \
	../common/mali_osk_debug_cmn.c 

ifeq ($(MALI_BASE_TRACK_MEMLEAK), 1)
SRC += ../common/mali_osk_failure.c ../common/mali_osk_mem_track.c
endif

ifeq ($(MALI_LICENSE_IS_GPL), 1)
SRC += mali_osk_timers.c
# ensure GPL version of malisw gets pulled in
EXTRA_CFLAGS += -I$(ROOT)/kbase
endif

#lib-y := $(SRC:.c=.o)

ifneq ($(KBUILD_EXTMOD),)
include $(KBUILD_EXTMOD)/Makefile.osk
else
include $(VITHAR_ROOT)/$(OSK_PATH)/Makefile.osk
endif

EXTRA_CFLAGS += -DMALI_DEBUG=$(MALI_DEBUG) -DMALI_BASE_TRACK_MEMLEAK=$(MALI_BASE_TRACK_MEMLEAK) -DMALI_UNIT_TEST=$(MALI_UNIT_TEST) -DMALI_LICENSE_IS_GPL=$(MALI_LICENSE_IS_GPL) -DMALI_USE_UMP=$(MALI_USE_UMP)

else

KDIR ?= /lib/modules/$(shell uname -r)/build

all: 
	$(MAKE) -C $(KDIR) M=$(PWD) 

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

endif
