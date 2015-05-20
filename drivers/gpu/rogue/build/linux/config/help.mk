########################################################################### ###
#@File
#@Title         Targets for printing config option help
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

define newline


endef
empty :=

define abbrev-option-value
$(if $(word 6,$(1)),$(wordlist 1,5,$(1))...,$(1))
endef

define print-option-help
# Print the option name and value
$(info $(1) ($(if $($(1)),$(call abbrev-option-value,$($(1))),<unset>), default $(if $(INTERNAL_CONFIG_DEFAULT_FOR_$(1)),$(call abbrev-option-value,$(INTERNAL_CONFIG_DEFAULT_FOR_$(1))),<unset>))$(if $(INTERNAL_DESCRIPTION_FOR_$(1)),:,))
# Ensure the config help text ends with a newline
$(and $(INTERNAL_DESCRIPTION_FOR_$(1)),$(if $(filter %_,$(word $(words $(INTERNAL_DESCRIPTION_FOR_$(1))),$(INTERNAL_DESCRIPTION_FOR_$(1)))),,$(eval INTERNAL_DESCRIPTION_FOR_$(1) := $(INTERNAL_DESCRIPTION_FOR_$(1))_ )))
# Print the config help text
$(info $(empty)  $(subst _ ,$(newline)  ,$(INTERNAL_DESCRIPTION_FOR_$(1))))
endef

.PHONY: confighelp allconfighelp
# Show only the config options that have help text
confighelp:
	@: $(foreach _o,$(sort $(ALL_TUNABLE_OPTIONS)),$(if $(INTERNAL_DESCRIPTION_FOR_$(_o)),$(call print-option-help,$(_o)),))
# Show all the config options
allconfighelp:
	@: $(foreach _o,$(sort $(ALL_TUNABLE_OPTIONS)),$(call print-option-help,$(_o)))


ifneq ($(filter confighelp-%,$(MAKECMDGOALS)),)
confighelp-%:
	@: $(if $(filter $*,$(ALL_TUNABLE_OPTIONS)),$(call print-option-help,$*),$(info $* is not a tunable config option))
endif
