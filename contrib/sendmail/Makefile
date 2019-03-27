#       $Id: Makefile.dist,v 8.15 2001-08-23 20:44:39 ca Exp $

SHELL= /bin/sh
SUBDIRS= libsm libsmutil libsmdb sendmail editmap mail.local \
	 mailstats makemap praliases rmail smrsh vacation
# libmilter: requires pthread
BUILD=   ./Build
OPTIONS= $(CONFIG) $(FLAGS)

all: FRC
	@for x in $(SUBDIRS); \
	do \
		(cd $$x; echo Making $@ in:; pwd; \
		$(SHELL) $(BUILD) $(OPTIONS)); \
	done

clean: FRC
	@for x in $(SUBDIRS); \
	do \
		(cd $$x; echo Making $@ in:; pwd; \
		$(SHELL) $(BUILD) $(OPTIONS) $@); \
	done

install: FRC
	@for x in $(SUBDIRS); \
	do \
		(cd $$x; echo Making $@ in:; pwd; \
		$(SHELL) $(BUILD) $(OPTIONS) $@); \
	done

install-docs: FRC
	@for x in $(SUBDIRS); \
	do \
		(cd $$x; echo Making $@ in:; pwd; \
		$(SHELL) $(BUILD) $(OPTIONS) $@); \
	done

fresh: FRC
	@for x in $(SUBDIRS); \
	do \
		(cd $$x; echo Making $@ in:; pwd; \
		$(SHELL) $(BUILD) $(OPTIONS) -c); \
	done

$(SUBDIRS): FRC
	@cd $@; pwd; \
	$(SHELL) $(BUILD) $(OPTIONS)

FRC:
