subdir-ccflags-y += -I$(srctree)/drivers/staging/lustre/include
subdir-ccflags-y += -I$(srctree)/drivers/staging/lustre/lustre/include

obj-$(CONFIG_LUSTRE_FS) += obdclass.o

obdclass-y := linux/linux-module.o linux/linux-sysctl.o \
	      llog.o llog_cat.o llog_obd.o llog_swab.o class_obd.o debug.o \
	      genops.o uuid.o lprocfs_status.o lprocfs_counters.o \
	      lustre_handles.o lustre_peer.o statfs_pack.o linkea.o \
	      obdo.o obd_config.o obd_mount.o lu_object.o lu_ref.o \
	      cl_object.o cl_page.o cl_lock.o cl_io.o kernelcomm.o
