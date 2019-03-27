YACC=yacc -v

DEST=.

all: $(DEST)/ipf_y.c $(DEST)/ipf_y.h $(DEST)/ipf_l.c \
	$(DEST)/ipmon_y.c $(DEST)/ipmon_y.h $(DEST)/ipmon_l.c \
	$(DEST)/ipnat_y.c $(DEST)/ipnat_y.h $(DEST)/ipnat_l.c \
	$(DEST)/ipscan_y.c $(DEST)/ipscan_y.h $(DEST)/ipscan_l.c \
	$(DEST)/ippool_y.c $(DEST)/ippool_y.h $(DEST)/ippool_l.c \
	$(DEST)/ipf_l.h $(DEST)/ipnat_l.h $(DEST)/ipscan_l.h \
	$(DEST)/ippool_l.h $(DEST)/ipmon_l.h

$(DEST)/ipf_y.h: $(DEST)/ipf_y.c

$(DEST)/ipf_y.c: ipf_y.y
	$(YACC) -d ipf_y.y
	sed -e 's/yy/ipf_yy/g' -e 's/y.tab.h/ipf_y.c/' \
	    -e 's/"ipf_y.y"/"..\/tools\/ipf_y.y"/' \
	    y.tab.c > $(DEST)/ipf_y.c
	sed -e 's/yy/ipf_yy/g' -e 's/y.tab.h/ipf_y.h/' y.tab.h > $(DEST)/ipf_y.h
	/bin/rm -f y.tab.c y.tab.h

$(DEST)/ipf_l.c: lexer.c
	sed -e 's/yy/ipf_yy/g' -e 's/y.tab.h/ipf_y.h/' \
	    -e 's/lexer.h/ipf_l.h/' lexer.c > $@

$(DEST)/ipmon_y.n: $(DEST)/ipmon_y.c

$(DEST)/ipmon_y.c $(DEST)/ipmon_y.h: ipmon_y.y
	$(YACC) -d ipmon_y.y
	sed -e 's/yy/ipmon_yy/g' -e 's/"ipmon_y.y"/"..\/tools\/ipmon_y.y"/' \
	    y.tab.c > $(DEST)/ipmon_y.c
	sed -e 's/yy/ipmon_yy/g' y.tab.h > $(DEST)/ipmon_y.h
	/bin/rm -f y.tab.c y.tab.h

$(DEST)/ipmon_l.c: lexer.c
	sed -e 's/yy/ipmon_yy/g' -e 's/y.tab.h/ipmon_y.h/' \
	    -e 's/lexer.h/ipmon_l.h/' lexer.c > $@

$(DEST)/ipscan_y.h: $(DEST)/ipscan_y.c

$(DEST)/ipscan_y.c $(DEST)/ipscan_y.h: ipscan_y.y
	$(YACC) -d ipscan_y.y
	sed -e 's/yy/ipscan_yy/g' \
	    -e 's/"ipscan_y.y"/"..\/tools\/ipscan_y.y"/' \
	    y.tab.c > $(DEST)/ipscan_y.c
	sed -e 's/yy/ipscan_yy/g' y.tab.h > $(DEST)/ipscan_y.h
	/bin/rm -f y.tab.c y.tab.h

$(DEST)/ipscan_l.c: lexer.c
	sed -e 's/yy/ipscan_yy/g' -e 's/y.tab.h/ipscan_y.h/' \
	    -e 's/lexer.h/ipscan_l.h/' lexer.c > $@

$(DEST)/ippool_y.h: $(DEST)/ippool_y.c

$(DEST)/ippool_y.c $(DEST)/ippool_y.h: ippool_y.y
	$(YACC) -d ippool_y.y
	sed -e 's/yy/ippool_yy/g' -e 's/"ippool_y.y"/"..\/tools\/ippool_y.y"/' \
	    y.tab.c > $(DEST)/ippool_y.c
	sed -e 's/yy/ippool_yy/g' y.tab.h > $(DEST)/ippool_y.h
	/bin/rm -f y.tab.c y.tab.h

$(DEST)/ippool_l.c: lexer.c
	sed -e 's/yy/ippool_yy/g' -e 's/y.tab.h/ippool_y.h/' \
	    -e 's/lexer.h/ippool_l.h/' lexer.c > $@

$(DEST)/ipnat_y.h: $(DEST)/ipnat_y.c

$(DEST)/ipnat_y.c $(DEST)/ipnat_y.h: ipnat_y.y
	$(YACC) -d ipnat_y.y
	sed -e 's/yy/ipnat_yy/g' -e 's/y.tab.c/ipnat_y.c/' \
	    -e s/\"ipnat_y.y\"/\"..\\/tools\\/ipnat_y.y\"/ \
	    y.tab.c > $(DEST)/ipnat_y.c
	sed -e 's/yy/ipnat_yy/g' -e 's/y.tab.h/ipnat_y.h/' \
	    y.tab.h > $(DEST)/ipnat_y.h
	/bin/rm -f y.tab.c y.tab.h

$(DEST)/ipnat_l.c: lexer.c
	sed -e 's/yy/ipnat_yy/g' -e 's/y.tab.h/ipnat_y.h/' \
	    -e 's/lexer.h/ipnat_l.h/' lexer.c > $@

$(DEST)/ipf_l.h: lexer.h
	sed -e 's/yy/ipf_yy/g' lexer.h > $@

$(DEST)/ipmon_l.h: lexer.h
	sed -e 's/yy/ipmon_yy/g' lexer.h > $@

$(DEST)/ipscan_l.h: lexer.h
	sed -e 's/yy/ipscan_yy/g' lexer.h > $@

$(DEST)/ippool_l.h: lexer.h
	sed -e 's/yy/ippool_yy/g' lexer.h > $@

$(DEST)/ipnat_l.h: lexer.h
	sed -e 's/yy/ipnat_yy/g' lexer.h > $@

clean:
	/bin/rm -f $(DEST)/ipf_y.c $(DEST)/ipf_y.h $(DEST)/ipf_l.c
	/bin/rm -f $(DEST)/ipmon_y.c $(DEST)/ipmon_y.h $(DEST)/ipmon_l.c
	/bin/rm -f $(DEST)/ipscan_y.c $(DEST)/ipscan_y.h $(DEST)/ipscan_l.c
	/bin/rm -f $(DEST)/ippool_y.c $(DEST)/ippool_y.h $(DEST)/ippool_l.c
	/bin/rm -f $(DEST)/ipnat_y.c $(DEST)/ipnat_y.h $(DEST)/ipnat_l.c
	/bin/rm -f $(DEST)/ipf_l.h $(DEST)/ipmon_l.h $(DEST)/ippool_l.h
	/bin/rm -f $(DEST)/ipscan_l.h $(DEST)/ipnat_l.h
