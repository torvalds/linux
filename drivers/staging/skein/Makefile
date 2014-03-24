#
# Makefile for the skein secure hash algorithm
#
subdir-ccflags-y := -I$(src)/include/

obj-$(CONFIG_CRYPTO_SKEIN) +=   skein.o \
				skeinApi.o \
				skein_block.o

obj-$(CONFIG_CRYPTO_THREEFISH) += threefish1024Block.o \
				  threefish256Block.o \
				  threefish512Block.o \
				  threefishApi.o
