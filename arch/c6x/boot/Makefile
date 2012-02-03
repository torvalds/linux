#
# Makefile for bootable kernel images
#

OBJCOPYFLAGS_vmlinux.bin := -O binary
$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

DTC_FLAGS ?= -p 1024

ifneq ($(DTB),)
obj-y += linked_dtb.o
endif

$(obj)/%.dtb: $(src)/dts/%.dts FORCE
	$(call cmd,dtc)

quiet_cmd_cp = CP      $< $@$2
	cmd_cp = cat $< >$@$2 || (rm -f $@ && echo false)

# Generate builtin.dtb from $(DTB).dtb
$(obj)/builtin.dtb: $(obj)/$(DTB).dtb
	$(call if_changed,cp)

$(obj)/linked_dtb.o: $(obj)/builtin.dtb

$(obj)/dtbImage.%: vmlinux
	$(call if_changed,objcopy)

clean-files := $(obj)/*.dtb
