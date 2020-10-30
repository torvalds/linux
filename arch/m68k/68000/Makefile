# SPDX-License-Identifier: GPL-2.0
##################################################
#
# Makefile for 68000 core based cpus
#
# 2012.10.21, Luis Alves <ljalvs@gmail.com>
#             Merged all 68000 based cpu's config
#             files into a single directory.
#

# 68328, 68EZ328, 68VZ328

obj-y			+= entry.o ints.o timers.o
obj-$(CONFIG_M68328)	+= m68328.o
obj-$(CONFIG_M68EZ328)	+= m68EZ328.o
obj-$(CONFIG_M68VZ328)	+= m68VZ328.o
obj-$(CONFIG_ROM)	+= romvec.o

obj-$(CONFIG_DRAGEN2)	+= dragen2.o
obj-$(CONFIG_UCSIMM)	+= ucsimm.o
obj-$(CONFIG_UCDIMM)	+= ucsimm.o

extra-y 		:= head.o
