# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_IBM_ASM) := ibmasm.o

ibmasm-y :=	module.o      \
		ibmasmfs.o    \
		event.o       \
		command.o     \
		remote.o      \
		heartbeat.o   \
		r_heartbeat.o \
		dot_command.o \
		lowlevel.o

ibmasm-$(CONFIG_SERIAL_8250) += uart.o

