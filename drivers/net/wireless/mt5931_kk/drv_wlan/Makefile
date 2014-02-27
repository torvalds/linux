# ---------------------------------------------------
# Compile Options
# ---------------------------------------------------

#include $(MTK_PATH_BUILD)/Makefile
ccflags-y += -DLINUX -DMT5931

ifeq ($(CONFIG_MTK_MT5931_WAPI_SUPPORT), y)
    ccflags-y += -DCFG_SUPPORT_WAPI=1
else
    ccflags-y += -DCFG_SUPPORT_WAPI=0
endif

ifeq ($(CONFIG_MTK_MT5931_SUPPORT_XLOG), y)
    ccflags-y += -DCFG_SUPPORT_XLOG=1
else
    ccflags-y += -DCFG_SUPPORT_XLOG=0
endif

ifeq ($(CONFIG_MTK_MT5931_SUPPORT_AEE), y)
    ccflags-y += -DCFG_SUPPORT_AEE=1
else
    ccflags-y += -DCFG_SUPPORT_AEE=0
endif

ifeq ($(CONFIG_MTK_MT5931_WLAN_PROC), y)
    ccflags-y += -DWLAN_INCLUDE_PROC
endif

#ifeq ($(CONFIG_MTK_MT5931_WIFI_HIF_SDIO1), y)
#    ccflags-y += -D_HIF_SDIO=1
#endif

ccflags-y += -D_HIF_SDIO=1

MODULE_NAME := wlan_mt5931

ifeq ($(CONFIG_MTK_MT5931_DEBUG), yes)
    ccflags-y += -DDBG=1
else
    ccflags-y += -DDBG=0
endif

ccflags-y += -I$(src)/os -I$(src)/os/linux/include -I$(src)/os/linux/hif/sdio/include
ccflags-y += -I$(src)/include -I$(src)/include/nic -I$(src)/include/mgmt

obj-$(CONFIG_MTK_MT5931_WIFI) += $(MODULE_NAME).o
obj-$(CONFIG_MTK_MT5931_POWER) += mtk_wmt_wifi.o
mtk_wmt_wifi-objs       := wmt_chrdev_wifi.o 
#obj-m += $(MODULE_NAME).o

# ---------------------------------------------------
# Directory List
# ---------------------------------------------------
COMMON_DIR  := common/
OS_DIR      := os/linux/
HIF_DIR	    := os/linux/hif/sdio/
NIC_DIR     := nic/
MGMT_DIR    := mgmt/


# ---------------------------------------------------
# Objects List
# ---------------------------------------------------

COMMON_OBJS :=	$(COMMON_DIR)dump.o \
	       		$(COMMON_DIR)wlan_lib.o \
	       		$(COMMON_DIR)wlan_oid.o \
	       		$(COMMON_DIR)wlan_bow.o \
	  	      $(COMMON_DIR)dual_ant_bwcs.o  		

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
			$(OS_DIR)gl_cfg80211.o \
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
                         
# ---------------------------------------------------
# P2P Objects List
# ---------------------------------------------------      

COMMON_OBJS += $(COMMON_DIR)wlan_p2p.o

NIC_OBJS += $(NIC_DIR)p2p_nic.o

OS_OBJS += $(OS_DIR)gl_p2p.o \
           $(OS_DIR)gl_p2p_cfg80211.o \
           $(OS_DIR)gl_p2p_init.o \
           $(OS_DIR)gl_p2p_kal.o

MGMT_OBJS += $(MGMT_DIR)p2p_assoc.o \
             $(MGMT_DIR)p2p_bss.o \
             $(MGMT_DIR)p2p_fsm.o \
             $(MGMT_DIR)p2p_func.o \
             $(MGMT_DIR)p2p_rlm.o \
             $(MGMT_DIR)p2p_rlm_obss.o \
             $(MGMT_DIR)p2p_scan.o \
             $(MGMT_DIR)p2p_ie.o \
             $(MGMT_DIR)p2p_state.o        
                 

ifeq ($(CONFIG_MTK_WAPI_SUPPORT), yes)
MGMT_OBJS += $(MGMT_DIR)wapi.o
endif

ifeq ($(CONFIG_MTK_MT5931_WLAN_PROC), y)
OS_OBJS += gl_proc.o
endif

HIF_OBJS :=  $(HIF_DIR)arm.o \
             $(HIF_DIR)sdio.o

$(MODULE_NAME)-objs  += $(COMMON_OBJS)
$(MODULE_NAME)-objs  += $(NIC_OBJS)
$(MODULE_NAME)-objs  += $(OS_OBJS)
$(MODULE_NAME)-objs  += $(HIF_OBJS)
$(MODULE_NAME)-objs  += $(MGMT_OBJS)


