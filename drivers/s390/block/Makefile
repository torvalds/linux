#
# S/390 block devices
#

dasd_eckd_mod-objs := dasd_eckd.o dasd_3990_erp.o dasd_alias.o
dasd_fba_mod-objs  := dasd_fba.o
dasd_diag_mod-objs := dasd_diag.o
dasd_mod-objs      := dasd.o dasd_ioctl.o dasd_proc.o dasd_devmap.o \
			dasd_genhd.o dasd_erp.o
ifdef CONFIG_DASD_EER
dasd_mod-objs      += dasd_eer.o
endif

obj-$(CONFIG_DASD) += dasd_mod.o
obj-$(CONFIG_DASD_DIAG) += dasd_diag_mod.o
obj-$(CONFIG_DASD_ECKD) += dasd_eckd_mod.o
obj-$(CONFIG_DASD_FBA)  += dasd_fba_mod.o
obj-$(CONFIG_BLK_DEV_XPRAM) += xpram.o
obj-$(CONFIG_DCSSBLK) += dcssblk.o

scm_block-objs := scm_drv.o scm_blk.o
obj-$(CONFIG_SCM_BLOCK) += scm_block.o
