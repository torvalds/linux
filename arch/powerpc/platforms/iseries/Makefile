EXTRA_CFLAGS	+= -mno-minimal-toc

obj-y += hvlog.o hvlpconfig.o lpardata.o setup.o dt_mod.o mf.o lpevents.o \
	hvcall.o proc.o htab.o iommu.o misc.o irq.o
obj-$(CONFIG_PCI) += pci.o vpdinfo.o
obj-$(CONFIG_SMP) += smp.o
obj-$(CONFIG_VIOPATH) += viopath.o
obj-$(CONFIG_MODULES) += ksyms.o

$(obj)/dt_mod.o:	$(obj)/dt.o
	@$(OBJCOPY) --rename-section .rodata.str1.8=.dt_strings $(obj)/dt.o $(obj)/dt_mod.o
