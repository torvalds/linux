# SPDX-License-Identifier: GPL-2.0
targets += trampoline.o purgatory.ro kexec-purgatory.c

LDFLAGS_purgatory.ro := -e purgatory_start -r --no-undefined

$(obj)/purgatory.ro: $(obj)/trampoline.o FORCE
		$(call if_changed,ld)

quiet_cmd_bin2c = BIN2C   $@
      cmd_bin2c = $(objtree)/scripts/bin2c kexec_purgatory < $< > $@

$(obj)/kexec-purgatory.c: $(obj)/purgatory.ro FORCE
	$(call if_changed,bin2c)

obj-y	+= kexec-purgatory.o
