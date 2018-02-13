# Check ABI for package against last release (if not same abinum)
abi-check-%: install-%
	@echo Debug: $@
	@perl -f $(DROOT)/scripts/abi-check "$*" "$(prev_abinum)" "$(abinum)" \
		"$(prev_abidir)" "$(abidir)" "$(skipabi)"

# Check the module list against the last release (always)
module-check-%: install-%
	@echo Debug: $@
	@perl -f $(DROOT)/scripts/module-check "$*" \
		"$(prev_abidir)" "$(abidir)" $(skipmodule)

# Check the reptoline jmp/call functions against the last release.
retpoline-check-%: install-%
	@echo Debug: $@
	$(SHELL) $(DROOT)/scripts/retpoline-check "$*" \
		"$(prev_abidir)" "$(abidir)" "$(skipretpoline)" "$(builddir)/build-$*"

checks-%: module-check-% abi-check-% retpoline-check-%
	@echo Debug: $@

# Check the config against the known options list.
config-prepare-check-%: $(stampdir)/stamp-prepare-tree-%
	@echo Debug: $@
	@perl -f $(DROOT)/scripts/config-check \
		$(builddir)/build-$*/.config "$(arch)" "$*" "$(commonconfdir)" "$(skipconfig)"

