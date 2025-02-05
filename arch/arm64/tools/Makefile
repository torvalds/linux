# SPDX-License-Identifier: GPL-2.0

gen := arch/$(ARCH)/include/generated
kapi := $(gen)/asm

kapisyshdr-y := cpucap-defs.h sysreg-defs.h

kapi-hdrs-y := $(addprefix $(kapi)/, $(kapisyshdr-y))

targets += $(addprefix ../../../, $(kapi-hdrs-y))

PHONY += kapi

all: $(syscall64) kapi

kapi:   $(kapi-hdrs-y)

quiet_cmd_gen_cpucaps = GEN     $@
      cmd_gen_cpucaps = mkdir -p $(dir $@); $(AWK) -f $(real-prereqs) > $@

quiet_cmd_gen_sysreg = GEN     $@
      cmd_gen_sysreg = mkdir -p $(dir $@); $(AWK) -f $(real-prereqs) > $@

$(kapi)/cpucap-defs.h: $(src)/gen-cpucaps.awk $(src)/cpucaps FORCE
	$(call if_changed,gen_cpucaps)

$(kapi)/sysreg-defs.h: $(src)/gen-sysreg.awk $(src)/sysreg FORCE
	$(call if_changed,gen_sysreg)
