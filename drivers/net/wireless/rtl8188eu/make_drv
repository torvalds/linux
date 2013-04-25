#!/bin/bash
#========================================
# Common functions
#========================================
intf_usb()
{
	sed -i '/CONFIG_USB_HCI = /c\CONFIG_USB_HCI = y' Makefile
	sed -i '/CONFIG_PCI_HCI = /c\CONFIG_PCI_HCI = n' Makefile
	sed -i '/CONFIG_SDIO_HCI = /c\CONFIG_SDIO_HCI = n' Makefile
}
intf_pcie()
{
	sed -i '/CONFIG_USB_HCI = /c\CONFIG_USB_HCI = n' Makefile
	sed -i '/CONFIG_PCI_HCI = /c\CONFIG_PCI_HCI = y' Makefile
	sed -i '/CONFIG_SDIO_HCI = /c\CONFIG_SDIO_HCI = n' Makefile
}
intf_sdio()
{
	sed -i '/CONFIG_USB_HCI = /c\CONFIG_USB_HCI = n' Makefile
	sed -i '/CONFIG_PCI_HCI = /c\CONFIG_PCI_HCI = n' Makefile
	sed -i '/CONFIG_SDIO_HCI = /c\CONFIG_SDIO_HCI = y' Makefile
}

nic_8192c()
{
	sed -i '/CONFIG_RTL8192C = /c\CONFIG_RTL8192C = y' Makefile
	sed -i '/CONFIG_RTL8192D = /c\CONFIG_RTL8192D = n' Makefile
	sed -i '/CONFIG_RTL8723A = /c\CONFIG_RTL8723A = n' Makefile
	sed -i '/CONFIG_RTL8188E = /c\CONFIG_RTL8188E = n' Makefile
}
nic_8192d()
{
	sed -i '/CONFIG_RTL8192C = /c\CONFIG_RTL8192C = n' Makefile
	sed -i '/CONFIG_RTL8192D = /c\CONFIG_RTL8192D = y' Makefile
	sed -i '/CONFIG_RTL8723A = /c\CONFIG_RTL8723A = n' Makefile
	sed -i '/CONFIG_RTL8188E = /c\CONFIG_RTL8188E = n' Makefile
}
nic_8723a(){
	sed -i '/CONFIG_RTL8192C = /c\CONFIG_RTL8192C = n' Makefile
	sed -i '/CONFIG_RTL8192D = /c\CONFIG_RTL8192D = n' Makefile
	sed -i '/CONFIG_RTL8723A = /c\CONFIG_RTL8723A = y' Makefile
	sed -i '/CONFIG_RTL8188E = /c\CONFIG_RTL8188E = n' Makefile
}
nic_8188e(){
	sed -i '/CONFIG_RTL8192C = /c\CONFIG_RTL8192C = n' Makefile
	sed -i '/CONFIG_RTL8192D = /c\CONFIG_RTL8192D = n' Makefile
	sed -i '/CONFIG_RTL8723A = /c\CONFIG_RTL8723A = n' Makefile
	sed -i '/CONFIG_RTL8188E = /c\CONFIG_RTL8188E = y' Makefile
}
ch_obj_cu()
{
	sed -i '/obj-$(CONFIG_RTL/c\obj-$(CONFIG_RTL8192CU) := $(MODULE_NAME).o' Makefile
	sed -i '/export CONFIG_RTL/c\export CONFIG_RTL8192CU = m' Makefile

	cp -f Kconfig_rtl8192c_usb_linux Kconfig
}

ch_obj_ce()
{
	sed -i '/obj-$(CONFIG_RTL/c\obj-$(CONFIG_RTL8192CE) := $(MODULE_NAME).o' Makefile
	sed -i '/export CONFIG_RTL/c\export CONFIG_RTL8192CE = m' Makefile

	cp -f Kconfig_rtl8192c_pci_linux Kconfig
}

ch_obj_du()
{
	sed -i '/obj-$(CONFIG_RTL/c\obj-$(CONFIG_RTL8192DU) := $(MODULE_NAME).o' Makefile
	sed -i '/export CONFIG_RTL/c\export CONFIG_RTL8192DU = m' Makefile

	cp -f Kconfig_rtl8192d_usb_linux Kconfig
}

ch_obj_de()
{
	sed -i '/obj-$(CONFIG_RTL/c\obj-$(CONFIG_RTL8192DE) := $(MODULE_NAME).o' Makefile
	sed -i '/export CONFIG_RTL/c\export CONFIG_RTL8192DE = m' Makefile

	cp -f Kconfig_rtl8192d_pci_linux Kconfig
}

ch_obj_as()
{
	sed -i '/obj-$(CONFIG_RTL/c\obj-$(CONFIG_RTL8723AS) := $(MODULE_NAME).o' Makefile
	sed -i '/export CONFIG_RTL/c\export CONFIG_RTL8723AS = m' Makefile

#	sed -i '/CONFIG_POWER_SAVING = /c\CONFIG_POWER_SAVING = y' Makefile

	cp -f Kconfig_rtl8723a_sdio_linux Kconfig
}

ch_obj_au()
{
	sed -i '/obj-$(CONFIG_RTL/ c obj-$(CONFIG_RTL8723AS-VAU) := $(MODULE_NAME).o' Makefile
	sed -i '/export CONFIG_RTL/ c export CONFIG_RTL8723AS-VAU = m' Makefile

#	sed -i '/CONFIG_POWER_SAVING = /c\CONFIG_POWER_SAVING = n' Makefile

	cp -f Kconfig_rtl8723a_usb_linux Kconfig
}

ch_obj_es()
{
	sed -i '/obj-$(CONFIG_RTL/c\obj-$(CONFIG_RTL8189ES) := $(MODULE_NAME).o' Makefile
	sed -i '/export CONFIG_RTL/c\export CONFIG_RTL8189ES = m' Makefile

	cp -f Kconfig_rtl8189e_sdio_linux Kconfig
}

ch_obj_eu()
{
	sed -i '/obj-$(CONFIG_RTL/c\obj-$(CONFIG_RTL8188EU) := $(MODULE_NAME).o' Makefile
	sed -i '/export CONFIG_RTL/c\export CONFIG_RTL8188EU = m' Makefile

	cp -f Kconfig_rtl8188e_usb_linux Kconfig
}

ch_obj_ee()
{
	sed -i '/obj-$(CONFIG_RTL/c\obj-$(CONFIG_RTL8188EE) := $(MODULE_NAME).o' Makefile
	sed -i '/export CONFIG_RTL/c\export CONFIG_RTL8188EE = m' Makefile

	cp -f Kconfig_rtl8188e_pci_linux Kconfig
}

get_svn_revision()
{
	if [ -z "$svn_rev" ] && [ -f .svn/entries ]; then
		svn_rev=`sed -n '11 s/^\([0-9][0-9]*\)$/\1/p' .svn/entries`
	fi
	if [ -z "$svn_rev" ]; then
		svn_rev="xxxx"
	fi
}

#========================================
# Sub functions
#========================================

add_version_file()
{
	defversion="#define DRIVERVERSION\t\"$postfix\""
	echo -e $defversion > include/rtw_version.h
}

conf_driver()
{
	sed -i '/CONFIG_AUTOCFG_CP = /c\CONFIG_AUTOCFG_CP = y' Makefile
	if [ -f include/rtw_version.h ]; then
		echo "rtw_version.h has existed!"
	else
		add_version_file
	fi
}

#========================================
# Main Script Start From Here
#========================================

if [ ! -f include/rtw_version.h ]; then
	# Make Version string
	if [ -z "$2" ]; then
		get_svn_revision
		version="v4.1.2_"$svn_rev
		datestr=$(date +%Y%m%d)
		postfix=$version.$datestr
	else
		postfix=$2
	fi
fi

if [ $# -eq 1 ]; then
	card=$1
	echo "You have selected $card"
else
	# Select NIC type
	echo "Please select card type(1/2):"
	select card in RTL8188eus RTL8189es;
	do
		echo "You have selected $card"
		break
	done
fi

# Make
case "$card" in
	[Rr][Tt][Ll]8192[Cc][Uu])
	conf_driver;
	nic_8192c;
	intf_usb;
	ch_obj_cu;;
	[Rr][Tt][Ll]8192[Cc][Ee])
	conf_driver;
	nic_8192c;
	intf_pcie;
	ch_obj_ce;;
	[Rr][Tt][Ll]8192[Dd][Uu])
	conf_driver;
	nic_8192d;
	intf_usb;
	ch_obj_du;;
	[Rr][Tt][Ll]8192[Dd][Ee])
	conf_driver;
	nic_8192d;
	intf_pcie;
	ch_obj_de;;
	[Rr][Tt][Ll]8723[Aa][Ss])
	conf_driver;
	nic_8723a;
	intf_sdio;
	ch_obj_as;;
	[Rr][Tt][Ll]8723[Aa][Ss]-[Vv][Aa][Uu])
	conf_driver;
	nic_8723a;
	intf_usb;
	ch_obj_au;;
	[Rr][Tt][Ll]8189[Ee][Ss])
	conf_driver;
	nic_8188e;
	intf_sdio;
	ch_obj_es;;
	[Rr][Tt][Ll]8188[Ee][Uu][Ss])
	conf_driver;
	nic_8188e;
	intf_usb;
	ch_obj_eu;;	
	[Rr][Tt][Ll]8188[Ee][Ee])
	conf_driver;
	nic_8188e;
	intf_pcie;
	ch_obj_ee;;
	*)
	echo "Unknown NIC type"
	;;
esac
