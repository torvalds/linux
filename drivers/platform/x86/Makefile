# SPDX-License-Identifier: GPL-2.0
#
# Makefile for linux/drivers/platform/x86
# x86 Platform-Specific Drivers
#

# Windows Management Interface
obj-$(CONFIG_ACPI_WMI)		+= wmi.o
obj-$(CONFIG_WMI_BMOF)		+= wmi-bmof.o

# WMI drivers
obj-$(CONFIG_ALIENWARE_WMI)		+= alienware-wmi.o
obj-$(CONFIG_HUAWEI_WMI)		+= huawei-wmi.o
obj-$(CONFIG_INTEL_WMI_SBL_FW_UPDATE)	+= intel-wmi-sbl-fw-update.o
obj-$(CONFIG_INTEL_WMI_THUNDERBOLT)	+= intel-wmi-thunderbolt.o
obj-$(CONFIG_MXM_WMI)			+= mxm-wmi.o
obj-$(CONFIG_PEAQ_WMI)			+= peaq-wmi.o
obj-$(CONFIG_XIAOMI_WMI)		+= xiaomi-wmi.o

# Acer
obj-$(CONFIG_ACERHDF)		+= acerhdf.o
obj-$(CONFIG_ACER_WIRELESS)	+= acer-wireless.o
obj-$(CONFIG_ACER_WMI)		+= acer-wmi.o

# Apple
obj-$(CONFIG_APPLE_GMUX)	+= apple-gmux.o

# ASUS
obj-$(CONFIG_ASUS_LAPTOP)	+= asus-laptop.o
obj-$(CONFIG_ASUS_WIRELESS)	+= asus-wireless.o
obj-$(CONFIG_ASUS_WMI)		+= asus-wmi.o
obj-$(CONFIG_ASUS_NB_WMI)	+= asus-nb-wmi.o
obj-$(CONFIG_EEEPC_LAPTOP)	+= eeepc-laptop.o
obj-$(CONFIG_EEEPC_WMI)		+= eeepc-wmi.o

# Dell
obj-$(CONFIG_DCDBAS)			+= dcdbas.o
obj-$(CONFIG_DELL_SMBIOS)		+= dell-smbios.o
dell-smbios-objs			:= dell-smbios-base.o
dell-smbios-$(CONFIG_DELL_SMBIOS_WMI)	+= dell-smbios-wmi.o
dell-smbios-$(CONFIG_DELL_SMBIOS_SMM)	+= dell-smbios-smm.o
obj-$(CONFIG_DELL_LAPTOP)		+= dell-laptop.o
obj-$(CONFIG_DELL_RBTN)			+= dell-rbtn.o
obj-$(CONFIG_DELL_RBU)			+= dell_rbu.o
obj-$(CONFIG_DELL_SMO8800)		+= dell-smo8800.o
obj-$(CONFIG_DELL_WMI)			+= dell-wmi.o
obj-$(CONFIG_DELL_WMI_DESCRIPTOR)	+= dell-wmi-descriptor.o
obj-$(CONFIG_DELL_WMI_AIO)		+= dell-wmi-aio.o
obj-$(CONFIG_DELL_WMI_LED)		+= dell-wmi-led.o

# Fujitsu
obj-$(CONFIG_AMILO_RFKILL)	+= amilo-rfkill.o
obj-$(CONFIG_FUJITSU_LAPTOP)	+= fujitsu-laptop.o
obj-$(CONFIG_FUJITSU_TABLET)	+= fujitsu-tablet.o

# GPD
obj-$(CONFIG_GPD_POCKET_FAN)	+= gpd-pocket-fan.o

# Hewlett Packard
obj-$(CONFIG_HP_ACCEL)		+= hp_accel.o
obj-$(CONFIG_HP_WIRELESS)	+= hp-wireless.o
obj-$(CONFIG_HP_WMI)		+= hp-wmi.o
obj-$(CONFIG_TC1100_WMI)	+= tc1100-wmi.o

# IBM Thinkpad and Lenovo
obj-$(CONFIG_IBM_RTL)		+= ibm_rtl.o
obj-$(CONFIG_IDEAPAD_LAPTOP)	+= ideapad-laptop.o
obj-$(CONFIG_SENSORS_HDAPS)	+= hdaps.o
obj-$(CONFIG_THINKPAD_ACPI)	+= thinkpad_acpi.o

# Intel
obj-$(CONFIG_INTEL_ATOMISP2_LED)	+= intel_atomisp2_led.o
obj-$(CONFIG_INTEL_ATOMISP2_PM)		+= intel_atomisp2_pm.o
obj-$(CONFIG_INTEL_CHT_INT33FE)		+= intel_cht_int33fe.o
intel_cht_int33fe-objs			:= intel_cht_int33fe_common.o \
					   intel_cht_int33fe_typec.o \
					   intel_cht_int33fe_microb.o
obj-$(CONFIG_INTEL_HID_EVENT)		+= intel-hid.o
obj-$(CONFIG_INTEL_INT0002_VGPIO)	+= intel_int0002_vgpio.o
obj-$(CONFIG_INTEL_MENLOW)		+= intel_menlow.o
obj-$(CONFIG_INTEL_OAKTRAIL)		+= intel_oaktrail.o
obj-$(CONFIG_INTEL_VBTN)		+= intel-vbtn.o

# Microsoft
obj-$(CONFIG_SURFACE3_WMI)		+= surface3-wmi.o
obj-$(CONFIG_SURFACE_3_BUTTON)		+= surface3_button.o
obj-$(CONFIG_SURFACE_3_POWER_OPREGION)	+= surface3_power.o
obj-$(CONFIG_SURFACE_PRO3_BUTTON)	+= surfacepro3_button.o

# MSI
obj-$(CONFIG_MSI_LAPTOP)	+= msi-laptop.o
obj-$(CONFIG_MSI_WMI)		+= msi-wmi.o

# OLPC
obj-$(CONFIG_XO15_EBOOK)	+= xo15-ebook.o
obj-$(CONFIG_XO1_RFKILL)	+= xo1-rfkill.o

# PC Engines
obj-$(CONFIG_PCENGINES_APU2)	+= pcengines-apuv2.o

# Samsung
obj-$(CONFIG_SAMSUNG_LAPTOP)	+= samsung-laptop.o
obj-$(CONFIG_SAMSUNG_Q10)	+= samsung-q10.o

# Toshiba
obj-$(CONFIG_TOSHIBA_BT_RFKILL)	+= toshiba_bluetooth.o
obj-$(CONFIG_TOSHIBA_HAPS)	+= toshiba_haps.o
obj-$(CONFIG_TOSHIBA_WMI)	+= toshiba-wmi.o

# toshiba_acpi must link after wmi to ensure that wmi devices are found
# before toshiba_acpi initializes
obj-$(CONFIG_ACPI_TOSHIBA)	+= toshiba_acpi.o

# Laptop drivers
obj-$(CONFIG_ACPI_CMPC)		+= classmate-laptop.o
obj-$(CONFIG_COMPAL_LAPTOP)	+= compal-laptop.o
obj-$(CONFIG_LG_LAPTOP)		+= lg-laptop.o
obj-$(CONFIG_PANASONIC_LAPTOP)	+= panasonic-laptop.o
obj-$(CONFIG_SONY_LAPTOP)	+= sony-laptop.o
obj-$(CONFIG_SYSTEM76_ACPI)	+= system76_acpi.o
obj-$(CONFIG_TOPSTAR_LAPTOP)	+= topstar-laptop.o

# Platform drivers
obj-$(CONFIG_I2C_MULTI_INSTANTIATE)	+= i2c-multi-instantiate.o
obj-$(CONFIG_MLX_PLATFORM)		+= mlx-platform.o
obj-$(CONFIG_TOUCHSCREEN_DMI)		+= touchscreen_dmi.o

# Intel uncore drivers
obj-$(CONFIG_INTEL_IPS)				+= intel_ips.o
obj-$(CONFIG_INTEL_RST)				+= intel-rst.o
obj-$(CONFIG_INTEL_SMARTCONNECT)		+= intel-smartconnect.o
obj-$(CONFIG_INTEL_SPEED_SELECT_INTERFACE)	+= intel_speed_select_if/
obj-$(CONFIG_INTEL_TURBO_MAX_3)			+= intel_turbo_max_3.o
obj-$(CONFIG_INTEL_UNCORE_FREQ_CONTROL)		+= intel-uncore-frequency.o

# Intel PMIC / PMC / P-Unit devices
obj-$(CONFIG_INTEL_BXTWC_PMIC_TMU)	+= intel_bxtwc_tmu.o
obj-$(CONFIG_INTEL_CHTDC_TI_PWRBTN)	+= intel_chtdc_ti_pwrbtn.o
obj-$(CONFIG_INTEL_MFLD_THERMAL)	+= intel_mid_thermal.o
obj-$(CONFIG_INTEL_MID_POWER_BUTTON)	+= intel_mid_powerbtn.o
obj-$(CONFIG_INTEL_MRFLD_PWRBTN)	+= intel_mrfld_pwrbtn.o
obj-$(CONFIG_INTEL_PMC_CORE)		+= intel_pmc_core.o intel_pmc_core_pltdrv.o
obj-$(CONFIG_INTEL_PMT_CLASS)		+= intel_pmt_class.o
obj-$(CONFIG_INTEL_PMT_TELEMETRY)	+= intel_pmt_telemetry.o
obj-$(CONFIG_INTEL_PMT_CRASHLOG)	+= intel_pmt_crashlog.o
obj-$(CONFIG_INTEL_PUNIT_IPC)		+= intel_punit_ipc.o
obj-$(CONFIG_INTEL_SCU_IPC)		+= intel_scu_ipc.o
obj-$(CONFIG_INTEL_SCU_PCI)		+= intel_scu_pcidrv.o
obj-$(CONFIG_INTEL_SCU_PLATFORM)	+= intel_scu_pltdrv.o
obj-$(CONFIG_INTEL_SCU_IPC_UTIL)	+= intel_scu_ipcutil.o
obj-$(CONFIG_INTEL_TELEMETRY)		+= intel_telemetry_core.o \
					   intel_telemetry_pltdrv.o \
					   intel_telemetry_debugfs.o
obj-$(CONFIG_PMC_ATOM)			+= pmc_atom.o
