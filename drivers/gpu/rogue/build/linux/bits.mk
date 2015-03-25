########################################################################### ###
#@Title         Useful special targets which don't build anything
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

ifneq ($(filter dumpvar-%,$(MAKECMDGOALS)),)
dumpvar-%: ;
$(foreach _var_to_dump,$(patsubst dumpvar-%,%,$(filter dumpvar-%,$(MAKECMDGOALS))),$(info $(if $(filter undefined,$(origin $(_var_to_dump))),# $$($(_var_to_dump)) is not set,$(_var_to_dump) := $($(_var_to_dump)))))
endif

ifneq ($(filter whereis-%,$(MAKECMDGOALS)),)
whereis-%: ;
$(foreach _module_to_find,$(patsubst whereis-%,%,$(filter whereis-%,$(MAKECMDGOALS))),$(info $(if $(INTERNAL_MAKEFILE_FOR_MODULE_$(_module_to_find)),$(INTERNAL_MAKEFILE_FOR_MODULE_$(_module_to_find)),# No module $(_module_to_find))))
endif

ifneq ($(filter whatis-%,$(MAKECMDGOALS)),)
whatis-$(HOST_OUT)/%: ;
whatis-$(TARGET_PRIMARY_OUT)/%: ;
whatis-$(TARGET_NEUTRAL_OUT)/%: ;
$(foreach _file_to_find,$(patsubst whatis-%,%,$(filter whatis-%,$(MAKECMDGOALS))),$(info $(strip $(foreach _m,$(ALL_MODULES),$(if $(filter $(_file_to_find),$(INTERNAL_TARGETS_FOR_$(_m))),$(_file_to_find) is in $(_m) which is defined in $(INTERNAL_MAKEFILE_FOR_MODULE_$(_m)),)))))
endif

.PHONY: ls-modules
ls-modules:
	@: $(foreach _m,$(ALL_MODULES),$(info $($(_m)_type) $(_m) $(patsubst $(TOP)/%,%,$(INTERNAL_MAKEFILE_FOR_MODULE_$(_m)))))

.PHONY: ls-types
ls-types:
	@: $(info $(sort $(patsubst host_%,%,$(foreach _m,$(ALL_MODULES),$($(_m)_type)))))

ifeq ($(strip $(MAKECMDGOALS)),visualise)
FORMAT ?= xlib
GRAPHVIZ ?= neato
visualise: $(OUT)/MAKE_RULES.dot
	$(GRAPHVIZ) -T$(FORMAT) -o $(OUT)/MAKE_RULES.$(FORMAT) $<
$(OUT)/MAKE_RULES.dot: $(OUT)/MAKE_RULES
	perl $(MAKE_TOP)/tools/depgraph.pl -t $(TOP) -g $(firstword $(GRAPHVIZ)) $(OUT)/MAKE_RULES >$(OUT)/MAKE_RULES.dot
$(OUT)/MAKE_RULES: $(ALL_MAKEFILES)
	-$(MAKE) -C $(TOP) -f $(MAKE_TOP)/toplevel.mk TOP=$(TOP) OUT=$(OUT) ls-modules -qp >$(OUT)/MAKE_RULES 2>&1
else
visualise:
	@: $(error visualise specified along with other goals. This is not supported)
endif

.PHONY: help confighelp
help:
	@echo 'Build targets'
	@echo '  make, make build       Build all components of the build'
	@echo '  make components        Build only the user-mode components'
	@echo '  make kbuild            Build only the kernel-mode components'
	@echo "  make docs              Build the build's supporting documentation"
	@echo '  make MODULE            Build the module MODULE and all of its dependencies'
	@echo '  make binary_.../target/libsomething.so'
	@echo '                         Build a particular file (including intermediates)'
	@echo 'Variables'
	@echo '  make V=1 ...           Print the commands that are executed'
	@echo '  make W=1 ...           Enable extra compiler warnings'
	@echo '  make D=opt ...         Set build system debug option (D=help for a list)'
	@echo '  make OUT=dir ...       Place output+intermediates in specified directory'
	@echo '  make CHECK=cmd ...     Check source with static analyser or other tool'
	@echo '  EXCLUDED_APIS=...      List of APIs to remove from the build'
	@echo '  make SOMEOPTION=1 ...  Set configuration options (see "make confighelp")'
	@echo '                         Defaults are set by $(PVR_BUILD_DIR)/Makefile'
	@echo 'Clean targets'
	@echo '  make clean             Remove output files for the current build'
	@echo '  make clobber           As "make clean", but remove build config too'
	@echo '  make clean-MODULE      Clean (or clobber) only files for MODULE'
	@echo ''
	@echo 'Special targets'
	@echo '  make whereis-MODULE    Show the path to the Linux.mk defining MODULE'
	@echo '  make whatis-FILE       Show which module builds an output FILE'
	@echo '  make ls-modules        List all modules defined by makefiles'

# This rule runs in the configuration stage, in config/help.mk. Make a dummy
# target here to suppress "no rule to make target 'confighelp' messages.
confighelp: ;

ifneq ($(filter help,$(D)),)
empty :=
space := $(empty) $(empty) 
$(info Debug options)
$(info $(space)D=modules            dump module info)
$(info $(space)D=config             dump all config options + type and origin)
$(info $(space)D=freeze-config      prevent config changes)
$(info $(space)D=config-changes     dump diffs when config changes)
$(info $(space)D=nobuild            stop before running the main build)
$(info Options can be combined: make D=freeze-config,config-changes)
$(error D=help given)
endif
