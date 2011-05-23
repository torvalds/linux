EXTRA_CFLAGS += -I$(srctree)/drivers/target/ \
		-I$(srctree)/drivers/scsi/ \
		-I$(srctree)/include/scsi/ \
		-I$(srctree)/drivers/target/tcm_fc/

tcm_fc-y +=	tfc_cmd.o \
		tfc_conf.o \
		tfc_io.o \
		tfc_sess.o

obj-$(CONFIG_TCM_FC)	+= tcm_fc.o

ifdef CONFIGFS_TCM_FC_DEBUG
EXTRA_CFLAGS	+= -DTCM_FC_DEBUG
endif
