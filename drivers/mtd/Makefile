#
# Makefile for the memory technology device drivers.
#

# Core functionality.
obj-$(CONFIG_MTD)		+= mtd.o
mtd-y				:= mtdcore.o mtdsuper.o
mtd-$(CONFIG_MTD_PARTITIONS)	+= mtdpart.o

obj-$(CONFIG_MTD_CONCAT)	+= mtdconcat.o
obj-$(CONFIG_MTD_REDBOOT_PARTS) += redboot.o
obj-$(CONFIG_MTD_CMDLINE_PARTS) += cmdlinepart.o
obj-$(CONFIG_MTD_AFS_PARTS)	+= afs.o
obj-$(CONFIG_MTD_AR7_PARTS)	+= ar7part.o
obj-$(CONFIG_MTD_OF_PARTS)      += ofpart.o

# 'Users' - code which presents functionality to userspace.
obj-$(CONFIG_MTD_CHAR)		+= mtdchar.o
obj-$(CONFIG_MTD_BLKDEVS)	+= mtd_blkdevs.o
obj-$(CONFIG_MTD_BLOCK)		+= mtdblock.o
obj-$(CONFIG_MTD_BLOCK_RO)	+= mtdblock_ro.o
obj-$(CONFIG_FTL)		+= ftl.o
obj-$(CONFIG_NFTL)		+= nftl.o
obj-$(CONFIG_INFTL)		+= inftl.o
obj-$(CONFIG_RFD_FTL)		+= rfd_ftl.o
obj-$(CONFIG_SSFDC)		+= ssfdc.o
obj-$(CONFIG_MTD_OOPS)		+= mtdoops.o

nftl-objs		:= nftlcore.o nftlmount.o
inftl-objs		:= inftlcore.o inftlmount.o

obj-y		+= chips/ lpddr/ maps/ devices/ nand/ onenand/ tests/

obj-$(CONFIG_MTD_UBI)		+= ubi/
