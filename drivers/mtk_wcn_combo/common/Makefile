# Copyright Statement:
#
# This software/firmware and related documentation ("MediaTek Software") are
# protected under relevant copyright laws. The information contained herein
# is confidential and proprietary to MediaTek Inc. and/or its licensors.
# Without the prior written permission of MediaTek inc. and/or its licensors,
# any reproduction, modification, use or disclosure of MediaTek Software,
# and information contained herein, in whole or in part, shall be strictly prohibited.
#
# MediaTek Inc. (C) 2010. All rights reserved.
#
# BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
# THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
# RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
# AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
# NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
# SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
# SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
# THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
# THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
# CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
# SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
# STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
# CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
# AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
# OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
# MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
#
# The following software/firmware and/or related documentation ("MediaTek Software")
# have been modified by MediaTek Inc. All revisions are subject to any receiver's
# applicable license agreements with MediaTek Inc.

#always := $(shell tar -zxf $(src)/core.tar.gz -C $(src))
always := $(shell if [ -f $(src)/core.tar.gz ]; then tar -zxf $(src)/core.tar.gz -C $(src); fi)
#always := $(shell tar -zxf $(src)/linux/pri.tar.gz -C $(src)/linux)
#always := $(shell if [ -f $(src)/linux/pri.tar.gz ]; then tar -zxf $(src)/linux/pri.tar.gz -C $(src)/linux; fi)

ccflags-y += -I$(src)/include -I$(src)/linux/include -I$(src)/core/include

# WMT DRIVER
obj-$(CONFIG_MTK_COMBO_COMM) += mtk_stp_wmt.o
# WMT DRIVER-core part
mtk_stp_wmt-y := core/wmt_core.o core/wmt_ctrl.o core/wmt_conf.o
mtk_stp_wmt-y += core/wmt_dbg.o core/wmt_func.o core/wmt_lib.o 
mtk_stp_wmt-y += core/btm_core.o 
mtk_stp_wmt-y += core/psm_core.o # to be removed
mtk_stp_wmt-y += core/stp_core.o core/stp_dbg.o
mtk_stp_wmt-$(CONFIG_MTK_COMBO_CHIP_MT6620) += core/wmt_ic_6620.o
#mtk_stp_wmt-$(CONFIG_MTK_COMBO_CHIP_MT6628) += core/wmt_ic_6628.o

# WMT DRIVER-linux part
mtk_stp_wmt-y += linux/wmt_dev.o linux/wmt_exp.o linux/stp_exp.o
# WMT DRIVER-OSAL
mtk_stp_wmt-y += linux/osal.o

# WMT DRIVER-platform part, specified by string CONFIG_MTK_COMBO_PLAT_PATH
PLAT := $(shell echo $(CONFIG_MTK_COMBO_PLAT_PATH))
mtk_stp_wmt-y += platform/$(PLAT)/wmt_plat_$(PLAT).o
# WMT DRIVER-platform chip part. Select desired hw according to project configuration
mtk_stp_wmt-$(CONFIG_MTK_COMBO_CHIP_MT6620) += platform/$(PLAT)/mtk_wcn_cmb_hw_6620.o 
mtk_stp_wmt-$(CONFIG_MTK_COMBO_CHIP_MT6628) += platform/$(PLAT)/mtk_wcn_cmb_hw_6628.o 

# WMT stub part (built-in kernel image)
obj-y += platform/$(PLAT)/mtk_wcn_cmb_stub_$(PLAT).o

# HIF-SDIO DRIVER
obj-$(CONFIG_MTK_COMBO_COMM) += mtk_hif_sdio.o
mtk_hif_sdio-y	:= linux/hif_sdio.o

# STP-UART DRIVER
obj-$(CONFIG_MTK_COMBO_COMM_UART) += mtk_stp_uart.o
mtk_stp_uart-y	:= linux/stp_uart.o

# STP-SDIO DRIVER 
#obj-$(CONFIG_MTK_COMBO_COMM_SDIO) += mtk_stp_sdio.o
#mtk_stp_sdio-y	:= linux/pri/stp_sdio.o

# MTK BT HW TEST DRIVER
obj-$(CONFIG_MTK_COMBO_BT_HW_TEST) += mtk_stp_bt.o
mtk_stp_bt-y	:= linux/stp_chrdev_bt.o

# STP-GPS DIVER
obj-$(CONFIG_MTK_COMBO_GPS) += mtk_stp_gps.o
mtk_stp_gps-y	:= linux/stp_chrdev_gps.o

# WMT-WIFI DIVER
obj-$(CONFIG_MTK_COMBO_WIFI) += mtk_wmt_wifi.o
mtk_wmt_wifi-y	:= linux/wmt_chrdev_wifi.o


