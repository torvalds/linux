#
# arch/cris/boot/Makefile
#

zImage: compressed/vmlinuz

compressed/vmlinuz: $(TOPDIR)/vmlinux
	@$(MAKE) -C compressed vmlinuz

clean:
	rm -f zImage tools/build compressed/vmlinux.out
	@$(MAKE) -C compressed clean
