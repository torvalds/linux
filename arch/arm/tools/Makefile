#
# linux/arch/arm/tools/Makefile
#
# Copyright (C) 2001 Russell King
#

gen := arch/$(ARCH)/include/generated
kapi := $(gen)/asm

kapi-hdrs-y := $(kapi)/mach-types.h

targets += $(addprefix ../../../,$(kapi-hdrs-y))

PHONY += kapi

kapi:	$(kapi-hdrs-y)

# Create output directory if not already present
_dummy := $(shell [ -d '$(kapi)' ] || mkdir -p '$(kapi)')

quiet_cmd_gen_mach = GEN     $@
      cmd_gen_mach = mkdir -p $(dir $@) && \
		     $(AWK) -f $(filter-out $(PHONY),$^) > $@ || \
		     { rm -f $@; /bin/false; }

$(kapi)/mach-types.h: $(src)/gen-mach-types $(src)/mach-types FORCE
	$(call if_changed,gen_mach)
