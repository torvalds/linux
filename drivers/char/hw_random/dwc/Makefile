# SPDX-License-Identifier: GPL-2.0

ccflags-y := -I $(srctree)/$(src)/src/trng/include \
	-I $(srctree)/$(src)/src/pdu/linux/include

obj-$(CONFIG_HW_RANDOM_DWC) += elppdu.o
elppdu-objs := src/pdu/linux/kernel/pdu.o \
		src/pdu/common/pdu/pdu_dev32.o \

obj-$(CONFIG_HW_RANDOM_DWC) += elpmem.o
elpmem-objs := src/pdu/linux/kernel/spacc_mem.o \

obj-$(CONFIG_HW_RANDOM_DWC) += nisttrng.o
nisttrng-objs := src/trng/kernel/nist_trng.o \
		 src/trng/trng/nist_trng.o \
		 src/trng/trng/nist_trng_private.o \

clean:
	@find \( -name '*.o' \
		-o -name '*.a' \
		-o -name '*.order' \
		\) -type f -print | xargs rm -rvf
