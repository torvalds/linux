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


# ---------------------------------------------------
# Compile Options
# ---------------------------------------------------
ccflags-y += -DLINUX -DMT6620

ifeq ($(MTK_WAPI_SUPPORT), yes)
    ccflags-y += -DCFG_SUPPORT_WAPI=1
else
    ccflags-y += -DCFG_SUPPORT_WAPI=0
endif

ifeq ($(HAVE_XLOG_FEATURE), yes)
    ccflags-y += -DCFG_SUPPORT_XLOG=1
else
    ccflags-y += -DCFG_SUPPORT_XLOG=0
endif

#ifeq ($(CONFIG_MTK_COMBO_WIFI_HIF_SDIO1), y)
#    ccflags-y += -D_HIF_SDIO=1
#endif
ccflags-y += -D_HIF_SDIO=1

#ccflags-y += -DDBG=0
ccflags-y += -DDBG=0
ccflags-y += -I$(src)/os -I$(src)/os/linux/include
ccflags-y += -I$(src)/include -I$(src)/include/nic -I$(src)/include/mgmt

ccflags-y += -I$(src)/../wlan/os -I$(src)/../wlan/os/linux/include -I$(src)/../wlan/os/linux/hif/sdio/include
ccflags-y += -I$(src)/../wlan/include -I$(src)/../wlan/include/nic -I$(src)/../wlan/include/mgmt

obj-$(CONFIG_MTK_COMBO_WIFI) += p2p.o

# ---------------------------------------------------
# Directory List
# ---------------------------------------------------
COMMON_DIR  := common/
OS_DIR      := os/linux/
NIC_DIR     := nic/
MGMT_DIR    := mgmt/

#always := $(shell cp -f ../mediatek/source/kernel/drivers/combo/drv_wlan/wlan/$(OS_DIR)gl_sec.o.new ../mediatek/source/kernel/drivers/combo/drv_wlan/wlan/$(OS_DIR)gl_sec.o)
#always := $(touch ../mediatek/source/kernel/drivers/combo/drv_wlan/wlan/$(OS_DIR)gl_sec.o)

# ---------------------------------------------------
# Objects List
# ---------------------------------------------------

COMMON_OBJS := $(COMMON_DIR)wlan_p2p.o

NIC_OBJS := $(NIC_DIR)p2p_nic.o

OS_OBJS :=	$(OS_DIR)gl_p2p.o \
			$(OS_DIR)gl_p2p_cfg80211.o \
			$(OS_DIR)gl_p2p_init.o \
			$(OS_DIR)gl_p2p_kal.o

MGMT_OBJS :=$(MGMT_DIR)p2p_fsm.o \
			$(MGMT_DIR)p2p_scan.o \
			$(MGMT_DIR)p2p_rlm.o \
			$(MGMT_DIR)p2p_rlm_obss.o\
			$(MGMT_DIR)p2p_bss.o\
			$(MGMT_DIR)p2p_assoc.o\
			$(MGMT_DIR)p2p_func.o\
			$(MGMT_DIR)p2p_state.o\
			$(MGMT_DIR)p2p_ie.o

#ifeq ($(MTK_WAPI_SUPPORT), yes)
#MGMT_OBJS += $(MGMT_DIR)wapi.o
#OS_OBJS += $(OS_DIR)gl_sec.o
#endif

#ifeq ($(WLAN_PROC), y)
#OS_OBJS += gl_proc.o
#endif

#HIF_OBJS :=  $(HIF_DIR)arm.o \
#             $(HIF_DIR)sdio.o

p2p-objs  += $(COMMON_OBJS)
p2p-objs  += $(NIC_OBJS)
p2p-objs  += $(OS_OBJS)
p2p-objs  += $(HIF_OBJS)
p2p-objs  += $(MGMT_OBJS)


