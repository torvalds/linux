# ----------------------------------------------------------------------
# Copyright (c) 2016, The Regents of the University of California All
# rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
# 
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
# 
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
# 
#     * Neither the name of The Regents of the University of California
#       nor the names of its contributors may be used to endorse or
#       promote products derived from this software without specific
#       prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL REGENTS OF THE
# UNIVERSITY OF CALIFORNIA BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
# OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
# TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
# USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
# DAMAGE.
# ----------------------------------------------------------------------

# Filename: Makefile
# Version: 2.0
# Description: Makefile for Linux PCIe device driver for RIFFA.
# Author: Matthew Jacobsen
# History: @mattj: Initial release. Version 2.0.

# You must specify the following variables. You can leave the defaults if you
# like, but make sure they will work in your system.
# The VENDOR_ID _must_ match what is configured on your FPGA's PCIe endpoint 
# header. Xilinx has a VENDOR_ID = 10EE.
NAME := riffa
VENDOR_ID0 := 10EE
VENDOR_ID1 := 1172
MAJNUM := 100

# Build variables
KVER := $(shell uname -r)
KDIR := /lib/modules/`uname -r`/build
RHR := /etc/redhat-release
LIB_SRCS := riffa.c
LIB_OBJS := $(patsubst %.c,%.o,$(LIB_SRCS))
LIB_HDR := riffa.h
LIB_VER_MAJ := 1
LIB_VER_MIN := 0
LIB_VER := $(LIB_VER_MAJ).$(LIB_VER_MIN)
DRVR_HDR := riffa_driver.h
DBUGVAL := DBUG

obj-m += $(NAME).o
$(NAME)-y := riffa_driver.o circ_queue.o

# Helper functions
define assert
  $(if $1,,$(error Assertion failed: $2))
endef
define assert-not-null
  $(call assert,$($1),The variable "$1" is null, please specify it.)
endef
define assert-variables
	$(call assert-not-null,NAME)
	$(call assert-not-null,MAJNUM)
	$(call assert-not-null,VENDOR_ID0)
	$(call assert-not-null,VENDOR_ID1)
	@printf "Compiling driver for kernel: %s with the following values\n" $(KVER)
	@printf "     NAME: '%s'\n" $(NAME)
	@printf "   MAJNUM: '%s'\n" $(MAJNUM)
	@printf "VENDOR_ID0: '%s'\n" $(VENDOR_ID0)
	@printf "VENDOR_ID1: '%s'\n" $(VENDOR_ID1)
	@printf "\n"
endef

all: builddvr
debug: CC += -DDEBUG -g -pthread
debug: DBUGVAL = DEBUG
debug: builddvr
builddvr: $(NAME).ko $(NAME).so.$(LIB_VER)

$(NAME).ko: *.c *.h
	$(call assert-variables)
	sed -i 's/#define MAJOR_NUM [^\n]*/#define MAJOR_NUM $(MAJNUM)/g' $(DRVR_HDR)
	sed -i 's/#define DEVICE_NAME [^\n]*/#define DEVICE_NAME "$(NAME)"/g' $(DRVR_HDR)
	sed -i 's/#define VENDOR_ID0 [^\n]*/#define VENDOR_ID0 0x$(VENDOR_ID0)/g' $(DRVR_HDR)
	sed -i 's/#define VENDOR_ID1 [^\n]*/#define VENDOR_ID1 0x$(VENDOR_ID1)/g' $(DRVR_HDR)
	sed -i 's/#define DEBUG [^\n]*/#define DBUG 1/g' $(DRVR_HDR)
	sed -i 's/#define DBUG [^\n]*/#define $(DBUGVAL) 1/g' $(DRVR_HDR)
	make -C $(KDIR) SUBDIRS=`pwd` modules
	rm -rf $(LIB_OBJS)

$(NAME).so.$(LIB_VER): $(LIB_OBJS)
	$(CC) -shared -Wl,-soname,lib$(NAME).so.$(LIB_VER_MAJ) -o lib$@ $^

$(LIB_OBJS): $(LIB_SRCS)
	$(CC) -g -Wall -fPIC -c $^

load: $(NAME).ko
	insmod $(NAME).ko
	
unload:
	rmmod $(NAME)

clean:
	rm -Rf *.ko *.cmd *.o *.so *.so.* .*.cmd Module.symvers Module.markers modules.order *.mod.c .tmp_versions

setup:
	if [ -f "$(RHR)" ]; then yum install kernel-devel-`uname -r`;\
	else apt-get install linux-headers-`uname -r`; fi

install: $(NAME).so.$(LIB_VER) $(NAME).ko 
	mkdir -p /lib/modules/$(KVER)/kernel/drivers/$(NAME)
	cp $(NAME).ko /lib/modules/$(KVER)/kernel/drivers/$(NAME)/
	if [ -f "$(RHR)" ]; then\
		printf "%b\n" "#!/bin/sh\nexec /sbin/modprobe $(NAME) >/dev/null 2>&1" > /etc/sysconfig/modules/$(NAME).modules;\
		chmod 755 /etc/sysconfig/modules/$(NAME).modules;\
	else\
		if ! grep -Fxq "$(NAME)" /etc/modules; then echo "$(NAME)" >> /etc/modules; fi;\
	fi
	printf "%b\n" "KERNEL==\"$(NAME)\", MODE=\"777\", GROUP=\"root\"" > /etc/udev/rules.d/99-$(NAME).rules
	printf "/usr/local/lib\n" > $(NAME).conf
	mv $(NAME).conf /etc/ld.so.conf.d/
	cp $(DRVR_HDR) /usr/local/include/
	cp $(LIB_HDR) /usr/local/include/
	mv lib$(NAME).so.1.0 /usr/local/lib
	ln -sf /usr/local/lib/lib$(NAME).so.$(LIB_VER) /usr/local/lib/lib$(NAME).so.$(LIB_VER_MAJ)
	ln -sf /usr/local/lib/lib$(NAME).so.$(LIB_VER) /usr/local/lib/lib$(NAME).so
	ldconfig
	depmod
	make unload
	make load

uninstall: 
	rm -f /usr/local/lib/lib$(NAME).so*
	rm -f /usr/local/include/$(LIB_HDR)
	rm -f /usr/local/include/$(DRVR_HDR)
	rm -f /etc/ld.so.conf.d/$(NAME).conf
	rm -rf /lib/modules/$(KVER)/kernel/drivers/$(NAME)
	rm -f /etc/udev/rules.d/99-$(NAME).rules
	if [ -f "$(RHR)" ]; then rm -f /etc/sysconfig/modules/$(NAME).modules;\
	else cp /etc/modules ./etc.modules.bak; sed -i '/$(NAME)/d' /etc/modules; fi
	ldconfig
	depmod


