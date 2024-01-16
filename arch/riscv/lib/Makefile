# SPDX-License-Identifier: GPL-2.0-only
lib-y			+= delay.o
lib-y			+= memcpy.o
lib-y			+= memset.o
lib-y			+= memmove.o
lib-y			+= strcmp.o
lib-y			+= strlen.o
lib-y			+= strncmp.o
ifeq ($(CONFIG_MMU), y)
lib-y				+= uaccess.o
lib-$(CONFIG_RISCV_ISA_V)	+= uaccess_vector.o
endif
lib-$(CONFIG_64BIT)	+= tishift.o
lib-$(CONFIG_RISCV_ISA_ZICBOZ)	+= clear_page.o

obj-$(CONFIG_FUNCTION_ERROR_INJECTION) += error-inject.o
lib-$(CONFIG_RISCV_ISA_V)	+= xor.o
lib-$(CONFIG_RISCV_ISA_V)	+= riscv_v_helpers.o
