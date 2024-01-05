# Check ABI for package against last release (if not same abinum)
abi-check-%: $(stampdir)/stamp-install-%
	@echo Debug: $@
	$(DROOT)/scripts/checks/abi-check "$*" \
		"$(prev_abidir)" "$(abidir)" $(do_skip_checks)

# Check the module list against the last release (always)
module-check-%: $(stampdir)/stamp-install-%
	@echo Debug: $@
	$(DROOT)/scripts/checks/module-check "$*" \
		"$(prev_abidir)" "$(abidir)" $(do_skip_checks)

# Check the signature of staging modules
module-signature-check-%: $(stampdir)/stamp-install-%
	@echo Debug: $@
	$(DROOT)/scripts/checks/module-signature-check "$*" \
		"$(DROOT)/$(mods_pkg_name)-$*" \
		"$(DROOT)/$(mods_extra_pkg_name)-$*" \
		$(do_skip_checks)

# Check the reptoline jmp/call functions against the last release.
retpoline-check-%: $(stampdir)/stamp-install-%
	@echo Debug: $@
	$(DROOT)/scripts/checks/retpoline-check "$*" \
		"$(prev_abidir)" "$(abidir)" $(do_skip_checks)

checks-%: module-check-% module-signature-check-% abi-check-% retpoline-check-%
	@echo Debug: $@

# Check the config against the known options list.
config-prepare-check-%: $(stampdir)/stamp-prepare-tree-%
	@echo Debug: $@
ifneq ($(do_skip_checks),true)
	python3 $(DROOT)/scripts/misc/annotations -f $(commonconfdir)/annotations \
		--arch $(arch) --flavour $* --check $(builddir)/build-$*/.config
endif
