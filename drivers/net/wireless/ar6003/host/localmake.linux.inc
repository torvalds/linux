#
#  Local Makefile includes for tool and kernel source paths
# 
#  Copyright 2004-2010 Atheros Communications, Inc.
#
# When creating a new build type, use the following template:
#  	ifeq ($(ATH_BUILD_TYPE),<platform name>) 
#       ATH_ARCH_CPU_TYPE :=  < cpu architecture >
#       ATH_CROSS_COMPILE_TYPE := < cross compiler path >
#       ATH_LINUXPATH := < kernel source path >

ATH_BUILD_TYPE := LOCAL_ARM_RK29
ATH_BUS_TYPE := SDIO
ATH_LINUXPATH := /home/ko/work/customer/yifang/trunk_exp
ATH_CROSS_COMPILE_TYPE := /home/ko/work/customer/yifang/kernel_rk29_v0.1/toolchain/arm-eabi-4.4.0/bin/arm-eabi-
ATH_ARCH_CPU_TYPE := arm


  $(info *************************)
  $(info PLATFORM: $(ATH_BUILD_TYPE))
  $(info BSP:      $(ATH_BSP_TYPE))
  $(info *************************)
  # Some environment settings
  ATH_BUILD_TYPE=$(ATH_BSP_TYPE)_ARM_NATIVEMMC
  ATH_ANDROID_ENV := yes
  ATH_SOFTMAC_FILE_USED := no
  ATH_DEBUG_DRIVER := yes
  ATH_HTC_RAW_INT_ENV :=yes
  ATH_AR6K_OTA_TEST_MODE := no

  # Some common variable
  ATH_BUS_TYPE := SDIO
  ATH_OS_SUB_TYPE := linux_2_6
  ATH_BUS_SUBTYPE := linux_sdio
  ATH_BUILD_3RDPARTY := no
  ATH_CFG80211_ENV := no
  ATH_BUILD_SYSTEM_TOOLS := no
  ATH_AR6K_HCI_PAL := no
  ATH_AR6K_DEBUG_ALLOC := no
  ATH_BUILD_P2P :=no
