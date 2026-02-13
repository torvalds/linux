# SPDX-License-Identifier: GPL-2.0-only
# RISC-V VDSO CFI Makefile
# This Makefile builds the VDSO with CFI support when CONFIG_RISCV_USER_CFI is enabled

# setting VDSO_CFI_BUILD triggers build for vdso differently
VDSO_CFI_BUILD := 1

# Set the source directory to the main vdso directory
src := $(srctree)/arch/riscv/kernel/vdso

# Copy all .S and .c files from vdso directory to vdso_cfi object build directory
vdso_c_sources := $(wildcard $(src)/*.c)
vdso_S_sources := $(wildcard $(src)/*.S)
vdso_c_objects := $(addprefix $(obj)/, $(notdir $(vdso_c_sources)))
vdso_S_objects := $(addprefix $(obj)/, $(notdir $(vdso_S_sources)))

$(vdso_S_objects): $(obj)/%.S: $(src)/%.S
	$(Q)cp $< $@

$(vdso_c_objects): $(obj)/%.c: $(src)/%.c
	$(Q)cp $< $@

# Include the main VDSO Makefile which contains all the build rules and sources
# The VDSO_CFI_BUILD variable will be passed to it to enable CFI compilation
include $(src)/Makefile
