# SPDX-License-Identifier: GPL-2.0
# Makefile for the Linux device tree

always-$(CONFIG_EISA)		+= devlist.h
obj-$(CONFIG_EISA)	        += eisa-bus.o
obj-${CONFIG_EISA_PCI_EISA}     += pci_eisa.o

# virtual_root.o should be the last EISA root device to initialize,
# so leave it at the end of the list.
obj-${CONFIG_EISA_VIRTUAL_ROOT} += virtual_root.o


$(obj)/eisa-bus.o: $(obj)/devlist.h

quiet_cmd_eisaid = GEN     $@
      cmd_eisaid = sed -e '/^\#/D' -e 's/^\([[:alnum:]]\{7\}\) \+"\([^"]*\)"/EISA_DEVINFO ("\1", "\2"),/' $< > $@

clean-files := devlist.h
$(obj)/devlist.h: $(src)/eisa.ids include/linux/device.h FORCE
	$(call if_changed,eisaid)
