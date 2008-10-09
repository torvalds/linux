#
# Makefile for the linux kernel.
#
#

# Common objects
obj-y 			:= time.o irq.o clock.o serial.o io.o id.o psc.o \
			   gpio.o mux.o devices.o usb.o

# Board specific
obj-$(CONFIG_MACH_DAVINCI_EVM)  += board-evm.o
