comedi-y			:= comedi_fops.o range.o drivers.o \
				   comedi_buf.o
comedi-$(CONFIG_PROC_FS)	+= proc.o
comedi-$(CONFIG_COMPAT)		+= comedi_compat32.o

obj-$(CONFIG_COMEDI)		+= comedi.o

obj-$(CONFIG_COMEDI)		+= kcomedilib/
obj-$(CONFIG_COMEDI)		+= drivers/
