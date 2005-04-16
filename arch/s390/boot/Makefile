#
# Makefile for the linux s390-specific parts of the memory manager.
#

COMPILE_VERSION := __linux_compile_version_id__`hostname |  \
			tr -c '[0-9A-Za-z]' '_'`__`date | \
			tr -c '[0-9A-Za-z]' '_'`_t

EXTRA_CFLAGS  := -DCOMPILE_VERSION=$(COMPILE_VERSION) -gstabs -I.

targets := image

$(obj)/image: vmlinux FORCE
	$(call if_changed,objcopy)

install: $(CONFIGURE) $(obj)/image
	sh -x  $(srctree)/$(obj)/install.sh $(KERNELRELEASE) $(obj)/image \
	      System.map Kerntypes "$(INSTALL_PATH)"
