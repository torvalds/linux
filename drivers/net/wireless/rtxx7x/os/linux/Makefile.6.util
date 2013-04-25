include $(RT28xx_DIR)/os/linux/config.mk

#ifdef CONFIG_AP_SUPPORT
ifeq ($(RT28xx_MODE),AP)
MOD_NAME = rtutil$(CHIPSET)ap
endif
#endif // CONFIG_AP_SUPPORT //

#ifdef CONFIG_STA_SUPPORT
ifeq ($(RT28xx_MODE), STA)
MOD_NAME = rtutil$(CHIPSET)sta
endif
#endif // CONFIG_STA_SUPPORT //

#ifdef CONFIG_APSTA_SUPPORT
ifeq ($(RT28xx_MODE), APSTA)
MOD_NAME = rtutil$(CHIPSET)apsta
endif
#endif // CONFIG_APSTA_SUPPORT //

obj-m := $(MOD_NAME).o

#ifdef CONFIG_AP_SUPPORT

rtutil$(CHIPSET)ap-objs := \
	../../common/rt_os_util.o\
	../../os/linux/rt_linux_symb.o\
	../../os/linux/rt_rbus_pci_util.o\
	../../os/linux/rt_usb_util.o\
	../../os/linux/rt_linux.o

ifeq ($(PLATFORM),BL2348)
rtutil$(CHIPSET)ap-objs += \
	../../os/linux/vr_bdlt.o
endif

ifeq ($(PLATFORM),BLUBB)
rtutil$(CHIPSET)ap-objs += \
	../../os/linux/vr_bdlt.o
endif

ifeq ($(HAS_BGFP_SUPPORT),y)
rtutil$(CHIPSET)ap-objs += \
	../../os/linux/br_ftph.o
endif

#endif // CONFIG_AP_SUPPORT //


#ifdef CONFIG_STA_SUPPORT

rtutil$(CHIPSET)sta-objs := \
	../../common/rt_os_util.o\
	../../os/linux/rt_linux_symb.o\
	../../os/linux/rt_rbus_pci_util.o\
	../../os/linux/rt_usb_util.o\
	../../os/linux/rt_linux.o
#endif // CONFIG_STA_SUPPORT //


#ifdef CONFIG_APSTA_SUPPORT
rtutil$(CHIPSET)apsta-objs := \
	../../common/rt_os_util.o\
	../../os/linux/rt_linux_symb.o\
	../../os/linux/rt_rbus_pci_util.o\
	../../os/linux/rt_usb_util.o\
	../../os/linux/rt_linux.o

ifeq ($(PLATFORM),BL2348)
rtutil$(CHIPSET)ap-objs += \
	../../os/linux/vr_bdlt.o
endif

ifeq ($(PLATFORM),BLUBB)
rtutil$(CHIPSET)ap-objs += \
	../../os/linux/vr_bdlt.o
endif

ifeq ($(HAS_BGFP_SUPPORT),y)
rtutil$(CHIPSET)ap-objs += \
	../../os/linux/br_ftph.o
endif

#endif // CONFIG_APSTA_SUPPORT //

clean:
	rm -f ../../common/*.o
	rm -f ../../common/.*.{cmd,flags,d}
	rm -f ../../os/linux/*.{o,ko,mod.{o,c}}
	rm -f ../../os/linux/.*.{cmd,flags,d}
	rm -fr ../../os/linux/.tmp_versions
	rm -f ../../os/linux/Module.symvers
	rm -f ../../os/linux/Modules.symvers
	rm -f ../../os/linux/Module.markers
	rm -f ../../os/linux/modules.order
	rm -f ../../chips/*.o
	rm -f ../../chips/.*.{cmd,flags,d}
ifeq ($(RT28xx_MODE),AP)
	rm -f ../../ap/*.o
	rm -f ../../ap/.*.{cmd,flags,d}
else
ifeq ($(RT28xx_MODE),STA)
	rm -f ../../sta/*.o
	rm -f ../../sta/.*.{cmd,flags,d}
endif
endif

install:
	install -d $(LINUX_SRC_MODULE)
	install -m 644 -c $(addsuffix .ko,$(MOD_NAME)) $(LINUX_SRC_MODULE)
	/sbin/depmod -a ${shell uname -r}

uninstall:
	rm -rf $(addprefix $(LINUX_SRC_MODULE),$(addsuffix .ko,$(MOD_NAME)))
	/sbin/depmod -a ${shell uname -r}