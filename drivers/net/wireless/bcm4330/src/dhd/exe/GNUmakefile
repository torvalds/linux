#
# GNUmakefile for dhd/exe
#
# Copyright (C) 1999-2011, Broadcom Corporation
#
#         Unless you and Broadcom execute a separate written software license
# agreement governing use of this software, this software is licensed to you
# under the terms of the GNU General Public License version 2 (the "GPL"),
# available at http://www.broadcom.com/licenses/GPLv2.php, with the
# following added to such license:
#
#      As a special exception, the copyright holders of this software give you
# permission to link this software with independent modules, and to copy and
# distribute the resulting executable under terms of your choice, provided that
# you also meet, for each linked independent module, the terms and conditions of
# the license of that module.  An independent module is a module which is not
# derived from this software.  The special exception does not apply to any
# modifications of the software.
#
#      Notwithstanding the above, under no circumstances may you combine this
# software in any way with any other Broadcom software provided under a license
# other than the GPL, without Broadcom's express prior written consent.
#
# $Id: GNUmakefile,v 1.25.4.3 2011-01-13 00:19:30 Exp $

ifndef	SRCBASE
	SRCBASE = ../..
endif

UNAME = $(shell uname)

#-----------------------------------------------------------------
# Windows build
# 1) windows, don't include Makerules due to all: conflict

ifeq ($(findstring CYGWIN,$(UNAME)),CYGWIN)

include ../../GNUmakefile.inc

else # UNAME

# 2) not windows, need to include first to pick up TARGETENV dependent vars
include $(SRCBASE)/Makerules



#ifdef BCMWPA2
CFLAGS += -DBCMWPA2
#endif
# Always include pktgen commands in the application
CFLAGS += -DSDTEST

#ifdef WLCNT
CFLAGS += -DWLCNT
#endif


CFLAGS += -DWLBTAMP

CFLAGS += -DDONGLEOVERLAYS

#-----------------------------------------------------------------
# Linux build
#
# This should be one of values recognized in src/Makerules

ifneq ($(findstring $(TARGETENV), "linux linuxmips linuxmips_be linux26mips linuxarm linuxarm_le android"),)
 
# $(TARGETARCH) is set based on TARGETENV in src/Makerules.* files
DHD_OBJS := dhdu.o dhdu_linux.o bcmutils.o bcmwifi.o miniopt.o
DHD_EXE  := dhd
DHD_OBJS := $(DHD_OBJS:%.o=%.o)
ifneq ($(findstring x86,$(TARGETARCH)),x86)
  DHD_EXE  := $(DHD_EXE)$(TARGETARCH)
  DHD_OBJS := $(DHD_OBJS:%.o=$(TARGETARCH)/%.o)
endif

CFLAGS += -DWLPFN -DLINUX -g -I$(SRCBASE)/wl/exe

vpath %.c $(SRCBASE)/shared
vpath %.c $(SRCBASE)/wl/exe

all: $(DHD_EXE)

clean:
	 rm -fr $(DHD_EXE) $(DHD_OBJS) $(TARGETARCH) 

$(DHD_EXE): $(DHD_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) -c $(CFLAGS) -DRWL_SOCKET -DRWL_WIFI -DRWL_DONGLE -o $@ $^


ifneq ($(findstring x86, $(TARGETARCH)),x86)
$(TARGETARCH)/%.o: %.c
	@mkdir -p $(TARGETARCH)
	$(CC) -c $(CFLAGS) -I ../../include -DRWL_SOCKET -DRWL_WIFI -DRWL_DONGLE -o $@ $^
endif

.PHONY: all clean

endif # TARGETENV linux

endif # UNAME
