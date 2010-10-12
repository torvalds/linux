#
# arch/microblaze/boot/Makefile
#

MKIMAGE := $(srctree)/scripts/mkuboot.sh

obj-y += linked_dtb.o

targets := linux.bin linux.bin.gz simpleImage.%

OBJCOPYFLAGS := -O binary

# Where the DTS files live
dtstree         := $(srctree)/$(src)/dts

# Ensure system.dtb exists
$(obj)/linked_dtb.o: $(obj)/system.dtb

# Generate system.dtb from $(DTB).dtb
ifneq ($(DTB),system)
$(obj)/system.dtb: $(obj)/$(DTB).dtb
	$(call if_changed,cp)
endif

$(obj)/linux.bin: vmlinux FORCE
	$(call if_changed,objcopy)
	$(call if_changed,uimage)
	@echo 'Kernel: $@ is ready' ' (#'`cat .version`')'

$(obj)/linux.bin.gz: $(obj)/linux.bin FORCE
	$(call if_changed,gzip)
	@echo 'Kernel: $@ is ready' ' (#'`cat .version`')'

quiet_cmd_cp = CP      $< $@$2
	cmd_cp = cat $< >$@$2 || (rm -f $@ && echo false)

quiet_cmd_strip = STRIP   $@
	cmd_strip = $(STRIP) -K microblaze_start -K _end -K __log_buf \
				-K _fdt_start vmlinux -o $@

quiet_cmd_uimage = UIMAGE  $@.ub
	cmd_uimage = $(CONFIG_SHELL) $(MKIMAGE) -A microblaze -O linux -T kernel \
		-C none -n 'Linux-$(KERNELRELEASE)' \
		-a $(CONFIG_KERNEL_BASE_ADDR) -e $(CONFIG_KERNEL_BASE_ADDR) \
		-d $@ $@.ub

$(obj)/simpleImage.%: vmlinux FORCE
	$(call if_changed,cp,.unstrip)
	$(call if_changed,objcopy)
	$(call if_changed,uimage)
	$(call if_changed,strip)
	@echo 'Kernel: $@ is ready' ' (#'`cat .version`')'

# Rule to build device tree blobs
DTC = $(objtree)/scripts/dtc/dtc

# Rule to build device tree blobs
quiet_cmd_dtc = DTC     $@
	cmd_dtc = $(DTC) -O dtb -o $(obj)/$*.dtb -b 0 -p 1024 $(dtstree)/$*.dts

$(obj)/%.dtb: $(dtstree)/%.dts FORCE
	$(call if_changed,dtc)

clean-files += *.dtb simpleImage.*.unstrip linux.bin.ub
