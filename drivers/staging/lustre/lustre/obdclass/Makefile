obj-$(CONFIG_LUSTRE_FS) += obdclass.o llog_test.o

obdclass-y := linux/linux-module.o linux/linux-obdo.o linux/linux-sysctl.o \
	      llog.o llog_cat.o llog_obd.o llog_swab.o class_obd.o debug.o \
	      genops.o uuid.o llog_ioctl.o lprocfs_status.o		   \
	      lprocfs_jobstats.o lustre_handles.o lustre_peer.o llog_osd.o \
	      local_storage.o statfs_pack.o obdo.o obd_config.o obd_mount.o\
	      mea.o lu_object.o dt_object.o capa.o cl_object.o   \
	      cl_page.o cl_lock.o cl_io.o lu_ref.o acl.o idmap.o	   \
	      md_local_object.o


ccflags-y := -I$(src)/../include
