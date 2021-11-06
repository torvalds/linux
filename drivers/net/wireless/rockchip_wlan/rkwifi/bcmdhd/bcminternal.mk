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
# be mentioned only in internal filelists like brcm.flist. The idea
# is that it will be conditionally included by makefiles using the
# "-include" syntax, with the result that internal builds will see
# this file and set BCMINTERNAL which will eventually result in a
# -DBCMINTERNAL option passed to the compiler along with possible
# other effects. External builds will never see it and it will be
# silently ignored.
#
# Any settings which should not be exposed to customers may be
# placed here. For instance, if we were working on a super-secret
# new feature in supersecret.c we could set a variable here like
#    BCMINTERNAL_OBJECTS := supersecret.o
# and later say
#    OBJECTS += $(BCMINTERNAL_OBJECTS)
# within the main makefile.
#
# The key point is that this file is never shipped to customers
# because it's present only in internal filelists so anything
# here is private.

BCMINTERNAL := 1

BCMINTERNAL_DFLAGS += -DBCMINTERNAL
BCMINTERNAL_DFLAGS += -DDHD_NO_MOG

# Support unreleased chips
BCMINTERNAL_DFLAGS += -DUNRELEASEDCHIP

ifneq ($(findstring -fwtrace,-$(TARGET)-),)
  BCMINTERNAL_DFLAGS += -DDHD_FWTRACE
  BCMINTERNAL_CFILES += dhd_fwtrace.c
endif

# support only for SDIO MFG Fedora builds
ifneq ($(findstring -sdstd-,-$(TARGET)-),)
  ifneq ($(findstring -mfgtest-,-$(TARGET)-),)
    BCMINTERNAL_DFLAGS += -DDHD_SPROM
    BCMINTERNAL_CFILES += bcmsrom.c bcmotp.c
  endif
endif

ifneq ($(findstring -pciefd-,$(TARGET)-),)
# NCI_BUS support
BCMINTERNAL_DFLAGS += -DSOCI_NCI_BUS -DBOOKER_NIC400_INF
BCMINTERNAL_CFILES += nciutils.c
endif
# vim: filetype=make shiftwidth=2
