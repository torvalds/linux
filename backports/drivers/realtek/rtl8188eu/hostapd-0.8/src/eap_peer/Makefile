all:
	@echo Nothing to be made.

clean:
	rm -f *~ *.o *.so *.d

install:
	if ls *.so >/dev/null 2>&1; then \
		install -d $(DESTDIR)$(LIBDIR)/wpa_supplicant && \
		cp *.so $(DESTDIR)$(LIBDIR)/wpa_supplicant \
	; fi
