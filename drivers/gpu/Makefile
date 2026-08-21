# SPDX-License-Identifier: GPL-2.0-only
# drm/tegra depends on host1x, so if both drivers are built-in care must be
# taken to initialize them in the correct order. Link order is the only way
# to ensure this currently.
# Similarly, buddy must come first since it is used by other drivers.
obj-$(CONFIG_GPU_BUDDY)	+= buddy.o
obj-y			+= host1x/ drm/ vga/ tests/
obj-$(CONFIG_IMX_IPUV3_CORE)	+= ipu-v3/
obj-$(CONFIG_TRACE_GPU_MEM)		+= trace/

# nova-core and nova-drm are built from this Makefile so nova-drm's dependency
# on nova-core can be expressed as a plain Make prerequisite rather than a
# recursive sub-make. This is a temporary workaround until the Rust build
# system supports cross-crate dependencies natively.

obj-$(CONFIG_NOVA_CORE) += nova-core.o
nova-core-y := nova-core/nova_core.o nova-core/nova_core_exports.o

obj-$(CONFIG_DRM_NOVA) += nova-drm.o
nova-drm-y := drm/nova/nova.o

# Export Rust symbols from nova-core only if nova-drm actually references them.
nova-core-export-deps := $(if $(CONFIG_DRM_NOVA),$(obj)/drm/nova/nova.o)

rust_needed_exports = \
	{ $(if $(strip $(2)),$(NM) -u $(2);,) echo "__DEFINED_RUST_SYMBOLS__"; \
	  $(NM) -p --defined-only $(1); } | \
	awk -v fmt='$(3)' ' \
		/^__DEFINED_RUST_SYMBOLS__$$/ { defs = 1; next } \
		!defs { if ($$NF ~ /^_R/) needed[$$NF] = 1; next } \
		defs && $$2 ~ /(T|R|D|B)/ && $$3 ~ /^_R/ && \
			$$3 !~ /_(init|cleanup)_module$$/ && \
			$$3 !~ /__(pfx|cfi|odr_asan)/ && \
			$$3 in needed { printf fmt, $$3 } \
	'

quiet_cmd_exports = EXPORTS $@
      cmd_exports = \
	$(call rust_needed_exports,$<,$(nova-core-export-deps),EXPORT_SYMBOL_RUST_GPL(%s);\n) > $@

$(obj)/nova-core/exports_nova_core_generated.h: $(obj)/nova-core/nova_core.o $(nova-core-export-deps) FORCE
	$(call if_changed,exports)

targets += nova-core/exports_nova_core_generated.h

$(obj)/nova-core/nova_core_exports.o: $(obj)/nova-core/exports_nova_core_generated.h
CFLAGS_nova-core/nova_core_exports.o := -I $(objtree)/$(obj)/nova-core

ifdef CONFIG_MODVERSIONS
# The C export shim declares Rust symbols as `extern int`, so reuse its export
# list but generate symbol CRCs from the Rust object instead of the shim's DWARF.
$(obj)/nova-core/nova_core_exports.o: private cmd_gensymtypes_c = \
	$(call getexportsymbols,\1) | \
	$(objtree)/scripts/gendwarfksyms/gendwarfksyms \
		$(if $(KBUILD_GENDWARFKSYMS_STABLE), --stable) \
		$(if $(KBUILD_SYMTYPES), --symtypes $(@:.o=.symtypes),) \
		$(obj)/nova-core/nova_core.o
endif

# Output nova-core's crate metadata for use by nova-drm at compile time.
RUSTFLAGS_nova-core/nova_core.o += \
	--emit=metadata=$(objtree)/$(obj)/nova-core/libnova_core.rmeta

# Allow nova-drm to import nova-core's types.
$(obj)/drm/nova/nova.o: $(obj)/nova-core/nova_core.o
RUSTFLAGS_drm/nova/nova.o := -L $(objtree)/$(obj)/nova-core --extern nova_core
