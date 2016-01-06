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

# Rules for making kernel modules with kbuild. This makefile doesn't define
# any rules that build the modules, it only copies the kbuild Makefile into
# the right place and then invokes kbuild to do the actual build

$(call target-build-only,kernel module)

MODULE_KBUILD_DIR := $(MODULE_OUT)/kbuild

# $(THIS_MODULE)_makefile names the kbuild makefile fragment used to build
# this module's objects
$(call must-be-nonempty,$(THIS_MODULE)_makefile)
MODULE_KBUILD_MAKEFILE := $($(THIS_MODULE)_makefile)

# $(THIS_MODULE)_target specifies the name of the kernel module
$(call must-be-nonempty,$(THIS_MODULE)_target)
MODULE_TARGETS := $($(THIS_MODULE)_target)
MODULE_KBUILD_OBJECTS := $($(THIS_MODULE)_target:.ko=.o)

$(call module-info-line,kernel module: $(MODULE_TARGETS))

# Unusually, we define $(THIS_MODULE)_install_path if the user didn't, as we
# can't use MODULE_INSTALL_PATH in the scripts.mk logic.
ifeq ($($(THIS_MODULE)_install_path),)
$(THIS_MODULE)_install_path := \
 $${MOD_DESTDIR}/$(patsubst $(MODULE_OUT)/%,%,$(MODULE_TARGETS))
endif

MODULE_INSTALL_PATH := $($(THIS_MODULE)_install_path)

# Here we could maybe include $(MODULE_KBUILD_MAKEFILE) and look at
# $(MODULE_KBUILD_OBJECTS)-y to see which source files might be built

.PHONY: $(THIS_MODULE)
$(THIS_MODULE): MODULE_KBUILD_MAKEFILE := $(MODULE_KBUILD_MAKEFILE)
$(THIS_MODULE): MODULE_KBUILD_OBJECTS := $(MODULE_KBUILD_OBJECTS)
$(THIS_MODULE):
	@echo "kbuild module '$@'"
	@echo " MODULE_KBUILD_MAKEFILE := $(MODULE_KBUILD_MAKEFILE)"
	@echo " MODULE_KBUILD_OBJECTS := $(MODULE_KBUILD_OBJECTS)"
	@echo ' Being built:' $(if $(filter $@,$(KERNEL_COMPONENTS)),"yes (separate module)",$(if $(filter $@,$(EXTRA_PVRSRVKM_COMPONENTS)),"yes (into pvrsrvkm)","no"))
	@echo "Module $@ is a kbuild module. Run 'make kbuild' to make it"
	@false

$(MODULE_INTERMEDIATES_DIR)/.install: MODULE_TYPE := $($(THIS_MODULE)_type)
$(MODULE_INTERMEDIATES_DIR)/.install: MODULE_INSTALL_PATH := $(MODULE_INSTALL_PATH)
$(MODULE_INTERMEDIATES_DIR)/.install: MODULE_TARGETS := $(patsubst $(MODULE_OUT)/%,%,$(MODULE_TARGETS))
$(MODULE_INTERMEDIATES_DIR)/.install: $(THIS_MAKEFILE) | $(MODULE_INTERMEDIATES_DIR)
	@echo 'install_file $(MODULE_TARGETS) $(MODULE_INSTALL_PATH) "$(MODULE_TYPE)" 0644 0:0' >$@

ALL_KBUILD_MODULES += $(THIS_MODULE)
INTERNAL_KBUILD_MAKEFILE_FOR_$(THIS_MODULE) := $(MODULE_KBUILD_MAKEFILE)
INTERNAL_KBUILD_OBJECTS_FOR_$(THIS_MODULE) := $(MODULE_KBUILD_OBJECTS)
