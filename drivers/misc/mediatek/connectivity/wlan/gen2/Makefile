# ---------------------------------------------------
# Compile Options
# ---------------------------------------------------
ccflags-y += -DLINUX -DMT6628

ccflags-y += -DCFG_SUPPORT_AGPS_ASSIST=1
ccflags-y += -DCFG_SUPPORT_TSF_USING_BOOTTIME=1
ccflags-y += -DCFG_P2P_LEGACY_COEX_REVISE=1
ccflags-y += -DARP_MONITER_ENABLE=1

ifeq ($(CONFIG_MTK_WAPI_SUPPORT), y)
    ccflags-y += -DCFG_SUPPORT_WAPI=1
else
    ccflags-y += -DCFG_SUPPORT_WAPI=0
endif

ifeq ($(CONFIG_MTK_WIFI_MCC_SUPPORT), y)
    ccflags-y += -DCFG_SUPPORT_MCC=1
else
	ccflags-y += -DCFG_SUPPORT_MCC=0
endif

ifeq ($(CONFIG_HAVE_XLOG_FEATURE), y)
    ccflags-y += -DCFG_SUPPORT_XLOG=1
else
    ccflags-y += -DCFG_SUPPORT_XLOG=0
endif

ifeq ($(CONFIG_MTK_AEE_FEATURE), y)
    ccflags-y += -DCFG_SUPPORT_AEE=1
else
    ccflags-y += -DCFG_SUPPORT_AEE=0
endif

#ifeq ($(CONFIG_MTK_COMBO_WIFI_HIF_SDIO1), y)
#    ccflags-y += -D_HIF_SDIO=1
#endif

ifeq ($(CONFIG_MTK_PASSPOINT_R1_SUPPORT), y)
    ccflags-y += -DCFG_SUPPORT_HOTSPOT_2_0=1
    ccflags-y += -DCFG_HS20_DEBUG=1
    ccflags-y += -DCFG_ENABLE_GTK_FRAME_FILTER=1
else
    ccflags-y += -DCFG_SUPPORT_HOTSPOT_2_0=0
    ccflags-y += -DCFG_HS20_DEBUG=0
    ccflags-y += -DCFG_ENABLE_GTK_FRAME_FILTER=0
endif

MTK_MET_PROFILING_SUPPORT = no
ifeq ($(MTK_MET_PROFILING_SUPPORT), yes)
    ccflags-y += -DCFG_SUPPORT_MET_PROFILING=1
else
    ccflags-y += -DCFG_SUPPORT_MET_PROFILING=0
endif

ifeq ($(CONFIG_MTK_TC1_FEATURE), y)
ifeq ($(CONFIG_MTK_GPT_SCHEME_SUPPORT), y)
    ccflags-y += -I$(srctree)/drivers/misc/mediatek/tc1_interface/gpt
else
    ccflags-y += -I$(srctree)/drivers/misc/mediatek/tc1_interface/pmt
endif
    ccflags-y += -DCFG_TC1_FEATURE=1
	ccflags-y += -DCFG_SUPPORT_CFG_FILE=1
else
    ccflags-y += -DCFG_TC1_FEATURE=0
endif

MTK_SRAM_SIZE_OPTION=0
ifeq ($(CONFIG_ARCH_MT6755), y)
    MTK_SRAM_SIZE_OPTION=2
endif
ifeq ($(CONFIG_ARCH_MT6735), y)
    MTK_SRAM_SIZE_OPTION=1
endif
ifeq ($(CONFIG_ARCH_MT6735M), y)
    MTK_SRAM_SIZE_OPTION=1
endif
ifeq ($(CONFIG_ARCH_MT6753), y)
    MTK_SRAM_SIZE_OPTION=1
endif
ifeq ($(CONFIG_ARCH_MT6580), y)
    MTK_SRAM_SIZE_OPTION=1
endif
ifeq ($(CONFIG_ARCH_MT8163), y)
    MTK_SRAM_SIZE_OPTION=1
endif
ccflags-y += -DCFG_SRAM_SIZE_OPTION=$(MTK_SRAM_SIZE_OPTION)

ifeq ($(strip $(TRUSTONIC_TEE_SUPPORT)),yes)
ifeq ($(strip $(MTK_TEE_CCCI_SECURE_SHARE_MEM_SUPPORT)),yes)
    ccflags-y += -DTRUSTONIC_TEE_SUPPORT
    ccflags-y += -DMTK_TEE_CCCI_SECURE_SHARE_MEM_SUPPORT
endif
endif

ccflags-y += -D_HIF_SDIO=1

ccflags-y += -DDBG=0
ccflags-y += -I$(src)/os -I$(src)/os/linux/include -I$(src)/os/linux/hif/ahb/include
ccflags-y += -I$(src)/include -I$(src)/include/nic -I$(src)/include/mgmt
ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/include
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/

MODULE_NAME := wlan_gen2
obj-$(CONFIG_MTK_COMBO_WIFI) += $(MODULE_NAME).o
#obj-m += $(MODULE_NAME).o if CONFIG_MTK_COMBO_WIFI=m ==> obj-m means ko module, not build in obj-y

# ---------------------------------------------------
# Directory List
# ---------------------------------------------------
COMMON_DIR  := common/
OS_DIR      := os/linux/
HIF_DIR	    := os/linux/hif/ahb/
NIC_DIR     := nic/
MGMT_DIR    := mgmt/
DMA_DIR     := ../../../../platform/$(call lc,$(MTK_PLATFORM))/kernel/drivers/wifi/
PLAT_DIR    := os/linux/plat/$(MTK_PLATFORM)/
HIF_AHB_PDMA := $(HIF_DIR)$(MTK_PLATFORM)/
#$(call lc,$(MTK_PLATFORM))


# ---------------------------------------------------
# Objects List
# ---------------------------------------------------

COMMON_OBJS :=	$(COMMON_DIR)dump.o \
	       		$(COMMON_DIR)wlan_lib.o \
	       		$(COMMON_DIR)wlan_oid.o \
	       		$(COMMON_DIR)wlan_bow.o \
				$(COMMON_DIR)debug.o

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
			$(OS_DIR)gl_vendor.o \
			$(OS_DIR)platform.o		\
			$(OS_DIR)gl_proc.o

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
 			 $(MGMT_DIR)swcr.o \
			 $(MGMT_DIR)roaming_fsm.o \
			 $(MGMT_DIR)hs20.o

# ---------------------------------------------------
# TDLS Objects List
# ---------------------------------------------------
MGMT_OBJS += $(MGMT_DIR)tdls.o \
			 $(MGMT_DIR)tdls_com.o

# ---------------------------------------------------
# STATS Objects List
# ---------------------------------------------------
MGMT_OBJS += $(MGMT_DIR)stats.o

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


ifeq ($(CONFIG_MTK_WAPI_SUPPORT), y)
MGMT_OBJS += $(MGMT_DIR)wapi.o
endif

ifeq ($(WLAN_PROC), y)
OS_OBJS += gl_proc.o
endif

#$(warning $(CONFIG_MACH_MT7623))

ifeq ($(CONFIG_MACH_MT7623), y)
HIF_AHB_PDMA = $(HIF_DIR)mt8127/
endif
HIF_OBJS :=  $(HIF_DIR)arm.o \
             $(HIF_DIR)ahb.o \
             $(HIF_AHB_PDMA)ahb_pdma.o
ifeq ($(CONFIG_ARCH_MT6755), y)
PLAT_OBJS := $(PLAT_DIR)plat_priv.o
$(MODULE_NAME)-objs  += $(PLAT_OBJS)
endif
$(MODULE_NAME)-objs  += $(COMMON_OBJS)
$(MODULE_NAME)-objs  += $(NIC_OBJS)
$(MODULE_NAME)-objs  += $(OS_OBJS)
$(MODULE_NAME)-objs  += $(HIF_OBJS)
$(MODULE_NAME)-objs  += $(MGMT_OBJS)

