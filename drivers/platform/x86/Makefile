# SPDX-License-Identifier: GPL-2.0
#
# Makefile for linux/drivers/platform/x86
# x86 Platform-Specific Drivers
#

# Windows Management Interface
obj-$(CONFIG_ACPI_WMI)		+= wmi.o
obj-$(CONFIG_WMI_BMOF)		+= wmi-bmof.o

# WMI drivers
obj-$(CONFIG_HUAWEI_WMI)		+= huawei-wmi.o
obj-$(CONFIG_MXM_WMI)			+= mxm-wmi.o
obj-$(CONFIG_PEAQ_WMI)			+= peaq-wmi.o
obj-$(CONFIG_XIAOMI_WMI)		+= xiaomi-wmi.o
obj-$(CONFIG_GIGABYTE_WMI)		+= gigabyte-wmi.o

# Acer
obj-$(CONFIG_ACERHDF)		+= acerhdf.o
obj-$(CONFIG_ACER_WIRELESS)	+= acer-wireless.o
obj-$(CONFIG_ACER_WMI)		+= acer-wmi.o

# AMD
obj-$(CONFIG_AMD_PMC)		+= amd-pmc.o

# Advantech
obj-$(CONFIG_ADV_SWBUTTON)	+= adv_swbutton.o

# Apple
obj-$(CONFIG_APPLE_GMUX)	+= apple-gmux.o

# ASUS
obj-$(CONFIG_ASUS_LAPTOP)	+= asus-laptop.o
obj-$(CONFIG_ASUS_WIRELESS)	+= asus-wireless.o
obj-$(CONFIG_ASUS_WMI)		+= asus-wmi.o
obj-$(CONFIG_ASUS_NB_WMI)	+= asus-nb-wmi.o
obj-$(CONFIG_EEEPC_LAPTOP)	+= eeepc-laptop.o
obj-$(CONFIG_EEEPC_WMI)		+= eeepc-wmi.o

# Cisco/Meraki
obj-$(CONFIG_MERAKI_MX100)	+= meraki-mx100.o

# Dell
obj-$(CONFIG_X86_PLATFORM_DRIVERS_DELL)		+= dell/

# Fujitsu
obj-$(CONFIG_AMILO_RFKILL)	+= amilo-rfkill.o
obj-$(CONFIG_FUJITSU_LAPTOP)	+= fujitsu-laptop.o
obj-$(CONFIG_FUJITSU_TABLET)	+= fujitsu-tablet.o

# GPD
obj-$(CONFIG_GPD_POCKET_FAN)	+= gpd-pocket-fan.o

# Hewlett Packard
obj-$(CONFIG_HP_ACCEL)		+= hp_accel.o
obj-$(CONFIG_HP_WMI)		+= hp-wmi.o
obj-$(CONFIG_TC1100_WMI)	+= tc1100-wmi.o

# Hewlett Packard Enterprise
obj-$(CONFIG_UV_SYSFS)       += uv_sysfs.o

# IBM Thinkpad and Lenovo
obj-$(CONFIG_IBM_RTL)		+= ibm_rtl.o
obj-$(CONFIG_IDEAPAD_LAPTOP)	+= ideapad-laptop.o
obj-$(CONFIG_SENSORS_HDAPS)	+= hdaps.o
obj-$(CONFIG_THINKPAD_ACPI)	+= thinkpad_acpi.o
obj-$(CONFIG_THINKPAD_LMI)	+= think-lmi.o

# Intel
obj-$(CONFIG_X86_PLATFORM_DRIVERS_INTEL)		+= intel/

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
obj-$(CONFIG_FW_ATTR_CLASS)		+= firmware_attributes_class.o
obj-$(CONFIG_I2C_MULTI_INSTANTIATE)	+= i2c-multi-instantiate.o
obj-$(CONFIG_MLX_PLATFORM)		+= mlx-platform.o
obj-$(CONFIG_TOUCHSCREEN_DMI)		+= touchscreen_dmi.o
obj-$(CONFIG_WIRELESS_HOTKEY)		+= wireless-hotkey.o

# Intel uncore drivers
obj-$(CONFIG_INTEL_IPS)				+= intel_ips.o

# Intel PMIC / PMC / P-Unit devices
obj-$(CONFIG_INTEL_SCU_IPC)		+= intel_scu_ipc.o
obj-$(CONFIG_INTEL_SCU_PCI)		+= intel_scu_pcidrv.o
obj-$(CONFIG_INTEL_SCU_PLATFORM)	+= intel_scu_pltdrv.o
obj-$(CONFIG_INTEL_SCU_WDT)		+= intel_scu_wdt.o
obj-$(CONFIG_INTEL_SCU_IPC_UTIL)	+= intel_scu_ipcutil.o
obj-$(CONFIG_PMC_ATOM)			+= pmc_atom.o
