# SPDX-License-Identifier: GPL-2.0
DT_DOC_CHECKER ?= dt-doc-validate
DT_EXTRACT_EX ?= dt-extract-example
DT_MK_SCHEMA ?= dt-mk-schema

DT_SCHEMA_MIN_VERSION = 2020.5

PHONY += check_dtschema_version
check_dtschema_version:
	@{ echo $(DT_SCHEMA_MIN_VERSION); \
	$(DT_DOC_CHECKER) --version 2>/dev/null || echo 0; } | sort -VC || \
	{ echo "ERROR: dtschema minimum version is v$(DT_SCHEMA_MIN_VERSION)" >&2; false; }

quiet_cmd_chk_binding = CHKDT   $(patsubst $(srctree)/%,%,$<)
      cmd_chk_binding = $(DT_DOC_CHECKER) -u $(srctree)/$(src) $< ; \
                        $(DT_EXTRACT_EX) $< > $@

$(obj)/%.example.dts: $(src)/%.yaml check_dtschema_version FORCE
	$(call if_changed,chk_binding)

# Use full schemas when checking %.example.dts
DT_TMP_SCHEMA := $(obj)/processed-schema-examples.yaml

find_cmd = find $(srctree)/$(src) \( -name '*.yaml' ! \
		-name 'processed-schema*' ! \
		-name '*.example.dt.yaml' \)

quiet_cmd_mk_schema = SCHEMA  $@
      cmd_mk_schema = rm -f $@ ; \
                      $(if $(DT_MK_SCHEMA_FLAGS), \
                           echo $(real-prereqs), \
                           $(find_cmd)) | \
                      xargs $(DT_MK_SCHEMA) $(DT_MK_SCHEMA_FLAGS) >> $@

DT_DOCS = $(shell $(find_cmd) | sed -e 's|^$(srctree)/||')

override DTC_FLAGS := \
	-Wno-avoid_unnecessary_addr_size \
	-Wno-graph_child_address \
	-Wno-interrupt_provider

$(obj)/processed-schema-examples.yaml: $(DT_DOCS) check_dtschema_version FORCE
	$(call if_changed,mk_schema)

ifeq ($(DT_SCHEMA_FILES),)

# Unless DT_SCHEMA_FILES is specified, use the full schema for dtbs_check too.
# Just copy processed-schema-examples.yaml

$(obj)/processed-schema.yaml: $(obj)/processed-schema-examples.yaml FORCE
	$(call if_changed,copy)

DT_SCHEMA_FILES = $(DT_DOCS)

else

# If DT_SCHEMA_FILES is specified, use it for processed-schema.yaml

$(obj)/processed-schema.yaml: DT_MK_SCHEMA_FLAGS := -u
$(obj)/processed-schema.yaml: $(DT_SCHEMA_FILES) check_dtschema_version FORCE
	$(call if_changed,mk_schema)

endif

extra-$(CHECK_DT_BINDING) += $(patsubst $(src)/%.yaml,%.example.dts, $(DT_SCHEMA_FILES))
extra-$(CHECK_DT_BINDING) += $(patsubst $(src)/%.yaml,%.example.dt.yaml, $(DT_SCHEMA_FILES))
extra-$(CHECK_DT_BINDING) += processed-schema-examples.yaml
extra-$(CHECK_DTBS) += processed-schema.yaml

# Hack: avoid 'Argument list too long' error for 'make clean'. Remove most of
# build artifacts here before they are processed by scripts/Makefile.clean
clean-files = $(shell find $(obj) \( -name '*.example.dts' -o \
			-name '*.example.dt.yaml' \) -delete 2>/dev/null)
