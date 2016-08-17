#
# Makefile for the 'hw manager' sub-component of powerplay.
# It provides the hardware management services for the driver.

HARDWARE_MGR = hwmgr.o processpptables.o functiontables.o \
	       hardwaremanager.o pp_acpi.o cz_hwmgr.o \
               cz_clockpowergating.o tonga_powertune.o\
	       tonga_processpptables.o ppatomctrl.o \
               tonga_hwmgr.o pppcielanes.o  tonga_thermal.o\
               fiji_powertune.o fiji_hwmgr.o tonga_clockpowergating.o \
               fiji_clockpowergating.o fiji_thermal.o \
	       polaris10_hwmgr.o polaris10_powertune.o polaris10_thermal.o \
	       polaris10_clockpowergating.o iceland_hwmgr.o \
	       iceland_clockpowergating.o iceland_thermal.o \
	       iceland_powertune.o

AMD_PP_HWMGR = $(addprefix $(AMD_PP_PATH)/hwmgr/,$(HARDWARE_MGR))

AMD_POWERPLAY_FILES += $(AMD_PP_HWMGR)
