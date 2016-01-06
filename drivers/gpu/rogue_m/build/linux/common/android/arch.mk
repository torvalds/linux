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

include ../common/android/platform_version.mk

# Now we have included the platform_version.mk file, we know we have a
# correctly configured OUT_DIR and can probe it to figure out our
# architecture. For backwards compatibility with KitKat, use the deprecated
# ro.product.cpu.abi (primary architecture) property instead of abilist64.

$(eval $(subst #,$(newline),$(shell cat $(BUILD_PROP) | \
    grep '^ro.product.cpu.abi=\|^ro.product.cpu.abilist32=' | \
    sed -e 's,ro.product.cpu.abi=,JNI_CPU_ABI=,' \
        -e 's,ro.product.cpu.abilist32=,JNI_CPU_ABI_2ND=,' | \
    tr ',' ' ' | tr '\n' '#')))

# If ARCH is set, use that to remap to an "Android" ARCH..
ANDROID_ARCH := $(filter arm arm64 mips mips64 x86 x86_64,$(ARCH))

# x86 is special and has another legacy ARCH name which is remapped
ifeq ($(ARCH),i386)
ANDROID_ARCH := x86
endif

ifeq ($(ANDROID_ARCH),)
# ..otherwise, try to use the ABI list to figure it out.
ifneq ($(filter armeabi-v7a armeabi,$(JNI_CPU_ABI)),)
ANDROID_ARCH=arm
else ifneq ($(filter arm64-v8a,$(JNI_CPU_ABI)),)
ANDROID_ARCH=arm64
else ifneq ($(filter mips,$(JNI_CPU_ABI)),)
ANDROID_ARCH=mips
else ifneq ($(filter mips64,$(JNI_CPU_ABI)),)
ANDROID_ARCH=mips64
else ifneq ($(filter x86,$(JNI_CPU_ABI)),)
ANDROID_ARCH=x86
else ifneq ($(filter x86_64,$(JNI_CPU_ABI)),)
ANDROID_ARCH=x86_64
else
$(error ARCH not set and JNI_CPU_ABI=$(JNI_CPU_ABI) was not remappable)
endif
endif

JNI_CPU_ABI := $(word 1,$(JNI_CPU_ABI))
JNI_CPU_ABI_2ND := $(word 1,$(JNI_CPU_ABI_2ND))

include ../common/android/arch_common.mk

ifneq ($(filter arm arm64 mips mips64,$(ANDROID_ARCH)),)
LDM_PLATFORM ?= 1
endif

ifneq ($(filter x86 x86_64,$(ANDROID_ARCH)),)
KERNEL_CROSS_COMPILE ?= undef
endif

ifneq ($(filter arm64 mips64 x86_64,$(ANDROID_ARCH)),)
PVR_ANDROID_ARCH_IS_64BIT := 1
ifeq ($(MULTIARCH),)
$(warning *** 64-bit architecture detected. Enabling MULTIARCH=1.)
$(warning *** If you want a 64-bit only build, use MULTIARCH=64only.)
export MULTIARCH := 1
endif
endif
