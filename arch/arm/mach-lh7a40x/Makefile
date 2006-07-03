#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y				:= time.o clocks.o
obj-m				:=
obj-n				:=
obj-				:=

obj-$(CONFIG_MACH_KEV7A400)	+= arch-kev7a400.o irq-lh7a400.o
obj-$(CONFIG_MACH_LPD7A400)	+= arch-lpd7a40x.o irq-lh7a400.o
obj-$(CONFIG_MACH_LPD7A404)	+= arch-lpd7a40x.o irq-lh7a404.o
obj-$(CONFIG_LPD7A40X_CPLD_SSP)	+= ssp-cpld.o
obj-$(CONFIG_FB_ARMCLCD)	+= clcd.o

