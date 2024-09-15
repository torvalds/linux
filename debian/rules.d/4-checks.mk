# Check the signature of staging modules
module-signature-check-%: $(stampdir)/stamp-install-%
	@echo Debug: $@
	$(DROOT)/scripts/checks/module-signature-check "$*" \
		"$(DROOT)/$(mods_pkg_name)-$*" \
		"$(DROOT)/$(mods_extra_pkg_name)-$*" \
		$(do_skip_checks)

checks-%: module-signature-check-%
	@echo Debug: $@

# Check the config against the known options list.
config-prepare-check-%: $(stampdir)/stamp-prepare-tree-%
	@echo Debug: $@
ifneq ($(do_skip_checks),true)
	python3 $(DROOT)/scripts/misc/annotations -f $(commonconfdir)/annotations \
		--arch $(arch) --flavour $* --check $(builddir)/build-$*/.config
endif
