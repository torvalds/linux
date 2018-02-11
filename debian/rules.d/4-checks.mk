# Check ABI for package against last release (if not same abinum)
abi-check-%: $(stampdir)/stamp-build-%
	@echo Debug: $@
	install -d $(abidir)
	sed -e 's/^\(.\+\)[[:space:]]\+\(.\+\)[[:space:]]\(.\+\)$$/\3 \2 \1/'	\
		$(builddir)/build-$*/Module.symvers | sort > $(abidir)/$*
	@perl -f $(DROOT)/scripts/abi-check "$*" "$(prev_abinum)" "$(abinum)" \
		"$(prev_abidir)" "$(abidir)" "$(skipabi)"

# Check the module list against the last release (always)
module-check-%: $(stampdir)/stamp-build-%
	@echo Debug: $@
	install -d $(abidir)
	find $(builddir)/build-$*/ -name \*.ko | \
		sed -e 's/.*\/\([^\/]*\)\.ko/\1/' | sort > $(abidir)/$*.modules
	@perl -f $(DROOT)/scripts/module-check "$*" \
		"$(prev_abidir)" "$(abidir)" $(skipmodule)

# Check the reptoline jmp/call functions against the last release.
retpoline-check-%: $(stampdir)/stamp-build-%
	@echo Debug: $@
	install -d $(abidir)
	if grep -q CONFIG_RETPOLINE=y $(builddir)/build-$*/.config; then \
		$(SHELL) $(DROOT)/scripts/retpoline-extract $(builddir)/build-$* | \
			sort >$(abidir)/$*.retpoline; \
	else \
		echo "# RETPOLINE NOT ENABLED" >$(abidir)/$*.retpoline; \
	fi
	$(SHELL) $(DROOT)/scripts/retpoline-check "$*" \
		"$(prev_abidir)" "$(abidir)" "$(skipretpoline)"

checks-%: module-check-% abi-check-% retpoline-check-%
	@echo Debug: $@

# Check the config against the known options list.
config-prepare-check-%: $(stampdir)/stamp-prepare-tree-%
	@echo Debug: $@
	@perl -f $(DROOT)/scripts/config-check \
		$(builddir)/build-$*/.config "$(arch)" "$*" "$(commonconfdir)" "$(skipconfig)"

