# -*- makefile -*-
# Makefile for Sphinx documentation
#

subdir-y :=

# You can set these variables from the command line.
SPHINXBUILD   = sphinx-build
SPHINXOPTS    =
SPHINXDIRS    = .
_SPHINXDIRS   = $(patsubst $(srctree)/Documentation/%/conf.py,%,$(wildcard $(srctree)/Documentation/*/conf.py))
SPHINX_CONF   = conf.py
PAPER         =
BUILDDIR      = $(obj)/output
PDFLATEX      = xelatex
LATEXOPTS     = -interaction=batchmode

# User-friendly check for sphinx-build
HAVE_SPHINX := $(shell if which $(SPHINXBUILD) >/dev/null 2>&1; then echo 1; else echo 0; fi)

ifeq ($(HAVE_SPHINX),0)

.DEFAULT:
	$(warning The '$(SPHINXBUILD)' command was not found. Make sure you have Sphinx installed and in PATH, or set the SPHINXBUILD make variable to point to the full path of the '$(SPHINXBUILD)' executable.)
	@echo "  SKIP    Sphinx $@ target."

else # HAVE_SPHINX

# User-friendly check for pdflatex
HAVE_PDFLATEX := $(shell if which $(PDFLATEX) >/dev/null 2>&1; then echo 1; else echo 0; fi)

# Internal variables.
PAPEROPT_a4     = -D latex_paper_size=a4
PAPEROPT_letter = -D latex_paper_size=letter
KERNELDOC       = $(srctree)/scripts/kernel-doc
KERNELDOC_CONF  = -D kerneldoc_srctree=$(srctree) -D kerneldoc_bin=$(KERNELDOC)
ALLSPHINXOPTS   =  $(KERNELDOC_CONF) $(PAPEROPT_$(PAPER)) $(SPHINXOPTS)
# the i18n builder cannot share the environment and doctrees with the others
I18NSPHINXOPTS  = $(PAPEROPT_$(PAPER)) $(SPHINXOPTS) .

# commands; the 'cmd' from scripts/Kbuild.include is not *loopable*
loop_cmd = $(echo-cmd) $(cmd_$(1)) || exit;

# $2 sphinx builder e.g. "html"
# $3 name of the build subfolder / e.g. "media", used as:
#    * dest folder relative to $(BUILDDIR) and
#    * cache folder relative to $(BUILDDIR)/.doctrees
# $4 dest subfolder e.g. "man" for man pages at media/man
# $5 reST source folder relative to $(srctree)/$(src),
#    e.g. "media" for the linux-tv book-set at ./Documentation/media

quiet_cmd_sphinx = SPHINX  $@ --> file://$(abspath $(BUILDDIR)/$3/$4)
      cmd_sphinx = $(MAKE) BUILDDIR=$(abspath $(BUILDDIR)) $(build)=Documentation/media $2 && \
	PYTHONDONTWRITEBYTECODE=1 \
	BUILDDIR=$(abspath $(BUILDDIR)) SPHINX_CONF=$(abspath $(srctree)/$(src)/$5/$(SPHINX_CONF)) \
	$(SPHINXBUILD) \
	-b $2 \
	-c $(abspath $(srctree)/$(src)) \
	-d $(abspath $(BUILDDIR)/.doctrees/$3) \
	-D version=$(KERNELVERSION) -D release=$(KERNELRELEASE) \
	$(ALLSPHINXOPTS) \
	$(abspath $(srctree)/$(src)/$5) \
	$(abspath $(BUILDDIR)/$3/$4)

htmldocs:
	@+$(foreach var,$(SPHINXDIRS),$(call loop_cmd,sphinx,html,$(var),,$(var)))

linkcheckdocs:
	@$(foreach var,$(SPHINXDIRS),$(call loop_cmd,sphinx,linkcheck,$(var),,$(var)))

latexdocs:
	@+$(foreach var,$(SPHINXDIRS),$(call loop_cmd,sphinx,latex,$(var),latex,$(var)))

ifeq ($(HAVE_PDFLATEX),0)

pdfdocs:
	$(warning The '$(PDFLATEX)' command was not found. Make sure you have it installed and in PATH to produce PDF output.)
	@echo "  SKIP    Sphinx $@ target."

else # HAVE_PDFLATEX

pdfdocs: latexdocs
	$(foreach var,$(SPHINXDIRS), $(MAKE) PDFLATEX=$(PDFLATEX) LATEXOPTS="$(LATEXOPTS)" -C $(BUILDDIR)/$(var)/latex || exit;)

endif # HAVE_PDFLATEX

epubdocs:
	@+$(foreach var,$(SPHINXDIRS),$(call loop_cmd,sphinx,epub,$(var),epub,$(var)))

xmldocs:
	@+$(foreach var,$(SPHINXDIRS),$(call loop_cmd,sphinx,xml,$(var),xml,$(var)))

endif # HAVE_SPHINX

# The following targets are independent of HAVE_SPHINX, and the rules should
# work or silently pass without Sphinx.

# no-ops for the Sphinx toolchain
sgmldocs:
	@:
psdocs:
	@:
mandocs:
	@:
installmandocs:
	@:

cleandocs:
	$(Q)rm -rf $(BUILDDIR)
	$(Q)$(MAKE) BUILDDIR=$(abspath $(BUILDDIR)) $(build)=Documentation/media clean

dochelp:
	@echo  ' Linux kernel internal documentation in different formats from ReST:'
	@echo  '  htmldocs        - HTML'
	@echo  '  latexdocs       - LaTeX'
	@echo  '  pdfdocs         - PDF'
	@echo  '  epubdocs        - EPUB'
	@echo  '  xmldocs         - XML'
	@echo  '  linkcheckdocs   - check for broken external links (will connect to external hosts)'
	@echo  '  cleandocs       - clean all generated files'
	@echo
	@echo  '  make SPHINXDIRS="s1 s2" [target] Generate only docs of folder s1, s2'
	@echo  '  valid values for SPHINXDIRS are: $(_SPHINXDIRS)'
	@echo
	@echo  '  make SPHINX_CONF={conf-file} [target] use *additional* sphinx-build'
	@echo  '  configuration. This is e.g. useful to build with nit-picking config.'
