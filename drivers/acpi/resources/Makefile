#
# Makefile for all Linux ACPI interpreter subdirectories
#

obj-y := rsaddr.o rscreate.o rsinfo.o rsio.o rslist.o rsmisc.o rsxface.o \
	 rscalc.o  rsirq.o  rsmemory.o  rsutils.o

obj-$(ACPI_FUTURE_USAGE) += rsdump.o

EXTRA_CFLAGS += $(ACPI_CFLAGS)
