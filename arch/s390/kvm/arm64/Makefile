# SPDX-License-Identifier: GPL-2.0

KVM := ../../../../virt/kvm
KVM_DEV_NAME = kvm_arm64
KVM_DEV_MINOR = MISC_DYNAMIC_MINOR
include $(srctree)/virt/kvm/Makefile.kvm
include $(srctree)/arch/$(SRCARCH)/kvm/gmap/Makefile
include $(src)/Makefile.gen

ccflags-y += -I $(src) -I $(objtree)/$(obj) -I$(srctree)/arch/$(SRCARCH)/kvm/gmap -DKVM_S390_ARM64

kvm-arm64-local := \
	arm.o \
	guest.o \
	handle_exit.o \
	inject_fault.o \
	mmio.o \
	mmu.o \
	reset.o

kvm-arm64-obj := $(kvm-arm64-local)
kvm-arm64-obj += $(patsubst %.o,%-arm64.o,$(kvm-y))
kvm-arm64-obj += $(patsubst %.o,%-arm64.o,$(gmap-y))

targets += $(kvm-arm64-obj)

$(obj)/%-arm64.o: $(src)/%.c FORCE
	@mkdir -p $(dir $@)
	$(call if_changed_rule,cc_o_c)
	$(call cmd,force_checksrc)

# %-arm64.o stems don't match any kvm-arm64-y entry so Kbuild derives wrong
# KBUILD_MODNAME/KBUILD_MODFILE. Force include a generated header to override.
KVM_ARM64_MODNAME_H := $(obj)/generated/kvm_arm64_modname.h
ccflags-y += -include $(KVM_ARM64_MODNAME_H)

$(addprefix $(obj)/,$(kvm-arm64-obj)): $(KVM_ARM64_MODNAME_H)
$(foreach sfx, i s ll lst, \
	$(eval $(patsubst %.o,%.$(sfx),$(addprefix $(obj)/,$(kvm-arm64-local))): $(KVM_ARM64_MODNAME_H)))

targets += generated/kvm_arm64_modname.h

define filechk_kvm_arm64_modname
	echo '#ifndef __KVM_ARM64_MODNAME_H';			\
	echo '#define __KVM_ARM64_MODNAME_H';			\
	echo '#undef KBUILD_MODNAME';				\
	echo '#define KBUILD_MODNAME "$(KVM_DEV_NAME)"';	\
	echo '#undef __KBUILD_MODNAME';				\
	echo '#define __KBUILD_MODNAME $(KVM_DEV_NAME)';	\
	echo '#undef KBUILD_MODFILE';				\
	echo '#define KBUILD_MODFILE "$(obj)/kvm-arm64"';	\
	echo '#endif /* __KVM_ARM64_MODNAME_H */'
endef

$(KVM_ARM64_MODNAME_H): FORCE
	$(call filechk,kvm_arm64_modname)

# Members are not in real-obj-y. Set part-of-builtin to enable instrumentation.
$(addprefix $(obj)/,$(kvm-arm64-obj)): private part-of-builtin := y

ifeq ($(CONFIG_KVM),m)

kvm-arm64-y = $(kvm-arm64-obj)

else ifeq ($(CONFIG_KVM),y)

prereq-o-cmd = $(foreach o, $(filter %.o, $^), $(dir $(o)).$(notdir $(o)).cmd)
cmd_gen_symversions_o = \
	grep --no-filename "^\#SYMVER" $(prereq-o-cmd) >> $(dot-target).cmd || true

cmd_ld_o_o = $(cmd_ld)
define rule_ld_o_o
	$(call cmd_and_savecmd,ld)
	$(call cmd,gen_symversions_o)
endef

LDFLAGS_kvm-unnamespaced.o := -r
$(obj)/kvm-unnamespaced.o: $(addprefix $(obj)/,$(kvm-arm64-obj)) FORCE
	$(call if_changed_rule,ld_o_o)

quiet_cmd_nm_filter = NMFLTR  $@
	cmd_nm_filter = $(NM) --defined-only --format=just-symbols $< | sort -u > $@

$(obj)/kvm_symbol_list: $(obj)/kvm-unnamespaced.o FORCE
	$(call if_changed,nm_filter)

# cmd_oc_o_o carries -DKBUILD_MODFILE so generate_builtin_ranges.awk
# can attribute kvm-namespaced.o to kvm_arm64 via its .cmd file.
cmd_oc_o_o = $(cmd_objcopy) -DKBUILD_MODFILE='"$(obj)/kvm-arm64"'
define rule_oc_o_o
	$(call cmd,objcopy)
	@printf '%s\n' 'savedcmd_$@ := $(call make-cmd,oc_o_o)' > $(dot-target).cmd
	$(call cmd,gen_objtooldep)
	$(call cmd,gen_symversions_o)
endef

OBJCOPYFLAGS_kvm-namespaced.o := -O $(LD_BFD) --localize-symbols=$(obj)/kvm_symbol_list
$(obj)/kvm-namespaced.o: $(obj)/kvm-unnamespaced.o $(obj)/kvm_symbol_list FORCE
	$(call if_changed_rule,oc_o_o)

kvm-arm64-y = kvm-namespaced.o

targets += kvm-unnamespaced.o kvm_symbol_list kvm-namespaced.o

endif

clean-files += kvm-unnamespaced.o kvm_symbol_list kvm-namespaced.o

obj-$(CONFIG_KVM) += kvm-arm64.o

