obj-$(CONFIG_RTLWIFI) 		+= rtlwifi.o
rtlwifi-objs	:=		\
		base.o		\
		cam.o		\
		core.o		\
		debug.o		\
		efuse.o		\
		ps.o		\
		rc.o		\
		regd.o		\
		stats.o

rtl8192c_common-objs +=		\

ifneq ($(CONFIG_PCI),)
rtlwifi-objs	+= pci.o
endif

ifneq ($(CONFIG_USB),)
rtlwifi-objs	+= usb.o
endif

obj-$(CONFIG_RTL8192C_COMMON)	+= rtl8192c/
obj-$(CONFIG_RTL8192CE)		+= rtl8192ce/
obj-$(CONFIG_RTL8192CU)		+= rtl8192cu/
obj-$(CONFIG_RTL8192SE)		+= rtl8192se/
obj-$(CONFIG_RTL8192DE)		+= rtl8192de/
obj-$(CONFIG_RTL8723AE)		+= rtl8723ae/
obj-$(CONFIG_RTL8188EE)		+= rtl8188ee/

ccflags-y += -D__CHECK_ENDIAN__
