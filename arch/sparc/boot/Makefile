# $Id: Makefile,v 1.10 2000/02/23 08:17:46 jj Exp $
# Makefile for the Sparc boot stuff.
#
# Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
# Copyright (C) 1997,1998 Jakub Jelinek (jj@ultra.linux.cz)

ROOT_IMG	:= /usr/src/root.img
ELFTOAOUT	:= elftoaout

hostprogs-y	:= piggyback btfixupprep
targets		:= tftpboot.img btfix.o btfix.S image

quiet_cmd_elftoaout	= ELFTOAOUT $@
      cmd_elftoaout	= $(ELFTOAOUT) $(obj)/image -o $@
quiet_cmd_piggy		= PIGGY   $@
      cmd_piggy		= $(obj)/piggyback $@ $(obj)/System.map $(ROOT_IMG)
quiet_cmd_btfix		= BTFIX   $@
      cmd_btfix		= $(OBJDUMP) -x vmlinux | $(obj)/btfixupprep > $@
quiet_cmd_sysmap        = SYSMAP  $(obj)/System.map
      cmd_sysmap        = $(CONFIG_SHELL) $(srctree)/scripts/mksysmap
quiet_cmd_image = LD      $@
      cmd_image = $(LD) $(LDFLAGS) $(EXTRA_LDFLAGS) $(LDFLAGS_$(@F)) -o $@

define rule_image
	$(if $($(quiet)cmd_image),               \
	  echo '  $($(quiet)cmd_image)' &&)      \
	  $(cmd_image);                          \
	$(if $($(quiet)cmd_sysmap),              \
	  echo '  $($(quiet)cmd_sysmap)' &&)  \
	$(cmd_sysmap) $@ $(obj)/System.map;      \
	if [ $$? -ne 0 ]; then                   \
		rm -f $@;                        \
		/bin/false;                      \
	fi;                                      \
	echo 'cmd_$@ := $(cmd_image)' > $(@D)/.$(@F).cmd
endef

BTOBJS := $(HEAD_Y) $(INIT_Y)
BTLIBS := $(CORE_Y) $(LIBS_Y) $(DRIVERS_Y) $(NET_Y)
LDFLAGS_image := -T arch/sparc/kernel/vmlinux.lds $(BTOBJS) \
                  --start-group $(BTLIBS) --end-group \
                  $(kallsyms.o) $(obj)/btfix.o

# Link the final image including btfixup'ed symbols.
# This is a replacement for the link done in the top-level Makefile.
# Note: No dependency on the prerequisite files since that would require
# make to try check if they are updated - and due to changes
# in gcc options (path for example) this would result in
# these files being recompiled for each build.
$(obj)/image: $(obj)/btfix.o FORCE
	$(call if_changed_rule,image)

$(obj)/tftpboot.img: $(obj)/piggyback $(obj)/System.map $(obj)/image FORCE
	$(call if_changed,elftoaout)
	$(call if_changed,piggy)

$(obj)/btfix.S: $(obj)/btfixupprep vmlinux FORCE
	$(call if_changed,btfix)
