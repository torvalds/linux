#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).

# Object file lists.

# Cpu revision
obj-$(CONFIG_NOMADIK_8815) += cpu-8815.o

# Specific board support
obj-$(CONFIG_MACH_NOMADIK_8815NHK) += board-nhk8815.o

# Nomadik extra devices
obj-$(CONFIG_I2C_BITBANG_8815NHK) += i2c-8815nhk.o
