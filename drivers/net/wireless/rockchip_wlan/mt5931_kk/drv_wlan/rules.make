# --------------------------------------------------------------------
# Copyright (c) 2007 MediaTek Inc.
#
# All rights reserved. Copying, compilation, modification, distribution
# or any other use whatsoever of this material is strictly prohibited
# except in accordance with a Software License Agreement with
# MediaTek Inc.
# --------------------------------------------------------------------

# --------------------------------------------------------------------
# This file contains rules which are shared between multiple Makefiles.
# --------------------------------------------------------------------

#
# False targets.
#
.PHONY: dummy

#
# Special variables which should not be exported
#
unexport O_TARGET
unexport obj-y
unexport subdir-y

comma   := ,


#
# Get things started.
#
first_rule: sub_dirs
	@$(MAKE) all_targets

SUB_DIRS	:= $(subdir-y)

#
# Common rules
#
%.o:	%.c
	@echo "  [CC]	$@"
	@$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

all_targets: $(O_TARGET)

#
# Rule to compile a set of .o files into one .o file
#
ifdef O_TARGET
$(O_TARGET): $(obj-y)
	@rm -f $@
    ifneq "$(strip $(obj-y))" ""
	@$(LD) $(EXTRA_LDFLAGS) -r -o $@ $(filter $(obj-y), $^)
    else
	@$(AR) rcs $@
    endif
	@ ( \
	    echo 'ifeq ($(strip $(subst $(comma),:,$(EXTRA_LDFLAGS) $(obj-y))),$$(strip $$(subst $$(comma),:,$$(EXTRA_LDFLAGS) $$(obj-y))))' ; \
	    echo 'FILES_FLAGS_UP_TO_DATE += $@' ; \
	    echo 'endif' \
	) > $(dir $@)/.$(notdir $@).flags
endif # O_TARGET

#
# A rule to make subdirectories
#
subdir-list = $(sort $(patsubst %,_subdir_%,$(SUB_DIRS)))
sub_dirs: dummy $(subdir-list)

ifdef SUB_DIRS
$(subdir-list) : dummy
	@$(MAKE) -C $(patsubst _subdir_%,%,$@)
endif


#
# A rule to do nothing
#
dummy:

#
# Find files whose flags have changed and force recompilation.
# For safety, this works in the converse direction:
#   every file is forced, except those whose flags are positively up-to-date.
#
FILES_FLAGS_UP_TO_DATE :=

# For use in expunging commas from flags, which mung our checking.
comma = ,

FILES_FLAGS_EXIST := $(wildcard .*.flags)
ifneq ($(FILES_FLAGS_EXIST),)
include $(FILES_FLAGS_EXIST)
endif

FILES_FLAGS_CHANGED := $(strip \
    $(filter-out $(FILES_FLAGS_UP_TO_DATE), \
	$(O_TARGET) \
	))

ifneq ($(FILES_FLAGS_CHANGED),)
$(FILES_FLAGS_CHANGED): dummy
endif

