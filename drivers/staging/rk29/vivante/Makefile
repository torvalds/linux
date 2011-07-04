##############################################################################
#
#    Copyright (C) 2005 - 2010 by Vivante Corp.
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the license, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program; if not write to the Free Software
#    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
##############################################################################


#
# Linux build file for kernel HAL driver.
#

################################################################
# Arch.

ARCH_TYPE       ?= arm
CPU_TYPE        ?= arm920
CPU_ARCH        ?= 0
STATIC_LINK     ?= 0
EGL_API_FB      ?= 0
USE_VDK         ?= 0
USE_PROFILER    ?= 0
USE_SW_FB       ?= 0
USE_3D_VG		=1
ABI             ?= 0
ANDROID         ?= 1
EGL_API_ANDROID ?= 1
ENUM_WORKAROUND ?= 0
ENDIANNESS		?=
QNX             ?= 0
LINUX_OABI		?= 0
USE_ARMCC		?= 0

ifeq ($(LINUX_OABI), 1)
ABI             ?= 0
else
ABI             ?= aapcs-linux
endif

# Force to use dma_coherent_* stuff.
NO_DMA_COHERENT ?= 1

# Enable to start GPU clock in drver.
ENABLE_GPU_CLOCK_BY_DRIVER = 1

# Set this value to 1 if you are using ARM L2 cache.
ENABLE_ARM_L2_CACHE	= 0

# Set this value to 1 if you are using DOVE board.
CONFIG_DOVE_GPU = 0


AQROOT          ?= drivers/staging/rk29/vivante
AQARCH          ?= $(AQROOT)/arch/XAQ2

#include $(AQROOT)/config
ARCH_TYPE	?= arm
SDK_DIR		?= $(AQROOT)/build/sdk
USE_3D_VG	= 1

DEBUG       = 0
gcdkREPORT_VIDMEM_USAGE = 0

#DRIVER_OUT_DIR = hal/driver
#KERNEL_DIR ?= $(TOOL_DIR)/kernel

OS_KERNEL_DIR   := hal/os/linux/kernel
ARCH_KERNEL_DIR := arch/XAQ2/hal/kernel
HAL_KERNEL_DIR  := hal/kernel

EXTRA_CFLAGS += -Werror

OBJS := $(OS_KERNEL_DIR)/gc_hal_kernel_debug.o \
        $(OS_KERNEL_DIR)/gc_hal_kernel_device.o \
        $(OS_KERNEL_DIR)/gc_hal_kernel_driver.o \
        $(OS_KERNEL_DIR)/gc_hal_kernel_linux.o \
        $(OS_KERNEL_DIR)/gc_hal_kernel_os.o

ifeq ($(USE_3D_VG), 1)

OBJS += $(HAL_KERNEL_DIR)/gc_hal_kernel.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_command.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_event.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_heap.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_mmu.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_video_memory.o

OBJS += $(ARCH_KERNEL_DIR)/gc_hal_kernel_hardware.o

else

OBJS += $(HAL_KERNEL_DIR)/gc_hal_kernel.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_command.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_heap.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_interrupt.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_mmu.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_video_memory.o

OBJS += $(ARCH_KERNEL_DIR)/gc_hal_kernel_hardware.o \
        $(ARCH_KERNEL_DIR)/gc_hal_kernel_hardware_command.o

endif


EXTRA_CFLAGS += -DLINUX -DDRIVER

ifeq ($(ENUM_WORKAROUND), 1)
EXTRA_CFLAGS += -DENUM_WORKAROUND=1
else
EXTRA_CFLAGS += -DENUM_WORKAROUND=0
endif

ifeq ($(FLAREON),1)
EXTRA_CFLAGS += -DFLAREON
endif

ifeq ($(DEBUG), 1)
EXTRA_CFLAGS += -DDBG=1 -DDEBUG -D_DEBUG
else
EXTRA_CFLAGS += -DDBG=0
endif

ifeq ($(NO_DMA_COHERENT), 1)
EXTRA_CFLAGS += -DNO_DMA_COHERENT
endif

ifeq ($(ENABLE_ARM_L2_CACHE), 1)
EXTRA_CFLAGS += -DENABLE_ARM_L2_CACHE=1
else
EXTRA_CFLAGS += -DENABLE_ARM_L2_CACHE=0
endif

ifeq ($(CONFIG_DOVE_GPU), 1)
EXTRA_CFLAGS += -DCONFIG_DOVE_GPU=1
endif

ifeq ($(gcdNO_POWER_MANAGEMENT), 1)
EXTRA_CFLAGS += -DgcdNO_POWER_MANAGEMENT=1
else
EXTRA_CFLAGS += -DgcdNO_POWER_MANAGEMENT=0
endif

ifneq ($(USE_PLATFORM_DRIVER), 0)
EXTRA_CFLAGS += -DUSE_PLATFORM_DRIVER=1
else
EXTRA_CFLAGS += -DUSE_PLATFORM_DRIVER=0
endif

ifeq ($(USE_PROFILER), 1)
EXTRA_CFLAGS += -DVIVANTE_PROFILER=1
else
EXTRA_CFLAGS += -DVIVANTE_PROFILER=0
endif

ifeq ($(ANDROID), 1)
EXTRA_CFLAGS += -DANDROID=1
endif

ifeq ($(ENABLE_GPU_CLOCK_BY_DRIVER), 1)
EXTRA_CFLAGS += -DENABLE_GPU_CLOCK_BY_DRIVER=1
else
EXTRA_CFLAGS += -DENABLE_GPU_CLOCK_BY_DRIVER=0
endif

ifeq ($(USE_NEW_LINUX_SIGNAL), 1)
EXTRA_CFLAGS += -DUSE_NEW_LINUX_SIGNAL=1
else
EXTRA_CFLAGS += -DUSE_NEW_LINUX_SIGNAL=0
endif

ifeq ($(NO_USER_DIRECT_ACCESS_FROM_KERNEL), 1)
EXTRA_CFLAGS += -DNO_USER_DIRECT_ACCESS_FROM_KERNEL=1
else
EXTRA_CFLAGS += -DNO_USER_DIRECT_ACCESS_FROM_KERNEL=0
endif

ifeq ($(gcdkREPORT_VIDMEM_USAGE), 1)
EXTRA_CFLAGS += -DgcdkREPORT_VIDMEM_USAGE=1
else
EXTRA_CFLAGS += -DgcdkREPORT_VIDMEM_USAGE=0
endif

EXTRA_CFLAGS += -I$(AQROOT)/hal/inc
EXTRA_CFLAGS += -I$(AQROOT)/hal/kernel
EXTRA_CFLAGS += -I$(AQARCH)/hal/kernel
EXTRA_CFLAGS += -I$(AQARCH)/cmodel/inc
EXTRA_CFLAGS += -I$(AQROOT)/hal/user

obj-$(CONFIG_VIVANTE) += galcore.o
galcore-objs  := $(OBJS)

