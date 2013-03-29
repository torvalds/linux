#
# Makefile for linux/drivers/platform/x86
# x86 Platform-Specific Drivers
#
obj-$(CONFIG_ASUS_LAPTOP)	+= asus-laptop.o
obj-$(CONFIG_ASUS_WMI)		+= asus-wmi.o
obj-$(CONFIG_ASUS_NB_WMI)	+= asus-nb-wmi.o
obj-$(CONFIG_EEEPC_LAPTOP)	+= eeepc-laptop.o
obj-$(CONFIG_EEEPC_WMI)		+= eeepc-wmi.o
obj-$(CONFIG_MSI_LAPTOP)	+= msi-laptop.o
obj-$(CONFIG_ACPI_CMPC)		+= classmate-laptop.o
obj-$(CONFIG_COMPAL_LAPTOP)	+= compal-laptop.o
obj-$(CONFIG_DELL_LAPTOP)	+= dell-laptop.o
obj-$(CONFIG_DELL_WMI)		+= dell-wmi.o
obj-$(CONFIG_DELL_WMI_AIO)	+= dell-wmi-aio.o
obj-$(CONFIG_ACER_WMI)		+= acer-wmi.o
obj-$(CONFIG_ACERHDF)		+= acerhdf.o
obj-$(CONFIG_HP_ACCEL)		+= hp_accel.o
obj-$(CONFIG_HP_WMI)		+= hp-wmi.o
obj-$(CONFIG_AMILO_RFKILL)	+= amilo-rfkill.o
obj-$(CONFIG_TC1100_WMI)	+= tc1100-wmi.o
obj-$(CONFIG_SONY_LAPTOP)	+= sony-laptop.o
obj-$(CONFIG_IDEAPAD_LAPTOP)	+= ideapad-laptop.o
obj-$(CONFIG_THINKPAD_ACPI)	+= thinkpad_acpi.o
obj-$(CONFIG_SENSORS_HDAPS)	+= hdaps.o
obj-$(CONFIG_FUJITSU_LAPTOP)	+= fujitsu-laptop.o
obj-$(CONFIG_FUJITSU_TABLET)	+= fujitsu-tablet.o
obj-$(CONFIG_PANASONIC_LAPTOP)	+= panasonic-laptop.o
obj-$(CONFIG_INTEL_MENLOW)	+= intel_menlow.o
obj-$(CONFIG_ACPI_WMI)		+= wmi.o
obj-$(CONFIG_MSI_WMI)		+= msi-wmi.o
obj-$(CONFIG_TOPSTAR_LAPTOP)	+= topstar-laptop.o

# toshiba_acpi must link after wmi to ensure that wmi devices are found
# before toshiba_acpi initializes
obj-$(CONFIG_ACPI_TOSHIBA)	+= toshiba_acpi.o

obj-$(CONFIG_TOSHIBA_BT_RFKILL)	+= toshiba_bluetooth.o
obj-$(CONFIG_INTEL_SCU_IPC)	+= intel_scu_ipc.o
obj-$(CONFIG_INTEL_SCU_IPC_UTIL) += intel_scu_ipcutil.o
obj-$(CONFIG_INTEL_MFLD_THERMAL) += intel_mid_thermal.o
obj-$(CONFIG_INTEL_IPS)		+= intel_ips.o
obj-$(CONFIG_GPIO_INTEL_PMIC)	+= intel_pmic_gpio.o
obj-$(CONFIG_XO1_RFKILL)	+= xo1-rfkill.o
obj-$(CONFIG_XO15_EBOOK)	+= xo15-ebook.o
obj-$(CONFIG_IBM_RTL)		+= ibm_rtl.o
obj-$(CONFIG_SAMSUNG_LAPTOP)	+= samsung-laptop.o
obj-$(CONFIG_MXM_WMI)		+= mxm-wmi.o
obj-$(CONFIG_INTEL_MID_POWER_BUTTON)	+= intel_mid_powerbtn.o
obj-$(CONFIG_INTEL_OAKTRAIL)	+= intel_oaktrail.o
obj-$(CONFIG_SAMSUNG_Q10)	+= samsung-q10.o
obj-$(CONFIG_APPLE_GMUX)	+= apple-gmux.o
obj-$(CONFIG_CHROMEOS_LAPTOP)	+= chromeos_laptop.o
