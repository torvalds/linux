# SPDX-License-Identifier: GPL-2.0
DT_DOC_CHECKER ?= dt-doc-validate
DT_EXTRACT_EX ?= dt-extract-example
DT_MK_SCHEMA ?= dt-mk-schema

DT_SCHEMA_LINT = $(shell which yamllint)

DT_SCHEMA_MIN_VERSION = 2020.8.1

PHONY += check_dtschema_version
check_dtschema_version:
	@{ echo $(DT_SCHEMA_MIN_VERSION); \
	$(DT_DOC_CHECKER) --version 2>/dev/null || echo 0; } | sort -VC || \
	{ echo "ERROR: dtschema minimum version is v$(DT_SCHEMA_MIN_VERSION)" >&2; false; }

quiet_cmd_extract_ex = DTEX    $@
      cmd_extract_ex = $(DT_EXTRACT_EX) $< > $@

$(obj)/%.example.dts: $(src)/%.yaml check_dtschema_version FORCE
	$(call if_changed,extract_ex)

# Use full schemas when checking %.example.dts
DT_TMP_SCHEMA := $(obj)/processed-schema-examples.json

find_cmd = find $(srctree)/$(src) \( -name '*.yaml' ! \
		-name 'processed-schema*' ! \
		-name '*.example.dt.yaml' \)

quiet_cmd_yamllint = LINT    $(src)
      cmd_yamllint = ($(find_cmd) | \
                     xargs $(DT_SCHEMA_LINT) -f parsable -c $(srctree)/$(src)/.yamllint) || true

quiet_cmd_chk_bindings = CHKDT   $@
      cmd_chk_bindings = ($(find_cmd) | \
                         xargs -n200 -P$$(nproc) $(DT_DOC_CHECKER) -u $(srctree)/$(src)) || true

quiet_cmd_mk_schema = SCHEMA  $@
      cmd_mk_schema = f=$$(mktemp) ; \
                      $(if $(DT_MK_SCHEMA_FLAGS), \
                           printf '%s\n' $(real-prereqs), \
                           $(find_cmd)) > $$f ; \
                      $(DT_MK_SCHEMA) -j $(DT_MK_SCHEMA_FLAGS) @$$f > $@ ; \
		      rm -f $$f

define rule_chkdt
	$(if $(DT_SCHEMA_LINT),$(call cmd,yamllint),)
	$(call cmd,chk_bindings)
	$(call cmd,mk_schema)
endef

DT_DOCS = $(shell $(find_cmd) | sed -e 's|^$(srctree)/||')

override DTC_FLAGS := \
	-Wno-avoid_unnecessary_addr_size \
	-Wno-graph_child_address \
	-Wno-interrupt_provider

$(obj)/processed-schema-examples.json: $(DT_DOCS) $(src)/.yamllint check_dtschema_version FORCE
	$(call if_changed_rule,chkdt)

ifeq ($(DT_SCHEMA_FILES),)

# Unless DT_SCHEMA_FILES is specified, use the full schema for dtbs_check too.
# Just copy processed-schema-examples.json

$(obj)/processed-schema.json: $(obj)/processed-schema-examples.json FORCE
	$(call if_changed,copy)

DT_SCHEMA_FILES = $(DT_DOCS)

else

# If DT_SCHEMA_FILES is specified, use it for processed-schema.json

$(obj)/processed-schema.json: DT_MK_SCHEMA_FLAGS := -u
$(obj)/processed-schema.json: $(DT_SCHEMA_FILES) check_dtschema_version FORCE
	$(call if_changed,mk_schema)

endif

extra-$(CHECK_DT_BINDING) += processed-schema-examples.json
extra-$(CHECK_DTBS) += processed-schema.json
extra-$(CHECK_DT_BINDING) += $(patsubst $(src)/%.yaml,%.example.dts, $(DT_SCHEMA_FILES))
extra-$(CHECK_DT_BINDING) += $(patsubst $(src)/%.yaml,%.example.dt.yaml, $(DT_SCHEMA_FILES))

# Hack: avoid 'Argument list too long' error for 'make clean'. Remove most of
# build artifacts here before they are processed by scripts/Makefile.clean
clean-files = $(shell find $(obj) \( -name '*.example.dts' -o \
			-name '*.example.dt.yaml' \) -delete 2>/dev/null)
