# -*- makefile -*-
# Optimized Sphinx Documentation Makefile for Linux Kernel

# Core Configuration
# ------------------
SPHINXBUILD    = sphinx-build
BUILDDIR       = $(obj)/output
KERNELDOC      = $(srctree)/scripts/kernel-doc
PDFLATEX       = xelatex
LATEXOPTS      = -interaction=batchmode -no-shell-escape
SPHINX_CONF    = conf.py
DOCS_THEME     =
DOCS_CSS       =

# Build Targets
# -------------
DOC_TARGETS = html texinfo latex pdf epub xml linkcheck

# YAML to RST Conversion
# ----------------------
YNL_INDEX    = $(srctree)/Documentation/networking/netlink_spec/index.rst
YNL_RST_DIR  = $(srctree)/Documentation/networking/netlink_spec
YNL_YAML_DIR = $(srctree)/Documentation/netlink/specs
YNL_TOOL     = $(srctree)/tools/net/ynl/pyynl/ynl_gen_rst.py
YNL_RST_FILES = $(patsubst $(YNL_YAML_DIR)/%.yaml,$(YNL_RST_DIR)/%.rst,$(wildcard $(YNL_YAML_DIR)/*.yaml))

# Dependency Checks
# -----------------
CHECK_DEPS = @$(srctree)/scripts/sphinx-pre-install --version-check
SPHINX_DEPS = $(shell which $(SPHINXBUILD) >/dev/null 2>&1 || echo "missing-sphinx")
LATEX_DEPS  = $(shell which $(PDFLATEX) >/dev/null 2>&1 || echo "missing-latex")

# Unified Build Command
# ---------------------
define SPHINX_CMD
$(CHECK_DEPS) && \
PYTHONDONTWRITEBYTECODE=1 \
BUILDDIR=$(abspath $(BUILDDIR)) \
SPHINX_CONF=$(abspath $(src)/$(3)/$(SPHINX_CONF)) \
$(PYTHON3) $(srctree)/scripts/jobserver-exec \
$(CONFIG_SHELL) $(srctree)/Documentation/sphinx/parallel-wrapper.sh \
$(SPHINXBUILD) -b $(1) -c $(abspath $(src)) \
-d $(abspath $(BUILDDIR)/.doctrees/$(2)) \
-D version=$(KERNELVERSION) -D release=$(KERNELRELEASE) \
$(ALLSPHINXOPTS) $(abspath $(src)/$(3)) \
$(abspath $(BUILDDIR)/$(2)/$(4))
endef

# Main Rules
# ----------
.PHONY: all $(DOC_TARGETS) cleandocs refcheckdocs

all: html

# YAML Documentation Generation
$(YNL_INDEX): $(YNL_RST_FILES)
	$(Q)$(YNL_TOOL) -o $@ -x

$(YNL_RST_DIR)/%.rst: $(YNL_YAML_DIR)/%.yaml
	$(Q)$(YNL_TOOL) -i $< -o $@

# Unified Documentation Targets
define GEN_DOC_TARGET
$(1)docs: $(if $(filter $(1),html),$(YNL_INDEX))
	@$(foreach dir,$(SPHINXDIRS), \
		$(call SPHINX_CMD,$(1),$(dir),$(dir),) &&) true
endef

$(foreach target,$(DOC_TARGETS),$(eval $(call GEN_DOC_TARGET,$(target))))

# Specialized Builds
# ------------------
pdfdocs: LATEX_OPTS += -D latex_paper_size=a4
pdfdocs: latex
	$(foreach dir,$(SPHINXDIRS), \
		$(MAKE) -C $(BUILDDIR)/$(dir)/latex && \
		mkdir -p $(BUILDDIR)/$(dir)/pdf && \
		mv $(BUILDDIR)/$(dir)/latex/*.pdf $(BUILDDIR)/$(dir)/pdf/;)

infodocs: texinfo
	$(MAKE) -C $(BUILDDIR)/texinfo info

# Validation Targets
# ------------------
refcheckdocs:
	$(Q)cd $(srctree) && scripts/documentation-file-ref-check

linkcheckdocs:
	@$(call SPHINX_CMD,linkcheck,linkcheck,.)

# Cleanup
# -------
cleandocs:
	$(Q)rm -f $(YNL_INDEX) $(YNL_RST_FILES)
	$(Q)rm -rf $(BUILDDIR)
	$(Q)$(MAKE) -C Documentation/userspace-api/media clean

# Help System
# -----------
dochelp:
	@echo "Linux Kernel Documentation Targets:"
	@echo "  htmldocs       - Generate HTML documentation"
	@echo "  pdfdocs        - Generate PDF documentation"
	@echo "  epubdocs       - Generate EPUB documentation"
	@echo "  cleandocs      - Remove all generated files"
	@echo "  refcheckdocs   - Validate document references"
	@echo "  linkcheckdocs  - Check external links"
	@echo "SPHINXDIRS: $(_SPHINXDIRS)"

# Dependency Handling
# -------------------
ifeq ($(SPHINX_DEPS),missing-sphinx)
$(DOC_TARGETS):
	$(error sphinx-build not found. Install Sphinx and ensure it's in PATH)
endif

ifeq ($(LATEX_DEPS),missing-latex)
pdfdocs:
	$(error $(PDFLATEX) not found. Install LaTeX suite for PDF generation)
endif

# Configuration Integration
# -------------------------
ifneq ($(wildcard $(srctree)/.config),)
  ifeq ($(CONFIG_RUST),y)
    ALLSPHINXOPTS += -t rustdoc
    htmldocs: rustdoc
  endif
endif
