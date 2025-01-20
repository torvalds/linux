.PHONY: build-indep
build-indep:
	@echo Debug: $@

# The binary-indep dependency chain is:
#
# install-headers <- install-source <- install-tools <- install-indep <- binary-indep
# install-headers <- binary-headers
#
indep_hdrpkg = $(indep_hdrs_pkg_name)
indep_hdrdir = $(CURDIR)/debian/$(indep_hdrpkg)/usr/src/$(indep_hdrpkg)

$(stampdir)/stamp-install-headers: $(stampdir)/stamp-prepare-indep
	@echo Debug: $@
	dh_testdir

ifeq ($(do_flavour_header_package),true)
	install -d $(indep_hdrdir)
	find . -path './debian' -prune -o -path './$(DEBIAN)' -prune \
	  -o -path './include/*' -prune \
	  -o -path './scripts/*' -prune -o -type f \
	  \( -name 'Makefile*' -o -name 'Kconfig*' -o -name 'Kbuild*' -o \
	     -name '*.sh' -o -name '*.pl' -o -name '*.lds' \) \
	  -print | cpio -pd --preserve-modification-time $(indep_hdrdir)
	cp -a scripts include $(indep_hdrdir)
	(find arch -name include -type d -print | \
		xargs -n1 -i: find : -type f) | \
		cpio -pd --preserve-modification-time $(indep_hdrdir)
	# Do not ship .o and .cmd artifacts in headers
	find $(indep_hdrdir) -name \*.o -or -name \*.cmd -exec rm -f {} \;
endif
	$(stamp)

srcpkg = linux-source-$(DEB_VERSION_UPSTREAM)
srcdir = $(CURDIR)/debian/$(srcpkg)/usr/src/$(srcpkg)
balldir = $(CURDIR)/debian/$(srcpkg)/usr/src/$(srcpkg)/$(srcpkg)
install-source: $(stampdir)/stamp-prepare-indep
	@echo Debug: $@
ifeq ($(do_source_package),true)

	install -d $(srcdir)
ifeq ($(do_source_package_content),true)
	find . -path './debian' -prune -o -path './$(DEBIAN)' -prune -o \
		-path './.*' -prune -o -print | \
		cpio -pd --preserve-modification-time $(balldir)
	(cd $(srcdir); tar cf - $(srcpkg)) | bzip2 -9c > \
		$(srcdir)/$(srcpkg).tar.bz2
	rm -rf $(balldir)
	$(LN) $(srcpkg)/$(srcpkg).tar.bz2 $(srcdir)/..
endif
endif

.PHONY: install-tools
install-tools: toolspkg = $(tools_common_pkg_name)
install-tools: toolsbin = $(CURDIR)/debian/$(toolspkg)/usr/bin
install-tools: toolssbin = $(CURDIR)/debian/$(toolspkg)/usr/sbin
install-tools: toolsman = $(CURDIR)/debian/$(toolspkg)/usr/share/man
install-tools: toolspython = $(CURDIR)/debian/$(toolspkg)/usr/lib/python3/dist-packages
install-tools: toolsbashcomp = $(CURDIR)/debian/$(toolspkg)/usr/share/bash-completion/completions
install-tools: hosttoolspkg = $(hosttools_pkg_name)
install-tools: hosttoolsbin = $(CURDIR)/debian/$(hosttoolspkg)/usr/bin
install-tools: hosttoolsman = $(CURDIR)/debian/$(hosttoolspkg)/usr/share/man
install-tools: hosttoolssystemd = $(CURDIR)/debian/$(hosttoolspkg)/lib/systemd/system
install-tools: cloudpkg = $(cloud_common_pkg_name)
install-tools: cloudbin = $(CURDIR)/debian/$(cloudpkg)/usr/bin
install-tools: cloudsbin = $(CURDIR)/debian/$(cloudpkg)/usr/sbin
install-tools: cloudman = $(CURDIR)/debian/$(cloudpkg)/usr/share/man
install-tools: $(stampdir)/stamp-prepare-indep $(stampdir)/stamp-build-perarch
	@echo Debug: $@

ifeq ($(do_tools_common),true)
	rm -rf $(builddir)/tools
	install -d $(builddir)/tools
	for i in *; do $(LN) $(CURDIR)/$$i $(builddir)/tools/; done
	rm $(builddir)/tools/tools
	rsync -a tools/ $(builddir)/tools/tools/

	install -d $(toolsbin)
	install -d $(toolssbin)
	install -d $(toolsman)/man1
	install -d $(toolsman)/man8
	install -d $(toolsbashcomp)
	install -d $(toolspython)

	install -m755 debian/tools/generic $(toolsbin)/usbip
	install -m755 debian/tools/generic $(toolsbin)/usbipd
	install -m644 $(CURDIR)/tools/usb/usbip/doc/*.8 $(toolsman)/man8/

	install -m755 debian/tools/generic $(toolsbin)/cpupower
	install -m644 $(CURDIR)/tools/power/cpupower/man/*.1 $(toolsman)/man1/

	install -m755 debian/tools/generic $(toolsbin)/rtla

	install -m755 debian/tools/generic $(toolsbin)/perf

	install -m755 debian/tools/generic $(toolssbin)/bpftool
	make -C $(builddir)/tools/tools/bpf/bpftool doc
	install -m644 $(builddir)/tools/tools/bpf/bpftool/Documentation/*.8 \
		$(toolsman)/man8
	install -m644 $(builddir)/tools/tools/bpf/bpftool/bash-completion/bpftool \
		$(toolsbashcomp)

	install -m755 debian/tools/generic $(toolsbin)/x86_energy_perf_policy
	install -m755 debian/tools/generic $(toolsbin)/turbostat

	cd $(builddir)/tools/tools/perf && make man
	install -m644 $(builddir)/tools/tools/perf/Documentation/*.1 \
		$(toolsman)/man1

	install -m644 $(CURDIR)/tools/power/x86/x86_energy_perf_policy/*.8 $(toolsman)/man8
	install -m644 $(CURDIR)/tools/power/x86/turbostat/*.8 $(toolsman)/man8

ifeq ($(do_tools_perf_python),true)
	# Python wrapper module for python-perf
	install -d $(toolspython)/perf
	install -m644 debian/tools/python-perf.py $(toolspython)/perf/__init__.py
endif
ifeq ($(do_cloud_tools),true)
ifeq ($(do_tools_hyperv),true)
	install -d $(cloudsbin)
	install -m755 debian/tools/generic $(cloudsbin)/hv_kvp_daemon
	install -m755 debian/tools/generic $(cloudsbin)/hv_vss_daemon
ifneq ($(build_arch),arm64)
	install -m755 debian/tools/generic $(cloudsbin)/hv_fcopy_uio_daemon
endif
	install -m755 debian/tools/generic $(cloudsbin)/lsvmbus
	install -m755 debian/cloud-tools/hv_get_dhcp_info $(cloudsbin)
	install -m755 debian/cloud-tools/hv_get_dns_info $(cloudsbin)
	install -m755 debian/cloud-tools/hv_set_ifconfig $(cloudsbin)

	install -d $(cloudman)/man8
	install -m644 $(CURDIR)/tools/hv/*.8 $(cloudman)/man8
endif
endif

ifeq ($(do_tools_acpidbg),true)
	install -m755 debian/tools/generic $(toolsbin)/acpidbg
endif

endif

ifeq ($(do_tools_host),true)
	install -d $(hosttoolsbin)
	install -d $(hosttoolsman)/man1
	install -d $(hosttoolssystemd)

	install -m 755 $(CURDIR)/tools/kvm/kvm_stat/kvm_stat $(hosttoolsbin)/
	install -m 644 $(CURDIR)/tools/kvm/kvm_stat/kvm_stat.service \
		$(hosttoolssystemd)/

	cd $(builddir)/tools/tools/kvm/kvm_stat && make man
	install -m644 $(builddir)/tools/tools/kvm/kvm_stat/*.1 \
		$(hosttoolsman)/man1
endif

$(stampdir)/stamp-prepare-indep:
	@echo Debug: $@
	dh_prep -i
	$(stamp)

.PHONY: install-indep
install-indep: $(stampdir)/stamp-install-headers install-source install-tools
	@echo Debug: $@

# This is just to make it easy to call manually. Normally done in
# binary-indep target during builds.
.PHONY: binary-headers
binary-headers: $(stampdir)/stamp-prepare-indep $(stampdir)/stamp-install-headers
	@echo Debug: $@
	dh_installchangelogs -p$(indep_hdrpkg)
	dh_installdocs -p$(indep_hdrpkg)
	dh_compress -p$(indep_hdrpkg)
	dh_fixperms -p$(indep_hdrpkg)
	dh_installdeb -p$(indep_hdrpkg)
	$(lockme) dh_gencontrol -p$(indep_hdrpkg)
	dh_md5sums -p$(indep_hdrpkg)
	dh_builddeb -p$(indep_hdrpkg)

binary-indep: cloudpkg = $(cloud_common_pkg_name)
binary-indep: hosttoolspkg = $(hosttools_pkg_name)
binary-indep: install-indep
	@echo Debug: $@
	dh_installchangelogs -i
	dh_installdocs -i
	dh_compress -i
	dh_fixperms -i
ifeq ($(do_tools_common),true)
ifeq ($(do_cloud_tools),true)
ifeq ($(do_tools_hyperv),true)
	dh_installinit -p$(cloudpkg) -n --name hv-kvp-daemon
	dh_installinit -p$(cloudpkg) -n --name hv-vss-daemon
ifneq ($(build_arch),arm64)
	dh_installinit -p$(cloudpkg) -n --name hv-fcopy-daemon
endif
	dh_installudev -p$(cloudpkg) -n --name hv-kvp-daemon
	dh_installudev -p$(cloudpkg) -n --name hv-vss-daemon
ifneq ($(build_arch),arm64)
	dh_installudev -p$(cloudpkg) -n --name hv-fcopy-daemon
endif
	dh_systemd_enable -p$(cloudpkg)
	dh_installinit -p$(cloudpkg) -o --name hv-kvp-daemon
	dh_installinit -p$(cloudpkg) -o --name hv-vss-daemon
ifneq ($(build_arch),arm64)
	dh_installinit -p$(cloudpkg) -o --name hv-fcopy-daemon
endif
	dh_systemd_start -p$(cloudpkg)
endif
	# Keep intel_sgx service disabled by default, so add it after dh_systemd_enable
	# and dh_systemd_start are called:
	dh_installinit -p$(cloudpkg) --no-start --no-enable --name intel-sgx-load-module
endif
endif
ifeq ($(do_tools_host),true)
	# Keep kvm_stat.service disabled by default (after dh_systemd_enable
	# and dh_systemd_start:
	dh_installinit -p$(hosttoolspkg) --no-enable --no-start --name kvm_stat
endif
	dh_installdeb -i
	$(lockme) dh_gencontrol -i
	dh_md5sums -i
	dh_builddeb -i
