# SPDX-License-Identifier: GPL-2.0
dtb-$(CONFIG_XILFPGA_NEXYS4DDR)	+= nexys4ddr.dtb

obj-y				+= $(patsubst %.dtb, %.dtb.o, $(dtb-y))

# Force kbuild to make empty built-in.o if necessary
obj-				+= dummy.o

always				:= $(dtb-y)
clean-files	:= *.dtb *.dtb.S
