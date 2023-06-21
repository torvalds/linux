# SPDX-License-Identifier: GPL-2.0

KASAN_SANITIZE := n

targets += trampoline_$(BITS).o purgatory.ro kexec-purgatory.c

# When profile-guided optimization is enabled, llvm emits two different
# overlapping text sections, which is not supported by kexec. Remove profile
# optimization flags.
KBUILD_CFLAGS := $(filter-out -fprofile-sample-use=% -fprofile-use=%,$(KBUILD_CFLAGS))

LDFLAGS_purgatory.ro := -e purgatory_start -r --no-undefined

$(obj)/purgatory.ro: $(obj)/trampoline_$(BITS).o FORCE
		$(call if_changed,ld)

quiet_cmd_bin2c = BIN2C   $@
      cmd_bin2c = $(objtree)/scripts/bin2c kexec_purgatory < $< > $@

$(obj)/kexec-purgatory.c: $(obj)/purgatory.ro FORCE
	$(call if_changed,bin2c)

obj-y	+= kexec-purgatory.o
