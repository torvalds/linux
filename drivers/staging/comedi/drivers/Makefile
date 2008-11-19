# Makefile for individual comedi drivers
#

# Comedi "helper" modules
obj-$(CONFIG_COMEDI)			+= comedi_fc.o
obj-$(CONFIG_COMEDI)			+= comedi_bond.o
obj-$(CONFIG_COMEDI)			+= comedi_test.o
obj-$(CONFIG_COMEDI)			+= comedi_parport.o

# Comedi PCI drivers
obj-$(CONFIG_COMEDI_PCI_DRIVERS)	+= mite.o
obj-$(CONFIG_COMEDI_PCI_DRIVERS)	+= icp_multi.o

# Comedi USB drivers
obj-$(CONFIG_COMEDI_USB_DRIVERS)	+= usbdux.o
obj-$(CONFIG_COMEDI_USB_DRIVERS)	+= usbduxfast.o
obj-$(CONFIG_COMEDI_USB_DRIVERS)	+= dt9812.o
