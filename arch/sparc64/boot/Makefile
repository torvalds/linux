# $Id: Makefile,v 1.4 1997/12/15 20:08:56 ecd Exp $
# Makefile for the Sparc64 boot stuff.
#
# Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
# Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)

ROOT_IMG	:= /usr/src/root.img
ELFTOAOUT	:= elftoaout

hostprogs-y	:= piggyback
targets		:= image tftpboot.img vmlinux.aout

quiet_cmd_elftoaout = ELF2AOUT $@
      cmd_elftoaout = $(ELFTOAOUT) vmlinux -o $@
quiet_cmd_piggy     = PIGGY   $@
      cmd_piggy     = $(obj)/piggyback $@ System.map $(ROOT_IMG)
quiet_cmd_strip     = STRIP   $@
      cmd_strip     = $(STRIP) -R .comment -R .note -K sun4u_init -K _end -K _start vmlinux -o $@


# Actual linking
$(obj)/image: vmlinux FORCE
	$(call if_changed,strip)
	@echo '  kernel: $@ is ready'

$(obj)/tftpboot.img: vmlinux $(obj)/piggyback System.map $(ROOT_IMG) FORCE
	$(call if_changed,elftoaout)
	$(call if_changed,piggy)
	@echo '  kernel: $@ is ready'

$(obj)/vmlinux.aout: vmlinux FORCE
	$(call if_changed,elftoaout)
	@echo '  kernel: $@ is ready'

