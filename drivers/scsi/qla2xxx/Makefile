EXTRA_CFLAGS += -DUNIQUE_FW_NAME

qla2xxx-y := qla_os.o qla_init.o qla_mbx.o qla_iocb.o qla_isr.o qla_gs.o \
		qla_dbg.o qla_sup.o qla_rscn.o qla_attr.o

obj-$(CONFIG_SCSI_QLA_FC) += qla2xxx.o

qla2100-y := ql2100.o ql2100_fw.o
qla2200-y := ql2200.o ql2200_fw.o
qla2300-y := ql2300.o ql2300_fw.o
qla2322-y := ql2322.o ql2322_fw.o
qla2400-y := ql2400.o ql2400_fw.o

obj-$(CONFIG_SCSI_QLA21XX) += qla2xxx.o qla2100.o
obj-$(CONFIG_SCSI_QLA22XX) += qla2xxx.o qla2200.o
obj-$(CONFIG_SCSI_QLA2300) += qla2xxx.o qla2300.o
obj-$(CONFIG_SCSI_QLA2322) += qla2xxx.o qla2322.o
obj-$(CONFIG_SCSI_QLA24XX) += qla2xxx.o qla2400.o
