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
#@Description   Common processing for all modules that compile code.
### ###########################################################################

# Filter for source types
MODULE_C_SOURCES := $(filter %.c,$(MODULE_SOURCES))
MODULE_CXX_SOURCES := $(call filter-cxx-files,$(MODULE_SOURCES))

MODULE_UNRECOGNISED_SOURCES := $(call filter-out-cxx-files,$(filter-out %.c,$(MODULE_SOURCES)))

ifneq ($(strip $(MODULE_UNRECOGNISED_SOURCES)),)
$(error In makefile $(THIS_MAKEFILE): Module $(THIS_MODULE) specified source files with unrecognised suffixes: $(MODULE_UNRECOGNISED_SOURCES))
endif

# Objects built from MODULE_SOURCES
MODULE_C_OBJECTS := $(addprefix $(MODULE_INTERMEDIATES_DIR)/,$(notdir $(MODULE_C_SOURCES:.c=.o)))
MODULE_CXX_OBJECTS := $(addprefix $(MODULE_INTERMEDIATES_DIR)/,$(notdir $(call objects-from-cxx-files,$(MODULE_CXX_SOURCES))))

# MODULE_GENERATED_DEPENDENCIES are generated as a side effect of running the
# rules below, but if we wanted to generate .d files for things that GCC
# couldn't handle, we could add a rule with $(MODULE_GENERATED_DEPENDENCIES)
# as a target
MODULE_GENERATED_DEPENDENCIES := $(MODULE_C_OBJECTS:.o=.d) $(MODULE_CXX_OBJECTS:.o=.d)
-include $(MODULE_GENERATED_DEPENDENCIES)

MODULE_DEPENDS := $(addprefix $(MODULE_OUT)/,$($(THIS_MODULE)_depends))
MODULE_DEPENDS += $(addprefix $(GENERATED_CODE_OUT)/,$($(THIS_MODULE)_genheaders))

# Add any MODULE_OUT relative include flags here
MODULE_INCLUDE_FLAGS += $(addprefix -I $(MODULE_OUT)/, $($(THIS_MODULE)_includes_relative))

define rule-for-objects-o-from-one-c
$(1): MODULE_CC := $$(MODULE_CC)
$(1): MODULE_CFLAGS := $$(MODULE_CFLAGS)
$(1): MODULE_INCLUDE_FLAGS := $$(MODULE_INCLUDE_FLAGS)
$(1): MODULE_ALLOWED_CFLAGS := $$(MODULE_ALLOWED_CFLAGS)
$(1): THIS_MODULE := $$(THIS_MODULE)
ifneq ($(PKG_CONFIG_ENV_VAR),)
$(1): export PKG_CONFIG_TOP_BUILD_DIR := $(abspath $(MODULE_OUT))
$(1): export $(PKG_CONFIG_ENV_VAR) := $(abspath $(MODULE_OUT)/lws_pkgconfig)
endif
$(1): $$(MODULE_DEPENDS) $$(THIS_MAKEFILE)
$(1): | $$(MODULE_INTERMEDIATES_DIR)
$(1): $(2)
	@: $(if $(MODULE_CHECK_CFLAGS),
		$(if $(filter-out $(MODULE_ALLOWED_CFLAGS),$($(THIS_MODULE)_cflags)),\
			$(error $(THIS_MODULE): LTO-incompatible cflag(s) used: \
				$(filter-out $(MODULE_ALLOWED_CFLAGS),$($(THIS_MODULE)_cflags)))))
	$$(check-src)
ifeq ($(MODULE_HOST_BUILD),true)
	$$(host-o-from-one-c)
else
	$$(target-o-from-one-c)
endif
endef

# This rule is used to compile C++ source files
define rule-for-objects-o-from-one-cxx
$(1): MODULE_CXX := $$(MODULE_CXX)
$(1): MODULE_CXXFLAGS := $$(MODULE_CXXFLAGS)
$(1): MODULE_INCLUDE_FLAGS := $$(MODULE_INCLUDE_FLAGS)
$(1): MODULE_ALLOWED_CFLAGS := $$(MODULE_ALLOWED_CFLAGS)
$(1): THIS_MODULE := $$(THIS_MODULE)
ifneq ($(PKG_CONFIG_ENV_VAR),)
$(1): export PKG_CONFIG_TOP_BUILD_DIR := $(abspath $(MODULE_OUT))
$(1): export $(PKG_CONFIG_ENV_VAR) := $(abspath $(MODULE_OUT)/lws_pkgconfig)
endif
$(1): $$(MODULE_DEPENDS) $$(THIS_MAKEFILE)
$(1): | $$(MODULE_INTERMEDIATES_DIR)
$(1): $(2)
	@: $(if $(MODULE_CHECK_CFLAGS),
		$(if $(filter-out $(MODULE_ALLOWED_CFLAGS),$($(THIS_MODULE)_cxxflags)),\
			$(error $(THIS_MODULE): LTO-incompatible cxxflag(s) used: \
				$(filter-out $(MODULE_ALLOWED_CFLAGS),$($(THIS_MODULE)_cxxflags)))))
ifeq ($(MODULE_HOST_BUILD),true)
	$$(host-o-from-one-cxx)
else
	$$(target-o-from-one-cxx)
endif
endef
