#
# Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.

obj-y	:= arcksyms.o setup.o irq.o time.o reset.o ptrace.o entry.o process.o
obj-y	+= signal.o traps.o sys.o troubleshoot.o stacktrace.o clk.o

obj-$(CONFIG_ARC_FPU_SAVE_RESTORE)	+= fpu.o
CFLAGS_fpu.o   += -mdpfp

obj-y += ctx_sw_asm.o

extra-y := vmlinux.lds head.o
