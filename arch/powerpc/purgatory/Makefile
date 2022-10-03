# SPDX-License-Identifier: GPL-2.0

KASAN_SANITIZE := n

targets += trampoline_$(BITS).o purgatory.ro

LDFLAGS_purgatory.ro := -e purgatory_start -r --no-undefined

$(obj)/purgatory.ro: $(obj)/trampoline_$(BITS).o FORCE
		$(call if_changed,ld)

$(obj)/kexec-purgatory.o: $(obj)/purgatory.ro

obj-y	+= kexec-purgatory.o
