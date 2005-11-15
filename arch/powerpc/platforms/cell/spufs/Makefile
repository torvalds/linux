obj-$(CONFIG_SPU_FS) += spufs.o

spufs-y += inode.o file.o context.o switch.o syscalls.o

$(obj)/switch.o: $(obj)/spu_save_dump.h $(obj)/spu_restore_dump.h
