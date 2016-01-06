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
ifneq ($(filter kylin_perf kylin_p1,$(TARGET_DEVICE)),)
override TARGET_DEVICE := sun9i
endif
endif

ifeq ($(TARGET_DEVICE),)
override TARGET_DEVICE := sun9i
endif

HAL_VARIANT := sun9i
PVR_SYSTEM := rgx_sunxi
LDM_PLATFORM := 1
SUPPORT_ADF := 1

RGX_BVNC = 1.75.2.30

KERNEL_CROSS_COMPILE ?= arm-eabi-

include ../common/android/paths.mk
include ../common/android/arch.mk
include ../common/android/features.mk

ION_DEFAULT_HEAP_ID_MASK := (1 << ION_HEAP_TYPE_DMA)
ION_FALLBACK_HEAP_ID_MASK := (1 << ION_HEAP_TYPE_CARVEOUT)
ifneq ($(is_at_least_lollipop),1)
SYS_INCLUDES += \
 -isystem $(ANDROID_ROOT)/device/img/sun9i/adf/libadf/include \
 -isystem $(ANDROID_ROOT)/device/img/sun9i/adf/libadfhwc/include
endif

DISPLAY_CONTROLLER ?= adf_sunxi

KERNEL_COMPONENTS += $(DISPLAY_CONTROLLER)

CACHEFLUSH_TYPE ?= CACHEFLUSH_GENERIC

PDUMP_STREAMBUF_SIZE_MB := 10

include ../config/core.mk
include ../common/android/extra_config.mk
