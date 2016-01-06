########################################################################### ###
#@Title         Define global variables
#@Copyright     Copyright (c) Imagination Technologies Ltd. All Rights Reserved
#@Description   This file is read once at the start of the build, after reading 
#               in config.mk. It should define the non-MODULE_* variables used 
#               in commands, like ALL_CFLAGS
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

# NOTE: You must *not* use the cc-option et al macros in COMMON_FLAGS,
# COMMON_CFLAGS or COMMON_USER_FLAGS. These flags are shared between
# host and target, which might use compilers with different capabilities.

# ANOTHER NOTE: All flags here must be architecture-independent (i.e. no
# -march or toolchain include paths)

# These flags are used for kernel, User C and User C++
#
COMMON_FLAGS := -W -Wall

# Some GCC warnings are C only, so we must mask them from C++
#
COMMON_CFLAGS := $(COMMON_FLAGS) \
 -Wdeclaration-after-statement -Wno-format-zero-length \
 -Wmissing-prototypes -Wstrict-prototypes

# User C and User C++ optimization control. Does not affect kernel.
#
ifeq ($(BUILD),debug)
COMMON_USER_FLAGS := -O0
else
OPTIM ?= -O2
ifeq ($(USE_LTO),1)
COMMON_USER_FLAGS := $(OPTIM) -flto
else
COMMON_USER_FLAGS := $(OPTIM)
endif
endif

# GCOV support for user-mode coverage statistics
#
ifeq ($(GCOV_BUILD),on)
COMMON_USER_FLAGS += -fprofile-arcs -ftest-coverage
endif

# Driver has not yet been audited for aliasing issues
#
COMMON_USER_FLAGS += -fno-strict-aliasing

# We always enable debugging. Either the release binaries are stripped
# and the symbols put in the symbolpackage, or we're building debug.
#
COMMON_USER_FLAGS += -g

# User C and User C++ warning flags
#
COMMON_USER_FLAGS += \
 -Wpointer-arith -Wunused-parameter \
 -Wmissing-format-attribute

# Additional warnings, and optional warnings.
#
TESTED_TARGET_USER_FLAGS := \
 $(call cc-option,-Wno-missing-field-initializers) \
 $(call cc-option,-fdiagnostics-show-option) \
 $(call cc-option,-Wno-self-assign) \
 $(call cc-option,-Wno-parentheses-equality)
TESTED_HOST_USER_FLAGS := \
 $(call host-cc-option,-Wno-missing-field-initializers) \
 $(call host-cc-option,-fdiagnostics-show-option) \
 $(call host-cc-option,-Wno-self-assign) \
 $(call host-cc-option,-Wno-parentheses-equality)

# These flags are clang-specific.
# -Wno-unused-command-line-argument works around a buggy interaction
# with ccache, see https://bugzilla.samba.org/show_bug.cgi?id=8118
# -fcolor-diagnostics force-enables colored error messages which
# get disabled when ccache is piped through ccache.
#
TESTED_TARGET_USER_FLAGS += \
 $(call cc-option,-Qunused-arguments) \
 $(call cc-option,-fcolor-diagnostics)
TESTED_HOST_USER_FLAGS += \
 $(call host-cc-option,-Qunused-arguments) \
 $(call host-cc-option,-fcolor-diagnostics)

ifeq ($(W),1)
TESTED_TARGET_USER_FLAGS += \
 $(call cc-option,-Wbad-function-cast) \
 $(call cc-option,-Wcast-qual) \
 $(call cc-option,-Wcast-align) \
 $(call cc-option,-Wconversion) \
 $(call cc-option,-Wdisabled-optimization) \
 $(call cc-option,-Wlogical-op) \
 $(call cc-option,-Wmissing-declarations) \
 $(call cc-option,-Wmissing-include-dirs) \
 $(call cc-option,-Wnested-externs) \
 $(call cc-option,-Wold-style-definition) \
 $(call cc-option,-Woverlength-strings) \
 $(call cc-option,-Wpacked) \
 $(call cc-option,-Wpacked-bitfield-compat) \
 $(call cc-option,-Wpadded) \
 $(call cc-option,-Wredundant-decls) \
 $(call cc-option,-Wshadow) \
 $(call cc-option,-Wswitch-default) \
 $(call cc-option,-Wvla) \
 $(call cc-option,-Wwrite-strings)
TESTED_HOST_USER_FLAGS += \
 $(call host-cc-option,-Wbad-function-cast) \
 $(call host-cc-option,-Wcast-qual) \
 $(call host-cc-option,-Wcast-align) \
 $(call host-cc-option,-Wconversion) \
 $(call host-cc-option,-Wdisabled-optimization) \
 $(call host-cc-option,-Wlogical-op) \
 $(call host-cc-option,-Wmissing-declarations) \
 $(call host-cc-option,-Wmissing-include-dirs) \
 $(call host-cc-option,-Wnested-externs) \
 $(call host-cc-option,-Wold-style-definition) \
 $(call host-cc-option,-Woverlength-strings) \
 $(call host-cc-option,-Wpacked) \
 $(call host-cc-option,-Wpacked-bitfield-compat) \
 $(call host-cc-option,-Wpadded) \
 $(call host-cc-option,-Wredundant-decls) \
 $(call host-cc-option,-Wshadow) \
 $(call host-cc-option,-Wswitch-default) \
 $(call host-cc-option,-Wvla) \
 $(call host-cc-option,-Wwrite-strings)
endif

TESTED_TARGET_USER_FLAGS += \
 $(call cc-optional-warning,-Wunused-but-set-variable) \
 $(call cc-optional-warning,-Wtypedef-redefinition)
TESTED_HOST_USER_FLAGS += \
 $(call host-cc-optional-warning,-Wunused-but-set-variable) \
 $(call host-cc-optional-warning,-Wtypedef-redefinition)

KBUILD_FLAGS := \
 -Wno-unused-parameter -Wno-sign-compare

TESTED_KBUILD_FLAGS := \
 $(call kernel-cc-option,-Wmissing-include-dirs) \
 $(call kernel-cc-option,-Wno-type-limits) \
 $(call kernel-cc-option,-Wno-pointer-arith) \
 $(call kernel-cc-option,-Wno-aggregate-return) \
 $(call kernel-cc-option,-Wno-unused-but-set-variable) \
 $(call kernel-cc-optional-warning,-Wbad-function-cast) \
 $(call kernel-cc-optional-warning,-Wcast-qual) \
 $(call kernel-cc-optional-warning,-Wcast-align) \
 $(call kernel-cc-optional-warning,-Wconversion) \
 $(call kernel-cc-optional-warning,-Wdisabled-optimization) \
 $(call kernel-cc-optional-warning,-Wlogical-op) \
 $(call kernel-cc-optional-warning,-Wmissing-declarations) \
 $(call kernel-cc-optional-warning,-Wmissing-include-dirs) \
 $(call kernel-cc-optional-warning,-Wnested-externs) \
 $(call kernel-cc-optional-warning,-Wno-missing-field-initializers) \
 $(call kernel-cc-optional-warning,-Wold-style-definition) \
 $(call kernel-cc-optional-warning,-Woverlength-strings) \
 $(call kernel-cc-optional-warning,-Wpacked) \
 $(call kernel-cc-optional-warning,-Wpacked-bitfield-compat) \
 $(call kernel-cc-optional-warning,-Wpadded) \
 $(call kernel-cc-optional-warning,-Wredundant-decls) \
 $(call kernel-cc-optional-warning,-Wshadow) \
 $(call kernel-cc-optional-warning,-Wswitch-default) \
 $(call kernel-cc-optional-warning,-Wvla) \
 $(call kernel-cc-optional-warning,-Wwrite-strings)

# User C only
#
ALL_CFLAGS := \
 $(COMMON_USER_FLAGS) $(COMMON_CFLAGS) $(TESTED_TARGET_USER_FLAGS) \
 $(SYS_CFLAGS)
ALL_HOST_CFLAGS := \
 $(COMMON_USER_FLAGS) $(COMMON_CFLAGS) $(TESTED_HOST_USER_FLAGS)

# User C++ only
#
ALL_CXXFLAGS := \
 -fno-rtti -fno-exceptions \
 $(COMMON_USER_FLAGS) $(COMMON_FLAGS) $(TESTED_TARGET_USER_FLAGS) \
 $(SYS_CXXFLAGS)
ALL_HOST_CXXFLAGS := \
 -fno-rtti -fno-exceptions \
 $(COMMON_USER_FLAGS) $(COMMON_FLAGS) $(TESTED_HOST_USER_FLAGS)

# Workaround for some target clangs that don't support -O0 w/ PIC.
#
ifeq ($(cc-is-clang),true)
ALL_CFLAGS := $(patsubst -O0,-O1,$(ALL_CFLAGS))
ALL_CXXFLAGS := $(patsubst -O0,-O1,$(ALL_CXXFLAGS))
endif

# Add GCOV_DIR just for target
#
ifeq ($(GCOV_BUILD),on)
ifneq ($(GCOV_DIR),)
ALL_CFLAGS += -fprofile-dir=$(GCOV_DIR)
ALL_CXXFLAGS += -fprofile-dir=$(GCOV_DIR)
endif
endif

# Kernel C only
#
ALL_KBUILD_CFLAGS := $(COMMON_CFLAGS) $(KBUILD_FLAGS) $(TESTED_KBUILD_FLAGS)

# User C and C++
#
# NOTE: ALL_HOST_LDFLAGS should probably be using -rpath-link too, and if we
# ever need to support building host shared libraries, it's required.
#
# We can't use it right now because we want to support non-GNU-compatible
# linkers like the Darwin 'ld' which doesn't support -rpath-link.
#
# For the same reason (Darwin 'ld') don't bother checking for text
# relocations in host binaries.
#
ALL_HOST_LDFLAGS :=
ALL_LDFLAGS := -Wl,--warn-shared-textrel

ifeq ($(GCOV_BUILD),on)
ALL_LDFLAGS += -fprofile-arcs
ALL_HOST_LDFLAGS += -fprofile-arcs
endif

ALL_LDFLAGS += $(SYS_LDFLAGS)

# This variable contains a list of all modules built by kbuild
ALL_KBUILD_MODULES :=

# This variable contains a list of all modules which contain C++ source files
ALL_CXX_MODULES :=

# Toolchain triple for cross environment
CROSS_TRIPLE := $(patsubst %-,%,$(CROSS_COMPILE))

ifneq ($(TOOLCHAIN),)
$(warning **********************************************)
$(warning  The TOOLCHAIN option has been removed, but)
$(warning  you have it set (via $(origin TOOLCHAIN)))
$(warning **********************************************)
endif

# We need the glibc version to generate the cache names for LLVM and XOrg components.
ifeq ($(CROSS_COMPILE),)
LIBC_VERSION_PROBE := $(shell ldd  $(shell which true) | awk '/libc.so/{print $$3'} )
LIBC_VERSION := $(shell $(LIBC_VERSION_PROBE)| tr -d '(),' | head -1)
endif
