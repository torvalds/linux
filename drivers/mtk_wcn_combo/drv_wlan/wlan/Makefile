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
#for the kernel module inserted issue
ccflags-y += -fno-pic

#MTK_WAPI_SUPPORT = yes

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

ccflags-y += -DDBG=0
#ccflags-y += -DDBG=1
ccflags-y += -I$(src)/os -I$(src)/os/linux/include -I$(src)/os/linux/hif/sdio/include
ccflags-y += -I$(src)/include -I$(src)/include/nic -I$(src)/include/mgmt

ccflags-y += -O2
obj-$(CONFIG_MTK_COMBO_WIFI) += wlan.o

# ---------------------------------------------------
# Directory List
# ---------------------------------------------------
COMMON_DIR  := common/
OS_DIR      := ./os/linux/
HIF_DIR	    := os/linux/hif/sdio/
NIC_DIR     := nic/
MGMT_DIR    := mgmt/
WAPI_DIR    :=./os/linux/

ifeq ($(MTK_WAPI_SUPPORT), yes)
always := $(shell cp -f $(WAPI_DIR)gl_sec.o.new $(WAPI_DIR)gl_sec.o)
always := $(touch $(OS_DIR)gl_sec.o)
endif
always := $(@echo ***********  $$(src))


# ---------------------------------------------------
# Objects List
# ---------------------------------------------------

COMMON_OBJS :=	$(COMMON_DIR)dump.o \
	       		$(COMMON_DIR)wlan_lib.o \
	       		$(COMMON_DIR)wlan_oid.o \
	       		$(COMMON_DIR)wlan_bow.o

NIC_OBJS := $(NIC_DIR)nic.o \
			$(NIC_DIR)nic_tx.o \
			$(NIC_DIR)nic_rx.o \
			$(NIC_DIR)nic_pwr_mgt.o \
			$(NIC_DIR)cmd_buf.o \
			$(NIC_DIR)que_mgt.o \
			$(NIC_DIR)nic_cmd_event.o

OS_OBJS :=	$(OS_DIR)gl_init.o \
			$(OS_DIR)gl_kal.o  \
			$(OS_DIR)gl_bow.o \
			$(OS_DIR)gl_wext.o \
			$(OS_DIR)gl_wext_priv.o \
			$(OS_DIR)gl_rst.o \
			$(OS_DIR)platform.o

MGMT_OBJS := $(MGMT_DIR)ais_fsm.o \
			 $(MGMT_DIR)aaa_fsm.o \
			 $(MGMT_DIR)assoc.o \
			 $(MGMT_DIR)auth.o \
			 $(MGMT_DIR)bss.o \
			 $(MGMT_DIR)cnm.o \
			 $(MGMT_DIR)cnm_timer.o \
			 $(MGMT_DIR)cnm_mem.o \
			 $(MGMT_DIR)hem_mbox.o \
			 $(MGMT_DIR)mib.o \
			 $(MGMT_DIR)privacy.o  \
			 $(MGMT_DIR)rate.o \
			 $(MGMT_DIR)rlm.o \
			 $(MGMT_DIR)rlm_domain.o \
			 $(MGMT_DIR)rlm_obss.o \
			 $(MGMT_DIR)rlm_protection.o \
			 $(MGMT_DIR)rsn.o \
			 $(MGMT_DIR)saa_fsm.o \
			 $(MGMT_DIR)scan.o \
			 $(MGMT_DIR)scan_fsm.o \
		 	 $(MGMT_DIR)sec_fsm.o \
                         $(MGMT_DIR)swcr.o \
                         $(MGMT_DIR)roaming_fsm.o

ifeq ($(MTK_WAPI_SUPPORT), yes)
MGMT_OBJS += $(MGMT_DIR)wapi.o
OS_OBJS += $(OS_DIR)gl_sec.o
endif

ifeq ($(WLAN_PROC), y)
OS_OBJS += gl_proc.o
endif

HIF_OBJS :=  $(HIF_DIR)arm.o \
             $(HIF_DIR)sdio.o

wlan-objs  += $(COMMON_OBJS)
wlan-objs  += $(NIC_OBJS)
wlan-objs  += $(OS_OBJS)
wlan-objs  += $(HIF_OBJS)
wlan-objs  += $(MGMT_OBJS)


