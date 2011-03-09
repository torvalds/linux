#
# Makefile for ACP Modem (Mwave).
#
# See the README file in this directory for more info. <paulsch@us.ibm.com>
#

obj-$(CONFIG_MWAVE) += mwave.o

mwave-y := mwavedd.o smapi.o tp3780i.o 3780i.o

# To have the mwave driver disable other uarts if necessary
# EXTRA_CFLAGS += -DMWAVE_FUTZ_WITH_OTHER_DEVICES

# To compile in lots (~20 KiB) of run-time enablable printk()s for debugging:
ccflags-y := -DMW_TRACE
