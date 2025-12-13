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
obj-$(CONFIG_NVIDIA_WMI_EC_BACKLIGHT)	+= nvidia-wmi-ec-backlight.o
obj-$(CONFIG_XIAOMI_WMI)		+= xiaomi-wmi.o
obj-$(CONFIG_REDMI_WMI)			+= redmi-wmi.o
obj-$(CONFIG_GIGABYTE_WMI)		+= gigabyte-wmi.o

# Acer
obj-$(CONFIG_ACERHDF)		+= acerhdf.o
obj-$(CONFIG_ACER_WIRELESS)	+= acer-wireless.o
obj-$(CONFIG_ACER_WMI)		+= acer-wmi.o

# AMD
obj-y				+= amd/

# Advantech
obj-$(CONFIG_ADV_SWBUTTON)	+= adv_swbutton.o

# Apple
obj-$(CONFIG_APPLE_GMUX)	+= apple-gmux.o

# ASUS
obj-$(CONFIG_ASUS_LAPTOP)	+= asus-laptop.o
obj-$(CONFIG_ASUS_WIRELESS)	+= asus-wireless.o
obj-$(CONFIG_ASUS_WMI)		+= asus-wmi.o
obj-$(CONFIG_ASUS_NB_WMI)	+= asus-nb-wmi.o
obj-$(CONFIG_ASUS_TF103C_DOCK)	+= asus-tf103c-dock.o
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
obj-$(CONFIG_X86_PLATFORM_DRIVERS_HP)	+= hp/

# Hewlett Packard Enterprise
obj-$(CONFIG_UV_SYSFS)       += uv_sysfs.o

obj-$(CONFIG_FW_ATTR_CLASS)	+= firmware_attributes_class.o

# IBM Thinkpad (before 2005)
obj-$(CONFIG_IBM_RTL)		+= ibm_rtl.o
obj-$(CONFIG_SENSORS_HDAPS)	+= hdaps.o

# Lenovo
obj-y				+= lenovo/

# Intel
obj-y				+= intel/

# Microsoft
obj-$(CONFIG_ACPI_QUICKSTART)  += quickstart.o

# MeeGoPad
obj-$(CONFIG_MEEGOPAD_ANX7428)	+= meegopad_anx7428.o

# MSI
obj-$(CONFIG_MSI_EC)		+= msi-ec.o
obj-$(CONFIG_MSI_LAPTOP)	+= msi-laptop.o
obj-$(CONFIG_MSI_WMI)		+= msi-wmi.o
obj-$(CONFIG_MSI_WMI_PLATFORM)	+= msi-wmi-platform.o

# OLPC
obj-$(CONFIG_XO15_EBOOK)	+= xo15-ebook.o
obj-$(CONFIG_XO1_RFKILL)	+= xo1-rfkill.o

# PC Engines
obj-$(CONFIG_PCENGINES_APU2)	+= pcengines-apuv2.o

# Portwell
obj-$(CONFIG_PORTWELL_EC)	+= portwell-ec.o

# Barco
obj-$(CONFIG_BARCO_P50_GPIO)	+= barco-p50-gpio.o

# Samsung
obj-$(CONFIG_SAMSUNG_GALAXYBOOK)	+= samsung-galaxybook.o
obj-$(CONFIG_SAMSUNG_LAPTOP)		+= samsung-laptop.o
obj-$(CONFIG_SAMSUNG_Q10)		+= samsung-q10.o

# Toshiba
obj-$(CONFIG_TOSHIBA_BT_RFKILL)	+= toshiba_bluetooth.o
obj-$(CONFIG_TOSHIBA_HAPS)	+= toshiba_haps.o
obj-$(CONFIG_TOSHIBA_WMI)	+= toshiba-wmi.o

# toshiba_acpi must link after wmi to ensure that wmi devices are found
# before toshiba_acpi initializes
obj-$(CONFIG_ACPI_TOSHIBA)	+= toshiba_acpi.o

# Inspur
obj-$(CONFIG_INSPUR_PLATFORM_PROFILE)	+= inspur_platform_profile.o

# Dasharo
obj-$(CONFIG_DASHARO_ACPI)	+= dasharo-acpi.o

# Laptop drivers
obj-$(CONFIG_ACPI_CMPC)		+= classmate-laptop.o
obj-$(CONFIG_COMPAL_LAPTOP)	+= compal-laptop.o
obj-$(CONFIG_LG_LAPTOP)		+= lg-laptop.o
obj-$(CONFIG_PANASONIC_LAPTOP)	+= panasonic-laptop.o
obj-$(CONFIG_SONY_LAPTOP)	+= sony-laptop.o
obj-$(CONFIG_SYSTEM76_ACPI)	+= system76_acpi.o
obj-$(CONFIG_TOPSTAR_LAPTOP)	+= topstar-laptop.o

# Platform drivers
obj-$(CONFIG_SERIAL_MULTI_INSTANTIATE)	+= serial-multi-instantiate.o
obj-$(CONFIG_TOUCHSCREEN_DMI)		+= touchscreen_dmi.o
obj-$(CONFIG_WIRELESS_HOTKEY)		+= wireless-hotkey.o
obj-$(CONFIG_X86_ANDROID_TABLETS)	+= x86-android-tablets/

# Intel uncore drivers
obj-$(CONFIG_INTEL_IPS)				+= intel_ips.o

# Intel miscellaneous drivers
intel_p2sb-y				:= p2sb.o
obj-$(CONFIG_P2SB)			+= intel_p2sb.o

# Intel PMIC / PMC / P-Unit devices
obj-$(CONFIG_INTEL_SCU_IPC)		+= intel_scu_ipc.o
obj-$(CONFIG_INTEL_SCU_PCI)		+= intel_scu_pcidrv.o
obj-$(CONFIG_INTEL_SCU_PLATFORM)	+= intel_scu_pltdrv.o
obj-$(CONFIG_INTEL_SCU_WDT)		+= intel_scu_wdt.o
obj-$(CONFIG_INTEL_SCU_IPC_UTIL)	+= intel_scu_ipcutil.o
obj-$(CONFIG_X86_INTEL_LPSS)		+= pmc_atom.o

# Siemens Simatic Industrial PCs
obj-$(CONFIG_SIEMENS_SIMATIC_IPC)	+= siemens/

# Silicom
obj-$(CONFIG_SILICOM_PLATFORM)		+= silicom-platform.o

# TUXEDO
obj-y					+= tuxedo/

# Winmate
obj-$(CONFIG_WINMATE_FM07_KEYS)		+= winmate-fm07-keys.o

# SEL
obj-$(CONFIG_SEL3350_PLATFORM)		+= sel3350-platform.o

# OneXPlayer
obj-$(CONFIG_OXP_EC)			+= oxpec.o
