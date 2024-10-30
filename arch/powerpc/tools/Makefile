# SPDX-License-Identifier: GPL-2.0-or-later

quiet_cmd_gen_ftrace_ool_stubs = GEN     $@
	cmd_gen_ftrace_ool_stubs = $< "$(CONFIG_PPC_FTRACE_OUT_OF_LINE_NUM_RESERVE)" "$(CONFIG_64BIT)" \
				   "$(OBJDUMP)" vmlinux.o $@

$(obj)/vmlinux.arch.S: $(src)/ftrace-gen-ool-stubs.sh vmlinux.o FORCE
	$(call if_changed,gen_ftrace_ool_stubs)

targets += vmlinux.arch.S
