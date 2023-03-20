########################################################################### ###
#@Title         Root kernel makefile
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

# This top-level kbuild makefile builds all the Linux kernel modules in the
# DDK. To run kbuild, this makefile is copied to $(TARGET_PRIMARY_OUT)/kbuild/Makefile
# and make is invoked in $(TARGET_PRIMARY_OUT)/kbuild.

# This makefile doesn't define any kbuild special variables apart from
# ccflags-y and obj-m. The variables for objects are picked up by including
# the kbuild makefile fragments named in $(INTERNAL_KBUILD_MAKEFILES). The
# list of objects that these fragments make is collected in
# $(INTERNAL_KBUILD_OBJECTS) and $(INTERNAL_EXTRA_KBUILD_OBJECTS). These
# variables are set according to the build's $(KERNEL_COMPONENTS) and
# $(EXTRA_PVRSRVKM_COMPONENTS). To add a new kernel module to the build, edit
# these variables in the per-build Makefile.

INTERNAL_KBUILD_OBJECTS=pvrsrvkm.o
INTERNAL_EXTRA_KBUILD_OBJECTS=""
TOP:=$(dir $(realpath $(lastword $(MAKEFILE_LIST))))
TOP:=$(TOP:/=)
BRIDGE_SOURCE_ROOT=$(TOP)/generated/rogue
TARGET_PRIMARY_ARCH=target_riscv64
PVR_ARCH=rogue
PVR_ARCH_DEFS=rogue
PVR_SYSTEM := sf_7110
#PDUMP ?= 1
BUILD ?= release
RGX_BVNC ?= 36.50.54.182
RGX_BNC ?= 36.50.54.182
PVRSRV_NEED_PVR_DPF=1
PVRSRV_NEED_PVR_ASSERT=1
PVR_SERVICES_DEBUG=1
WINDOW_SYSTEM=nulldrmws

#include $(OUT)/config_kernel.mk
include $(srctree)/$(src)/config_kernel.mk

.SECONDARY:

define symlink-source-file
@if [ ! -e $(dir $@) ]; then mkdir -p $(dir $@); fi
@if [ ! -h $@ ]; then ln -sf $< $@; fi
endef

bridge_base := $(BRIDGE_SOURCE_ROOT)

#$(OUT)/$(TARGET_PRIMARY_ARCH)/kbuild/%.c: $(TOP)/%.c
#	$(symlink-source-file)

#$(OUT)/$(TARGET_PRIMARY_ARCH)/kbuild/generated/$(PVR_ARCH)/%.c: $(bridge_base)/%.c
#	$(symlink-source-file)

#$(OUT)/$(TARGET_PRIMARY_ARCH)/kbuild/external/%.c: $(abspath $(srctree))/%.c
#	$(symlink-source-file)

#ccflags-y += -D__linux__ -include $(OUT)/config_kernel.h \
# -include kernel_config_compatibility.h \
# -I$(OUT)/include -I$(TOP)/kernel/drivers/staging/imgtec
ccflags-y += -D__linux__ -include $(srctree)/$(src)/config_kernel.h \
 -include $(srctree)/drivers/gpu/drm/img/kernel_config_compatibility.h \
 -I$(OUT)/include \
 -I$(srctree)/drivers/gpu/drm/img \
 -I$(srctree)/$(src)/hwdefs/rogue \
 -I$(srctree)/$(src)/hwdefs/rogue/km

include $(srctree)/$(src)/services/server/env/linux/Kbuild.mk
include $(srctree)/$(src)/services/system/rogue/sf_7110/Kbuild.mk

#include $(INTERNAL_KBUILD_MAKEFILES)

define add-file-cflags
CFLAGS_$(notdir $(1)) := $(CFLAGS_$(1))
endef

# Define old style CFLAG_ variables for kernels older than 5.4
$(foreach _m,$(INTERNAL_KBUILD_OBJECTS:.o=-y), \
 $(foreach _f,$($(_m)), \
  $(if $(CFLAGS_$(_f)), \
   $(eval $(call add-file-cflags,$(_f))))))

#ifneq ($(KERNEL_DRIVER_DIR),)
# ccflags-y += \
#   -I$(abspath $(srctree))/$(KERNEL_DRIVER_DIR)/$(PVR_SYSTEM) \
#   -I$(abspath $(srctree))/$(KERNEL_DRIVER_DIR)
#endif

$(if $($(PVRSRVKM_NAME)-y),,$(warning $(PVRSRVKM_NAME)-y was empty, which could mean that srvkm is missing from $$(KERNEL_COMPONENTS)))
$(PVRSRVKM_NAME)-y += $(foreach _m,$(INTERNAL_EXTRA_KBUILD_OBJECTS:.o=),$($(_m)-y))

#obj-m += $(INTERNAL_KBUILD_OBJECTS)
obj-$(CONFIG_DRM_IMG_ROGUE) += $(INTERNAL_KBUILD_OBJECTS)
