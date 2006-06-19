obj-y += switch.o

obj-$(CONFIG_SPU_FS) += spufs.o
spufs-y += inode.o file.o context.o syscalls.o
spufs-y += sched.o backing_ops.o hw_ops.o run.o

# Rules to build switch.o with the help of SPU tool chain
SPU_CROSS	:= spu-
SPU_CC		:= $(SPU_CROSS)gcc
SPU_AS		:= $(SPU_CROSS)gcc
SPU_LD		:= $(SPU_CROSS)ld
SPU_OBJCOPY	:= $(SPU_CROSS)objcopy
SPU_CFLAGS	:= -O2 -Wall -I$(srctree)/include -I$(objtree)/include2
SPU_AFLAGS	:= -c -D__ASSEMBLY__ -I$(srctree)/include -I$(objtree)/include2
SPU_LDFLAGS	:= -N -Ttext=0x0

$(obj)/switch.o: $(obj)/spu_save_dump.h $(obj)/spu_restore_dump.h
clean-files := spu_save_dump.h spu_restore_dump.h

# Compile SPU files
      cmd_spu_cc = $(SPU_CC) $(SPU_CFLAGS) -c -o $@ $<
quiet_cmd_spu_cc = SPU_CC  $@
$(obj)/spu_%.o: $(src)/spu_%.c
	$(call if_changed,spu_cc)

# Assemble SPU files
      cmd_spu_as = $(SPU_AS) $(SPU_AFLAGS) -o $@ $<
quiet_cmd_spu_as = SPU_AS  $@
$(obj)/spu_%.o: $(src)/spu_%.S
	$(call if_changed,spu_as)

# Link SPU Executables
      cmd_spu_ld = $(SPU_LD) $(SPU_LDFLAGS) -o $@ $^
quiet_cmd_spu_ld = SPU_LD  $@
$(obj)/spu_%: $(obj)/spu_%_crt0.o $(obj)/spu_%.o
	$(call if_changed,spu_ld)

# Copy into binary format
      cmd_spu_objcopy = $(SPU_OBJCOPY) -O binary $< $@
quiet_cmd_spu_objcopy = OBJCOPY $@
$(obj)/spu_%.bin: $(src)/spu_%
	$(call if_changed,spu_objcopy)

# create C code from ELF executable
cmd_hexdump   = ( \
		echo "/*" ; \
		echo " * $*_dump.h: Copyright (C) 2005 IBM." ; \
		echo " * Hex-dump auto generated from $*.c." ; \
		echo " * Do not edit!" ; \
		echo " */" ; \
		echo "static unsigned int $*_code[] __page_aligned = {" ; \
		hexdump -v -e '"0x" 4/1 "%02x" "," "\n"' $< ; \
		echo "};" ; \
		) > $@
quiet_cmd_hexdump = HEXDUMP $@
$(obj)/%_dump.h: $(obj)/%.bin
	$(call if_changed,hexdump)
