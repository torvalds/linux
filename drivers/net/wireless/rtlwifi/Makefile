obj-$(CONFIG_RTLWIFI) 		+= rtlwifi.o
rtlwifi-objs	:=		\
		base.o		\
		cam.o		\
		core.o		\
		debug.o		\
		efuse.o		\
		ps.o		\
		rc.o		\
		regd.o

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

ccflags-y += -D__CHECK_ENDIAN__
