obj-$(CONFIG_BCM2835_VCHIQ)	+= vchiq.o

vchiq-objs := \
   interface/vchiq_arm/vchiq_core.o  \
   interface/vchiq_arm/vchiq_arm.o \
   interface/vchiq_arm/vchiq_kern_lib.o \
   interface/vchiq_arm/vchiq_2835_arm.o \
   interface/vchiq_arm/vchiq_debugfs.o \
   interface/vchiq_arm/vchiq_shim.o \
   interface/vchiq_arm/vchiq_util.o \
   interface/vchiq_arm/vchiq_connected.o \

ccflags-y += -DVCOS_VERIFY_BKPTS=1 -Idrivers/staging/vc04_services -DUSE_VCHIQ_ARM -D__VCCOREVER__=0x04000000

