#
# arch/cris/arch-v32/boot/Makefile
#
target = $(target_boot_dir)
src    = $(src_boot_dir)

zImage: compressed/vmlinuz

compressed/vmlinuz: $(objtree)/vmlinux
	@$(MAKE) -f $(src)/compressed/Makefile $(objtree)/vmlinuz

clean:
	rm -f zImage tools/build compressed/vmlinux.out
	@$(MAKE) -f $(src)/compressed/Makefile clean
