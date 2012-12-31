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
SRC=\
	common/mali_kbase_device.c		\
	common/mali_kbase_cache_policy.c \
	common/mali_kbase_mem.c			\
	common/mali_kbase_mmu.c			\
	common/mali_kbase_jd.c			\
	common/mali_kbase_jm.c			\
	common/mali_kbase_gpuprops.c		\
	common/mali_kbase_js.c			\
	common/mali_kbase_js_affinity.c         \
	common/mali_kbase_pm.c			\
	common/mali_kbase_event.c		\
	common/mali_kbase_context.c		\
	common/mali_kbase_pm.c			\
	common/mali_kbase_pm_driver.c		\
	common/mali_kbase_pm_metrics.c		\
	common/mali_kbase_pm_always_on.c	\
	common/mali_kbase_pm_demand.c		\
	common/mali_kbase_config.c		\
	common/mali_kbase_security.c		\
	common/mali_kbase_instr.c		\
	common/mali_kbase_instr_7115.c	\
	common/mali_kbase_softjobs.c \
	common/mali_kbase_8401_workaround.c \
	linux/mali_kbase_mem_linux.c		\
	linux/mali_kbase_core_linux.c			\
	linux/mali_kbase_config_linux.c	

obj-$(CONFIG_VITHAR) += $(KBASE_PATH)/platform/mali_kbase_platform.o
obj-$(CONFIG_VITHAR_RT_PM) += $(KBASE_PATH)/platform/mali_kbase_runtime_pm.o
obj-$(CONFIG_VITHAR) += $(KBASE_PATH)/platform/mali_kbase_dvfs.o
RELATIVE_ROOT=../..
ROOT=$(KBUILD_EXTMOD)/$(RELATIVE_ROOT)
MALI_KBASE_SRC_LINUX_PATH ?= $(PWD)/linux

# Dummy model
ifeq ($(MALI_NO_MALI), 1)
SRC += common/mali_kbase_model_dummy.c
SRC += linux/mali_kbase_model_linux.c
endif

#HW error simulation
ifeq ($(MALI_NO_MALI), 1)
SRC += common/mali_kbase_model_error_generator.c
endif

# Use vsync metrics example using PL111 driver, if available
ifeq ($(wildcard $(KBUILD_EXTMOD)/linux/mali_kbase_pm_metrics_linux.c),)
SRC += common/mali_kbase_pm_metrics_dummy.c
else
SRC += linux/mali_kbase_pm_metrics_linux.c
endif

# if MALI_FAKE_PLATFORM_DEVICE == 1 or MALI_LICENSE_IS_GPL == 0 then include proper config
ifneq ($(findstring 1,$(MALI_FAKE_PLATFORM_DEVICE))$(findstring 0,$(MALI_LICENSE_IS_GPL)), )
SRC+=linux/config/mali_kbase_config_$(MALI_PLATFORM_CONFIG).c
# ensure GPL version of malisw gets pulled in
EXTRA_CFLAGS += -I$(ROOT)/kbase
endif

ifeq ($(findstring 0,$(MALI_LICENSE_IS_GPL)),)
# ensure GPL version of malisw gets pulled in
EXTRA_CFLAGS += -I$(ROOT)/kbase
endif

SRC+=common/mali_kbase_js_policy_$(MALI_KBASE_SCHEDULING_POLICY).c

ifeq ($(MALI_MOCK_TEST), 1)
SRC += ../tests/internal/src/mock/mali_kbase_pm_driver_mock.c
endif

ifeq ($(MALI_BASE_TRACK_MEMLEAK), 1)
SRC += common/mali_kbase_mem_track.c
endif

ifneq ($(KBUILD_EXTMOD),)
include $(KBUILD_EXTMOD)/Makefile.kbase
else
include $(VITHAR_ROOT)/$(KBASE_PATH)/Makefile.kbase
endif
ifneq ($(KBUILD_EXTMOD),)
OSK_PATH = $(ROOT)/osk
include $(OSK_PATH)/src/linux/Makefile.osk
else
OSK_PATH = $(VITHAR_ROOT)/osk
include $(OSK_PATH)/src/linux/Makefile.osk
endif

EXTRA_CFLAGS += -DMALI_DEBUG=$(MALI_DEBUG) -DMALI_BACKEND_KERNEL=$(MALI_BACKEND_KERNEL) -DMALI_USE_UMP=$(MALI_USE_UMP) \
				-DMALI_ANDROID=$(MALI_ANDROID) -DMALI_BASE_TRACK_MEMLEAK=$(MALI_BASE_TRACK_MEMLEAK) \
				-DMALI_KERNEL_TEST_API=$(MALI_KERNEL_TEST_API) -I$(ROOT)/kbase/midg_gpus/$(MALI_HW_VERSION) \
				-DMALI_UNIT_TEST=$(MALI_UNIT_TEST) -DMALI_INFINITE_CACHE=$(MALI_INFINITE_CACHE) \
				-DMALI_UNCACHED=$(MALI_UNCACHED) \
				-DMALI_LICENSE_IS_GPL=$(MALI_LICENSE_IS_GPL) \
				-DMALI_FAKE_PLATFORM_DEVICE=$(MALI_FAKE_PLATFORM_DEVICE) \
				-DMALI_MOCK_TEST=$(MALI_MOCK_TEST) \
				-DMALI_RELEASE_NAME=\""$(MALI_RELEASE_NAME)"\" \
				-DMALI_GATOR_SUPPORT=$(MALI_GATOR_SUPPORT) \
				-DMALI_KBASE_SRC_LINUX_PATH=$(MALI_KBASE_SRC_LINUX_PATH) \
				-DMALI_NO_MALI=$(MALI_NO_MALI) \
				-DMALI_ERROR_INJECT_ON=$(MALI_ERROR_INJECT_ON) \
				-DMALI_CUSTOMER_RELEASE=$(MALI_CUSTOMER_RELEASE) \
				-DMALI_INSTRUMENTATION_LEVEL=$(MALI_INSTRUMENTATION_LEVEL)

#MODULE:=mali_kbase.ko
#obj-m := $(MODULE:.ko=.o)
#$(MODULE:.ko=-y) := $(SRC:.c=.o)
#$(MODULE:.ko=-objs) := $(SRC:.c=.o) $(RELATIVE_ROOT)/osk/src/linux/lib.a

else

KDIR ?= /lib/modules/$(shell uname -r)/build

# the mali_kbase module depends on the ukk module
# we get the symbols from the ukk module using KBUILD_EXTRA_SYMBOLS to prevent warnings about unknown functions
all:
	$(MAKE) -C $(KDIR) M=$(PWD) KBUILD_EXTRA_SYMBOLS="$(PWD)/../../uk/src/ukk/linux/Module.symvers $(PWD)/../../ump/src/devicedrv/Module.symvers" modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

endif
