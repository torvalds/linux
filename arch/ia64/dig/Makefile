#
# ia64/platform/dig/Makefile
#
# Copyright (C) 1999 Silicon Graphics, Inc.
# Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
#

obj-y := setup.o
ifeq ($(CONFIG_INTEL_IOMMU), y)
obj-$(CONFIG_IA64_GENERIC) += machvec.o machvec_vtd.o
else
obj-$(CONFIG_IA64_GENERIC) += machvec.o
endif

