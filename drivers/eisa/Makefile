# SPDX-License-Identifier: GPL-2.0
# Makefile for the Linux device tree

obj-$(CONFIG_EISA)	        += eisa-bus.o
obj-${CONFIG_EISA_PCI_EISA}     += pci_eisa.o

# virtual_root.o should be the last EISA root device to initialize,
# so leave it at the end of the list.
obj-${CONFIG_EISA_VIRTUAL_ROOT} += virtual_root.o


# Ugly hack to get DEVICE_NAME_SIZE value...
DEVICE_NAME_SIZE = 50

$(obj)/eisa-bus.o: $(obj)/devlist.h

quiet_cmd_eisaid = GEN     $@
      cmd_eisaid = sed -e '/^\#/D' -e 's/^\([[:alnum:]]\{7\}\) \+"\([^"]\{1,$(DEVICE_NAME_SIZE)\}\).*"/EISA_DEVINFO ("\1", "\2"),/' $< > $@

clean-files := devlist.h
$(obj)/devlist.h: $(src)/eisa.ids include/linux/device.h
	$(call cmd,eisaid)
