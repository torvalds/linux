#
# Makefile for arch/m68knommu/platform/68328.
#

head-y			= head-$(MODEL).o
head-$(CONFIG_PILOT)	= head-pilot.o
head-$(CONFIG_DRAGEN2)	= head-de2.o

obj-y			+= entry.o ints.o timers.o
obj-$(CONFIG_M68328)	+= config.o
obj-$(CONFIG_ROM)	+= romvec.o

extra-y			:= head.o
extra-$(CONFIG_M68328)	+= bootlogo.rh head.o

$(obj)/bootlogo.rh: $(src)/bootlogo.h
	perl $(src)/bootlogo.pl < $(src)/bootlogo.h > $(obj)/bootlogo.rh

$(obj)/head.o: $(obj)/$(head-y)
	ln -sf $(head-y) $(obj)/head.o

clean-files := $(obj)/bootlogo.rh $(obj)/head.o $(head-y)
