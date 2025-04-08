# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux,intel}.com)
#

# Don't instrument UML-specific code; without this, we may crash when
# accessing the instrumentation buffer for the first time from the
# kernel.
KCOV_INSTRUMENT                := n

CPPFLAGS_vmlinux.lds := -DSTART=$(LDS_START)		\
                        -DELF_ARCH=$(LDS_ELF_ARCH)	\
                        -DELF_FORMAT=$(LDS_ELF_FORMAT)	\
			$(LDS_EXTRA)
extra-y := vmlinux.lds

obj-y = config.o exec.o exitcode.o irq.o ksyms.o mem.o \
	physmem.o process.o ptrace.o reboot.o sigio.o \
	signal.o sysrq.o time.o tlb.o trap.o \
	um_arch.o umid.o kmsg_dump.o capflags.o skas/
obj-y += load_file.o

obj-$(CONFIG_BLK_DEV_INITRD) += initrd.o
obj-$(CONFIG_GPROF)	+= gprof_syms.o
obj-$(CONFIG_OF) += dtb.o
obj-$(CONFIG_EARLY_PRINTK) += early_printk.o
obj-$(CONFIG_STACKTRACE) += stacktrace.o
obj-$(CONFIG_GENERIC_PCI_IOMAP) += ioport.o

USER_OBJS := config.o

include $(srctree)/arch/um/scripts/Makefile.rules

targets := config.c config.tmp capflags.c

# Be careful with the below Sed code - sed is pitfall-rich!
# We use sed to lower build requirements, for "embedded" builders for instance.

$(obj)/config.tmp: $(objtree)/.config FORCE
	$(call if_changed,quote1)

quiet_cmd_quote1 = QUOTE   $@
      cmd_quote1 = sed -e 's/"/\\"/g' -e 's/^/"/' -e 's/$$/\\n",/' \
		   $< > $@

$(obj)/config.c: $(src)/config.c.in $(obj)/config.tmp FORCE
	$(call if_changed,quote2)

quiet_cmd_mkcapflags = MKCAP   $@
      cmd_mkcapflags = $(CONFIG_SHELL) $(src)/../../x86/kernel/cpu/mkcapflags.sh $@ $^

cpufeature = $(src)/../../x86/include/asm/cpufeatures.h
vmxfeature = $(src)/../../x86/include/asm/vmxfeatures.h

$(obj)/capflags.c: $(cpufeature) $(vmxfeature) $(src)/../../x86/kernel/cpu/mkcapflags.sh FORCE
	$(call if_changed,mkcapflags)

quiet_cmd_quote2 = QUOTE   $@
      cmd_quote2 = sed -e '/CONFIG/{'          \
		  -e 's/"CONFIG"//'            \
		  -e 'r $(obj)/config.tmp'     \
		  -e 'a \'                     \
		  -e '""'                      \
		  -e '}'                       \
		  $< > $@
