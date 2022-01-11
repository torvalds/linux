# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_OPTEE) += optee.o
optee-objs += core.o
optee-objs += call.o
optee-objs += notif.o
optee-objs += rpc.o
optee-objs += supp.o
optee-objs += device.o
optee-objs += smc_abi.o
optee-objs += ffa_abi.o

# for tracing framework to find optee_trace.h
CFLAGS_smc_abi.o := -I$(src)
