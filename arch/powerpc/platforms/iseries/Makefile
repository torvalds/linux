EXTRA_CFLAGS	+= -mno-minimal-toc

extra-y += dt.o

obj-y += exception.o
obj-y += hvlog.o hvlpconfig.o lpardata.o setup.o dt_mod.o mf.o lpevents.o \
	hvcall.o proc.o htab.o iommu.o misc.o irq.o
obj-$(CONFIG_PCI) += pci.o
obj-$(CONFIG_SMP) += smp.o
obj-$(CONFIG_VIOPATH) += viopath.o vio.o
obj-$(CONFIG_MODULES) += ksyms.o

quiet_cmd_dt_strings = DT_STR  $@
      cmd_dt_strings = $(OBJCOPY) --rename-section .rodata.str1.8=.dt_strings \
				$< $@

$(obj)/dt_mod.o:	$(obj)/dt.o
	$(call if_changed,dt_strings)
