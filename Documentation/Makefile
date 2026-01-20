# -*- makefile -*-
# Makefile for Sphinx documentation
#

# for cleaning
subdir- := devicetree/bindings

ifneq ($(MAKECMDGOALS),cleandocs)
# Check for broken documentation file references
ifeq ($(CONFIG_WARN_MISSING_DOCUMENTS),y)
$(shell $(srctree)/tools/docs/documentation-file-ref-check --warn)
endif

# Check for broken ABI files
ifeq ($(CONFIG_WARN_ABI_ERRORS),y)
$(shell $(srctree)/tools/docs/get_abi.py --dir $(srctree)/Documentation/ABI validate)
endif
endif

# You can set these variables from the command line.
SPHINXBUILD   = sphinx-build
SPHINXOPTS    =
SPHINXDIRS    = .
DOCS_THEME    =
DOCS_CSS      =
RUSTDOC       =
PAPER         =
BUILDDIR      = $(obj)/output
PDFLATEX      = xelatex
LATEXOPTS     = -interaction=batchmode -no-shell-escape

PYTHONPYCACHEPREFIX ?= $(abspath $(BUILDDIR)/__pycache__)

# Wrapper for sphinx-build

BUILD_WRAPPER = $(srctree)/tools/docs/sphinx-build-wrapper

# For denylisting "variable font" files
# Can be overridden by setting as an env variable
FONTS_CONF_DENY_VF ?= $(HOME)/deny-vf

# User-friendly check for sphinx-build
HAVE_SPHINX := $(shell if which $(SPHINXBUILD) >/dev/null 2>&1; then echo 1; else echo 0; fi)

ifeq ($(HAVE_SPHINX),0)

.DEFAULT:
	$(warning The '$(SPHINXBUILD)' command was not found. Make sure you have Sphinx installed and in PATH, or set the SPHINXBUILD make variable to point to the full path of the '$(SPHINXBUILD)' executable.)
	@echo
	@$(srctree)/tools/docs/sphinx-pre-install
	@echo "  SKIP    Sphinx $@ target."

else # HAVE_SPHINX

# Common documentation targets
htmldocs mandocs infodocs texinfodocs latexdocs epubdocs xmldocs pdfdocs linkcheckdocs:
	$(Q)PYTHONPYCACHEPREFIX="$(PYTHONPYCACHEPREFIX)" \
		$(srctree)/tools/docs/sphinx-pre-install --version-check
	+$(Q)PYTHONPYCACHEPREFIX="$(PYTHONPYCACHEPREFIX)" \
		$(PYTHON3) $(BUILD_WRAPPER) $@ \
		--sphinxdirs="$(SPHINXDIRS)" $(RUSTDOC) \
		--builddir="$(BUILDDIR)" --deny-vf=$(FONTS_CONF_DENY_VF) \
		--theme=$(DOCS_THEME) --css=$(DOCS_CSS) --paper=$(PAPER)


endif

# The following targets are independent of HAVE_SPHINX, and the rules should
# work or silently pass without Sphinx.

htmldocs-redirects: $(srctree)/Documentation/.renames.txt
	@tools/docs/gen-redirects.py --output $(BUILDDIR) < $<

refcheckdocs:
	$(Q)cd $(srctree); tools/docs/documentation-file-ref-check

cleandocs:
	$(Q)rm -rf $(BUILDDIR)

# Used only on help
_SPHINXDIRS   = $(shell printf "%s\n" $(patsubst $(srctree)/Documentation/%/index.rst,%,$(wildcard $(srctree)/Documentation/*/index.rst)) | sort -f)

dochelp:
	@echo  ' Linux kernel internal documentation in different formats from ReST:'
	@echo  '  htmldocs        - HTML'
	@echo  '  htmldocs-redirects - generate HTML redirects for moved pages'
	@echo  '  texinfodocs     - Texinfo'
	@echo  '  infodocs        - Info'
	@echo  '  mandocs         - Man pages'
	@echo  '  latexdocs       - LaTeX'
	@echo  '  pdfdocs         - PDF'
	@echo  '  epubdocs        - EPUB'
	@echo  '  xmldocs         - XML'
	@echo  '  linkcheckdocs   - check for broken external links'
	@echo  '                    (will connect to external hosts)'
	@echo  '  refcheckdocs    - check for references to non-existing files under'
	@echo  '                    Documentation'
	@echo  '  cleandocs       - clean all generated files'
	@echo
	@echo  '  make SPHINXDIRS="s1 s2" [target] Generate only docs of folder s1, s2'
	@echo  '  top level values for SPHINXDIRS are: $(_SPHINXDIRS)'
	@echo  '  you may also use a subdirectory like SPHINXDIRS=userspace-api/media,'
	@echo  '  provided that there is an index.rst file at the subdirectory.'
	@echo
	@echo  '  make DOCS_THEME={sphinx-theme} selects a different Sphinx theme.'
	@echo
	@echo  '  make DOCS_CSS={a .css file} adds a DOCS_CSS override file for html/epub output.'
	@echo
	@echo  '  make PAPER={a4|letter} Specifies the paper size used for LaTeX/PDF output.'
	@echo
	@echo  '  make FONTS_CONF_DENY_VF={path} sets a deny list to block variable Noto CJK fonts'
	@echo  '  for PDF build. See tools/lib/python/kdoc/latex_fonts.py for more details'
	@echo
	@echo  '  Default location for the generated documents is Documentation/output'
