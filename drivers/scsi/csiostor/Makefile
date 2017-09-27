#
## Chelsio FCoE driver
#
##

ccflags-y += -I$(srctree)/drivers/net/ethernet/chelsio/cxgb4

obj-$(CONFIG_SCSI_CHELSIO_FCOE) += csiostor.o

csiostor-objs := csio_attr.o csio_init.o csio_lnode.o csio_scsi.o \
		csio_hw.o csio_hw_t5.o csio_isr.o \
		csio_mb.o csio_rnode.o csio_wr.o
