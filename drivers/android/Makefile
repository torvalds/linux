ccflags-y += -I$(src)			# needed for trace events

binder_linux-y := binder.o binder_alloc.o
obj-$(CONFIG_ANDROID_BINDER_IPC) += binder_linux.o
binder_linux-$(CONFIG_ANDROID_BINDERFS) += binderfs.o
binder_linux-$(CONFIG_ANDROID_BINDER_IPC_SELFTEST) += binder_alloc_selftest.o

# binder-$(CONFIG_ANDROID_BINDER_IPC) := binder.o binder_alloc.o
# binder-$(CONFIG_ANDROID_BINDERFS) += binderfs.o
