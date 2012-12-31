#
# Copyright (C) 2010-2012 ARM Limited. All rights reserved.
# 
# This program is free software and is provided to you under the terms of the GNU General Public License version 2
# as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
# 
# A copy of the licence is included with the program, and can also be obtained from Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#

OSKOS :=linux
FILES_PREFIX=
MALI_FILE_PREFIX := drivers/media/video/samsung/mali
KBUILDROOT =

ifeq ($(CONFIG_MALI_DED_ONLY),y)
USING_OS_MEMORY=0
USING_MMU=0
USING_DED=1
endif

ifeq ($(CONFIG_MALI_DED_MMU),y)
USING_OS_MEMORY=0
USING_MMU=1
USING_DED=1
endif

ifeq ($(CONFIG_MALI_OSMEM_ONLY),y)
USING_MMU=1
USING_DED=0
USING_OS_MEMORY=1
endif

ifeq ($(CONFIG_MALI_DED_OSMEM),y)
USING_MMU=1
USING_DED=1
USING_OS_MEMORY=1
endif

ifeq ($(CONFIG_PM),y)
USING_PMM=1
endif

ifeq ($(CONFIG_PM_RUNTIME),y)  
USING_MALI_RUN_TIME_PM=1
endif

ifeq ($(CONFIG_VIDEO_MALI400MP_DVFS),y)
USING_GPU_UTILIZATION=1
USING_MALI_DVFS_ENABLED=1
endif

ifeq ($(CONFIG_VIDEO_MALI400MP_DEBUG),y)
BUILD=debug
endif

ifeq ($(CONFIG_VIDEO_MALI400MP_STREAMLINE_PROFILING),y)
USING_PROFILING=1
USING_TRACEPOINTS=1
endif

# set up defaults if not defined by the user
PANIC_ON_WATCHDOG_TIMEOUT ?= 1 
USING_MALI400 ?= 1
USING_MMU ?= 1
USING_DED ?= 0
USING_UMP ?= 0
ONLY_ZBT ?= 0
USING_ZBT ?= 0
USING_OS_MEMORY ?= 1
USING_PMM ?= 0
USING_MALI_RUN_TIME_PM ?= 0
USING_MALI_PMM_TESTSUITE ?= 0
USING_MALI_PMU ?= 0
USING_GPU_UTILIZATION ?= 0
OS_MEMORY_KERNEL_BUFFER_SIZE_IN_MB ?= 6
USING_PROFILING ?= 0
USING_TRACEPOINTS ?= 0
USING_MALI_MAJOR_PREDEFINE = 1
USING_MALI_DVFS_ENABLED ?= 0
TIMESTAMP ?= default
BUILD ?= release 
USING_MALI_PMM_EARLYSUSPEND ?= 0
#USING_KERNEL_WITH_DMA_ALLOC_PHYS_PAGE ?= 0
CONFIG_MALI_MEM_SIZE ?= 64

#config validtion check
ifeq ($(USING_OS_MEMORY),1)
        USING_MMU = 1
endif
# Check if a Mali Core sub module should be enabled, true or false returned
#submodule_enabled = $(shell gcc $(DEFINES) -E $(MALI_FILE_PREFIX)/arch/config.h | grep type | grep -c $(2))

# Inside the kernel build system

# This conditional makefile exports the global definition ARM_INTERNAL_BUILD. Customer releases will not include arm_internal.mak
-include ../../../arm_internal.mak

# Set up our defines, which will be passed to gcc
DEFINES += -DUSING_ZBT=$(USING_ZBT)
DEFINES += -DUSING_OS_MEMORY=$(USING_OS_MEMORY)
DEFINES += -DUSING_MMU=$(USING_MMU)
DEFINES += -DUSING_DED=$(USING_DED)
DEFINES += -DUSING_UMP=$(USING_UMP)
DEFINES += -DONLY_ZBT=$(ONLY_ZBT)
DEFINES += -D_MALI_OSK_SPECIFIC_INDIRECT_MMAP
DEFINES += -DUSING_MALI_PMU=$(USING_MALI_PMU)
DEFINES += -DMALI_PMM_RUNTIME_JOB_CONTROL_ON=$(USING_MALI_RUN_TIME_PM)
DEFINES += -DUSING_MALI_PMM=$(USING_PMM)
DEFINES += -DMALI_GPU_UTILIZATION=$(USING_GPU_UTILIZATION)
DEFINES += -DCONFIG_MALI_MEM_SIZE=$(CONFIG_MALI_MEM_SIZE)
DEFINES += -D_MALI_OSK_SPECIFIC_INDIRECT_MMAP
DEFINES += -DMALI_TIMELINE_PROFILING_ENABLED=$(USING_PROFILING)
DEFINES += -DMALI_POWER_MGMT_TEST_SUITE=$(USING_MALI_PMM_TESTSUITE)
DEFINES += -DMALI_MAJOR_PREDEFINE=$(USING_MALI_MAJOR_PREDEFINE)
DEFINES += -DMALI_DVFS_ENABLED=$(USING_MALI_DVFS_ENABLED)
DEFINES += -DUSING_MALI_PMM_EARLYSUSPEND=$(USING_MALI_PMM_EARLYSUSPEND)
DEFINES += -DMALI_STATE_TRACKING=1
DEFINES += -DMALI_OS_MEMORY_KERNEL_BUFFER_SIZE_IN_MB=$(OS_MEMORY_KERNEL_BUFFER_SIZE_IN_MB)
DEFINES += -DMALI_TRACEPOINTS_ENABLED=$(USING_TRACEPOINTS)

ifeq ($(BUILD),debug)
DEFINES += -DDEBUG
endif

# Linux has its own mmap cleanup handlers (see mali_kernel_mem_mmu.o)
DEFINES += -DMALI_UKK_HAS_IMPLICIT_MMAP_CLEANUP

# UMP
ifeq ($(CONFIG_VIDEO_UMP),y)
	DEFINES += -DMALI_USE_UNIFIED_MEMORY_PROVIDER=1 -DHAVE_UNLOCKED_IOCTL
	EXTRA_CFLAGS += -I$(MALI_FILE_PREFIX)/../ump/include
else
	DEFINES += -DMALI_USE_UNIFIED_MEMORY_PROVIDER=0
endif

# Target build file
obj-$(CONFIG_VIDEO_MALI400MP)		+= mali.o

# Use our defines when compiling
# MALI
INCLUDES = \
		-I$(MALI_FILE_PREFIX)\
		-I$(MALI_FILE_PREFIX)/platform\
		-I$(MALI_FILE_PREFIX)/common \
		-I$(MALI_FILE_PREFIX)/linux 

EXTRA_CFLAGS += $(INCLUDES)\
		$(DEFINES) 


EXTRA_CFLAGS += -I$(MALI_FILE_PREFIX)/linux/license/gpl
EXTRA_CFLAGS += -I$(MALI_FILE_PREFIX)/common/pmm

# Source files which always are included in a build
ifeq ($(CONFIG_VIDEO_UMP),y)
OSKFILES=\
        $(FILES_PREFIX)$(OSKOS)/mali_osk_irq.o \
        $(FILES_PREFIX)$(OSKOS)/mali_osk_low_level_mem.o \
        $(FILES_PREFIX)$(OSKOS)/mali_osk_mali.o \
        $(FILES_PREFIX)$(OSKOS)/mali_osk_notification.o \
        $(FILES_PREFIX)$(OSKOS)/mali_osk_time.o \
        $(FILES_PREFIX)$(OSKOS)/mali_osk_timers.o
else
OSKFILES=\
        $(FILES_PREFIX)$(OSKOS)/mali_osk_atomics.o \
        $(FILES_PREFIX)$(OSKOS)/mali_osk_irq.o \
        $(FILES_PREFIX)$(OSKOS)/mali_osk_locks.o \
        $(FILES_PREFIX)$(OSKOS)/mali_osk_low_level_mem.o \
        $(FILES_PREFIX)$(OSKOS)/mali_osk_math.o \
        $(FILES_PREFIX)$(OSKOS)/mali_osk_memory.o \
        $(FILES_PREFIX)$(OSKOS)/mali_osk_misc.o \
        $(FILES_PREFIX)$(OSKOS)/mali_osk_mali.o \
        $(FILES_PREFIX)$(OSKOS)/mali_osk_notification.o \
        $(FILES_PREFIX)$(OSKOS)/mali_osk_time.o \
        $(FILES_PREFIX)$(OSKOS)/mali_osk_timers.o
endif #($(CONFIG_VIDEO_UMP),y)

ifeq ($(CONFIG_CPU_EXYNOS4210),y)
	MALI_PLATFORM_FILE = platform/orion-m400/mali_platform.o
else
	MALI_PLATFORM_FILE = platform/pegasus-m400/mali_platform.o
endif #($(CONFIG_CPU_EXYNOS4210),y)

UKKFILES=\
        $(FILES_PREFIX)$(OSKOS)/mali_ukk_mem.o \
        $(FILES_PREFIX)$(OSKOS)/mali_ukk_gp.o \
        $(FILES_PREFIX)$(OSKOS)/mali_ukk_pp.o \
        $(FILES_PREFIX)$(OSKOS)/mali_ukk_core.o \
        $(FILES_PREFIX)$(OSKOS)/mali_ukk_vsync.o

mali-y := \
	$(KBUILDROOT)common/mali_kernel_core.o \
	$(KBUILDROOT)linux/mali_kernel_linux.o \
	$(KBUILDROOT)linux/mali_osk_indir_mmap.o \
	$(KBUILDROOT)common/mali_kernel_rendercore.o \
	$(KBUILDROOT)common/mali_kernel_descriptor_mapping.o \
	$(KBUILDROOT)common/mali_kernel_vsync.o \
    $(KBUILDROOT)linux/mali_kernel_sysfs.o \
	$(KBUILDROOT)$(MALI_PLATFORM_FILE) \
	$(KBUILDROOT)$(OSKFILES) \
	$(KBUILDROOT)$(UKKFILES) 
	#__malidrv_build_info.o

ifeq ($(USING_PROFILING),1)
EXTRA_CFLAGS += -I$(MALI_FILE_PREFIX)/timestamp-default
EXTRA_CFLAGS += -I$(MALI_FILE_PREFIX)/profiling/include
mali-y += \
	common/mali_kernel_profiling.o \
	timestamp-$(TIMESTAMP)/mali_timestamp.o \
	linux/mali_ukk_profiling.o

EXTRA_CFLAGS += -I$(KBUILD_EXTMOD)/timestamp-$(TIMESTAMP)
endif

ifeq ($(USING_TRACEPOINTS),1)
mali-y += \
	linux/mali_osk_profiling.o
endif

# Selecting files to compile by parsing the config file

ifeq ($(USING_PMM),1)
mali-y += \
	common/pmm/mali_pmm.o \
	common/pmm/mali_pmm_policy.o \
	common/pmm/mali_pmm_policy_alwayson.o \
	common/pmm/mali_pmm_policy_jobcontrol.o \
	common/pmm/mali_pmm_state.o \
	linux/mali_kernel_pm.o \
	linux/mali_osk_pm.o \
	linux/mali_device_pause_resume.o
endif
ifeq ($(USING_MALI_PMU),1)
mali-y += \
	common/pmm/mali_pmm_pmu.o
endif

ifeq ($(USING_GPU_UTILIZATION),1)
mali-y += \
	common/mali_kernel_utilization.o
endif

# Mali-400 PP in use
EXTRA_CFLAGS += -DUSING_MALI400
mali-y += common/mali_kernel_MALI200.o

# Mali-400 GP in use
mali-y += common/mali_kernel_GP2.o

# Mali MMU in use
mali-y += \
	common/mali_kernel_mem_mmu.o \
	common/mali_kernel_memory_engine.o \
	common/mali_block_allocator.o \
	common/mali_kernel_mem_os.o

# Mali Level2 cache in use
EXTRA_CFLAGS += -DUSING_MALI400_L2_CACHE
mali-y += common/mali_kernel_l2_cache.o

ifeq ($(USING_MALI_DVFS_ENABLED),1)
ifeq ($(CONFIG_CPU_EXYNOS4210),y)
mali-y += \
	platform/orion-m400/mali_platform_dvfs.o
else
mali-y += \
	platform/pegasus-m400/mali_platform_dvfs.o
endif #($(CONFIG_CPU_EXYNOS4210),y)
endif #($(USING_MALI_DVFS_ENABLED),1)

ifeq ($(PANIC_ON_WATCHDOG_TIMEOUT),1)
	EXTRA_CFLAGS += -DUSING_KERNEL_PANIC
endif

# Get subversion revision number, fall back to 0000 if no svn info is available
# SVN_REV:=$(shell ((svnversion | grep -qv exported && echo -n 'Revision: ' && svnversion) || git svn info | sed -e 's/$$$$/M/' | grep '^Revision: ' || echo ${MALI_RELEASE_NAME}) 2>/dev/null | sed -e 's/^Revision: //')
SVN_REV:=0000

EXTRA_CFLAGS += -DSVN_REV=$(SVN_REV)
EXTRA_CFLAGS += -DSVN_REV_STRING=\"$(SVN_REV)\"

