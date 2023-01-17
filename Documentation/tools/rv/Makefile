# SPDX-License-Identifier: GPL-2.0-only

INSTALL		?= install
RM		?= rm -f
RMDIR		?= rmdir --ignore-fail-on-non-empty

PREFIX		?= /usr/share
MANDIR		?= $(PREFIX)/man
MAN1DIR		= $(MANDIR)/man1

MAN1_RST	= $(wildcard rv*.rst)

_DOC_MAN1	= $(patsubst %.rst,%.1,$(MAN1_RST))
DOC_MAN1	= $(addprefix $(OUTPUT),$(_DOC_MAN1))

RST2MAN_DEP	:= $(shell command -v rst2man 2>/dev/null)
RST2MAN_OPTS	+= --verbose

TEST_RST2MAN = $(shell sh -c "rst2man --version > /dev/null 2>&1 || echo n")

$(OUTPUT)%.1: %.rst
ifndef RST2MAN_DEP
	$(info ********************************************)
	$(info ** NOTICE: rst2man not found)
	$(info **)
	$(info ** Consider installing the latest rst2man from your)
	$(info ** distribution, e.g., 'dnf install python3-docutils' on Fedora,)
	$(info ** or from source:)
	$(info **)
	$(info **  https://docutils.sourceforge.io/docs/dev/repository.html )
	$(info **)
	$(info ********************************************)
	$(error NOTICE: rst2man required to generate man pages)
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
