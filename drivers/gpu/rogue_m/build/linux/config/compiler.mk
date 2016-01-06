########################################################################### ###
#@File
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

# Check for valid values of $(MULTIARCH).
ifeq ($(strip $(MULTIARCH)),0)
$(error MULTIARCH must be empty to disable multiarch)
endif

define calculate-compiler-preferred-target
 ifeq ($(2),qcc)
  $(1)_compiler_preferred_target := qcc
 else
  $(1)_compiler_preferred_target := $$(shell $(2) -dumpmachine)
  ifeq ($$($(1)_compiler_preferred_target),)
   $$(warning No output from '$(2) -dumpmachine')
   $$(warning Check that the compiler is in your PATH and CROSS_COMPILE is)
   $$(warning set correctly.)
   $$(error Unable to run compiler '$(2)')
  endif
  ifneq ($$(filter x86_64-%,$$($(1)_compiler_preferred_target)),)
   $(1)_compiler_preferred_target := x86_64-linux-gnu
  endif
  ifneq ($$(filter i386-% i486-% i686-%,$$($(1)_compiler_preferred_target)),)
   $(1)_compiler_preferred_target := i386-linux-gnu
  endif
  ifneq ($$(filter armv7a-cros-linux-gnueabi,$$($(1)_compiler_preferred_target)),)
   $(1)_compiler_preferred_target := arm-linux-gnueabi
  endif
 endif
endef

define cross-compiler-name
 ifeq ($$(origin CC),file)
  $(1) := $(2)$(3)
 else
  ifeq ($$(_CLANG),true)
   $(1) := $(3) -target $$(patsubst %-,%,$(2))
  else
   $(1) := $(3)
  endif
 endif
endef

# Work out the host compiler architecture
$(eval $(call calculate-compiler-preferred-target,host,$(HOST_CC)))

ifeq ($(host_compiler_preferred_target),x86_64-linux-gnu)
 HOST_PRIMARY_ARCH := host_x86_64
 HOST_32BIT_ARCH   := host_i386
 HOST_FORCE_32BIT  := -m32
else
ifeq ($(host_compiler_preferred_target),i386-linux-gnu)
 HOST_PRIMARY_ARCH := host_i386
 HOST_32BIT_ARCH   := host_i386
else
 $(error Unknown host compiler target architecture $(host_compiler_preferred_target))
endif
endif

# Workaround our lack of support for non-Linux HOST_CCs
ifneq ($(HOST_CC_IS_LINUX),1)
 $(warning $$(HOST_CC) is non-Linux. Trying to work around.)
 override HOST_CC := $(HOST_CC) -D__linux__
 $(eval $(call BothConfigMake,HOST_CC,$(HOST_CC)))
endif

$(eval $(call BothConfigMake,HOST_PRIMARY_ARCH,$(HOST_PRIMARY_ARCH)))
$(eval $(call BothConfigMake,HOST_32BIT_ARCH,$(HOST_32BIT_ARCH)))
$(eval $(call BothConfigMake,HOST_FORCE_32BIT,$(HOST_FORCE_32BIT)))

TARGET_ALL_ARCH := 
TARGET_PRIMARY_ARCH :=
TARGET_SECONDARY_ARCH :=

# Work out the target compiler cross triple, and include the corresponding
# compilers/*.mk file, which sets TARGET_PRIMARY_ARCH and
# TARGET_SECONDARY_ARCH for that compiler.
#
compilers := ../config/compilers
define include-compiler-file
 ifeq ($(strip $(1)),)
  $$(error empty arg passed to include-compiler-file)
 endif
 ifeq ($$(wildcard $$(compilers)/$(1).mk),)
  $$(warning ******************************************************)
  $$(warning Compiler target '$(1)' not recognised)
  $$(warning (missing $$(compilers)/$(1).mk file))
  $$(warning ******************************************************)
  $$(error Compiler '$(1)' not recognised)
 endif
 include $$(compilers)/$(1).mk
endef

# Check the kernel cross compiler to work out which architecture it targets.
# We can then tell if CROSS_COMPILE targets a different architecture.
ifneq ($(origin KERNEL_CROSS_COMPILE),undefined)
 # First, calculate the value of KERNEL_CROSS_COMPILE as it would be seen by
 # the main build, so we can check it here in the config stage.
 $(call one-word-only,KERNEL_CROSS_COMPILE)
 _kernel_cross_compile := $(if $(filter undef,$(KERNEL_CROSS_COMPILE)),,$(KERNEL_CROSS_COMPILE))
 # We can take shortcuts with KERNEL_CROSS_COMPILE, as we don't want to
 # respect CC and we don't support clang in that part currently.
 _kernel_cross_compile := $(_kernel_cross_compile)gcc
 # Then check the compiler.
 $(eval $(call calculate-compiler-preferred-target,target,$(_kernel_cross_compile)))
 $(eval $(call include-compiler-file,$(target_compiler_preferred_target)))
 _kernel_primary_arch := $(TARGET_PRIMARY_ARCH)
else
 # We can take shortcuts with KERNEL_CROSS_COMPILE, as we don't want to
 # respect CC and we don't support clang in that part currently.
 _kernel_cross_compile := $(CROSS_COMPILE)gcc
 # KERNEL_CROSS_COMPILE will be the same as CROSS_COMPILE, so we don't need
 # to do the compatibility check.
 _kernel_primary_arch :=
endif

$(eval $(call cross-compiler-name,_cc,$(CROSS_COMPILE),$(CC)))
$(eval $(call cross-compiler-name,_cc_secondary,$(if $(CROSS_COMPILE_SECONDARY),$(CROSS_COMPILE_SECONDARY),$(CROSS_COMPILE)),$(CC_SECONDARY)))
$(eval $(call calculate-compiler-preferred-target,target,$(_cc)))
$(eval $(call include-compiler-file,$(target_compiler_preferred_target)))

# Sanity check: if KERNEL_CROSS_COMPILE was set, it has to target the same
# architecture as CROSS_COMPILE.
ifneq ($(_kernel_primary_arch),)
 ifneq ($(TARGET_PRIMARY_ARCH),$(_kernel_primary_arch))
  $(warning ********************************************************)
  $(warning Error: Kernel and user-mode cross compilers build for)
  $(warning different targets)
  $(warning $(space)$(space)CROSS_COMPILE=$(CROSS_COMPILE))
  $(warning $(space)$(space)$(space)builds for $(TARGET_PRIMARY_ARCH))
  $(warning $(space)$(space)KERNEL_CROSS_COMPILE=$(KERNEL_CROSS_COMPILE))
  $(warning $(space)$(space)$(space)builds for $(_kernel_primary_arch))
  $(warning ********************************************************)
  $(error Mismatching kernel and user-mode cross compilers)
 endif
endif

ifneq ($(MULTIARCH),32only)
TARGET_ALL_ARCH += $(TARGET_PRIMARY_ARCH)
endif
ifneq ($(MULTIARCH),64only)
TARGET_ALL_ARCH += $(TARGET_SECONDARY_ARCH)
endif

$(eval $(call BothConfigMake,TARGET_PRIMARY_ARCH,$(TARGET_PRIMARY_ARCH)))
$(eval $(call BothConfigMake,TARGET_SECONDARY_ARCH,$(TARGET_SECONDARY_ARCH)))
$(eval $(call BothConfigMake,TARGET_ALL_ARCH,$(TARGET_ALL_ARCH)))
$(eval $(call BothConfigMake,TARGET_FORCE_32BIT,$(TARGET_FORCE_32BIT)))

$(info ******* Multiarch build: $(if $(MULTIARCH),yes,no))
$(info ******* Primary arch:    $(if $(TARGET_PRIMARY_ARCH),$(TARGET_PRIMARY_ARCH),none))
$(info ******* Secondary arch:  $(if $(TARGET_SECONDARY_ARCH),$(TARGET_SECONDARY_ARCH),none))

# Find the paths to libgcc for the primary and secondary architectures.
LIBGCC := $(shell $(_cc) -print-libgcc-file-name)
LIBGCC_SECONDARY := $(shell $(_cc_secondary) $(TARGET_FORCE_32BIT) -print-libgcc-file-name)
