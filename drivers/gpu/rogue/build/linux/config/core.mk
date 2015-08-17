########################################################################### ###
#@File
#@Title         Root build configuration.
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

# Configuration wrapper for new build system. This file deals with
# configuration of the build. Add to this file anything that deals
# with switching driver options on/off and altering the defines or
# objects the build uses.
#
# At the end of this file is an exhaustive list of all variables
# that are passed between the platform/config stage and the generic
# build. PLEASE refrain from adding more variables than necessary
# to this stage -- almost all options can go through config.h.
#

# Sanity check: Make sure preconfig has been included
ifeq ($(TOP),)
$(error TOP not defined: Was preconfig.mk included in root makefile?)
endif

################################# MACROS ####################################

ALL_TUNABLE_OPTIONS :=

# This records the config option's help text and default value. Note that
# the help text can't contain a literal comma. Use $(comma) instead.
define RegisterOptionHelp
ALL_TUNABLE_OPTIONS += $(1)
ifeq ($(INTERNAL_DESCRIPTION_FOR_$(1)),)
INTERNAL_DESCRIPTION_FOR_$(1) := $(3)
endif
INTERNAL_CONFIG_DEFAULT_FOR_$(1) := $(2)
$(if $(4),\
	$(error Too many arguments in config option '$(1)' (stray comma in help text?)))
endef

# Write out a kernel GNU make option.
#
define KernelConfigMake
$$(shell echo "override $(1) := $(2)" >>$(CONFIG_KERNEL_MK).new)
$(if $(filter config,$(D)),$(info KernelConfigMake $(1) := $(2) 	# $(if $($(1)),$(origin $(1)),default)))
endef

# Write out a GNU make option for both user & kernel
#
define BothConfigMake
$$(eval $$(call KernelConfigMake,$(1),$(2)))
$$(eval $$(call UserConfigMake,$(1),$(2)))
endef

# Conditionally write out a kernel GNU make option
#
define _TunableKernelConfigMake
ifneq ($$($(1)),)
ifneq ($$($(1)),0)
$$(eval $$(call KernelConfigMake,$(1),$$($(1))))
endif
else
ifneq ($(2),)
$$(eval $$(call KernelConfigMake,$(1),$(2)))
endif
endif
endef

define TunableKernelConfigMake
$$(eval $$(call _TunableKernelConfigMake,$(1),$(2)))
$(call RegisterOptionHelp,$(1),$(2),$(3),$(4))
endef

# Conditionally write out a GNU make option for both user & kernel
#
define TunableBothConfigMake
$$(eval $$(call _TunableKernelConfigMake,$(1),$(2)))
$$(eval $$(call _TunableUserConfigMake,$(1),$(2)))
$(call RegisterOptionHelp,$(1),$(2),$(3),$(4))
endef

# Write out a kernel-only option
#
define KernelConfigC
$$(shell echo "#define $(1) $(2)" >>$(CONFIG_KERNEL_H).new)
$(if $(filter config,$(D)),$(info KernelConfigC    #define $(1) $(2) 	/* $(if $($(1)),$(origin $(1)),default) */),)
endef

# Write out an option for both user & kernel
#
define BothConfigC
$$(eval $$(call KernelConfigC,$(1),$(2)))
$$(eval $$(call UserConfigC,$(1),$(2)))
endef

# Conditionally write out a kernel-only option
#
define _TunableKernelConfigC
ifneq ($$($(1)),)
ifneq ($$($(1)),0)
ifeq ($$($(1)),1)
$$(eval $$(call KernelConfigC,$(1),))
else
$$(eval $$(call KernelConfigC,$(1),$$($(1))))
endif
endif
else
ifneq ($(2),)
ifeq ($(2),1)
$$(eval $$(call KernelConfigC,$(1),))
else
$$(eval $$(call KernelConfigC,$(1),$(2)))
endif
endif
endif
endef

define TunableKernelConfigC
$$(eval $$(call _TunableKernelConfigC,$(1),$(2)))
ALL_TUNABLE_OPTIONS += $(1)
ifeq ($(INTERNAL_DESCRIPTION_FOR_$(1)),)
INTERNAL_DESCRIPTION_FOR_$(1) := $(3)
endif
INTERNAL_CONFIG_DEFAULT_FOR_$(1) := $(2)
endef

# Conditionally write out an option for both user & kernel
#
define TunableBothConfigC
$$(eval $$(call _TunableKernelConfigC,$(1),$(2)))
$$(eval $$(call _TunableUserConfigC,$(1),$(2)))
$(call RegisterOptionHelp,$(1),$(2),$(3),$(4))
endef

# Use this to mark config options which have to exist, but aren't
# user-tunable. Warn if an attempt is made to change it.
#
define NonTunableOption
$(if $(filter command line environment,$(origin $(1))),\
	$(error Changing '$(1)' is not supported))
endef

############################### END MACROS ##################################

# Check we have a new enough version of GNU make.
#
need := 3.81
ifeq ($(filter $(need),$(firstword $(sort $(MAKE_VERSION) $(need)))),)
$(error A version of GNU make >= $(need) is required - this is version $(MAKE_VERSION))
endif

# Decide whether we need a BVNC
ifneq ($(COMPILER_BVNC_LIST),)
 DONT_NEED_RGX_BVNC := 1
endif

include ../defs.mk

# Infer PVR_BUILD_DIR from the directory configuration is launched from.
# Check anyway that such a directory exists.
#
PVR_BUILD_DIR := $(notdir $(abspath .))
$(call directory-must-exist,$(TOP)/build/linux/$(PVR_BUILD_DIR))

# Output directory for configuration, object code,
# final programs/libraries, and install/rc scripts.
#
BUILD        ?= release
ifneq ($(filter $(WINDOW_SYSTEM),xorg wayland nullws nulldrmws ews_drm nulladfws ews_adf),)
OUT          ?= $(TOP)/binary_$(PVR_BUILD_DIR)_$(WINDOW_SYSTEM)_$(BUILD)
else
OUT          ?= $(TOP)/binary_$(PVR_BUILD_DIR)_$(BUILD)
endif
override OUT := $(if $(filter /%,$(OUT)),$(OUT),$(TOP)/$(OUT))

CONFIG_MK			:= $(OUT)/config.mk
CONFIG_H			:= $(OUT)/config.h
CONFIG_KERNEL_MK	:= $(OUT)/config_kernel.mk
CONFIG_KERNEL_H		:= $(OUT)/config_kernel.h

# Convert commas to spaces in $(D). This is so you can say "make
# D=config-changes,freeze-config" and have $(filter config-changes,$(D))
# still work.
override D := $(subst $(comma),$(space),$(D))

# Create the OUT directory 
#
$(shell mkdir -p $(OUT))

# Some targets don't need information about any modules. If we only specify
# these targets on the make command line, set INTERNAL_CLOBBER_ONLY to
# indicate that toplevel.mk shouldn't read any makefiles
CLOBBER_ONLY_TARGETS := clean clobber help install
INTERNAL_CLOBBER_ONLY :=
ifneq ($(strip $(MAKECMDGOALS)),)
INTERNAL_CLOBBER_ONLY := \
$(if \
 $(strip $(foreach _cmdgoal,$(MAKECMDGOALS),\
          $(if $(filter $(_cmdgoal),$(CLOBBER_ONLY_TARGETS)),,x))),,true)
endif

# For a clobber-only build, we shouldn't regenerate any config files
ifneq ($(INTERNAL_CLOBBER_ONLY),true)

-include ../config/user-defs.mk

#
# Core handling 


# delete any previous intermediary files
$(shell \
	for file in $(CONFIG_KERNEL_H).new $(CONFIG_KERNEL_MK).new ; do \
		rm -f $$file; \
	done)

ifeq ($(DONT_NEED_RGX_BVNC),)
 # Extract the BNC config name
 RGX_BNC_SPLIT := $(subst .,$(space) ,$(RGX_BVNC))
 RGX_BNC := $(word 1,$(RGX_BNC_SPLIT)).V.$(word 3,$(RGX_BNC_SPLIT)).$(word 4,$(RGX_BNC_SPLIT))
 
 # Check BVNC core version
 ALL_KM_BVNCS := \
  $(patsubst rgxcore_km_%.h,%,\
 	$(notdir $(shell ls $(TOP)/hwdefs/km/cores/rgxcore_km_*.h)))
 ifeq ($(filter $(RGX_BVNC),$(ALL_KM_BVNCS)),)
 $(error Error: Invalid Kernel core RGX_BVNC=$(RGX_BVNC). \
 	Valid Kernel core BVNCs: $(subst $(space),$(comma)$(space),$(ALL_KM_BVNCS)))
 endif
 
 # Check if BVNC core file exist
 RGX_BVNC_CORE_KM := $(TOP)/hwdefs/km/cores/rgxcore_km_$(RGX_BVNC).h
 RGX_BVNC_CORE_KM_HEADER := \"cores/rgxcore_km_$(RGX_BVNC).h\" 
 # "rgxcore_km_$(RGX_BVNC).h"
 ifeq ($(wildcard $(RGX_BVNC_CORE_KM)),)
 $(error The file $(RGX_BVNC_CORE_KM) does not exist. \
 	Valid BVNCs: $(ALL_KM_BVNCS))
 endif
 
 # Check BNC config version
 ALL_KM_BNCS := \
  $(patsubst rgxconfig_km_%.h,%,\
 	$(notdir $(shell ls $(TOP)/hwdefs/km/configs/rgxconfig_km_*.h)))
 ifeq ($(filter $(RGX_BNC),$(ALL_KM_BNCS)),)
 $(error Error: Invalid Kernel config RGX_BNC=$(RGX_BNC). \
 	Valid Kernel config BNCs: $(subst $(space),$(comma)$(space),$(ALL_KM_BNCS)))
 endif
 
 # Check if BNC config file exist
 RGX_BNC_CONFIG_KM := $(TOP)/hwdefs/km/configs/rgxconfig_km_$(RGX_BNC).h
 RGX_BNC_CONFIG_KM_HEADER := \"configs/rgxconfig_km_$(RGX_BNC).h\" 
 #"rgxcore_km_$(RGX_BNC).h"
 ifeq ($(wildcard $(RGX_BNC_CONFIG_KM)),)
 $(error The file $(RGX_BNC_CONFIG_KM) does not exist. \
 	Valid BNCs: $(ALL_KM_BNCS))
 endif
endif

# Enforced dependencies. Move this to an include.
#
SUPPORT_LINUX_USING_WORKQUEUES ?= 1
ifeq ($(SUPPORT_LINUX_USING_WORKQUEUES),1)
override PVR_LINUX_USING_WORKQUEUES := 1
override PVR_LINUX_MISR_USING_PRIVATE_WORKQUEUE := 1
override PVR_LINUX_TIMERS_USING_WORKQUEUES := 1
else ifeq ($(SUPPORT_LINUX_USING_SHARED_WORKQUEUES),1)
override PVR_LINUX_USING_WORKQUEUES := 1
override PVR_LINUX_MISR_USING_WORKQUEUE := 1
override PVR_LINUX_TIMERS_USING_SHARED_WORKQUEUE := 1
endif

ifeq ($(NO_HARDWARE),1)
override SYS_USING_INTERRUPTS := 0
endif

# Rather than requiring the user to have to define two variables (one quoted,
# one not), make PVRSRV_MODNAME a non-tunable and give it an overridable
# default here.
#
PVRSRV_MODNAME ?= pvrsrvkm

MAX_POOL_PAGES ?= 10240

# Normally builds don't touch these, but we use them to influence the
# components list. Make sure these are defined early enough to make this
# possible.
#
ifeq ($(SUPPORT_DRM),1)
SUPPORT_DISPLAY_CLASS := 0
else
ifeq ($(SUPPORT_ADF),1)
SUPPORT_DISPLAY_CLASS := 0
else
ifeq ($(SUPPORT_DC),1)
SUPPORT_DISPLAY_CLASS ?= 1
else
endif
endif
endif

ifeq ($(DONT_NEED_RGX_BVNC),)
# we can only do this stuff if we have a BVNC

 SUPPORT_RAY_TRACING := \
  $(shell grep -qw RGX_FEATURE_RAY_TRACING $(RGX_BNC_CONFIG_KM) && echo 1)

 SUPPORT_DMA :=\
  $(shell grep -qw RGX_FEATURE_META_DMA_CHANNEL_COUNT $(RGX_BNC_CONFIG_KM) && echo 1)
endif

# Default place for shared libraries
SHLIB_DESTDIR ?= /usr/lib

# Build's selected list of components.
# - components.mk is a per-build file that specifies the components that are
#   to be built
-include components.mk

# Set up the host and target compiler.
include ../config/compiler.mk

# PDUMP needs extra components
#
ifeq ($(PDUMP),1)
ifneq ($(COMPONENTS),)
COMPONENTS += pdump
endif
ifeq ($(SUPPORT_DRM),1)
EXTRA_PVRSRVKM_COMPONENTS += dbgdrv
else
KERNEL_COMPONENTS += dbgdrv
endif
endif

# HWPerf KM Interface example
#
ifeq ($(SUPPORT_KERNEL_HWPERF_TEST),1)
KERNEL_COMPONENTS += rgxhwpdrv
endif

# PVRGDB needs extra components
#
ifeq ($(PVRGDB),1)
ifneq ($(COMPONENTS),)
COMPONENTS += pvrdebugger pvrgdb pvrdebugipc
endif
override SUPPORT_EXPORTING_MEMORY_CONTEXT := 1
endif

$(if $(filter config,$(D)),$(info Build configuration:))

################################# CONFIG ####################################

ifneq ($(SUPPORT_NEUTRINO_PLATFORM), 1)

# If KERNELDIR is set, write it out to the config.mk, with
# KERNEL_COMPONENTS and KERNEL_ID
#
ifneq ($(strip $(KERNELDIR)),)
PVRSRV_MODULE_BASEDIR ?= /lib/modules/$(KERNEL_ID)/extra/
$(eval $(call BothConfigMake,KERNELDIR,$(KERNELDIR)))
$(eval $(call BothConfigMake,KERNEL_ID,$(KERNEL_ID)))
$(eval $(call KernelConfigMake,KERNEL_COMPONENTS,$(KERNEL_COMPONENTS)))
$(eval $(call TunableKernelConfigMake,EXTRA_PVRSRVKM_COMPONENTS,,\
List of components that should be built in to pvrsrvkm.ko$(comma) rather than_\
forming separate kernel modules._\
))

# If KERNEL_CROSS_COMPILE is set to "undef", this is magically
# equivalent to being unset. If it is unset, we use CROSS_COMPILE
# (which might also be unset). If it is set, use it directly.
ifneq ($(KERNEL_CROSS_COMPILE),undef)
KERNEL_CROSS_COMPILE ?= $(CROSS_COMPILE)
$(eval $(call TunableBothConfigMake,KERNEL_CROSS_COMPILE,))
endif

# Check the KERNELDIR has a kernel built and also check that it is
# not 64-bit, which we do not support.
KERNEL_AUTOCONF := \
 $(strip $(wildcard $(KERNELDIR)/include/linux/autoconf.h) \
         $(wildcard $(KERNELDIR)/include/generated/autoconf.h))
ifeq ($(KERNEL_AUTOCONF),)
$(warning autoconf.h not found in $$(KERNELDIR)/include/linux \
or $$(KERNELDIR)/include/generated. Check your $$(KERNELDIR) variable \
and kernel configuration.)
endif
else
$(if $(KERNEL_COMPONENTS),$(warning KERNELDIR is not set. Kernel components cannot be built))
endif

endif # !Neutrino

# Normally this is off for Linux, and only used by Android, but if customers
# are testing their display engines using NULLADFWS, they need to enable it
# for dmabuf support under Linux. The sync header is needed by adf_pdp.
#
SUPPORT_ION ?= 0
ifneq ($(SUPPORT_ION),0)
# Support kernels built out-of-tree with O=/other/path
# In those cases, KERNELDIR will be O, not the source tree.
ifneq ($(wildcard $(KERNELDIR)/source),)
KSRCDIR := $(KERNELDIR)/source
else
KSRCDIR := $(KERNELDIR)
endif
ifneq ($(wildcard $(KSRCDIR)/drivers/staging/android/ion/ion.h),)
# The kernel has a more recent version of ion, located in drivers/staging.
# Change the default header paths and the behaviour wrt sg_dma_len.
PVR_ANDROID_ION_HEADER := \"../drivers/staging/android/ion/ion.h\"
PVR_ANDROID_ION_PRIV_HEADER := \"../drivers/staging/android/ion/ion_priv.h\"
PVR_ANDROID_ION_USE_SG_LENGTH := 1
endif
ifneq ($(wildcard $(KSRCDIR)/drivers/staging/android/sync.h),)
# The kernel has a more recent version of the sync driver, located in
# drivers/staging. Change the default header path.
PVR_ANDROID_SYNC_HEADER := \"../drivers/staging/android/sync.h\"
endif
$(eval $(call BothConfigMake,SUPPORT_ION,1))
$(eval $(call BothConfigC,SUPPORT_ION,))
$(eval $(call TunableKernelConfigC,PVR_ANDROID_ION_HEADER,\"linux/ion.h\"))
$(eval $(call TunableKernelConfigC,PVR_ANDROID_ION_PRIV_HEADER,\"../drivers/gpu/ion/ion_priv.h\"))
$(eval $(call TunableKernelConfigC,PVR_ANDROID_ION_USE_SG_LENGTH,))
$(eval $(call TunableKernelConfigC,PVR_ANDROID_SYNC_HEADER,\"linux/sync.h\"))
endif

$(eval $(call UserConfigC,PVRSRV_MODULE_BASEDIR,\"$(PVRSRV_MODULE_BASEDIR)\"))

# Ideally configured by platform Makefiles, as necessary
#

$(if $(USE_CCACHE),$(if $(USE_DISTCC),$(error\
Enabling both USE_CCACHE and USE_DISTCC at the same time is not supported)))

# Invariant options for Linux
#
$(eval $(call BothConfigC,LINUX,))

$(eval $(call BothConfigC,PVR_BUILD_DIR,"\"$(PVR_BUILD_DIR)\""))
$(eval $(call BothConfigC,PVR_BUILD_TYPE,"\"$(BUILD)\""))
$(eval $(call BothConfigC,PVRSRV_MODNAME,"\"$(PVRSRV_MODNAME)\""))
$(eval $(call BothConfigMake,PVRSRV_MODNAME,$(PVRSRV_MODNAME)))
$(eval $(call BothConfigMake,PVR_BUILD_DIR,$(PVR_BUILD_DIR)))
$(eval $(call BothConfigMake,PVR_BUILD_TYPE,$(BUILD)))

$(eval $(call BothConfigC,SUPPORT_RGX,1))
$(eval $(call UserConfigMake,SUPPORT_RGX,1))

$(eval $(call BothConfigC,PVR_SECURE_HANDLES,))
$(eval $(call UserConfigC,PVR_TLS_USE_GCC__thread_KEYWORD,))

ifneq ($(DISPLAY_CONTROLLER),)
$(eval $(call BothConfigC,DISPLAY_CONTROLLER,$(DISPLAY_CONTROLLER)))
$(eval $(call BothConfigMake,DISPLAY_CONTROLLER,$(DISPLAY_CONTROLLER)))
endif

$(eval $(call UserConfigC,OPK_DEFAULT,"\"$(OPK_DEFAULT)\""))
$(eval $(call UserConfigC,OPK_FALLBACK,"\"$(OPK_FALLBACK)\""))

$(eval $(call BothConfigMake,PVR_SYSTEM,$(PVR_SYSTEM)))

ifeq ($(MESA_EGL),1)
$(eval $(call UserConfigMake,LIB_IMG_EGL,pvr_dri_if))
else
$(eval $(call UserConfigMake,LIB_IMG_EGL,IMGegl))
endif

# Build-type dependent options
#
$(eval $(call BothConfigMake,BUILD,$(BUILD)))

ifeq ($(BUILD),debug)
PVR_RI_DEBUG ?= 1
SUPPORT_PAGE_FAULT_DEBUG ?= 1
$(eval $(call BothConfigC,DEBUG,))
$(eval $(call KernelConfigC,DEBUG_LINUX_MEMORY_ALLOCATIONS,))
$(eval $(call KernelConfigC,DEBUG_LINUX_MEM_AREAS,))
$(eval $(call KernelConfigC,DEBUG_LINUX_MMAP_AREAS,))
$(eval $(call KernelConfigC,DEBUG_BRIDGE_KM,))
$(eval $(call KernelConfigC,DEBUG_HANDLEALLOC_KM,))
$(eval $(call UserConfigC,DLL_METRIC,1))
$(eval $(call TunableBothConfigC,RGXFW_ALIGNCHECKS,1))
$(eval $(call TunableBothConfigC,PVRSRV_DEBUG_CCB_MAX,))
else ifeq ($(BUILD),release)
$(eval $(call BothConfigC,RELEASE,))
$(eval $(call TunableBothConfigMake,DEBUGLINK,1))
$(eval $(call TunableBothConfigC,RGXFW_ALIGNCHECKS,))
else ifeq ($(BUILD),timing)
$(eval $(call BothConfigC,TIMING,))
$(eval $(call UserConfigC,DLL_METRIC,1))
$(eval $(call TunableBothConfigMake,DEBUGLINK,1))
else
$(error BUILD= must be either debug, release or timing)
endif



# User-configurable options
#
ifeq ($(DONT_NEED_RGX_BVNC),)
 $(eval $(call TunableBothConfigC,RGX_BVNC_CORE_KM_HEADER,))
    $(eval $(call TunableBothConfigC,RGX_BNC_CONFIG_KM_HEADER,))
   endif

$(eval $(call TunableBothConfigC,SUPPORT_DBGDRV_EVENT_OBJECTS,1))
$(eval $(call TunableBothConfigC,PVR_DBG_BREAK_ASSERT_FAIL,,\
Enable this to treat PVR_DBG_BREAK as PVR_ASSERT(0)._\
Otherwise it is ignored._\
))
$(eval $(call TunableBothConfigC,PDUMP,,\
Enable parameter dumping in the driver._\
This adds code to record the parameters being sent to the hardware for_\
later analysis._\
))
$(eval $(call TunableBothConfigC,NO_HARDWARE,,\
Disable hardware interactions (e.g. register writes) that the driver would_\
normally perform. A driver built with this option can$(apos)t drive hardware$(comma)_\
but with PDUMP enabled$(comma) it can capture parameters to be played back later._\
))
$(eval $(call TunableBothConfigC,PDUMP_DEBUG_OUTFILES,))
$(eval $(call TunableBothConfigC,SYS_USING_INTERRUPTS,1))
$(eval $(call TunableBothConfigC,PVRSRV_NEED_PVR_DPF,,\
Enable this to turn on PVR_DPF in release builds._\
))
$(eval $(call TunableBothConfigC,PVRSRV_NEED_PVR_ASSERT,,\
Enable this to turn on PVR_ASSERT in release builds._\
))
$(eval $(call TunableBothConfigC,PVRSRV_NEED_PVR_TRACE,,\
Enable this to turn on PVR_TRACE in release builds._\
))
$(eval $(call TunableBothConfigC,REFCOUNT_DEBUG,))
$(eval $(call TunableBothConfigC,DC_DEBUG,))
$(eval $(call TunableBothConfigC,SCP_DEBUG,))
$(eval $(call TunableBothConfigC,CACHEFLUSH_TYPE,CACHEFLUSH_GENERIC))
$(eval $(call TunableBothConfigC,SUPPORT_INSECURE_EXPORT,1))
$(eval $(call TunableBothConfigC,SUPPORT_SECURE_EXPORT,1,\
Enable support for secure device memory and sync export._\
This replaces export handles with file descriptors$(comma) which can be passed_\
between processes to share memory._\
))
$(eval $(call TunableBothConfigC,SUPPORT_PMMIF,))
$(eval $(call TunableBothConfigC,SUPPORT_GPUTRACE_EVENTS,))
$(eval $(call TunableBothConfigC,SUPPORT_KERNEL_HWPERF,))
$(eval $(call TunableBothConfigC,SUPPORT_DISPLAY_CLASS,))
$(eval $(call TunableBothConfigC,PVRSRV_DEBUG_CCB_MAX,))
$(eval $(call TunableBothConfigC,SUPPORT_TRUSTED_DEVICE,))
$(eval $(call TunableBothConfigC,TRUSTED_DEVICE_DEFAULT_ENABLED,))
$(eval $(call TunableBothConfigC,SUPPORT_EXPORTING_MEMORY_CONTEXT,))
$(eval $(call TunableBothConfigC,SUPPORT_USER_REGISTER_CONFIGURATION,))
$(eval $(call TunableBothConfigC,FIX_DUSTS_POW_ON_INIT,))

$(eval $(call TunableKernelConfigC,DEBUG_HANDLEALLOC_INFO_KM,))
$(eval $(call TunableKernelConfigC,SUPPORT_LINUX_X86_WRITECOMBINE,1))
$(eval $(call TunableKernelConfigC,SUPPORT_LINUX_X86_PAT,1))
$(eval $(call TunableKernelConfigC,PVRSRV_RESET_ON_HWTIMEOUT,))
$(eval $(call TunableKernelConfigC,PVR_LINUX_USING_WORKQUEUES,))
$(eval $(call TunableKernelConfigC,PVR_LINUX_MISR_USING_WORKQUEUE,))
$(eval $(call TunableKernelConfigC,PVR_LINUX_MISR_USING_PRIVATE_WORKQUEUE,))
$(eval $(call TunableKernelConfigC,PVR_LINUX_TIMERS_USING_WORKQUEUES,))
$(eval $(call TunableKernelConfigC,PVR_LINUX_TIMERS_USING_SHARED_WORKQUEUE,))
$(eval $(call TunableKernelConfigC,PVR_LDM_PLATFORM_PRE_REGISTERED,))
$(eval $(call TunableKernelConfigC,PVR_LDM_DRIVER_REGISTRATION_NAME,"\"$(PVRSRV_MODNAME)\""))
$(eval $(call TunableBothConfigC,LDM_PLATFORM,))
$(eval $(call TunableBothConfigC,LDM_PCI,))
$(eval $(call TunableBothConfigC,PVRSRV_ENABLE_FULL_SYNC_TRACKING,))
$(eval $(call TunableKernelConfigC,SYNC_DEBUG,))
$(eval $(call TunableKernelConfigC,SUPPORT_DUMP_CLIENT_CCB_COMMANDS,))
$(eval $(call TunableKernelConfigC,PVR_LINUX_DONT_USE_RANGE_BASED_INVALIDATE,))
$(eval $(call TunableKernelConfigC,PVR_MMAP_USE_VM_INSERT,))
$(eval $(call TunableKernelConfigC,SUPPORT_MMU_PxE_MAP_ON_DEMAND,))
$(eval $(call TunableKernelConfigC,SUPPORT_MMU_MODIFICATION_LOGGING,,\
Enable support for logging of page table modifications. This is as debug_\
feature for use when debugging page-faults which are showing what look to_\
be unexpected values. It keeps a history of the last few modifications types_\
(map/unmap) and the value written during as a result of that operation._\
))
$(eval $(call TunableKernelConfigC,SUPPORT_DC_COMPLETE_TIMEOUT_DEBUG,))
$(eval $(call TunableKernelConfigC,SUPPORT_SYSTEM_INTERRUPT_HANDLING,,\
Enable support for system level interrupt handling. This is intended_\
for use on systems that have two or more levels of interrupt registers_\
which require the top level register to be cleared by the system layer_\
because it is not specific to one single device._\
))

$(eval $(call TunableBothConfigC,SUPPORT_PVR_VALGRIND,))





ifneq ($(SUPPORT_ANDROID_PLATFORM),1)
  endif

  
$(eval $(call TunableBothConfigMake,CACHEFLUSH_TYPE,CACHEFLUSH_GENERIC))
$(eval $(call TunableBothConfigMake,PDUMP,))
$(eval $(call TunableBothConfigMake,SUPPORT_INSECURE_EXPORT,1))
$(eval $(call TunableBothConfigMake,SUPPORT_SECURE_EXPORT,1))
$(eval $(call TunableBothConfigMake,SUPPORT_PMMIF,))
$(eval $(call TunableBothConfigMake,SUPPORT_DISPLAY_CLASS,))
$(eval $(call TunableBothConfigMake,SUPPORT_RAY_TRACING,))

$(eval $(call TunableBothConfigMake,SUPPORT_GPUTRACE_EVENTS,))
$(eval $(call TunableBothConfigMake,SUPPORT_KERNEL_HWPERF,))

$(eval $(call TunableBothConfigMake,OPTIM,,\
Specify the optimisation flags passed to the compiler. Normally this_\
is autoconfigured based on the build type._\
))
$(eval $(call TunableBothConfigC,SUPPORT_MMU_FREELIST,))
$(eval $(call TunableBothConfigC,SUPPORT_VFP,))

$(eval $(call TunableBothConfigC,SUPPORT_META_SLAVE_BOOT,))

$(eval $(call UserConfigC,EGL_BASENAME_SUFFIX,\"$(EGL_BASENAME_SUFFIX)\"))





$(eval $(call TunableBothConfigC,PVR_TESTING_UTILS,,\
Enable this to build in support for testing the PVR Transport Layer API._\
))


TQ_CAPTURE_PARAMS ?= 1

$(eval $(call TunableBothConfigC,TDMETACODE,))
$(eval $(call TunableBothConfigC,PVR_DPF_ADHOC_DEBUG_ON,))
$(eval $(call TunableBothConfigC,RGXFW_DEBUG_LOG_GROUP,))
$(eval $(call TunableBothConfigC,SUPPORT_POWMON_WO_GPIO_PIN,))


$(eval $(call TunableKernelConfigMake,PVR_HANDLE_BACKEND,generic,\
Specifies the back-end that should be used$(comma) by the Services kernel handle_\
interface$(comma) to allocate handles. The available backends are:_\
* generic (OS agnostic)_\
* idr (Uses the Linux IDR interface)_\
))


$(eval $(call TunableBothConfigC,PVRSRV_ENABLE_PROCESS_STATS,1,\
Enable Process Statistics via DebugFS._\
))

$(eval $(call TunableKernelConfigC,SUPPORT_SHARED_SLC,,))

# PVR_RI_DEBUG is set to enable RI annotation of devmem allocations
# This is enabled by default for debug builds.
#
$(eval $(call TunableBothConfigMake,PVR_RI_DEBUG,))
$(eval $(call TunableBothConfigC,PVR_RI_DEBUG,,\
Enable Resource Information (RI) debug. This logs details of_\
resource allocations with annotation to help indicate their use._\
))

$(eval $(call TunableBothConfigMake,SUPPORT_PAGE_FAULT_DEBUG,))
$(eval $(call TunableBothConfigC,SUPPORT_PAGE_FAULT_DEBUG,,\
Collect information about allocations such as descriptive strings_\
and timing data for more detailed page fault analysis._\
))

$(eval $(call TunableKernelConfigC,PVRSRV_ENABLE_MEMORY_STATS,,\
Enable Memory allocations to be recorded and published via Process Statistics._\
))

$(eval $(call TunableKernelConfigC,PVRSRV_ENABLE_FW_TRACE_DEBUGFS,,\
Enable automatic decoding of Firmware Trace via DebugFS._\
))

$(eval $(call TunableKernelConfigC,PVR_LINUX_PYSMEM_MAX_POOL_PAGES,"$(MAX_POOL_PAGES)"))

# ARM-Linux specific: 
# When allocating uncached or write-combine memory we need to invalidate the
# CPU cache before we can use the acquired pages. 
# The threshhold defines at which number of pages we want to do a full 
# cache flush instead of invalidating pages one by one.
$(eval $(call TunableKernelConfigC,PVR_LINUX_ARM_PAGEALLOC_FLUSH_THRESHOLD, 256))

$(eval $(call TunableKernelConfigC,PVR_LINUX_VMALLOC_ALLOCATION_THRESHOLD, 16384 ))

# Tunable RGX_MAX_TA_SYNCS / RGX_MAX_3D_SYNCS to increase the size of sync array in the DDK
# If defined, these macros take up the values as defined in the environment,
# Else, the default value is taken up as defined in include/rgxapi.h
#

endif # INTERNAL_CLOBBER_ONLY

export INTERNAL_CLOBBER_ONLY
export TOP
export OUT

MAKE_ETC := -Rr --no-print-directory -C $(TOP) TOP=$(TOP) OUT=$(OUT) \
	        -f build/linux/toplevel.mk

# This must match the default value of MAKECMDGOALS below, and the default
# goal in toplevel.mk
.DEFAULT_GOAL := build

ifeq ($(MAKECMDGOALS),)
MAKECMDGOALS := build
else
# We can't pass autogen to toplevel.mk
MAKECMDGOALS := $(filter-out autogen,$(MAKECMDGOALS))
endif

.PHONY: autogen
autogen:
ifeq ($(INTERNAL_CLOBBER_ONLY),)
	@$(MAKE) -s --no-print-directory -C $(TOP) \
		-f build/linux/prepare_tree.mk \
		LDM_PCI=$(LDM_PCI) \
		LDM_PLATFORM=$(LDM_PLATFORM)
else
	@:
endif

include ../config/help.mk

# This deletes built-in suffix rules. Otherwise the submake isn't run when
# saying e.g. "make thingy.a"
.SUFFIXES:

# Because we have a match-anything rule below, we'll run the main build when
# we're actually trying to remake various makefiles after they're read in.
# These rules try to prevent that
%.mk: ;
Makefile%: ;
Makefile: ;

.PHONY: build kbuild install
build kbuild install: autogen
	@$(if $(MAKECMDGOALS),$(MAKE) $(MAKE_ETC) $(MAKECMDGOALS) $(eval MAKECMDGOALS :=),:)

%: autogen
	@$(if $(MAKECMDGOALS),$(MAKE) $(MAKE_ETC) $(MAKECMDGOALS) $(eval MAKECMDGOALS :=),:)
