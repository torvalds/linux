#
# S/390 character devices
#

obj-y += ctrlchar.o keyboard.o defkeymap.o sclp.o sclp_rw.o sclp_quiesce.o \
	 sclp_info.o

obj-$(CONFIG_TN3270) += raw3270.o
obj-$(CONFIG_TN3270_CONSOLE) += con3270.o
obj-$(CONFIG_TN3270_TTY) += tty3270.o
obj-$(CONFIG_TN3270_FS) += fs3270.o

obj-$(CONFIG_TN3215) += con3215.o

obj-$(CONFIG_SCLP_TTY) += sclp_tty.o
obj-$(CONFIG_SCLP_CONSOLE) += sclp_con.o
obj-$(CONFIG_SCLP_VT220_TTY) += sclp_vt220.o
obj-$(CONFIG_SCLP_CPI) += sclp_cpi.o

obj-$(CONFIG_ZVM_WATCHDOG) += vmwatchdog.o
obj-$(CONFIG_VMLOGRDR) += vmlogrdr.o
obj-$(CONFIG_VMCP) += vmcp.o

tape-$(CONFIG_S390_TAPE_BLOCK) += tape_block.o
tape-$(CONFIG_PROC_FS) += tape_proc.o
tape-objs := tape_core.o tape_std.o tape_char.o $(tape-y)
obj-$(CONFIG_S390_TAPE) += tape.o tape_class.o
obj-$(CONFIG_S390_TAPE_34XX) += tape_34xx.o
obj-$(CONFIG_S390_TAPE_3590) += tape_3590.o
obj-$(CONFIG_MONREADER) += monreader.o
obj-$(CONFIG_MONWRITER) += monwriter.o
