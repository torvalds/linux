#
# arch/cris/boot/Makefile
#
target = $(target_boot_dir)
src    = $(src_boot_dir)

zImage: compressed/vmlinuz

compressed/vmlinuz:
	@$(MAKE) -f $(src)/compressed/Makefile $(target_compressed_dir)/vmlinuz

clean:
	@$(MAKE) -f $(src)/compressed/Makefile clean
