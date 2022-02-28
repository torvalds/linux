# SPDX-License-Identifier: GPL-2.0
DT_DOC_CHECKER ?= dt-doc-validate
DT_EXTRACT_EX ?= dt-extract-example
DT_MK_SCHEMA ?= dt-mk-schema

DT_SCHEMA_LINT = $(shell which yamllint)

DT_SCHEMA_MIN_VERSION = 2021.2.1

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

# Use full schemas when checking %.example.dts
DT_TMP_SCHEMA := $(obj)/processed-schema-examples.json

find_all_cmd = find $(srctree)/$(src) \( -name '*.yaml' ! \
		-name 'processed-schema*' ! \
		-name '*.example.dt.yaml' \)

find_cmd = $(find_all_cmd) | grep -F "$(DT_SCHEMA_FILES)"
CHK_DT_DOCS := $(shell $(find_cmd))

quiet_cmd_yamllint = LINT    $(src)
      cmd_yamllint = ($(find_cmd) | \
                     xargs -n200 -P$$(nproc) \
		     $(DT_SCHEMA_LINT) -f parsable -c $(srctree)/$(src)/.yamllint >&2) || true

quiet_cmd_chk_bindings = CHKDT   $@
      cmd_chk_bindings = ($(find_cmd) | \
                         xargs -n200 -P$$(nproc) $(DT_DOC_CHECKER) -u $(srctree)/$(src)) || true

quiet_cmd_mk_schema = SCHEMA  $@
      cmd_mk_schema = f=$$(mktemp) ; \
                      $(if $(DT_MK_SCHEMA_FLAGS), \
                           printf '%s\n' $(real-prereqs), \
                           $(find_all_cmd)) > $$f ; \
                      $(DT_MK_SCHEMA) -j $(DT_MK_SCHEMA_FLAGS) @$$f > $@ ; \
		      rm -f $$f

define rule_chkdt
	$(if $(DT_SCHEMA_LINT),$(call cmd,yamllint),)
	$(call cmd,chk_bindings)
	$(call cmd,mk_schema)
endef

DT_DOCS = $(patsubst $(srctree)/%,%,$(shell $(find_all_cmd)))

override DTC_FLAGS := \
	-Wno-avoid_unnecessary_addr_size \
	-Wno-graph_child_address \
	-Wno-interrupt_provider \
	-Wno-unique_unit_address \
	-Wunique_unit_address_if_enabled

# Disable undocumented compatible checks until warning free
override DT_CHECKER_FLAGS ?=

$(obj)/processed-schema-examples.json: $(DT_DOCS) $(src)/.yamllint check_dtschema_version FORCE
	$(call if_changed_rule,chkdt)

ifeq ($(DT_SCHEMA_FILES),)

# Unless DT_SCHEMA_FILES is specified, use the full schema for dtbs_check too.
# Just copy processed-schema-examples.json

$(obj)/processed-schema.json: $(obj)/processed-schema-examples.json FORCE
	$(call if_changed,copy)

else

# If DT_SCHEMA_FILES is specified, use it for processed-schema.json

$(obj)/processed-schema.json: DT_MK_SCHEMA_FLAGS := -u
$(obj)/processed-schema.json: $(CHK_DT_DOCS) check_dtschema_version FORCE
	$(call if_changed,mk_schema)

endif

always-$(CHECK_DT_BINDING) += processed-schema-examples.json
always-$(CHECK_DTBS)       += processed-schema.json
always-$(CHECK_DT_BINDING) += $(patsubst $(srctree)/$(src)/%.yaml,%.example.dts, $(CHK_DT_DOCS))
always-$(CHECK_DT_BINDING) += $(patsubst $(srctree)/$(src)/%.yaml,%.example.dt.yaml, $(CHK_DT_DOCS))

# Hack: avoid 'Argument list too long' error for 'make clean'. Remove most of
# build artifacts here before they are processed by scripts/Makefile.clean
clean-files = $(shell find $(obj) \( -name '*.example.dts' -o \
			-name '*.example.dt.yaml' \) -delete 2>/dev/null)
