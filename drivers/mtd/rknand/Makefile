#
# linux/drivers/nand/Makefile
#
# $Id: Makefile,v 1.2 2010/12/27 08:46:52 Administrator Exp $
#
obj-$(CONFIG_MTD_NAND_RK29XX)		+= flash.o ftl.o rknand_base.o emmc.o
obj-$(CONFIG_MTD_RKNAND_BUFFER)		+= rknand_buffer.o

#obj-$(CONFIG_MTD_UBI)		+= ubi/
$(obj)/flash.o: $(obj)/flash.uu
	uudecode $(obj)/flash.uu -o $(obj)/flash.o
$(obj)/ftl.o: $(obj)/ftl.uu
	uudecode $(obj)/ftl.uu -o $(obj)/ftl.o
$(obj)/rknand_buffer.o: $(obj)/rknand_buffer.uu
	uudecode $(obj)/rknand_buffer.uu -o $(obj)/rknand_buffer.o