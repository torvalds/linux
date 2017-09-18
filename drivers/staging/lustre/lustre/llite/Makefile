subdir-ccflags-y += -I$(srctree)/drivers/staging/lustre/include
subdir-ccflags-y += -I$(srctree)/drivers/staging/lustre/lustre/include

obj-$(CONFIG_LUSTRE_FS) += lustre.o
lustre-y := dcache.o dir.o file.o llite_lib.o llite_nfs.o \
	    rw.o rw26.o namei.o symlink.o llite_mmap.o range_lock.o \
	    xattr.o xattr_cache.o xattr_security.o \
	    super25.o statahead.o glimpse.o lcommon_cl.o lcommon_misc.o \
	    vvp_dev.o vvp_page.o vvp_lock.o vvp_io.o vvp_object.o \
	    lproc_llite.o
