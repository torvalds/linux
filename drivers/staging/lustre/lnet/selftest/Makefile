subdir-ccflags-y += -I$(srctree)/drivers/staging/lustre/include
subdir-ccflags-y += -I$(srctree)/drivers/staging/lustre/lustre/include

obj-$(CONFIG_LNET_SELFTEST) := lnet_selftest.o

lnet_selftest-y := console.o conrpc.o conctl.o framework.o timer.o rpc.o \
		   module.o ping_test.o brw_test.o
