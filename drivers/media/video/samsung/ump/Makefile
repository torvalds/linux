#
# Copyright (C) 2010 ARM Limited. All rights reserved.
# 
# This program is free software and is provided to you under the terms of the GNU General Public License version 2
# as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
# 
# A copy of the licence is included with the program, and can also be obtained from Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#

ifeq ($(CONFIG_UMP_DED_ONLY),y)
UMP_MEM_SIZE= $(CONFIG_UMP_MEM_SIZE)
USING_MEMORY=0
endif

ifeq ($(CONFIG_UMP_OSMEM_ONLY),y)
UMP_MEM_SIZE= $(CONFIG_UMP_MEM_SIZE)
USING_MEMORY=1
endif

ifeq ($(CONFIG_UMP_VCM_ONLY),y)
UMP_MEM_SIZE= $(CONFIG_UMP_MEM_SIZE)
USING_MEMORY=2
endif


# For UMP Debug
ifeq ($(CONFIG_VIDEO_UMP_DEBUG),y)
DEFINES += -DDEBUG
endif

# Set up our defines, which will be passed to gcc
DEFINES += -DMALI_USE_UNIFIED_MEMORY_PROVIDER
DEFINES += -DUSING_MEMORY=$(USING_MEMORY)
DEFINES += -DUMP_MEM_SIZE=$(UMP_MEM_SIZE)
DEFINES += -DMALI_STATE_TRACKING=1

UDD_FILE_PREFIX := drivers/media/video/samsung/ump/
KBUILDROOT = 

# linux build system integration

obj-$(CONFIG_VIDEO_UMP)	+= ump.o

# For customer releases the Linux Device Drivers will be provided as ARM proprietary and GPL releases:
# The ARM proprietary product will only include the license/proprietary directory
# The GPL product will only include the license/gpl directory

INCLUDES += \
		-I$(UDD_FILE_PREFIX)\
		-I$(UDD_FILE_PREFIX)common\
		-I$(UDD_FILE_PREFIX)linux\
		-I$(UDD_FILE_PREFIX)include\
		-I$(UDD_FILE_PREFIX)linux/license/gpl/\
		-I$(UDD_FILE_PREFIX)../mali/common\
		-I$(UDD_FILE_PREFIX)../mali/linux

OSKFILES+=\
		$(KBUILDROOT)../mali/linux/mali_osk_atomics.o \
		$(KBUILDROOT)../mali/linux/mali_osk_locks.o \
		$(KBUILDROOT)../mali/linux/mali_osk_math.o \
		$(KBUILDROOT)../mali/linux/mali_osk_memory.o \
		$(KBUILDROOT)../mali/linux/mali_osk_misc.o 

ump-y := \
		$(KBUILDROOT)linux/ump_kernel_linux.o \
		$(KBUILDROOT)linux/ump_kernel_memory_backend_os.o \
		$(KBUILDROOT)linux/ump_kernel_memory_backend_dedicated.o \
		$(KBUILDROOT)linux/ump_memory_backend.o \
		$(KBUILDROOT)linux/ump_ukk_wrappers.o \
		$(KBUILDROOT)linux/ump_ukk_ref_wrappers.o \
		$(KBUILDROOT)linux/ump_osk_atomics.o \
		$(KBUILDROOT)linux/ump_osk_low_level_mem.o \
		$(KBUILDROOT)linux/ump_osk_misc.o \
		$(KBUILDROOT)common/ump_kernel_common.o \
		$(KBUILDROOT)common/ump_kernel_descriptor_mapping.o \
		$(KBUILDROOT)common/ump_kernel_api.o \
		$(KBUILDROOT)common/ump_kernel_ref_drv.o\
		$(OSKFILES)

ump-$(CONFIG_UMP_VCM_ALLOC) += \
                $(KBUILDROOT)linux/ump_kernel_memory_backend_vcm.o \

EXTRA_CFLAGS += $(INCLUDES) \
		$(DEFINES) 
		
		
# Get subversion revision number, fall back to 0000 if no svn info is available
# SVN_REV:=$(shell ((svnversion | grep -qv exported && echo -n 'Revision: ' && svnversion) || git svn info | sed -e 's/$$$$/M/' | grep '^Revision: ' || echo ${MALI_RELEASE_NAME}) 2>/dev/null | sed -e 's/^Revision: //')
SVN_REV:=0000
		
EXTRA_CFLAGS += -DSVN_REV=$(SVN_REV)
EXTRA_CFLAGS += -DSVN_REV_STRING=\"$(SVN_REV)\"
