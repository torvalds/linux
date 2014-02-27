#------------------------------------------------------------------------------
# <copyright file="makefile" company="Atheros">
#    Copyright (c) 2005-2010 Atheros Corporation.  All rights reserved.
# 
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#
#
#------------------------------------------------------------------------------
#==============================================================================
# Author(s): ="Atheros"
#==============================================================================
# AR6K Host driver makefile
# 
# Minimal build invocation:
#   
#     make (all | clean | clobber)  
#
# Extended Make invocation:
#
#     make ATH_BUILD_TYPE=<build type> ATH_SDIO_STACK_BASE=<sdio stack install path> (all | clean | clobber)
#
# Notes:
#     1. This makefile must be invoked from the host/ directory
#     2. The <build type> must match an entry in localmake.linux.inc.
#     3. The localmake.linux.inc can be overridden using an include file outside the build tree.
#        This file (name and path) can be set via the ATH_MAKE_INCLUDE_OVERRIDE variable.
#        ** If ATH_MAKE_INCLUDE_OVERRIDE is used, you can define all build variables in that file 
#        instead of using command line arguments **.  This feature is provided for developers
#        that may want to customize the build using a single include file.
#
#        For example :
#             
#            " make ATH_MAKE_INCLUDE_OVERRIDE=$HOME/mymake.inc "
#        
#             could be used, as long as "mymake.inc" defines all the required variables (see below)
#
#  Required Variables:  
#      
#      ATH_OS_SUB_TYPE - on linux, this must be "linux_2_4" for 2.4 kernels or left blank for 2.6 kernels.
#      ATH_LINUXPATH - linux kernel source path
#      ATH_CROSS_COMPILE_TYPE - optional cross compiler path , leave blank for local gcc compilation
#      ATH_ARCH_CPU_TYPE  - CPU architecture type, leave blank for local gcc compilation
#      ATH_SDIO_STACK_BASE - SDIO Stack installation path to compile the SDIO HIF layer against an externally supplied
#                            SDIO stack source.
#
#  Override variables:
#
#    ATH_MAKE_INCLUDE_OVERRIDE - full path to include file which overrides the default (localmake.linux.inc)
#                                this file can contain other overrides specific to a developers
#                                workspace environment.
#    ATH_BUILD_OUTPUT_OVERRIDE - output path override for compiled executable and
#                                database image
#

# Include local variables
ifdef ATH_MAKE_INCLUDE_OVERRIDE
_LOCALMAKE_INCLUDE = $(ATH_MAKE_INCLUDE_OVERRIDE)
else
_LOCALMAKE_INCLUDE = localmake.linux.inc
endif

-include $(_LOCALMAKE_INCLUDE)

export  ATH_SRC_BASE
export  ATH_BUILD_TYPE
export  ATH_OS_SUB_TYPE
export  ATH_LINUXPATH
export  ATH_CROSS_COMPILE_TYPE
export  ATH_ARCH_CPU_TYPE
export  ATH_SDIO_STACK_BASE
export  ATH_BUS_TYPE
export  ATH_BUS_SUBTYPE
export  ATH_HC_DRIVERS
export  ATH_HTC_RAW_INT_ENV
export  ATH_ANDROID_ENV
export  ATH_SOFTMAC_FILE_USED
export  ATH_CFG80211_ENV
export  ATH_DEBUG_DRIVER
export  ATH_AR6K_HCI_PAL
export  ATH_AR6K_DEBUG_ALLOC
#export  ATH_BUILD_P2P

ATH_SRC_BASE :=$(shell pwd)
MAKE :=make
CC :=$(ATH_CROSS_COMPILE_TYPE)gcc
LD :=$(ATH_CROSS_COMPILE_TYPE)ld
STRIP :=$(ATH_CROSS_COMPILE_TYPE)strip
ATH_HIF_TYPE :=`echo $(ATH_BUS_TYPE) | tr [:upper:] [:lower:]`

export  STRIP

#export compiler variables for 3rd party applications (like the WPA supplicant)
export  CC
export  LD
export  STRIP

# Set cross compile type (if any)
ifdef ATH_CROSS_COMPILE_TYPE
_CROSS_COMPILE_LINE := ARCH=$(ATH_ARCH_CPU_TYPE) CROSS_COMPILE=$(ATH_CROSS_COMPILE_TYPE)
endif

# Figure out module extension
ifneq ($(ATH_OS_SUB_TYPE),linux_2_4)
KMOD_EXTENSION :=ko
endif
ifeq ($(ATH_OS_SUB_TYPE),linux_2_4)
KMOD_EXTENSION :=o
endif

# Set up object output areas
ifdef ATH_BUILD_OUTPUT_OVERRIDE
_COMPILED_OBJECTS_PATH :=$(ATH_BUILD_OUTPUT_OVERRIDE)
COMPILED_BIN_OBJECTS_PATH :=$(ATH_BUILD_OUTPUT_OVERRIDE)
COMPILED_IMAGE_OBJECTS_PATH :=$(ATH_BUILD_OUTPUT_OVERRIDE)
_MAKE_IMAGE_OUTPUT_DIR :=
_MAKE_BIN_OUTPUT_DIR :=
_CLEAN_IMAGE_OUTPUT_DIR :=
_CLEAN_BIN_OUTPUT_DIR :=
else
_COMPILED_OBJECTS_PATH := $(ATH_SRC_BASE)/.output/$(ATH_BUILD_TYPE)-$(ATH_BUS_TYPE)
COMPILED_BIN_OBJECTS_PATH := $(ATH_SRC_BASE)/.output/bin
COMPILED_IMAGE_OBJECTS_PATH :=$(_COMPILED_OBJECTS_PATH)/$(COMPILED_OBJECTS_PREFIX)image
_MAKE_IMAGE_OUTPUT_DIR := mkdir --parents $(COMPILED_IMAGE_OBJECTS_PATH)
_MAKE_BIN_OUTPUT_DIR := mkdir --parents $(COMPILED_BIN_OBJECTS_PATH)
_CLEAN_IMAGE_OUTPUT_DIR := rm -R -f $(COMPILED_IMAGE_OBJECTS_PATH)
_CLEAN_BIN_OUTPUT_DIR := rm -R -f $(COMPILED_BIN_OBJECTS_PATH)
endif

# Export the required variables to other Makefiles in the system
export COMPILED_IMAGE_OBJECTS_PATH
export COMPILED_BIN_OBJECTS_PATH

all: modules

modules:
ifndef ATH_BUILD_TYPE 
	@echo "Please edit the $(_LOCALMAKE_INCLUDE) file"
	exit 1
endif
	$(_MAKE_IMAGE_OUTPUT_DIR)
	$(_MAKE_BIN_OUTPUT_DIR)
	$(MAKE)  $(_CROSS_COMPILE_LINE) -C $(ATH_LINUXPATH) ATH_HIF_TYPE=$(ATH_HIF_TYPE) SUBDIRS=$(ATH_SRC_BASE)/os/linux modules
	$(MAKE) -C hif/$(ATH_HIF_TYPE)
#	$(MAKE) -C tools/wmiconfig/
#	$(MAKE) -C tools/recEvent/
	cp -f $(ATH_SRC_BASE)/os/linux/ar6000.$(KMOD_EXTENSION) $(COMPILED_IMAGE_OBJECTS_PATH)
	$(STRIP) -g -S -d $(COMPILED_IMAGE_OBJECTS_PATH)/ar6000.$(KMOD_EXTENSION)
#	cp -f tools/wmiconfig/wmiconfig $(COMPILED_IMAGE_OBJECTS_PATH)
#	cp -f tools/recEvent/recEvent $(COMPILED_IMAGE_OBJECTS_PATH)
#	cp -f tools/recEvent/restore.sh $(COMPILED_IMAGE_OBJECTS_PATH)
	
clean:
	rm -f os/linux/ar6000.mod.c os/linux/*.o os/linux/*.ko wmi/*.o htc/AR6000/src/*.o htc/*.o \
	bmi/src/*.o wlan/src/*.o driver/* tools/wmiconfig/wmiconfig \
	tools/recEvent/recEvent

# The kernel module build process leaves some intermediate files, this will clean up all these files
	find $(ATH_SRC_BASE) \( -not -path "*.output*" -a -name '*.[oas]' -o -name core -o -name '.*.flags' -o -name '.ko' -o -name '.*.cmd' \) -type f -print \
		| grep -v lxdialog/ | xargs rm -f
	$(MAKE) -C hif/$(ATH_HIF_TYPE) clean

clobber:clean
	rm -rf .output
