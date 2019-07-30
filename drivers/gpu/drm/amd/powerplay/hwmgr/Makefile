#
# Copyright 2017 Advanced Micro Devices, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#
#
# Makefile for the 'hw manager' sub-component of powerplay.
# It provides the hardware management services for the driver.

HARDWARE_MGR = hwmgr.o processpptables.o \
		hardwaremanager.o smu8_hwmgr.o \
		pppcielanes.o\
		process_pptables_v1_0.o ppatomctrl.o ppatomfwctrl.o \
		smu7_hwmgr.o smu7_powertune.o smu7_thermal.o \
		smu7_clockpowergating.o \
		vega10_processpptables.o vega10_hwmgr.o vega10_powertune.o \
		vega10_thermal.o smu10_hwmgr.o pp_psm.o\
		vega12_processpptables.o vega12_hwmgr.o \
		vega12_thermal.o \
		pp_overdriver.o smu_helper.o \
		vega20_processpptables.o vega20_hwmgr.o vega20_powertune.o \
		vega20_thermal.o common_baco.o vega10_baco.o  vega20_baco.o \
		vega12_baco.o smu9_baco.o

AMD_PP_HWMGR = $(addprefix $(AMD_PP_PATH)/hwmgr/,$(HARDWARE_MGR))

AMD_POWERPLAY_FILES += $(AMD_PP_HWMGR)
