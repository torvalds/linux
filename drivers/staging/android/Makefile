ccflags-y += -I$(src)			# needed for trace events

obj-y					+= ion/

ashmem_linux-y				+= ashmem.o
obj-$(CONFIG_ASHMEM)			+= ashmem_linux.o
obj-$(CONFIG_ANDROID_VSOC)		+= vsoc.o
