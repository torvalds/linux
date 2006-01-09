#
# Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

extra-y := vmlinux.lds
clean-files :=

obj-y = config.o exec_kern.o exitcode.o \
	init_task.o irq.o irq_user.o ksyms.o mem.o physmem.o \
	process_kern.o ptrace.o reboot.o resource.o sigio_user.o sigio_kern.o \
	signal_kern.o smp.o syscall_kern.o sysrq.o time.o \
	time_kern.o tlb.o trap_kern.o uaccess.o um_arch.o umid.o \
	user_util.o

obj-$(CONFIG_BLK_DEV_INITRD) += initrd.o
obj-$(CONFIG_GPROF)	+= gprof_syms.o
obj-$(CONFIG_GCOV)	+= gmon_syms.o
obj-$(CONFIG_TTY_LOG)	+= tty_log.o
obj-$(CONFIG_SYSCALL_DEBUG) += syscall.o

obj-$(CONFIG_MODE_TT) += tt/
obj-$(CONFIG_MODE_SKAS) += skas/

user-objs-$(CONFIG_TTY_LOG) += tty_log.o

USER_OBJS := $(user-objs-y) config.o time.o tty_log.o user_util.o

include arch/um/scripts/Makefile.rules

targets := config.c config.tmp

# Be careful with the below Sed code - sed is pitfall-rich!
# We use sed to lower build requirements, for "embedded" builders for instance.

$(obj)/config.tmp: $(objtree)/.config FORCE
	$(call if_changed,quote1)

quiet_cmd_quote1 = QUOTE   $@
      cmd_quote1 = sed -e 's/"/\\"/g' -e 's/^/"/' -e 's/$$/\\n"/' \
		   $< > $@

$(obj)/config.c: $(src)/config.c.in $(obj)/config.tmp FORCE
	$(call if_changed,quote2)

quiet_cmd_quote2 = QUOTE   $@
      cmd_quote2 = sed -e '/CONFIG/{'          \
		  -e 's/"CONFIG"\;/""/'        \
		  -e 'r $(obj)/config.tmp'     \
		  -e 'a \'                     \
		  -e '""\;'                    \
		  -e '}'                       \
		  $< > $@
