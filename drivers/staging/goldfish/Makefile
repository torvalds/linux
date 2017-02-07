#
# Makefile for the Goldfish audio driver
#

obj-$(CONFIG_GOLDFISH_AUDIO) += goldfish_audio.o
obj-$(CONFIG_MTD_GOLDFISH_NAND)	+= goldfish_nand.o

# and sync

ccflags-y := -Idrivers/staging/android
goldfish_sync-objs := goldfish_sync_timeline_fence.o goldfish_sync_timeline.o
obj-$(CONFIG_GOLDFISH_SYNC) += goldfish_sync.o
