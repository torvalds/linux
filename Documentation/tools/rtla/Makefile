# SPDX-License-Identifier: GPL-2.0-only
# Based on bpftool's Documentation Makefile

INSTALL		?= install
RM		?= rm -f
RMDIR		?= rmdir --ignore-fail-on-non-empty

PREFIX		?= /usr/share
MANDIR		?= $(PREFIX)/man
MAN1DIR		= $(MANDIR)/man1

MAN1_RST	= $(wildcard rtla*.rst)

_DOC_MAN1	= $(patsubst %.rst,%.1,$(MAN1_RST))
DOC_MAN1	= $(addprefix $(OUTPUT),$(_DOC_MAN1))

RST2MAN_DEP	:= $(shell command -v rst2man 2>/dev/null)
RST2MAN_OPTS	+= --verbose

$(OUTPUT)%.1: %.rst
ifndef RST2MAN_DEP
	$(error "rst2man not found, but required to generate man pages")
endif
	rst2man $(RST2MAN_OPTS) $< > $@

man1: $(DOC_MAN1)
man: man1

clean:
	$(RM) $(DOC_MAN1)

install: man
	$(INSTALL) -d -m 755 $(DESTDIR)$(MAN1DIR)
	$(INSTALL) -m 644 $(DOC_MAN1) $(DESTDIR)$(MAN1DIR)

uninstall:
	$(RM) $(addprefix $(DESTDIR)$(MAN1DIR)/,$(_DOC_MAN1))
	$(RMDIR) $(DESTDIR)$(MAN1DIR)

.PHONY: man man1 clean install uninstall
.DEFAULT_GOAL := man
