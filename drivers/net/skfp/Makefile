#
# Makefile for the SysKonnect FDDI PCI adapter driver
#

obj-$(CONFIG_SKFP) += skfp.o

skfp-objs :=  skfddi.o    hwmtm.o    fplustm.o  smt.o      cfm.o     \
              ecm.o       pcmplc.o   pmf.o      queue.o    rmt.o     \
	      smtdef.o    smtinit.o  smttimer.o srf.o      hwt.o     \
	      drvfbi.o   ess.o

# NOTE:
#   Compiling this driver produces some warnings (and some more are 
#   switched off below), but I did not fix this, because the Hardware
#   Module source (see skfddi.c for details) is used for different
#   drivers, and fixing it for Linux might bring problems on other
#   projects. To keep the source common for all those drivers (and
#   thus simplify fixes to it), please do not clean it up!

EXTRA_CFLAGS += -Idrivers/net/skfp -DPCI -DMEM_MAPPED_IO -Wno-strict-prototypes 
