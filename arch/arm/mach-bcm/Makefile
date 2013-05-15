#
# Copyright (C) 2012 Broadcom Corporation
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation version 2.
#
# This program is distributed "as is" WITHOUT ANY WARRANTY of any
# kind, whether express or implied; without even the implied warranty
# of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

obj-$(CONFIG_ARCH_BCM)		:= board_bcm.o bcm_kona_smc.o bcm_kona_smc_asm.o
plus_sec := $(call as-instr,.arch_extension sec,+sec)
AFLAGS_bcm_kona_smc_asm.o	:=-Wa,-march=armv7-a$(plus_sec)
