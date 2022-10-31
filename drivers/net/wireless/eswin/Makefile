ECRNX_VERS_NUM=ECR6600U_V1.1.0B04P05

# config_ceva_rtos = y use ceva rtos and add task_cli id
# config_ceva_rtos = n use freertos and no task_cli id

#export DRIVER_PATH ?= $(shell pwd)
export DRIVER_PATH =  $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
DRIVER_PATH := $(DRIVER_PATH:/=)

#if build the host driver within linux kernel, the DRIVER_PATH should be defined to absolutly path
export product=6600u
ifeq ($(product), 6600u)
	-include $(DRIVER_PATH)/feature_config/6600u_config
else
	-include $(DRIVER_PATH)/feature_config/6600_config
	ifeq ($(os), )
		export CONFIG_CEVA_RTOS=y
	else
		export CONFIG_CEVA_RTOS=n
	endif
endif

###################### Platform Related #######################
CONFIG_PLATFORM_RTK_RTD2851D = n
CONFIG_PLATFORM_MTK_MT9255 = n
CONFIG_PLATFORM_RASPBERRY = y
CONFIG_PLATFORM_X86 = n
CONFIG_PLATFORM_AML_T963 = n
###############################################################

ifeq ($(CONFIG_PLATFORM_RTK_RTD2851D), y)
export KERNELDIR=/work3/zhanghong/realtek/rtk10/vendor/realtek/tool/kernel/linux/linux-4.14/
export KBUILDDIR=/work3/zhanghong/realtek/rtk10/vendor/realtek/tool/kernel/linux/linux-4.14/
endif

ifeq ($(CONFIG_PLATFORM_MTK_MT9255), y)
export KERNELDIR= $(DRIVER_PATH)/../../../../../../kernel/fusion/4.9
export KBUILDDIR= $(DRIVER_PATH)/../../../../../../kernel/fusion/4.9
export CROSS_COMPILE:=$(DRIVER_PATH)/../../../../../../prebuilts/mtk_toolchain/gcc-arm-linux-gnu-5.5.0-ubuntu/x86_64/bin/arm-linux-gnueabi-
endif

ifeq ($(CONFIG_PLATFORM_RASPBERRY), y)
export KERNELDIR=/lib/modules/$(shell uname -r)/build
export KBUILDDIR=/lib/modules/$(shell uname -r)/build
endif

ifeq ($(CONFIG_PLATFORM_X86), y)
export KERNELDIR=/lib/modules/$(shell uname -r)/build
export KBUILDDIR=/lib/modules/$(shell uname -r)/build
endif

ifeq ($(CONFIG_PLATFORM_AML_T963), y)
ifeq ($(DRIVER_DIR), )
export DRIVER_PATH ?= $(shell pwd)
else
export DRIVER_PATH ?= $(DRIVER_DIR)
endif
export KERNELDIR ?= $(DRIVER_PATH)/../../../../../../out/target/product/T963/obj/KERNEL_OBJ
export KBUILDDIR ?= $(DRIVER_PATH)/../../../../../../out/target/product/T963/obj/KERNEL_OBJ
CROSS_COMPILE ?= /opt/gcc-linaro-6.3.1-2017.02-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-
ARCH := arm
endif
#
# } // WAITING FOR KCONFIG
#

subdir-ccflags-$(CONFIG_6600_HAL) += -DCONFIG_6600_HAL
subdir-ccflags-$(CONFIG_DEBUG_FS) += -DCONFIG_ECRNX_DEBUGFS
subdir-ccflags-$(CONFIG_DEBUG_FS) += -DCONFIG_ECRNX_UM_HELPER_DFLT=\"$(CONFIG_ECRNX_UM_HELPER_DFLT)\"
subdir-ccflags-$(CONFIG_ECRNX_P2P_DEBUGFS) += -DCONFIG_ECRNX_P2P_DEBUGFS
subdir-ccflags-$(CONFIG_CEVA_RTOS) += -DCONFIG_CEVA_RTOS
subdir-ccflags-$(CONFIG_ECRNX_DBG) += -DCONFIG_ECRNX_DBG
subdir-ccflags-$(CONFIG_ECRNX_KTHREAD) += -DCONFIG_ECRNX_KTHREAD
subdir-ccflags-$(CONFIG_ECRNX_WORKQUEUE) += -DCONFIG_ECRNX_WORKQUEUE
subdir-ccflags-$(CONFIG_ECRNX_TASKLET) += -DCONFIG_ECRNX_TASKLET
subdir-ccflags-y += -DCONFIG_ECRNX_DBG_LEVEL=$(CONFIG_ECRNX_DBG_LEVEL)

# FW VARS
subdir-ccflags-y += -DNX_VIRT_DEV_MAX=$(NX_VIRT_DEV_MAX)
subdir-ccflags-y += -DNX_REMOTE_STA_MAX=$(NX_REMOTE_STA_MAX)
subdir-ccflags-y += -DNX_MU_GROUP_MAX=$(NX_MU_GROUP_MAX)
subdir-ccflags-y += -DNX_TXDESC_CNT=$(NX_TXDESC_CNT)
subdir-ccflags-y += -DNX_TX_MAX_RATES=$(NX_TX_MAX_RATES)
subdir-ccflags-y += -DNX_CHAN_CTXT_CNT=$(NX_CHAN_CTXT_CNT)
#subdir-ccflags-y += -DCONFIG_POWERKEY_GPIO=$(CONFIG_POWERKEY_GPIO)
# FW ARCH:
subdir-ccflags-$(CONFIG_ECRNX_SDM) += -DCONFIG_ECRNX_SDM
subdir-ccflags-$(CONFIG_ECRNX_TL4) += -DCONFIG_ECRNX_TL4
subdir-ccflags-$(CONFIG_ECRNX_OLD_IPC) += -DCONFIG_ECRNX_OLD_IPC

#FW VER INFO
DRIVER_BUILD_TIME = "$(shell TZ=CST date -u "+%Y-%m-%d %H:%M:%S CST")"
subdir-ccflags-y += -DECRNX_VERS_MOD=\"$(ECRNX_VERS_NUM)\"
subdir-ccflags-y += -DECRNX_VERS_BANNER=\"$(ECRNX_MODULE_NAME)-$(ECRNX_VERS_NUM)-build\:$(DRIVER_BUILD_TIME)\"

ifeq ($(CONFIG_ECRNX_FULLMAC), m)
MAC_SRC = fullmac
else ifeq ($(CONFIG_ECRNX_SOFTMAC), m)
MAC_SRC = softmac
endif


obj-m := $(ECRNX_MODULE_NAME).o

ifneq ($(KERNELRELEASE),)
include $(DRIVER_PATH)/$(MAC_SRC)/Makefile

else
all: modules

.PHONY: modules clean copy strip

modules:
ifeq ($(product), 6600u)
	$(warning "select chip is $(product).")
else
	$(warning "select chip is 6600.")
endif
	rm -rf *.ko
ifeq ($(os), )
	$(warning "select slave is used CEVA RTOS.")
else
	$(warning "select slave is not used CEVA_RTOS.")
endif
ifeq ($(CONFIG_PLATFORM_MTK_MT9255), y)
	$(MAKE) -C $(KERNELDIR) CROSS_COMPILE=$(CROSS_COMPILE) O=$(KBUILDDIR) M=$(DRIVER_PATH) $@
else ifeq ($(CONFIG_PLATFORM_AML_T963), y)
	$(MAKE) -C $(KERNELDIR) CROSS_COMPILE=$(CROSS_COMPILE) ARCH=$(ARCH) O=$(KBUILDDIR) M=$(DRIVER_PATH) $@
else
	$(MAKE) -C $(KERNELDIR) O=$(KBUILDDIR) M=$(DRIVER_PATH) $@
endif
	rm -rf *.o
	rm -rf *.mod *.mod.c

copy:
	cp -f $(DRIVER_PATH)/fullmac/$(ECRNX_MODULE_NAME).ko $(MODDESTDIR)

strip:
	$(CROSS_COMPILE)strip --strip-unneeded $(ECRNX_MODULE_NAME).ko
clean:
	rm -rf *.o
	rm -rf *.ko *.mod *.mod.c
	$(MAKE) -C $(KERNELDIR) O=$(KBUILDDIR) M=$(DRIVER_PATH) $@
#	@$(DRIVER_PATH)/mklink.sh clean
endif
