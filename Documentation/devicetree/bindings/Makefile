# SPDX-License-Identifier: GPL-2.0
DT_DOC_CHECKER ?= dt-doc-validate
DT_EXTRACT_EX ?= dt-extract-example
DT_MK_SCHEMA ?= dt-mk-schema
DT_MK_SCHEMA_FLAGS := $(if $(DT_SCHEMA_FILES), -u)

quiet_cmd_chk_binding = CHKDT   $(patsubst $(srctree)/%,%,$<)
      cmd_chk_binding = $(DT_DOC_CHECKER) $< ; \
                        $(DT_EXTRACT_EX) $< > $@

$(obj)/%.example.dts: $(src)/%.yaml FORCE
	$(call if_changed,chk_binding)

DT_TMP_SCHEMA := processed-schema.yaml
extra-y += $(DT_TMP_SCHEMA)

quiet_cmd_mk_schema = SCHEMA  $@
      cmd_mk_schema = $(DT_MK_SCHEMA) $(DT_MK_SCHEMA_FLAGS) -o $@ $(filter-out FORCE, $^)

DT_DOCS = $(shell cd $(srctree)/$(src) && find * -name '*.yaml')
DT_SCHEMA_FILES ?= $(addprefix $(src)/,$(DT_DOCS))

extra-y += $(patsubst $(src)/%.yaml,%.example.dts, $(DT_SCHEMA_FILES))
extra-y += $(patsubst $(src)/%.yaml,%.example.dtb, $(DT_SCHEMA_FILES))

$(obj)/$(DT_TMP_SCHEMA): $(DT_SCHEMA_FILES) FORCE
	$(call if_changed,mk_schema)
