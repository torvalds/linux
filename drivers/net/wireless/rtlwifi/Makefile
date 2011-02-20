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
		usb.o

ifeq ($(CONFIG_PCI),y)
rtlwifi-objs	+= pci.o
endif

obj-$(CONFIG_RTL8192CE)		+= rtl8192ce/
obj-$(CONFIG_RTL8192CU)		+= rtl8192cu/

ccflags-y += -D__CHECK_ENDIAN__
