########################################################################### ###
#@Copyright     Copyright (c) Imagination Technologies Ltd. All Rights Reserved
#@License       Dual MIT/GPLv2
# 
# The contents of this file are subject to the MIT license as set out below.
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# Alternatively, the contents of this file may be used under the terms of
# the GNU General Public License Version 2 ("GPL") in which case the provisions
# of GPL are applicable instead of those above.
# 
# If you wish to allow use of your version of this file only under the terms of
# GPL, and not to allow others to use your version of this file under the terms
# of the MIT license, indicate your decision by deleting the provisions above
# and replace them with the notice and other provisions required by GPL as set
# out in the file called "GPL-COPYING" included in this distribution. If you do
# not delete the provisions above, a recipient may use your version of this file
# under the terms of either the MIT license or GPL.
# 
# This License is also included in this distribution in the file called
# "MIT-COPYING".
# 
# EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
# PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
# BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
# PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
### ###########################################################################

include ../config/preconfig.mk

# If a TARGET_PRODUCT is specified but not a TARGET_DEVICE, try to
# derive the TARGET_DEVICE from TARGET_PRODUCT.
#
ifeq ($(TARGET_DEVICE),)
override TARGET_DEVICE := \
 $(patsubst mini_%,%,$(patsubst full_%,%,$(TARGET_PRODUCT)))
ifeq ($(TARGET_DEVICE),arm64_img)
override TARGET_DEVICE := arm64
endif
ifeq ($(TARGET_DEVICE),armv7a_neon_img)
override TARGET_DEVICE := armv7-a-neon
endif
endif

ifeq ($(TARGET_DEVICE),)
# Prefer arm64 now for MULTIARCH=1 builds.
# Use TARGET_DEVICE=armv7-a-neon for 32-bit only builds.
override TARGET_DEVICE := arm64
endif

ifeq ($(TARGET_DEVICE),armv7-a-neon)
# The 32-bit only Android build is still using separate toolchains for
# kernel and userspace, force this on the DDK side when it's detected.
KERNEL_CROSS_COMPILE ?= arm-eabi-
endif

ifeq ($(MULTIARCH),1)
override TARGET_DEVICE := rk3368
else
override TARGET_DEVICE := rk3368_32
endif
HAL_VARIANT ?= rk3368
PVR_SYSTEM := rk3368
NO_HARDWARE := 0
LDM_PLATFORM := 1
#RGX_BVNC ?= 1.75.2.30
#rockchip G6110
RGX_BVNC ?= 5.9.1.46

include ../common/android/paths.mk
include ../common/android/arch.mk
include ../common/android/features.mk

ifeq ($(SUPPORT_ADF),1)
ifneq ($(is_future_version),1)
SYS_INCLUDES += \
 -isystem $(ANDROID_ROOT)/device/img/generic/adf/libadf/include \
 -isystem $(ANDROID_ROOT)/device/img/generic/adf/libadfhwc/include
endif
endif

PVR_LDM_PLATFORM_PRE_REGISTERED := 1

#zxl: build adf_fbdev.ko
ifeq ($(SUPPORT_ADF),1)
ADF_FBDEV ?= adf_fbdev
KERNEL_COMPONENTS += $(ADF_FBDEV)
endif

ifneq ($(SUPPORT_ADF),1)
ifeq ($(SUPPORT_DC),1)
DISPLAY_CONTROLLER ?= dc_fbdev
KERNEL_COMPONENTS += $(DISPLAY_CONTROLLER)
ifeq ($(DISPLAY_CONTROLLER),dc_example)
DC_EXAMPLE_WIDTH ?= 640
DC_EXAMPLE_HEIGHT ?= 480
DC_EXAMPLE_BIT_DEPTH ?= 32
DC_EXAMPLE_DPI ?= 160
endif
endif
endif

#zxl: 3 fb buffer
DC_FBDEV_NUM_PREFERRED_BUFFERS := 3

CACHEFLUSH_TYPE ?= CACHEFLUSH_GENERIC

include ../config/core.mk
include ../common/3rdparty.mk
include ../common/android/extra_config.mk
