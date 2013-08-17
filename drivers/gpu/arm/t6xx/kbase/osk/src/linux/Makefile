#
# (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
#
# This program is free software and is provided to you under the terms of the GNU General Public License version 2
# as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
#
# A copy of the licence is included with the program, and can also be obtained from Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
#


ifneq ($(KERNELRELEASE),)

# kernel/drivers/gpu/arm/t6xx
RELATIVE_ROOT=../../../../../../../../..
ROOT=$(KBUILD_EXTMOD)/$(RELATIVE_ROOT)
OSK_PATH=$(ROOT)/kernel/drivers/gpu/arm/t6xx/kbase/osk

EXTRA_CFLAGS += \
	-I$(ROOT)/kernel/drivers/gpu/arm/t6xx/kbase \
	-DMALI_UNIT_TEST=$(MALI_UNIT_TEST) \
	$(SCONS_CFLAGS)

include $(OSK_PATH)/src/linux/Makefile.osk

SRC=\
	mali_osk_debug.c \
	../common/mali_osk_bitops_cmn.c \
	../common/mali_osk_debug_cmn.c

ifeq ($(CONFIG_MALI_QA_RESFAIL), y)
SRC += ../common/mali_osk_failure.c
endif

ifeq ($(CONFIG_MALI_QA_LEAK), y)
SRC += ../common/mali_osk_mem_track.c
endif

SRC += mali_osk_timers.c

lib-y := $(SRC:.c=.o)

else

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) $(SCONS_CONFIGS)

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

endif
