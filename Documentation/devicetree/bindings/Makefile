# SPDX-License-Identifier: GPL-2.0
DT_DOC_CHECKER ?= dt-doc-validate
DT_EXTRACT_EX ?= dt-extract-example
DT_MK_SCHEMA ?= dt-mk-schema

quiet_cmd_chk_binding = CHKDT   $(patsubst $(srctree)/%,%,$<)
      cmd_chk_binding = $(DT_DOC_CHECKER) -u $(srctree)/$(src) $< ; \
                        $(DT_EXTRACT_EX) $< > $@

$(obj)/%.example.dts: $(src)/%.yaml FORCE
	$(call if_changed,chk_binding)

# Use full schemas when checking %.example.dts
DT_TMP_SCHEMA := $(obj)/processed-schema-examples.yaml

quiet_cmd_mk_schema = SCHEMA  $@
      cmd_mk_schema = $(DT_MK_SCHEMA) $(DT_MK_SCHEMA_FLAGS) -o $@ $(real-prereqs)

DT_DOCS = $(addprefix $(src)/, \
	$(shell \
	cd $(srctree)/$(src) && \
	find * \( -name '*.yaml' ! \
		-name 'processed-schema*' ! \
		-name '*.example.dt.yaml' \) \
	))

DT_SCHEMA_FILES ?= $(DT_DOCS)

extra-$(CHECK_DT_BINDING) += $(patsubst $(src)/%.yaml,%.example.dts, $(DT_SCHEMA_FILES))
extra-$(CHECK_DT_BINDING) += $(patsubst $(src)/%.yaml,%.example.dt.yaml, $(DT_SCHEMA_FILES))
extra-$(CHECK_DT_BINDING) += processed-schema-examples.yaml

override DTC_FLAGS := \
	-Wno-avoid_unnecessary_addr_size \
	-Wno-graph_child_address

$(obj)/processed-schema-examples.yaml: $(DT_DOCS) FORCE
	$(call if_changed,mk_schema)

$(obj)/processed-schema.yaml: DT_MK_SCHEMA_FLAGS := -u
$(obj)/processed-schema.yaml: $(DT_SCHEMA_FILES) FORCE
	$(call if_changed,mk_schema)

extra-y += processed-schema.yaml
