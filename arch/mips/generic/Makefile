#
# Copyright (C) 2016 Imagination Technologies
# Author: Paul Burton <paul.burton@mips.com>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation;  either version 2 of the  License, or (at your
# option) any later version.
#

obj-y += init.o
obj-y += irq.o
obj-y += proc.o

obj-$(CONFIG_YAMON_DT_SHIM)		+= yamon-dt.o
obj-$(CONFIG_LEGACY_BOARD_SEAD3)	+= board-sead3.o
obj-$(CONFIG_KEXEC)			+= kexec.o
