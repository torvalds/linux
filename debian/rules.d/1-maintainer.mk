# The following targets are for the maintainer only! do not run if you don't
# know what they do.

.PHONY: help
help:
	@echo "These are the targets in addition to the normal $(DEBIAN) ones:"
	@echo
	@echo "  printenv            : Print some variables used in the build"
	@echo "  updateconfigs       : Update core arch configs"
	@echo "  defaultconfigs      : Update core arch configs using defaults"
	@echo "  genconfigs          : Generate core arch configs in CONFIGS/*"
	@echo "  editconfigs         : Edit core arch configs"
	@echo "  printchanges        : Print the current changelog entries (from git)"
	@echo "  insertchanges       : Insert current changelog entries (from git)"
	@echo "  compileselftests    : Only compile the selftests listed on ubuntu_selftests variable"
	@echo "  runselftests        : Run the selftests listed on ubuntu_selftests variable"
	@echo
	@echo "Environment variables:"
	@echo
	@echo "  CONCURRENCY_LEVEL=X : Use -jX for kernel compile"

.PHONY: printdebian
printdebian:
	@echo "$(DEBIAN)"

configs-targets := updateconfigs defaultconfigs genconfigs editconfigs

.PHONY: $(configs-targets)
$(configs-targets):
	dh_testdir
	kmake='$(kmake)' skip_checks=$(do_skip_checks) conc_level=$(conc_level) \
		$(SHELL) $(DROOT)/scripts/misc/kernelconfig $@

.PHONY: printenv
printenv:
	@dh_testdir
	@echo "src_pkg_name              = $(src_pkg_name)"
	@echo "series                    = $(series)"
	@echo "release                   = $(release)"
	@echo "revision                  = $(revision)"
	@echo "uploadnum                 = $(uploadnum)"
	@echo "prev_revision             = $(prev_revision)"
	@echo "abinum                    = $(abinum)"
	@echo "upstream_tag              = $(upstream_tag)"
	@echo "flavours                  = $(flavours)"
	@echo "bin_pkg_name              = $(bin_pkg_name)"
	@echo "hdr_pkg_name              = $(hdrs_pkg_name)"
	@echo "rust_pkg_name             = $(rust_pkg_name)"
	@echo "ubuntu_selftests          = $(ubuntu_selftests)"
	@echo "arch                      = $(arch)"
	@echo "kmake                     = $(kmake)"
	@echo
	@echo "CONCURRENCY_LEVEL         = $(CONCURRENCY_LEVEL)"
	@echo "DEB_HOST_GNU_TYPE         = $(DEB_HOST_GNU_TYPE)"
	@echo "DEB_BUILD_GNU_TYPE        = $(DEB_BUILD_GNU_TYPE)"
	@echo "DEB_HOST_ARCH             = $(DEB_HOST_ARCH)"
	@echo "DEB_BUILD_ARCH            = $(DEB_BUILD_ARCH)"
	@echo
	@echo "any_signed                = $(any_signed)"
	@echo " uefi_signed              = $(uefi_signed)"
	@echo " opal_signed              = $(opal_signed)"
	@echo " sipl_signed              = $(sipl_signed)"
	@echo
	@echo "do_skip_checks            = $(do_skip_checks)"
	@echo "do_full_build             = $(do_full_build)"
	@echo "do_mainline_build         = $(do_mainline_build)"
	@echo "do_dbgsym_package         = $(do_dbgsym_package)"
	@echo "do_dtbs                   = $(do_dtbs)"
	@echo "do_source_package         = $(do_source_package)"
	@echo "do_source_package_content = $(do_source_package_content)"
	@echo "do_extras_package         = $(do_extras_package)"
	@echo "do_flavour_image_package  = $(do_flavour_image_package)"
	@echo "do_flavour_header_package = $(do_flavour_header_package)"
	@echo "do_common_headers_indep   = $(do_common_headers_indep)"
	@echo "do_lib_rust               = $(do_lib_rust)"
	@echo "do_tools                  = $(do_tools)"
	@echo "do_tools_common           = $(do_tools_common)"
	@echo "do_any_tools              = $(do_any_tools)"
	@echo "do_linux_tools            = $(do_linux_tools)"
	@echo " do_tools_acpidbg         = $(do_tools_acpidbg)"
	@echo " do_tools_bpftool         = $(do_tools_bpftool)"
	@echo " do_tools_cpupower        = $(do_tools_cpupower)"
	@echo " do_tools_host            = $(do_tools_host)"
	@echo " do_tools_perf            = $(do_tools_perf)"
	@echo " do_tools_perf_jvmti      = $(do_tools_perf_jvmti)"
	@echo " do_tools_usbip           = $(do_tools_usbip)"
	@echo " do_tools_x86             = $(do_tools_x86)"
	@echo "do_cloud_tools            = $(do_cloud_tools)"
	@echo " do_tools_hyperv          = $(do_tools_hyperv)"
	@echo
	@echo "all_dkms_modules          = $(all_dkms_modules)"
	@$(foreach mod,$(all_dkms_modules),$(foreach var,$(do_$(mod)),\
		printf " %-24s = %s\n" "do_$(mod)" "$(var)";))

.PHONY: printchanges
printchanges:
	@baseCommit=$$(git log --pretty=format:'%H %s' | \
		gawk '/UBUNTU: '".*Ubuntu-.*`echo $(prev_fullver) | sed 's/+/\\\\+/'`"'(~.*)?$$/ { print $$1; exit }'); \
	if [ -z "$$baseCommit" ]; then \
		echo "WARNING: couldn't find a commit for the previous version. Using the lastest one." >&2; \
		baseCommit=$$(git log --pretty=format:'%H %s' | \
			gawk '/UBUNTU:\s*Ubuntu-.*$$/ { print $$1; exit }'); \
	fi; \
	git log "$$baseCommit"..HEAD | \
	$(DROOT)/scripts/misc/git-ubuntu-log

.PHONY: insertchanges
insertchanges: autoreconstruct finalchecks
	$(DROOT)/scripts/misc/insert-changes $(DROOT) $(DEBIAN)

.PHONY: autoreconstruct
autoreconstruct:
	# No need for reconstruct for -rc kernels since we don't upload an
	# orig tarball, so just remove it.
	if grep -q "^EXTRAVERSION = -rc[0-9]\+$$" Makefile; then \
		echo "exit 0" >$(DEBIAN)/reconstruct; \
	else \
		$(DROOT)/scripts/misc/gen-auto-reconstruct $(upstream_tag) $(DEBIAN)/reconstruct $(DROOT)/source/options; \
	fi

.PHONY: finalchecks
finalchecks: debian/control
	$(DROOT)/scripts/checks/final-checks "$(DEBIAN)" "$(prev_fullver)" $(do_skip_checks)

.PHONY: compileselftests
compileselftests:
	# a loop is needed here to fail on errors
	for test in $(ubuntu_selftests); do \
		$(kmake) -C tools/testing/selftests TARGETS="$$test"; \
	done;

.PHONY: runselftests
runselftests:
	$(kmake) -C tools/testing/selftests TARGETS="$(ubuntu_selftests)" run_tests
