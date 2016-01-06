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

define must-be-defined
$(if $(filter undefined,$(origin $(1))),$(error In makefile $(THIS_MAKEFILE): $$($(1)) must be defined),)
endef

define must-be-nonempty
$(if $(strip $($(1))),,$(error In makefile $(THIS_MAKEFILE): $$($(1)) must contain a value))
endef

define directory-must-exist
$(if $(wildcard $(abspath $(1)/)),,$(error Directory $(1) must exist))
endef

define one-word-only
$(if $(filter-out $(firstword $($(1))),$($(1))),$(error In makefile $(THIS_MAKEFILE): $$($(1)) must contain only one word),)
endef

define module-library
$(patsubst lib%.so,%,$(if $($(1)_target),$($(1)_target),$(1).so))
endef

# This is done to allow module type makefiles to use $(THIS_MAKEFILE)
define register-module
INTERNAL_MAKEFILE_FOR_MODULE_$(1) := $(THIS_MAKEFILE)
endef

define process-module-arch
MODULE_ARCH := $$(strip $(2))
include $$(MAKE_TOP)/moduledefs_common.mk
include $$(MAKE_TOP)/moduledefs/$$(MODULE_ARCH).mk
include $$(MAKE_TOP)/modules/$$(strip $$($$(THIS_MODULE)_type)).mk
.SECONDARY: $$(MODULE_INTERMEDIATES_DIR)
$$(MODULE_INTERMEDIATES_DIR):
	$$(make-directory)
MODULE_CLEAN_TARGETS += $$(MODULE_INTERMEDIATES_DIR)
INTERNAL_TARGETS_FOR_$(1) += $$(MODULE_TARGETS)
INTERNAL_CLEAN_TARGETS_FOR_$(1) += $$(MODULE_CLEAN_TARGETS)
INTERNAL_CLOBBER_TARGETS_FOR_$(1) += $$(MODULE_CLEAN_TARGETS) $$(MODULE_CLOBBER_TARGETS) $$(MODULE_TARGETS)
endef

target_neutral_types := \
 apk \
 bison_parser \
 bridge \
 copy_files \
 custom \
 flex_lexer \
 flexxx_lexer \
 gen_dispatch \
 rsc_scriptc \
 image_header \
 inline_shaders \
 java_archive \
 module_group \
 pds_header \
 rgxfw \
 test_image \
 usc_header \
 rscbitcode_header \
 yuv_test_image

doc_types := \
 doc \
 doxygen

define calculate-arch-list
# Work out the target platforms for this module
MODULE_ARCH_LIST := $(2)
ifeq ($$(MODULE_ARCH_LIST),)
ifneq ($$(filter $(1),$(doc_types)),)
MODULE_ARCH_LIST := doc
else
ifneq ($$(filter $(1),$(target_neutral_types)),)
MODULE_ARCH_LIST := target_neutral
else
ifneq ($$(filter $(1),kernel_module),)
MODULE_ARCH_LIST := $(TARGET_PRIMARY_ARCH)
else
MODULE_ARCH_LIST := $(TARGET_ALL_ARCH)
endif
endif
endif
endif
endef

define process-module
THIS_MODULE := $(1)
THIS_MAKEFILE := $(INTERNAL_MAKEFILE_FOR_MODULE_$(1))
INTERNAL_TARGETS_FOR_$(1) :=
INTERNAL_CLEAN_TARGETS_FOR_$(1) :=
INTERNAL_CLOBBER_TARGETS_FOR_$(1) :=
include $$(MAKE_TOP)/this_makefile.mk
$$(call must-be-nonempty,THIS_MAKEFILE)
$$(call must-be-nonempty,$(1)_type)
$$(eval $$(call calculate-arch-list,$$($(1)_type),$$($(1)_arch)))
INTERNAL_ARCH_LIST_FOR_$(1) := $$(MODULE_ARCH_LIST)
$$(foreach _m,$$(MODULE_ARCH_LIST),$$(eval $$(call process-module-arch,$(1),$$(_m))))
endef

# This can be used by module_type.mk files to indicate that they can't be
# built as host_module_type
define target-build-only
$(if $(filter true,$(MODULE_HOST_BUILD)),$(error In makefile $(THIS_MAKEFILE): Module $(THIS_MODULE) attempted to build a host $(1), which is not supported))
endef

define relative-to-top
$(patsubst $(TOP)/%,%,$(1))
endef

define cc-check
$(shell \
	CC_CHECK=$(patsubst @%,%,$(CC_CHECK)) && \
	$(patsubst @%,%,$(CHMOD)) +x $$CC_CHECK && \
	$$CC_CHECK --cc "$(1)" --out "$(2)" $(3))
endef

define cc-is-clang
$(call cc-check,$(patsubst @%,%,$(CC)),$(OUT),--clang)
endef

define cc-option
$(call cc-check,$(patsubst @%,%,$(CC)),$(OUT),$(1))
endef

define cxx-option
$(call cc-check,$(patsubst @%,%,$(CXX)),$(OUT),$(1))
endef

define host-cc-option
$(call cc-check,$(patsubst @%,%,$(HOST_CC)),$(OUT),$(1))
endef

define host-cxx-option
$(call cc-check,$(patsubst @%,%,$(HOST_CXX)),$(OUT),$(1))
endef

define kernel-cc-option
$(call cc-check,$(KERNEL_CROSS_COMPILE)gcc,$(OUT),$(1))
endef

# Turn a particular warning on, or explicitly turn it off, depending on
# the value of W. The "-W" or "-Wno-" part of the warning need not be
# specified.
define cc-optional-warning
$(call cc-option,-W$(if $(W),,no-)$(patsubst -W%,%,$(patsubst -Wno-%,%,$(1))))
endef

define host-cc-optional-warning
$(call host-cc-option,-W$(if $(W),,no-)$(patsubst -W%,%,$(patsubst -Wno-%,%,$(1))))
endef

define kernel-cc-optional-warning
$(call kernel-cc-option,-W$(if $(W),,no-)$(patsubst -W%,%,$(patsubst -Wno-%,%,$(1))))
endef

define module-info-line
$(if $(filter modules,$(D)),$(info [$(THIS_MODULE)] <$(MODULE_ARCH)> $(1)),)
endef

# $(call if-exists,A,B) => A if A is a file which exists, otherwise B
define if-exists
$(if $(wildcard $(1)),$(1),$(2))
endef

#
# Joins a given list of strings together with the given separator.
#
# (1): the list of strings to join
# (2): the separator to use for joining
#
NOOP=
SPACE=$(NOOP) $(NOOP)
define list-join
$(subst $(SPACE),$(2),$(strip $(1)))
endef

# 
# Check if a given path is absolute
#
# $(1): path to check
# $(2): return when true
# $(3): return when false
#
define if-abs-path
$(if $(filter /%,$(1)),$(2),$(3))
endef

# 
# Add a prefix to every path in a list, when the path isn't absolute.
#
# $(1): prefix to add
# $(2): list of paths
#
define addprefix-ifnot-abs
$(foreach _path,$(2),$(call if-abs-path,$(_path),$(_path),$(1)$(_path)))
endef

#
# Return the directory part of a path normalized (without trailing slashes)
#
# $(1): Path to normalize
#
define normalized-dir
$(if $(filter /,$(dir $(1))),$(dir $(1)),$(if $(findstring /,$(dir $(1))),$(patsubst %/,%,$(dir $(1))),$(dir $(1))))
endef

# Definitions to handle the various suffixes allowed for C++ files.
INTERNAL_CXX_SUFFIXES := .cc .cxx .cpp .c++
define filter-cxx-files
$(filter $(addprefix %,$(INTERNAL_CXX_SUFFIXES)),$(1))
endef

define filter-out-cxx-files
$(filter-out $(addprefix %,$(INTERNAL_CXX_SUFFIXES)),$(1))
endef

define objects-from-cxx-files
$(foreach _suffix,$(INTERNAL_CXX_SUFFIXES),$(patsubst %$(_suffix),%.o,$(filter %$(_suffix),$(1))))
endef

define unsupported-module-var
$(if $(strip $($(THIS_MODULE)_$(1))),$(error In makefile $(THIS_MAKEFILE): Setting '$(THIS_MODULE)_$(1)' has no effect, because $(THIS_MODULE) has type $($(THIS_MODULE)_type)))
endef
