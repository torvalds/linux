obj-$(CONFIG_LUSTRE_FS) += libcfs.o

libcfs-linux-objs := linux-tracefile.o linux-debug.o
libcfs-linux-objs += linux-prim.o linux-mem.o linux-cpu.o
libcfs-linux-objs += linux-fs.o linux-sync.o linux-tcpip.o
libcfs-linux-objs += linux-proc.o linux-curproc.o
libcfs-linux-objs += linux-utils.o linux-module.o
libcfs-linux-objs += linux-crypto.o linux-crypto-crc32.o
libcfs-linux-objs += linux-crypto-adler.o
libcfs-linux-objs += linux-crypto-crc32pclmul.o

libcfs-linux-objs := $(addprefix linux/,$(libcfs-linux-objs))

libcfs-all-objs := debug.o fail.o nidstrings.o module.o tracefile.o \
		   watchdog.o libcfs_string.o hash.o kernel_user_comm.o \
		   prng.o workitem.o upcall_cache.o libcfs_cpu.o \
		   libcfs_mem.o libcfs_lock.o crc32-pclmul_asm.o

libcfs-objs := $(libcfs-linux-objs) $(libcfs-all-objs)

ccflags-y := -I$(src)/../include
ccflags-y += -I$(src)/
