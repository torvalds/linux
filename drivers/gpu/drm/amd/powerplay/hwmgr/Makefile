# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the 'hw manager' sub-component of powerplay.
# It provides the hardware management services for the driver.

HARDWARE_MGR = hwmgr.o processpptables.o \
		hardwaremanager.o pp_acpi.o cz_hwmgr.o \
		cz_clockpowergating.o pppcielanes.o\
		process_pptables_v1_0.o ppatomctrl.o ppatomfwctrl.o \
		smu7_hwmgr.o smu7_powertune.o smu7_thermal.o \
		smu7_clockpowergating.o \
		vega10_processpptables.o vega10_hwmgr.o vega10_powertune.o \
		vega10_thermal.o rv_hwmgr.o pp_psm.o\
		pp_overdriver.o

AMD_PP_HWMGR = $(addprefix $(AMD_PP_PATH)/hwmgr/,$(HARDWARE_MGR))

AMD_POWERPLAY_FILES += $(AMD_PP_HWMGR)
