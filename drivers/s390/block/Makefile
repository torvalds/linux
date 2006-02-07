#
# S/390 block devices
#

dasd_eckd_mod-objs := dasd_eckd.o dasd_3990_erp.o dasd_9343_erp.o
dasd_fba_mod-objs  := dasd_fba.o dasd_3370_erp.o dasd_9336_erp.o
dasd_diag_mod-objs := dasd_diag.o
dasd_eer_mod-objs := dasd_eer.o
dasd_mod-objs      := dasd.o dasd_ioctl.o dasd_proc.o dasd_devmap.o \
			dasd_genhd.o dasd_erp.o

obj-$(CONFIG_DASD) += dasd_mod.o
obj-$(CONFIG_DASD_DIAG) += dasd_diag_mod.o
obj-$(CONFIG_DASD_ECKD) += dasd_eckd_mod.o
obj-$(CONFIG_DASD_FBA)  += dasd_fba_mod.o
obj-$(CONFIG_DASD_CMB)  += dasd_cmb.o
obj-$(CONFIG_DASD_EER)  += dasd_eer.o
obj-$(CONFIG_BLK_DEV_XPRAM) += xpram.o
obj-$(CONFIG_DCSSBLK) += dcssblk.o
