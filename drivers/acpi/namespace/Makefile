#
# Makefile for all Linux ACPI interpreter subdirectories
#

obj-y := nsaccess.o  nsload.o    nssearch.o  nsxfeval.o \
	 nsalloc.o   nseval.o    nsnames.o   nsutils.o   nsxfname.o \
	 nsdump.o    nsinit.o    nsobject.o  nswalk.o    nsxfobj.o  \
	 nsparse.o

obj-$(ACPI_FUTURE_USAGE) += nsdumpdv.o

EXTRA_CFLAGS += $(ACPI_CFLAGS)
