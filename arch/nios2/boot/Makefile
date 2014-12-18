#
# arch/nios2/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

UIMAGE_LOADADDR = $(shell $(NM) vmlinux | awk '$$NF == "_stext" {print $$1}')
UIMAGE_ENTRYADDR = $(shell $(NM) vmlinux | awk '$$NF == "_start" {print $$1}')
UIMAGE_COMPRESSION = gzip

OBJCOPYFLAGS_vmlinux.bin := -O binary

targets += vmlinux.bin vmlinux.gz vmImage

$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/vmlinux.gz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,gzip)

$(obj)/vmImage: $(obj)/vmlinux.gz
	$(call if_changed,uimage)
	@$(kecho) 'Kernel: $@ is ready'

# Rule to build device tree blobs
DTB_SRC := $(patsubst "%",%,$(CONFIG_NIOS2_DTB_SOURCE))

# Make sure the generated dtb gets removed during clean
extra-$(CONFIG_NIOS2_DTB_SOURCE_BOOL) += system.dtb

$(obj)/system.dtb: $(DTB_SRC) FORCE
	$(call cmd,dtc)

# Ensure system.dtb exists
$(obj)/linked_dtb.o: $(obj)/system.dtb

obj-$(CONFIG_NIOS2_DTB_SOURCE_BOOL) += linked_dtb.o

targets += $(dtb-y)

# Rule to build device tree blobs with make command
$(obj)/%.dtb: $(src)/dts/%.dts FORCE
	$(call if_changed_dep,dtc)

$(obj)/dtbs: $(addprefix $(obj)/, $(dtb-y))

clean-files := *.dtb

install:
	sh $(srctree)/$(src)/install.sh $(KERNELRELEASE) $(BOOTIMAGE) System.map "$(INSTALL_PATH)"
