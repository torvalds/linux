#
# linux/drivers/nand/Makefile
#
# $Id: Makefile,v 1.2.4.1 2010/07/14 02:28:12 Administrator Exp $
#
obj-$(CONFIG_MTD_NAND_RK29XX)		+= flash.o
obj-$(CONFIG_MTD_NAND_RK29XX)		+= ftl.o
obj-$(CONFIG_MTD_NAND_RK29XX)		+= rknand_base.o
obj-$(CONFIG_MTD_RKNAND_BUFFER)		+= rknand_buffer.o

#obj-$(CONFIG_MTD_UBI)		+= ubi/
#$(obj)/flash.o: $(obj)/flash.uu
#	uudecode $(obj)/flash.uu -o $(obj)/flash.o
#$(obj)/ftl.o: $(obj)/ftl.uu
#	uudecode $(obj)/ftl.uu -o $(obj)/ftl.o
#$(obj)/rknand_buffer.o: $(obj)/rknand_buffer.uu
#	uudecode $(obj)/rknand_buffer.uu -o $(obj)/rknand_buffer.o