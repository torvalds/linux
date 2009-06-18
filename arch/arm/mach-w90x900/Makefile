#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y				:= irq.o time.o mfp-w90p910.o gpio.o clock.o

# W90X900 CPU support files

obj-$(CONFIG_CPU_W90P910)	+= w90p910.o

# machine support

obj-$(CONFIG_MACH_W90P910EVB)	+= mach-w90p910evb.o
