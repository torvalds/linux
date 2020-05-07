# SPDX-License-Identifier: GPL-2.0
DT_DOC_CHECKER ?= dt-doc-validate
DT_EXTRACT_EX ?= dt-extract-example
DT_MK_SCHEMA ?= dt-mk-schema
DT_MK_SCHEMA_USERONLY_FLAG := $(if $(DT_SCHEMA_FILES), -u)

quiet_cmd_chk_binding = CHKDT   $(patsubst $(srctree)/%,%,$<)
      cmd_chk_binding = $(DT_DOC_CHECKER) -u $(srctree)/$(src) $< ; \
                        $(DT_EXTRACT_EX) $< > $@

$(obj)/%.example.dts: $(src)/%.yaml FORCE
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

DT_SCHEMA_FILES ?= $(DT_DOCS)

extra-$(CHECK_DT_BINDING) += $(patsubst $(src)/%.yaml,%.example.dts, $(DT_SCHEMA_FILES))
extra-$(CHECK_DT_BINDING) += $(patsubst $(src)/%.yaml,%.example.dt.yaml, $(DT_SCHEMA_FILES))
extra-$(CHECK_DT_BINDING) += processed-schema-examples.yaml

override DTC_FLAGS := \
	-Wno-avoid_unnecessary_addr_size \
	-Wno-graph_child_address

$(obj)/processed-schema-examples.yaml: $(DT_DOCS) FORCE
	$(call if_changed,mk_schema)

$(obj)/processed-schema.yaml: DT_MK_SCHEMA_FLAGS := $(DT_MK_SCHEMA_USERONLY_FLAG)
$(obj)/processed-schema.yaml: $(DT_SCHEMA_FILES) FORCE
	$(call if_changed,mk_schema)

extra-y += processed-schema.yaml
