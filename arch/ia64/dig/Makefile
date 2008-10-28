#
# ia64/platform/dig/Makefile
#
# Copyright (C) 1999 Silicon Graphics, Inc.
# Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
#

obj-y := setup.o
ifeq ($(CONFIG_DMAR), y)
obj-$(CONFIG_IA64_GENERIC) += machvec.o machvec_vtd.o dig_vtd_iommu.o
else
obj-$(CONFIG_IA64_GENERIC) += machvec.o
endif
obj-$(CONFIG_IA64_DIG_VTD) += dig_vtd_iommu.o
