# SPDX-License-Identifier: GPL-2.0
DT_DOC_CHECKER ?= dt-doc-validate
DT_EXTRACT_EX ?= dt-extract-example
DT_MK_SCHEMA ?= dt-mk-schema
DT_MK_SCHEMA_FLAGS := $(if $(DT_SCHEMA_FILES), -u)

quiet_cmd_chk_binding = CHKDT   $(patsubst $(srctree)/%,%,$<)
      cmd_chk_binding = $(DT_DOC_CHECKER) -u $(srctree)/$(src) $< ; \
                        $(DT_EXTRACT_EX) $< > $@

$(obj)/%.example.dts: $(src)/%.yaml FORCE
	$(call if_changed,chk_binding)

DT_TMP_SCHEMA := processed-schema.yaml

quiet_cmd_mk_schema = SCHEMA  $@
      cmd_mk_schema = $(DT_MK_SCHEMA) $(DT_MK_SCHEMA_FLAGS) -o $@ $(real-prereqs)

DT_DOCS = $(shell \
	cd $(srctree)/$(src) && \
	find * \( -name '*.yaml' ! \
		-name $(DT_TMP_SCHEMA) ! \
		-name '*.example.dt.yaml' \) \
	)

DT_SCHEMA_FILES ?= $(addprefix $(src)/,$(DT_DOCS))

ifeq ($(CHECK_DTBS),)
extra-y += $(patsubst $(src)/%.yaml,%.example.dts, $(DT_SCHEMA_FILES))
extra-y += $(patsubst $(src)/%.yaml,%.example.dt.yaml, $(DT_SCHEMA_FILES))
endif

$(obj)/$(DT_TMP_SCHEMA): $(DT_SCHEMA_FILES) FORCE
	$(call if_changed,mk_schema)

extra-y += $(DT_TMP_SCHEMA)
