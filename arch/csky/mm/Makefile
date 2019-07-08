# SPDX-License-Identifier: GPL-2.0-only
ifeq ($(CONFIG_CPU_HAS_CACHEV2),y)
obj-y +=			cachev2.o
else
obj-y +=			cachev1.o
endif

obj-y +=			dma-mapping.o
obj-y +=			fault.o
obj-$(CONFIG_HIGHMEM) +=	highmem.o
obj-y +=			init.o
obj-y +=			ioremap.o
obj-y +=			syscache.o
obj-y +=			tlb.o
