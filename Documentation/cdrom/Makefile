LATEXFILE = cdrom-standard

all:
	make clean
	latex $(LATEXFILE)
	latex $(LATEXFILE)
	@if [ -x `which gv` ]; then \
		`dvips -q -t letter -o $(LATEXFILE).ps $(LATEXFILE).dvi` ;\
		`gv -antialias -media letter -nocenter $(LATEXFILE).ps` ;\
	else \
		`xdvi $(LATEXFILE).dvi &` ;\
	fi
	make sortofclean

clean:
	rm -f $(LATEXFILE).ps $(LATEXFILE).dvi $(LATEXFILE).aux $(LATEXFILE).log 

sortofclean:
	rm -f $(LATEXFILE).aux $(LATEXFILE).log 


