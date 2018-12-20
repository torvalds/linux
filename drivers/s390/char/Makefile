# SPDX-License-Identifier: GPL-2.0
#
# S/390 character devices
#

ifdef CONFIG_FUNCTION_TRACER
# Do not trace early setup code
CFLAGS_REMOVE_sclp_early_core.o	= $(CC_FLAGS_FTRACE)
endif

GCOV_PROFILE_sclp_early_core.o		:= n
KCOV_INSTRUMENT_sclp_early_core.o	:= n
UBSAN_SANITIZE_sclp_early_core.o	:= n
KASAN_SANITIZE_sclp_early_core.o	:= n

CFLAGS_sclp_early_core.o		+= -D__NO_FORTIFY

CFLAGS_REMOVE_sclp_early_core.o	+= $(CC_FLAGS_EXPOLINE)

obj-y += ctrlchar.o keyboard.o defkeymap.o sclp.o sclp_rw.o sclp_quiesce.o \
	 sclp_cmd.o sclp_config.o sclp_cpi_sys.o sclp_ocf.o sclp_ctl.o \
	 sclp_early.o sclp_early_core.o sclp_sd.o

obj-$(CONFIG_TN3270) += raw3270.o
obj-$(CONFIG_TN3270_CONSOLE) += con3270.o
obj-$(CONFIG_TN3270_TTY) += tty3270.o
obj-$(CONFIG_TN3270_FS) += fs3270.o

obj-$(CONFIG_TN3215) += con3215.o

obj-$(CONFIG_SCLP_TTY) += sclp_tty.o
obj-$(CONFIG_SCLP_CONSOLE) += sclp_con.o
obj-$(CONFIG_SCLP_VT220_TTY) += sclp_vt220.o
obj-$(CONFIG_SCLP_ASYNC) += sclp_async.o

obj-$(CONFIG_PCI) += sclp_pci.o

obj-$(CONFIG_VMLOGRDR) += vmlogrdr.o
obj-$(CONFIG_VMCP) += vmcp.o

tape-$(CONFIG_PROC_FS) += tape_proc.o
tape-objs := tape_core.o tape_std.o tape_char.o $(tape-y)
obj-$(CONFIG_S390_TAPE) += tape.o tape_class.o
obj-$(CONFIG_S390_TAPE_34XX) += tape_34xx.o
obj-$(CONFIG_S390_TAPE_3590) += tape_3590.o
obj-$(CONFIG_MONREADER) += monreader.o
obj-$(CONFIG_MONWRITER) += monwriter.o
obj-$(CONFIG_S390_VMUR) += vmur.o
obj-$(CONFIG_CRASH_DUMP) += sclp_sdias.o zcore.o

hmcdrv-objs := hmcdrv_mod.o hmcdrv_dev.o hmcdrv_ftp.o hmcdrv_cache.o diag_ftp.o sclp_ftp.o
obj-$(CONFIG_HMC_DRV) += hmcdrv.o

chkbss := sclp_early_core.o
include $(srctree)/arch/s390/scripts/Makefile.chkbss
