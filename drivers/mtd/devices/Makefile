#
# linux/drivers/devices/Makefile
#
# $Id: Makefile.common,v 1.7 2004/12/22 17:51:15 joern Exp $

#                       *** BIG UGLY NOTE ***
#
# The removal of get_module_symbol() and replacement with
# inter_module_register() et al has introduced a link order dependency
# here where previously there was none.  We now have to ensure that
# doc200[01].o are linked before docprobe.o

obj-$(CONFIG_MTD_DOC2000)	+= doc2000.o
obj-$(CONFIG_MTD_DOC2001)	+= doc2001.o
obj-$(CONFIG_MTD_DOC2001PLUS)	+= doc2001plus.o
obj-$(CONFIG_MTD_DOCPROBE)	+= docprobe.o
obj-$(CONFIG_MTD_DOCECC)	+= docecc.o
obj-$(CONFIG_MTD_SLRAM)		+= slram.o
obj-$(CONFIG_MTD_PHRAM)		+= phram.o
obj-$(CONFIG_MTD_PMC551)	+= pmc551.o
obj-$(CONFIG_MTD_MS02NV)	+= ms02-nv.o
obj-$(CONFIG_MTD_MTDRAM)	+= mtdram.o
obj-$(CONFIG_MTD_LART)		+= lart.o
obj-$(CONFIG_MTD_BLKMTD)	+= blkmtd.o
obj-$(CONFIG_MTD_BLOCK2MTD)	+= block2mtd.o
obj-$(CONFIG_MTD_DATAFLASH)	+= mtd_dataflash.o
obj-$(CONFIG_MTD_M25P80)	+= m25p80.o
