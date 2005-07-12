#
# Makefile for the Macintosh-specific device drivers.
#

# Each configuration option enables a list of files.

obj-$(CONFIG_PPC_PMAC)		+= macio_asic.o macio_sysfs.o

obj-$(CONFIG_PMAC_MEDIABAY)	+= mediabay.o
obj-$(CONFIG_MAC_EMUMOUSEBTN)	+= mac_hid.o
obj-$(CONFIG_INPUT_ADBHID)	+= adbhid.o
obj-$(CONFIG_ANSLCD)		+= ans-lcd.o

obj-$(CONFIG_ADB_PMU)		+= via-pmu.o
obj-$(CONFIG_ADB_CUDA)		+= via-cuda.o
obj-$(CONFIG_PMAC_APM_EMU)	+= apm_emu.o
obj-$(CONFIG_PMAC_SMU)		+= smu.o

obj-$(CONFIG_ADB)		+= adb.o
obj-$(CONFIG_ADB_MACII)		+= via-macii.o
obj-$(CONFIG_ADB_MACIISI)	+= via-maciisi.o
obj-$(CONFIG_ADB_IOP)		+= adb-iop.o
obj-$(CONFIG_ADB_PMU68K)	+= via-pmu68k.o
obj-$(CONFIG_ADB_MACIO)		+= macio-adb.o

obj-$(CONFIG_THERM_PM72)	+= therm_pm72.o
obj-$(CONFIG_THERM_WINDTUNNEL)	+= therm_windtunnel.o
obj-$(CONFIG_THERM_ADT746X)	+= therm_adt746x.o
