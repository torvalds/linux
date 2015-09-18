#
# Makefile for 68360 machines.
#
model-y			  := ram
model-$(CONFIG_ROMKERNEL) := rom

obj-y := config.o commproc.o entry.o ints.o

extra-y := head.o

$(obj)/head.o: $(obj)/head-$(model-y).o
	ln -sf head-$(model-y).o $(obj)/head.o
