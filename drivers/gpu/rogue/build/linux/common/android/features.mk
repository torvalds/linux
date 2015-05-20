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

# Basic support option tuning for Android
#
SUPPORT_ANDROID_PLATFORM := 1
SUPPORT_OPENGLES1_V1_ONLY := 1
DONT_USE_SONAMES := 1

# Always print debugging after 5 seconds of no activity
#
CLIENT_DRIVER_DEFAULT_WAIT_RETRIES := 50

# Android WSEGL is always the same
#
OPK_DEFAULT := libpvrANDROID_WSEGL.so

# srvkm is always built
#
KERNEL_COMPONENTS := srvkm

# Kernel modules are always installed here under Android
#
# Time:2014-08-26 
# Note:change module's path from /system/modules/ to /system/lib/modules/
# Modifier: zxl
PVRSRV_MODULE_BASEDIR := /system/lib/modules/

# Enable secure FD export in Services
#
SUPPORT_SECURE_EXPORT := 1

# It is no longer supported disable this for Android, but we can still
# do so for the Linux DDK, so don't use NonTunableOption.
#
override SUPPORT_ION := 1

# Show GPU activity in systrace
#
SUPPORT_GPUTRACE_EVENTS ?= 1

##############################################################################
# Unless overridden by the user, assume the RenderScript Compute API level
# matches that of the SDK API_LEVEL.
#
RSC_API_LEVEL ?= $(API_LEVEL)
ifeq ($(RSC_API_LEVEL),21)
RSC_API_LEVEL := 20
endif

##############################################################################
# JB MR1 introduces cross-process syncs associated with a fd.
# This requires a new enough kernel version to have the base/sync driver.
#
#zxl:Temporarily closed for user build mode(Depend on libgui: LOCAL_CFLAGS += -DDONT_USE_FENCE_SYNC)
EGL_EXTENSION_ANDROID_NATIVE_FENCE_SYNC ?= 1
PVR_ANDROID_NATIVE_WINDOW_HAS_SYNC ?= 1

##############################################################################
# Handle various platform includes for unittests
#
SYS_INCLUDES += \
 -isystem $(ANDROID_ROOT)/libnativehelper/include/nativehelper
SYS_KHRONOS_INCLUDES += \
 -I$(ANDROID_ROOT)/frameworks/native/opengl/include

##############################################################################
# Android doesn't use these install script variables. They're still in place
# because the Linux install scripts use them.
#
SHLIB_DESTDIR := not-used
EGL_DESTDIR := not-used

# Must give our EGL/GLES libraries a globally unique name
#
EGL_BASENAME_SUFFIX := _POWERVR_ROGUE

# Use stlport instead of libstdc++ for STL compatibility
#
SYS_CXXFLAGS := -fuse-cxa-atexit $(SYS_CFLAGS)
SYS_INCLUDES += \
 -isystem $(ANDROID_ROOT)/bionic \
 -isystem $(ANDROID_ROOT)/external/stlport/stlport

##############################################################################
# ICS requires that at least one driver EGLConfig advertises the
# EGL_RECORDABLE_ANDROID attribute. The platform requires that surfaces
# rendered with this config can be consumed by an OMX video encoder.
#
EGL_EXTENSION_ANDROID_RECORDABLE := 1

##############################################################################
# ICS added the EGL_ANDROID_blob_cache extension. Enable support for this
# extension in EGL/GLESv2.
#
EGL_EXTENSION_ANDROID_BLOB_CACHE ?= 1

##############################################################################
# Framebuffer target extension is used to find configs compatible with
# the framebuffer
#
EGL_EXTENSION_ANDROID_FRAMEBUFFER_TARGET := 1

##############################################################################
# Disable the MEMINFO wrapper pvCpuVirtAddr feature. All Android DDK
# components no longer require it. This enables lazy CPU mappings, which
# improves allocation performance.
#
ifneq ($(PDUMP),1)
PVRSRV_NO_MEMINFO_CPU_VIRT_ADDR ?= 1
endif

##############################################################################
# JB added a new corkscrew API for userland backtracing.
#
ifeq ($(is_at_least_lollipop),0)
PVR_ANDROID_HAS_CORKSCREW_API := 1
endif

##############################################################################
# This is currently broken on KK. Disable until fixed.
#
SUPPORT_ANDROID_APPHINTS := 0

##############################################################################
# KitKat added very provisional/early support for sRGB render targets
#
# (Leaving this optional until the framework makes it mandatory.)
#
PVR_ANDROID_HAS_HAL_PIXEL_FORMAT_sRGB ?= 1

##############################################################################
# Switch on ADF support for KitKat MR1 or newer.
#
# Customers using AOSP KitKat MR1 sources need to copy and build the libadf
# and libadfhwc libraries from AOSP master system/core into their device/
# directories and build the components as dynamic libraries. Examples of how
# to do this are shown in the bundled 'pc_android' and 'generic_arm_android'
# directories in the device package.
#
# Customers using AOSP master do not need to make any changes.
# ADF requires kernel/common derivative kernels >= 3.10.
#
ifeq ($(is_at_least_kitkat_mr1),1)
#SUPPORT_ADF ?= 1
#SUPPORT_DC ?= 1
endif

##############################################################################
# Versions of Android between Cupcake and KitKat MR1 required Java 6.
#
ifeq ($(is_at_least_lollipop),0)
LEGACY_USE_JAVA6 ?= 1
endif

##############################################################################
# Versions of Android between ICS and KitKat MR1 used ion .heap_mask instead
# of .heap_id_mask.
#
ifeq ($(is_at_least_lollipop),0)
PVR_ANDROID_HAS_ION_FIELD_HEAP_MASK := 1
endif

##############################################################################
# Lollipop supports 64-bit. Configure BCC to emit both 32-bit and 64-bit LLVM
# bitcode in the renderscript driver.
#
ifeq ($(is_at_least_lollipop),1)
PVR_ANDROID_BCC_MULTIARCH_SUPPORT := 1
endif

##############################################################################
# Lollipop annotates the cursor allocation with USAGE_CURSOR to enable it to
# be accelerated with special cursor hardware (rather than wasting an
# overlay). This flag stops the DDK from blocking the allocation.
#
ifeq ($(is_at_least_lollipop),1)
PVR_ANDROID_HAS_GRALLOC_USAGE_CURSOR := 1
endif

##############################################################################
# Lollipop changed the camera HAL metadata specification to require that
# CONTROL_MAX_REGIONS specifies 3 integers (instead of 1).
#
ifeq ($(is_at_least_lollipop),1)
PVR_ANDROID_CAMERA_CONTROL_MAX_REGIONS_HAS_THREE := 1
endif

# Placeholder for future version handling
#
ifeq ($(is_future_version),1)
-include ../common/android/future_version.mk
endif
