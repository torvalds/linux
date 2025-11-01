# SPDX-License-Identifier: GPL-2.0-only
# pylint: disable=C0103,C0209

"""
The Linux Kernel documentation build configuration file.
"""

import os
import shutil
import sys

from  textwrap import dedent

import sphinx

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
sys.path.insert(0, os.path.abspath("sphinx"))

from load_config import loadConfig               # pylint: disable=C0413,E0401

# Minimal supported version
needs_sphinx = "3.4.3"

# Get Sphinx version
major, minor, patch = sphinx.version_info[:3]          # pylint: disable=I1101

# Include_patterns were added on Sphinx 5.1
if (major < 5) or (major == 5 and minor < 1):
    has_include_patterns = False
else:
    has_include_patterns = True
    # Include patterns that don't contain directory names, in glob format
    include_patterns = ["**.rst"]

# Location of Documentation/ directory
doctree = os.path.abspath(".")

# Exclude of patterns that don't contain directory names, in glob format.
exclude_patterns = []

# List of patterns that contain directory names in glob format.
dyn_include_patterns = []
dyn_exclude_patterns = ["output"]

# Currently, only netlink/specs has a parser for yaml.
# Prefer using include patterns if available, as it is faster
if has_include_patterns:
    dyn_include_patterns.append("netlink/specs/*.yaml")
else:
    dyn_exclude_patterns.append("netlink/*.yaml")
    dyn_exclude_patterns.append("devicetree/bindings/**.yaml")
    dyn_exclude_patterns.append("core-api/kho/bindings/**.yaml")

# Properly handle directory patterns and LaTeX docs
# -------------------------------------------------

def config_init(app, config):
    """
    Initialize path-dependent variabled

    On Sphinx, all directories are relative to what it is passed as
    SOURCEDIR parameter for sphinx-build. Due to that, all patterns
    that have directory names on it need to be dynamically set, after
    converting them to a relative patch.

    As Sphinx doesn't include any patterns outside SOURCEDIR, we should
    exclude relative patterns that start with "../".
    """

    # setup include_patterns dynamically
    if has_include_patterns:
        for p in dyn_include_patterns:
            full = os.path.join(doctree, p)

            rel_path = os.path.relpath(full, start=app.srcdir)
            if rel_path.startswith("../"):
                continue

            config.include_patterns.append(rel_path)

    # setup exclude_patterns dynamically
    for p in dyn_exclude_patterns:
        full = os.path.join(doctree, p)

        rel_path = os.path.relpath(full, start=app.srcdir)
        if rel_path.startswith("../"):
            continue

        config.exclude_patterns.append(rel_path)

    # LaTeX and PDF output require a list of documents with are dependent
    # of the app.srcdir. Add them here

    # When SPHINXDIRS is used, we just need to get index.rst, if it exists
    if not os.path.samefile(doctree, app.srcdir):
        doc = os.path.basename(app.srcdir)
        fname = "index"
        if os.path.exists(os.path.join(app.srcdir, fname + ".rst")):
            latex_documents.append((fname, doc + ".tex",
                                    "Linux %s Documentation" % doc.capitalize(),
                                    "The kernel development community",
                                    "manual"))
            return

    # When building all docs, or when a main index.rst doesn't exist, seek
    # for it on subdirectories
    for doc in os.listdir(app.srcdir):
        fname = os.path.join(doc, "index")
        if not os.path.exists(os.path.join(app.srcdir, fname + ".rst")):
            continue

        has = False
        for l in latex_documents:
            if l[0] == fname:
                has = True
                break

        if not has:
            latex_documents.append((fname, doc + ".tex",
                                    "Linux %s Documentation" % doc.capitalize(),
                                    "The kernel development community",
                                    "manual"))

# helper
# ------


def have_command(cmd):
    """Search ``cmd`` in the ``PATH`` environment.

    If found, return True.
    If not found, return False.
    """
    return shutil.which(cmd) is not None


# -- General configuration ------------------------------------------------

# Add any Sphinx extensions in alphabetic order
extensions = [
    "automarkup",
    "kernel_abi",
    "kerneldoc",
    "kernel_feat",
    "kernel_include",
    "kfigure",
    "maintainers_include",
    "parser_yaml",
    "rstFlatTable",
    "sphinx.ext.autosectionlabel",
    "sphinx.ext.ifconfig",
    "translations",
]
# Since Sphinx version 3, the C function parser is more pedantic with regards
# to type checking. Due to that, having macros at c:function cause problems.
# Those needed to be escaped by using c_id_attributes[] array
c_id_attributes = [
    # GCC Compiler types not parsed by Sphinx:
    "__restrict__",

    # include/linux/compiler_types.h:
    "__iomem",
    "__kernel",
    "noinstr",
    "notrace",
    "__percpu",
    "__rcu",
    "__user",
    "__force",
    "__counted_by_le",
    "__counted_by_be",

    # include/linux/compiler_attributes.h:
    "__alias",
    "__aligned",
    "__aligned_largest",
    "__always_inline",
    "__assume_aligned",
    "__cold",
    "__attribute_const__",
    "__copy",
    "__pure",
    "__designated_init",
    "__visible",
    "__printf",
    "__scanf",
    "__gnu_inline",
    "__malloc",
    "__mode",
    "__no_caller_saved_registers",
    "__noclone",
    "__nonstring",
    "__noreturn",
    "__packed",
    "__pure",
    "__section",
    "__always_unused",
    "__maybe_unused",
    "__used",
    "__weak",
    "noinline",
    "__fix_address",
    "__counted_by",

    # include/linux/memblock.h:
    "__init_memblock",
    "__meminit",

    # include/linux/init.h:
    "__init",
    "__ref",

    # include/linux/linkage.h:
    "asmlinkage",

    # include/linux/btf.h
    "__bpf_kfunc",
]

# Ensure that autosectionlabel will produce unique names
autosectionlabel_prefix_document = True
autosectionlabel_maxdepth = 2

# Load math renderer:
# For html builder, load imgmath only when its dependencies are met.
# mathjax is the default math renderer since Sphinx 1.8.
have_latex = have_command("latex")
have_dvipng = have_command("dvipng")
load_imgmath = have_latex and have_dvipng

# Respect SPHINX_IMGMATH (for html docs only)
if "SPHINX_IMGMATH" in os.environ:
    env_sphinx_imgmath = os.environ["SPHINX_IMGMATH"]
    if "yes" in env_sphinx_imgmath:
        load_imgmath = True
    elif "no" in env_sphinx_imgmath:
        load_imgmath = False
    else:
        sys.stderr.write("Unknown env SPHINX_IMGMATH=%s ignored.\n" % env_sphinx_imgmath)

if load_imgmath:
    extensions.append("sphinx.ext.imgmath")
    math_renderer = "imgmath"
else:
    math_renderer = "mathjax"

# Add any paths that contain templates here, relative to this directory.
templates_path = ["sphinx/templates"]

# The suffixes of source filenames that will be automatically parsed
source_suffix = {
    ".rst": "restructuredtext",
    ".yaml": "yaml",
}

# The encoding of source files.
# source_encoding = 'utf-8-sig'

# The master toctree document.
master_doc = "index"

# General information about the project.
project = "The Linux Kernel"
copyright = "The kernel development community"         # pylint: disable=W0622
author = "The kernel development community"

# The version info for the project you're documenting, acts as replacement for
# |version| and |release|, also used in various other places throughout the
# built documents.
#
# In a normal build, version and release are set to KERNELVERSION and
# KERNELRELEASE, respectively, from the Makefile via Sphinx command line
# arguments.
#
# The following code tries to extract the information by reading the Makefile,
# when Sphinx is run directly (e.g. by Read the Docs).
try:
    makefile_version = None
    makefile_patchlevel = None
    with open("../Makefile", encoding="utf=8") as fp:
        for line in fp:
            key, val = [x.strip() for x in line.split("=", 2)]
            if key == "VERSION":
                makefile_version = val
            elif key == "PATCHLEVEL":
                makefile_patchlevel = val
            if makefile_version and makefile_patchlevel:
                break
except Exception:
    pass
finally:
    if makefile_version and makefile_patchlevel:
        version = release = makefile_version + "." + makefile_patchlevel
    else:
        version = release = "unknown version"


def get_cline_version():
    """
    HACK: There seems to be no easy way for us to get at the version and
    release information passed in from the makefile...so go pawing through the
    command-line options and find it for ourselves.
    """

    c_version = c_release = ""
    for arg in sys.argv:
        if arg.startswith("version="):
            c_version = arg[8:]
        elif arg.startswith("release="):
            c_release = arg[8:]
    if c_version:
        if c_release:
            return c_version + "-" + c_release
        return c_version
    return version  # Whatever we came up with before


# The language for content autogenerated by Sphinx. Refer to documentation
# for a list of supported languages.
#
# This is also used if you do content translation via gettext catalogs.
# Usually you set "language" from the command line for these cases.
language = "en"

# There are two options for replacing |today|: either, you set today to some
# non-false value, then it is used:
# today = ''
# Else, today_fmt is used as the format for a strftime call.
# today_fmt = '%B %d, %Y'

# The reST default role (used for this markup: `text`) to use for all
# documents.
# default_role = None

# If true, '()' will be appended to :func: etc. cross-reference text.
# add_function_parentheses = True

# If true, the current module name will be prepended to all description
# unit titles (such as .. function::).
# add_module_names = True

# If true, sectionauthor and moduleauthor directives will be shown in the
# output. They are ignored by default.
# show_authors = False

# The name of the Pygments (syntax highlighting) style to use.
pygments_style = "sphinx"

# A list of ignored prefixes for module index sorting.
# modindex_common_prefix = []

# If true, keep warnings as "system message" paragraphs in the built documents.
# keep_warnings = False

# If true, `todo` and `todoList` produce output, else they produce nothing.
todo_include_todos = False

primary_domain = "c"
highlight_language = "none"

# -- Options for HTML output ----------------------------------------------

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.

# Default theme
html_theme = "alabaster"
html_css_files = []

if "DOCS_THEME" in os.environ:
    html_theme = os.environ["DOCS_THEME"]

if html_theme in ["sphinx_rtd_theme", "sphinx_rtd_dark_mode"]:
    # Read the Docs theme
    try:
        import sphinx_rtd_theme

        html_theme_path = [sphinx_rtd_theme.get_html_theme_path()]

        # Add any paths that contain custom static files (such as style sheets) here,
        # relative to this directory. They are copied after the builtin static files,
        # so a file named "default.css" will overwrite the builtin "default.css".
        html_css_files = [
            "theme_overrides.css",
        ]

        # Read the Docs dark mode override theme
        if html_theme == "sphinx_rtd_dark_mode":
            try:
                import sphinx_rtd_dark_mode            # pylint: disable=W0611

                extensions.append("sphinx_rtd_dark_mode")
            except ImportError:
                html_theme = "sphinx_rtd_theme"

        if html_theme == "sphinx_rtd_theme":
            # Add color-specific RTD normal mode
            html_css_files.append("theme_rtd_colors.css")

        html_theme_options = {
            "navigation_depth": -1,
        }

    except ImportError:
        html_theme = "alabaster"

if "DOCS_CSS" in os.environ:
    css = os.environ["DOCS_CSS"].split(" ")

    for l in css:
        html_css_files.append(l)

if html_theme == "alabaster":
    html_theme_options = {
        "description": get_cline_version(),
        "page_width": "65em",
        "sidebar_width": "15em",
        "fixed_sidebar": "true",
        "font_size": "inherit",
        "font_family": "serif",
    }

sys.stderr.write("Using %s theme\n" % html_theme)

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ["sphinx-static"]

# If true, Docutils "smart quotes" will be used to convert quotes and dashes
# to typographically correct entities.  However, conversion of "--" to "—"
# is not always what we want, so enable only quotes.
smartquotes_action = "q"

# Custom sidebar templates, maps document names to template names.
# Note that the RTD theme ignores this
html_sidebars = {"**": ["searchbox.html",
                        "kernel-toc.html",
                        "sourcelink.html"]}

# about.html is available for alabaster theme. Add it at the front.
if html_theme == "alabaster":
    html_sidebars["**"].insert(0, "about.html")

# The name of an image file (relative to this directory) to place at the top
# of the sidebar.
html_logo = "images/logo.svg"

# Output file base name for HTML help builder.
htmlhelp_basename = "TheLinuxKerneldoc"

# -- Options for LaTeX output ---------------------------------------------

latex_elements = {
    # The paper size ('letterpaper' or 'a4paper').
    "papersize": "a4paper",
    "passoptionstopackages": dedent(r"""
        \PassOptionsToPackage{svgnames}{xcolor}
    """),
    # The font size ('10pt', '11pt' or '12pt').
    "pointsize": "11pt",
    # Needed to generate a .ind file
    "printindex": r"\footnotesize\raggedright\printindex",
    # Latex figure (float) alignment
    # 'figure_align': 'htbp',
    # Don't mangle with UTF-8 chars
    "fontenc": "",
    "inputenc": "",
    "utf8extra": "",
    # Set document margins
    "sphinxsetup": dedent(r"""
        hmargin=0.5in, vmargin=1in,
        parsedliteralwraps=true,
        verbatimhintsturnover=false,
    """),
    #
    # Some of our authors are fond of deep nesting; tell latex to
    # cope.
    #
    "maxlistdepth": "10",
    # For CJK One-half spacing, need to be in front of hyperref
    "extrapackages": r"\usepackage{setspace}",
    "fontpkg": dedent(r"""
        \usepackage{fontspec}
        \setmainfont{DejaVu Serif}
        \setsansfont{DejaVu Sans}
        \setmonofont{DejaVu Sans Mono}
        \newfontfamily\headingfont{DejaVu Serif}
    """),
    "preamble": dedent(r"""
        % Load kerneldoc specific LaTeX settings
        \input{kerneldoc-preamble.sty}
    """)
}

# This will be filled up by config-inited event
latex_documents = []

# The name of an image file (relative to this directory) to place at the top of
# the title page.
# latex_logo = None

# For "manual" documents, if this is true, then toplevel headings are parts,
# not chapters.
# latex_use_parts = False

# If true, show page references after internal links.
# latex_show_pagerefs = False

# If true, show URL addresses after external links.
# latex_show_urls = False

# Documents to append as an appendix to all manuals.
# latex_appendices = []

# If false, no module index is generated.
# latex_domain_indices = True

# Additional LaTeX stuff to be copied to build directory
latex_additional_files = [
    "sphinx/kerneldoc-preamble.sty",
]


# -- Options for manual page output ---------------------------------------

# One entry per manual page. List of tuples
# (source start file, name, description, authors, manual section).
man_pages = [
    (master_doc, "thelinuxkernel", "The Linux Kernel Documentation", [author], 1)
]

# If true, show URL addresses after external links.
# man_show_urls = False


# -- Options for Texinfo output -------------------------------------------

# Grouping the document tree into Texinfo files. List of tuples
# (source start file, target name, title, author,
#  dir menu entry, description, category)
texinfo_documents = [(
        master_doc,
        "TheLinuxKernel",
        "The Linux Kernel Documentation",
        author,
        "TheLinuxKernel",
        "One line description of project.",
        "Miscellaneous",
    ),]

# -- Options for Epub output ----------------------------------------------

# Bibliographic Dublin Core info.
epub_title = project
epub_author = author
epub_publisher = author
epub_copyright = copyright

# A list of files that should not be packed into the epub file.
epub_exclude_files = ["search.html"]

# =======
# rst2pdf
#
# Grouping the document tree into PDF files. List of tuples
# (source start file, target name, title, author, options).
#
# See the Sphinx chapter of https://ralsina.me/static/manual.pdf
#
# FIXME: Do not add the index file here; the result will be too big. Adding
# multiple PDF files here actually tries to get the cross-referencing right
# *between* PDF files.
pdf_documents = [
    ("kernel-documentation", "Kernel", "Kernel", "J. Random Bozo"),
]

# kernel-doc extension configuration for running Sphinx directly (e.g. by Read
# the Docs). In a normal build, these are supplied from the Makefile via command
# line arguments.
kerneldoc_bin = "../scripts/kernel-doc.py"
kerneldoc_srctree = ".."

# ------------------------------------------------------------------------------
# Since loadConfig overwrites settings from the global namespace, it has to be
# the last statement in the conf.py file
# ------------------------------------------------------------------------------
loadConfig(globals())


def setup(app):
    """Patterns need to be updated at init time on older Sphinx versions"""

    app.connect('config-inited', config_init)
