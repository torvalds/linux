#
# Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux,intel}.com)
# Licensed under the GPL
#

CPPFLAGS_vmlinux.lds := -U$(SUBARCH) -DSTART=$(LDS_START) \
                        -DELF_ARCH=$(LDS_ELF_ARCH)        \
                        -DELF_FORMAT=$(LDS_ELF_FORMAT)
extra-y := vmlinux.lds
clean-files :=

obj-y = config.o exec.o exitcode.o init_task.o irq.o ksyms.o mem.o \
	physmem.o process.o ptrace.o reboot.o sigio.o \
	signal.o smp.o syscall.o sysrq.o time.o tlb.o trap.o uaccess.o \
	um_arch.o umid.o skas/

obj-$(CONFIG_BLK_DEV_INITRD) += initrd.o
obj-$(CONFIG_GPROF)	+= gprof_syms.o
obj-$(CONFIG_GCOV)	+= gmon_syms.o
obj-$(CONFIG_EARLY_PRINTK) += early_printk.o

USER_OBJS := config.o

include arch/um/scripts/Makefile.rules

targets := config.c config.tmp

# Be careful with the below Sed code - sed is pitfall-rich!
# We use sed to lower build requirements, for "embedded" builders for instance.

$(obj)/config.tmp: $(objtree)/.config FORCE
	$(call if_changed,quote1)

quiet_cmd_quote1 = QUOTE   $@
      cmd_quote1 = sed -e 's/"/\\"/g' -e 's/^/"/' -e 's/$$/\\n",/' \
		   $< > $@

$(obj)/config.c: $(src)/config.c.in $(obj)/config.tmp FORCE
	$(call if_changed,quote2)

quiet_cmd_quote2 = QUOTE   $@
      cmd_quote2 = sed -e '/CONFIG/{'          \
		  -e 's/"CONFIG"//'            \
		  -e 'r $(obj)/config.tmp'     \
		  -e 'a \'                     \
		  -e '""'                      \
		  -e '}'                       \
		  $< > $@
