#
# Broadcom Proprietary and Confidential. Copyright (C) 2020,
# All Rights Reserved.
#
# This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom;
# the contents of this file may not be disclosed to third parties,
# copied or duplicated in any form, in whole or in part, without
# the prior written permission of Broadcom.
#
#
# <<Broadcom-WL-IPTag/Secret:>>

# This file should be seen only by internal builds because it will
# be mentioned only in internal filelists like brcm.flist.
# See extended comment bcminternal.mk for details.

BCMINTERNAL := 1

BCMINTERNAL_DFLAGS += -DDHD_NO_MOG

ifneq ($(CONFIG_BCMDHD_PCIE),)
  # Enable Register access via dhd IOVAR
  BCMINTERNAL_DFLAGS += -DDHD_PCIE_REG_ACCESS
  # latency timestamping
  BCMINTERNAL_DFLAGS += -DDHD_PKTTS
  # Traffic Pattern Analysis on Socket Flow
  BCMINTERNAL_DFLAGS += -DDHD_QOS_ON_SOCK_FLOW
  # QoS unit testing support
  BCMINTERNAL_DFLAGS += -DDHD_QOS_ON_SOCK_FLOW_UT
  # Auto QOS
  BCMINTERNAL_DFLAGS += -DWL_AUTO_QOS

  ifneq ($(filter -DCUSTOMER_HW4, $(DHDCFLAGS)),)
    # These will be moved to hw4 Makefile for 4389b0
    BCMINTERNAL_DFLAGS += -DWBRC
    BCMINTERNAL_DFLAGS += -DWLAN_ACCEL_BOOT
    BCMINTERNAL_DFLAGS += -DDHD_HTPUT_TUNABLES
    # BCMINTERNAL_DFLAGS += -DDHD_FIS_DUMP
    # SCAN TYPES, if kernel < 4.17 ..back port support required
    ifneq ($(CONFIG_CFG80211_SCANTYPE_BKPORT),)
    DHDCFLAGS += -DWL_SCAN_TYPE
    endif
    # Jig builds
    # No reset during dhd attach
    BCMINTERNAL_DFLAGS += -DDHD_SKIP_DONGLE_RESET_IN_ATTACH
    # Dongle Isolation will ensure no resets devreset ON/OFF
    BCMINTERNAL_DFLAGS += -DDONGLE_ENABLE_ISOLATION
    # Quiesce dongle using DB7 trap
    BCMINTERNAL_DFLAGS += -DDHD_DONGLE_TRAP_IN_DETACH
    # Collect socram during dongle init failurs for internal builds
    BCMINTERNAL_DFLAGS += -DDEBUG_DNGL_INIT_FAIL
    # Dongle reset during Wifi ON to keep in sane state
    BCMINTERNAL_DFLAGS += -DFORCE_DONGLE_RESET_IN_DEVRESET_ON
    # Perform Backplane Reset else FLR will happen
    # BCMINTERNAL_DFLAGS += -DDHD_USE_BP_RESET_SS_CTRL
    BCMINTERNAL_DFLAGS += -DWIFI_TURNOFF_DELAY=10

  endif

  # NCI_BUS support
  BCMINTERNAL_DFLAGS += -DSOCI_NCI_BUS
endif


BCMINTERNAL_DFLAGS += -DDHD_BUS_MEM_ACCESS

# Support multiple chips
BCMINTERNAL_DFLAGS += -DSUPPORT_MULTIPLE_CHIPS

# Support unreleased chips
BCMINTERNAL_DFLAGS += -DUNRELEASEDCHIP

# Collect socram if readshared fails
BCMINTERNAL_DFLAGS += -DDEBUG_DNGL_INIT_FAIL

# Force enable memdump value to DUMP_MEMFILE if it is disabled
BCMINTERNAL_DFLAGS += -DDHD_INIT_DEFAULT_MEMDUMP

ifneq ($(filter -DDHD_QOS_ON_SOCK_FLOW,$(BCMINTERNAL_DFLAGS)),)
BCMINTERNAL_DHDOFILES += dhd_linux_sock_qos.o
endif
ifneq ($(filter -DSOCI_NCI_BUS,$(BCMINTERNAL_DFLAGS)),)
BCMINTERNAL_DHDOFILES += nciutils.o
endif
ifneq ($(filter -DWBRC,$(BCMINTERNAL_DFLAGS)),)
BCMINTERNAL_DHDOFILES += wb_regon_coordinator.o
endif
# vim: filetype=make shiftwidth=2
