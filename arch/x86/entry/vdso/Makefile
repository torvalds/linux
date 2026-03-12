# SPDX-License-Identifier: GPL-2.0
#
# Building vDSO images for x86.
#

# Regular kernel objects
obj-y				:= vma.o extable.o
obj-$(CONFIG_COMPAT_32)		+= vdso32-setup.o

# vDSO directories
obj-$(CONFIG_X86_64)		+= vdso64/
obj-$(CONFIG_COMPAT_32)		+= vdso32/
