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

# Find out the path of the Linux.mk makefile currently being processed, and
# set paths used by the build rules

# This magic is used so we can use this_makefile.mk twice: first when reading
# in each Linux.mk, and then again when generating rules. There we set
# $(THIS_MAKEFILE), and $(REMAINING_MAKEFILES) should be empty
ifneq ($(strip $(REMAINING_MAKEFILES)),)

# Absolute path to the Linux.mk being processed
THIS_MAKEFILE := $(firstword $(REMAINING_MAKEFILES))

# The list of makefiles left to process
REMAINING_MAKEFILES := $(wordlist 2,$(words $(REMAINING_MAKEFILES)),$(REMAINING_MAKEFILES))

else

# When generating rules, we should have read in every Linux.mk
$(if $(INTERNAL_INCLUDED_ALL_MAKEFILES),,$(error No makefiles left in $$(REMAINING_MAKEFILES), but $$(INTERNAL_INCLUDED_ALL_MAKEFILES) is not set))

endif

# Path to the directory containing Linux.mk
THIS_DIR := $(patsubst %/,%,$(dir $(THIS_MAKEFILE)))
ifeq ($(strip $(THIS_DIR)),)
$(error Empty $$(THIS_DIR) for makefile "$(THIS_MAKEFILE)")
endif

modules :=
