#
# Makefile for arch/m68knommu/platform/68328.
#

model-y			  := ram
model-$(CONFIG_ROMKERNEL) := rom

head-y			= head-$(model-y).o
head-$(CONFIG_PILOT)	= head-pilot.o
head-$(CONFIG_DRAGEN2)	= head-de2.o

obj-y			+= entry.o ints.o timers.o
obj-$(CONFIG_M68328)	+= config.o
obj-$(CONFIG_ROM)	+= romvec.o

extra-y			:= head.o

$(obj)/head.o: $(obj)/$(head-y)
	ln -sf $(head-y) $(obj)/head.o

clean-files := $(obj)/head.o $(head-y)
