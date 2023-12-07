MODULE_NAME = aic_btusb

CONFIG_AIC8800_BTUSB_SUPPORT = m
CONFIG_SUPPORT_VENDOR_APCF = n
# Need to set fw path in BOARD_KERNEL_CMDLINE
CONFIG_USE_FW_REQUEST = n

ifeq ($(CONFIG_SUPPORT_VENDOR_APCF), y)
obj-$(CONFIG_AIC8800_BTUSB_SUPPORT) := $(MODULE_NAME).o aic_btusb_external_featrue.o
else
obj-$(CONFIG_AIC8800_BTUSB_SUPPORT) := $(MODULE_NAME).o
endif

ccflags-$(CONFIG_SUPPORT_VENDOR_APCF) += -DCONFIG_SUPPORT_VENDOR_APCF
#$(MODULE_NAME)-y := aic_btusb_ioctl.o\
#	aic_btusb.o \

ccflags-$(CONFIG_USE_FW_REQUEST) += -DCONFIG_USE_FW_REQUEST


# Platform support list
CONFIG_PLATFORM_ROCKCHIP ?= n
CONFIG_PLATFORM_ALLWINNER ?= n
CONFIG_PLATFORM_AMLOGIC ?= n
CONFIG_PLATFORM_UBUNTU ?= y
CONFIG_PLATFORM_ING ?= n

ifeq ($(CONFIG_PLATFORM_ING), y)
ARCH := mips
KDIR ?= /home/yaya/E/Ingenic/T31/Ingenic-SDK-T31-1.1.5-20220428/opensource/kernel
CROSS_COMPILE := /home/yaya/E/Ingenic/T31/Ingenic-SDK-T31-1.1.5-20220428/resource/toolchain/gcc_472/mips-gcc472-glibc216-32bit/bin/mips-linux-gnu-
endif

ifeq ($(CONFIG_PLATFORM_ROCKCHIP), y)
ccflags-$(CONFIG_PLATFORM_ROCKCHIP) += -DCONFIG_PLATFORM_ROCKCHIP
#KDIR  := /home/yaya/E/Rockchip/3229/Android9/rk3229_android9.0_box/kernel
#ARCH ?= arm
#CROSS_COMPILE ?= /home/yaya/E/Rockchip/3229/Android9/rk3229_android9.0_box/prebuilts/gcc/linux-x86/arm/gcc-linaro-6.3.1-2017.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-
#KDIR := /home/yaya/E/Rockchip/3229/Android7/RK3229_ANDROID7.1_v1.01_20170914/rk3229_Android7.1_v1.01_xml0914/kernel
#ARCH ?= arm
#CROSS_COMPILE ?= /home/yaya/E/Rockchip/3229/Android7/RK3229_ANDROID7.1_v1.01_20170914/rk3229_Android7.1_v1.01_xml0914/prebuilts/gcc/linux-x86/arm/arm-eabi-4.6/bin/arm-eabi-
ARCH := arm64
KDIR ?= /home/yaya/E/Rockchip/3566/firefly/Android11.0/Firefly-RK356X_Android11.0_git_20210824/RK356X_Android11.0/kernel
CROSS_COMPILE := /home/yaya/E/Rockchip/3566/firefly/Android11.0/Firefly-RK356X_Android11.0_git_20210824/RK356X_Android11.0/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-

endif

ifeq ($(CONFIG_PLATFORM_ALLWINNER), y)
ccflags-$(CONFIG_PLATFORM_ALLWINNER) += -DCONFIG_PLATFORM_ALLWINNER
KDIR  := /home/yaya/E/Allwinner/R818/R818/AndroidQ/lichee/kernel/linux-4.9
ARCH ?= arm64
CROSS_COMPILE ?= /home/yaya/E/Allwinner/R818/R818/AndroidQ/lichee/out/gcc-linaro-5.3.1-2016.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
endif

ifeq ($(CONFIG_PLATFORM_AMLOGIC), y)
        ccflags-$(CONFIG_PLATFORM_AMLOGIC) += -DCONFIG_PLATFORM_AMLOGIC
endif

ifeq ($(CONFIG_PLATFORM_UBUNTU), y)
ccflags-$(CONFIG_PLATFORM_UBUNTU) += -DCONFIG_PLATFORM_UBUNTU
endif

all: modules
modules:
	make -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

install:
	mkdir -p $(MODDESTDIR)
	install -p -m 644 $(MODULE_NAME).ko  $(MODDESTDIR)/
	/sbin/depmod -a ${KVER}
	echo $(MODULE_NAME) >> /etc/modules-load.d/aic_bt.conf

uninstall:
	rm -rfv $(MODDESTDIR)/$(MODULE_NAME).ko
	/sbin/depmod -a ${KVER}
	rm -f /etc/modules-load.d/aic_bt.conf

clean:
	rm -rf *.o *.ko *.o.* *.mod.* modules.* Module.* .a* .o* .*.o.* *.mod .tmp* .cache.mk .Module.symvers.cmd .modules.order.cmd

