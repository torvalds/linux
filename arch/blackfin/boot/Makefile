#
# arch/blackfin/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

MKIMAGE := $(srctree)/scripts/mkuboot.sh

targets := vmImage vmImage.bin vmImage.bz2 vmImage.gz vmImage.lzma
extra-y += vmlinux.bin vmlinux.bin.gz vmlinux.bin.bz2 vmlinux.bin.lzma

quiet_cmd_uimage = UIMAGE  $@
      cmd_uimage = $(CONFIG_SHELL) $(MKIMAGE) -A $(ARCH) -O linux -T kernel \
                   -C $(2) -n '$(MACHINE)-$(KERNELRELEASE)' -a $(CONFIG_BOOT_LOAD) \
                   -e $(shell $(NM) vmlinux | awk '$$NF == "__start" {print $$1}') \
                   -d $< $@

$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/vmlinux.bin.gz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,gzip)

$(obj)/vmlinux.bin.bz2: $(obj)/vmlinux.bin FORCE
	$(call if_changed,bzip2)

$(obj)/vmlinux.bin.lzma: $(obj)/vmlinux.bin FORCE
	$(call if_changed,lzma)

$(obj)/vmImage.bin: $(obj)/vmlinux.bin
	$(call if_changed,uimage,none)

$(obj)/vmImage.bz2: $(obj)/vmlinux.bin.bz2
	$(call if_changed,uimage,bzip2)

$(obj)/vmImage.gz: $(obj)/vmlinux.bin.gz
	$(call if_changed,uimage,gzip)

$(obj)/vmImage.lzma: $(obj)/vmlinux.bin.lzma
	$(call if_changed,uimage,lzma)

suffix-y                      := bin
suffix-$(CONFIG_KERNEL_GZIP)  := gz
suffix-$(CONFIG_KERNEL_BZIP2) := bz2
suffix-$(CONFIG_KERNEL_LZMA)  := lzma
$(obj)/vmImage: $(obj)/vmImage.$(suffix-y)
	@ln -sf $(notdir $<) $@

install:
	sh $(srctree)/$(src)/install.sh $(KERNELRELEASE) $(BOOTIMAGE) System.map "$(INSTALL_PATH)"
