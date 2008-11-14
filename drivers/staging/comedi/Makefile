obj-$(CONFIG_COMEDI) += comedi.o
obj-$(CONFIG_COMEDI_RT) += comedi_rt.o

obj-$(CONFIG_COMEDI)	+= kcomedilib/
obj-$(CONFIG_COMEDI)	+= drivers/

comedi-objs :=		\
	comedi_fops.o	\
	proc.o		\
	range.o		\
	drivers.o	\
	comedi_compat32.o \
	comedi_ksyms.o	\

comedi_rt-objs :=	\
	rt_pend_tq.o	\
	rt.o
