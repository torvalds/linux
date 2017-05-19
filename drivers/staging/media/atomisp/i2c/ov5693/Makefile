obj-$(CONFIG_VIDEO_OV5693) += ov5693.o

# HACK! While this driver is in bad shape, don't enable several warnings
#       that would be otherwise enabled with W=1
ccflags-y += -Wno-unused-but-set-variable -Wno-missing-prototypes \
             -Wno-unused-const-variable -Wno-missing-declarations
