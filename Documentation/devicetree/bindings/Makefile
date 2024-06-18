# SPDX-License-Identifier: GPL-2.0
DT_DOC_CHECKER ?= dt-doc-validate
DT_EXTRACT_EX ?= dt-extract-example
DT_MK_SCHEMA ?= dt-mk-schema

DT_SCHEMA_LINT = $(shell which yamllint || \
  echo "warning: python package 'yamllint' not installed, skipping" >&2)

DT_SCHEMA_MIN_VERSION = 2023.9

PHONY += check_dtschema_version
check_dtschema_version:
	@which $(DT_DOC_CHECKER) >/dev/null || \
		{ echo "Error: '$(DT_DOC_CHECKER)' not found!" >&2; \
		  echo "Ensure dtschema python package is installed and in your PATH." >&2; \
		  echo "Current PATH is:" >&2; \
		  echo "$$PATH" >&2; false; }
	@{ echo $(DT_SCHEMA_MIN_VERSION); \
	$(DT_DOC_CHECKER) --version 2>/dev/null || echo 0; } | sort -Vc >/dev/null || \
	{ echo "ERROR: dtschema minimum version is v$(DT_SCHEMA_MIN_VERSION)" >&2; false; }

quiet_cmd_extract_ex = DTEX    $@
      cmd_extract_ex = $(DT_EXTRACT_EX) $< > $@

$(obj)/%.example.dts: $(src)/%.yaml check_dtschema_version FORCE
	$(call if_changed,extract_ex)

find_all_cmd = find $(src) \( -name '*.yaml' ! \
		-name 'processed-schema*' \)

find_cmd = $(find_all_cmd) | \
		sed 's|^$(srctree)/||' | \
		grep -F -e "$(subst :," -e ",$(DT_SCHEMA_FILES))" | \
		sed 's|^|$(srctree)/|'
CHK_DT_EXAMPLES := $(patsubst $(srctree)/%.yaml,%.example.dtb, $(shell $(find_cmd)))

quiet_cmd_yamllint = LINT    $(src)
      cmd_yamllint = ($(find_cmd) | \
                     xargs -n200 -P$$(nproc) \
		     $(DT_SCHEMA_LINT) -f parsable -c $(src)/.yamllint >&2) \
		     && touch $@ || true

quiet_cmd_chk_bindings = CHKDT   $(src)
      cmd_chk_bindings = ($(find_cmd) | \
			  xargs -n200 -P$$(nproc) $(DT_DOC_CHECKER) -u $(src)) \
			  && touch $@ || true

quiet_cmd_mk_schema = SCHEMA  $@
      cmd_mk_schema = f=$$(mktemp) ; \
                      $(find_all_cmd) > $$f ; \
                      $(DT_MK_SCHEMA) -j $(DT_MK_SCHEMA_FLAGS) @$$f > $@ ; \
		      rm -f $$f

DT_DOCS = $(patsubst $(srctree)/%,%,$(shell $(find_all_cmd)))

override DTC_FLAGS := \
	-Wno-avoid_unnecessary_addr_size \
	-Wno-graph_child_address \
	-Wno-interrupt_provider \
	-Wno-unique_unit_address \
	-Wunique_unit_address_if_enabled

$(obj)/processed-schema.json: $(DT_DOCS) check_dtschema_version FORCE
	$(call if_changed,mk_schema)

targets += .dt-binding.checked .yamllint.checked
$(obj)/.yamllint.checked: $(DT_DOCS) $(src)/.yamllint FORCE
	$(if $(DT_SCHEMA_LINT),$(call if_changed,yamllint),)

$(obj)/.dt-binding.checked: $(DT_DOCS) FORCE
	$(call if_changed,chk_bindings)

always-y += processed-schema.json
targets += $(patsubst $(obj)/%,%, $(CHK_DT_EXAMPLES))
targets += $(patsubst $(obj)/%.dtb,%.dts, $(CHK_DT_EXAMPLES))

# Hack: avoid 'Argument list too long' error for 'make clean'. Remove most of
# build artifacts here before they are processed by scripts/Makefile.clean
clean-files = $(shell find $(obj) \( -name '*.example.dts' -o \
			-name '*.example.dtb' \) -delete 2>/dev/null)

dt_compatible_check: $(obj)/processed-schema.json
	$(Q)$(srctree)/scripts/dtc/dt-extract-compatibles $(srctree) | xargs dt-check-compatible -v -s $<

PHONY += dt_binding_check
dt_binding_check: $(obj)/.dt-binding.checked $(obj)/.yamllint.checked $(CHK_DT_EXAMPLES)
