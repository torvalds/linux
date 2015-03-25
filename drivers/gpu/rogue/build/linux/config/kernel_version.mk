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

$(if $(KERNELDIR),,$(error KERNELDIR must be set to obtain a version))

override KERNEL_VERSION := \
 $(shell grep "^VERSION = " $(KERNELDIR)/Makefile | cut -f3 -d' ')
override KERNEL_PATCHLEVEL := \
 $(shell grep "^PATCHLEVEL = " $(KERNELDIR)/Makefile | cut -f3 -d' ')
override KERNEL_SUBLEVEL := \
 $(shell grep "^SUBLEVEL = " $(KERNELDIR)/Makefile | cut -f3 -d' ')
override KERNEL_EXTRAVERSION := \
 $(shell grep "^EXTRAVERSION = " $(KERNELDIR)/Makefile | cut -f3 -d' ')

# Break the kernel version up into a space separated list
kernel_version_as_list := $(KERNEL_VERSION) \
				$(KERNEL_PATCHLEVEL) \
				$(KERNEL_SUBLEVEL) \
				$(patsubst .%,%,$(KERNEL_EXTRAVERSION))

# The base ID doesn't have to be accurate; we only use it for
# feature checks which will not care about extraversion bits
#
override KERNEL_BASE_ID := \
 $(KERNEL_VERSION).$(KERNEL_PATCHLEVEL).$(KERNEL_SUBLEVEL)

# Try to get the kernel ID from the kernel.release file.
# 
KERNEL_ID ?= \
 $(shell cat $(KERNELDIR)/include/config/kernel.release 2>/dev/null)

# If the kernel ID isn't set yet, try to set it from the UTS_RELEASE
# macro.
#
ifeq ($(strip $(KERNEL_ID)),)
KERNEL_ID := \
 $(shell grep -h '\#define UTS_RELEASE' \
	$(KERNELDIR)/include/linux/* | cut -f3 -d' ' | sed s/\"//g)
endif

ifeq ($(strip $(KERNEL_ID)),)
KERNEL_ID := \
 $(KERNEL_VERSION).$(KERNEL_PATCHLEVEL).$(KERNEL_SUBLEVEL)$(KERNEL_EXTRAVERSION)
endif

# Return 1 if the kernel version is at least the value passed to the
# function, else return nothing.
# Examples
# 	$(call kernel-version-at-least,2,6,35)
# 	$(call kernel-version-at-least,2,6,35,7)
#
define kernel-version-at-least
$(shell set -- $(kernel_version_as_list) 0 0 0 0; \
	Y=true; \
	for D in $1 $2 $3 $4; \
	do \
		[ $$1 ] || break; \
		[ $$1 -eq $$D ] && { shift; continue; };\
		[ $$1 -lt $$D ] && Y=; \
		break; \
	done; \
	echo $$Y)
endef
