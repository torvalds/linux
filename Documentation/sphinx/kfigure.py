# -*- coding: utf-8; mode: python -*-
# pylint: disable=C0103, R0903, R0912, R0915
u"""
    scalable figure and image handling
    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    Sphinx extension which implements scalable image handling.

    :copyright:  Copyright (C) 2016  Markus Heiser
    :license:    GPL Version 2, June 1991 see Linux/COPYING for details.

    The build for image formats depend on image's source format and output's
    destination format. This extension implement methods to simplify image
    handling from the author's POV. Directives like ``kernel-figure`` implement
    methods *to* always get the best output-format even if some tools are yest
    installed. For more details take a look at ``convert_image(...)`` which is
    the core of all conversions.

    * ``.. kernel-image``: for image handling / a ``.. image::`` replacement

    * ``.. kernel-figure``: for figure handling / a ``.. figure::`` replacement

    * ``.. kernel-render``: for render markup / a concept to embed *render*
      markups (or languages). Supported markups (see ``RENDER_MARKUP_EXT``)

      - ``DOT``: render embedded Graphviz's **DOC**
      - ``SVG``: render embedded Scalable Vector Graphics (**SVG**)
      - ... *developable*

    Used tools:

    * ``dot(1)``: Graphviz (http://www.graphviz.org). If Graphviz is yest
      available, the DOT language is inserted as literal-block.

    * SVG to PDF: To generate PDF, you need at least one of this tools:

      - ``convert(1)``: ImageMagick (https://www.imagemagick.org)

    List of customizations:

    * generate PDF from SVG / used by PDF (LaTeX) builder

    * generate SVG (html-builder) and PDF (latex-builder) from DOT files.
      DOT: see http://www.graphviz.org/content/dot-language

    """

import os
from os import path
import subprocess
from hashlib import sha1
import sys

from docutils import yesdes
from docutils.statemachine import ViewList
from docutils.parsers.rst import directives
from docutils.parsers.rst.directives import images
import sphinx

from sphinx.util.yesdes import clean_astext
from six import iteritems

import kernellog

PY3 = sys.version_info[0] == 3

if PY3:
    _unicode = str
else:
    _unicode = unicode

# Get Sphinx version
major, miyesr, patch = sphinx.version_info[:3]
if major == 1 and miyesr > 3:
    # patches.Figure only landed in Sphinx 1.4
    from sphinx.directives.patches import Figure  # pylint: disable=C0413
else:
    Figure = images.Figure

__version__  = '1.0.0'

# simple helper
# -------------

def which(cmd):
    """Searches the ``cmd`` in the ``PATH`` environment.

    This *which* searches the PATH for executable ``cmd`` . First match is
    returned, if yesthing is found, ``None` is returned.
    """
    envpath = os.environ.get('PATH', None) or os.defpath
    for folder in envpath.split(os.pathsep):
        fname = folder + os.sep + cmd
        if path.isfile(fname):
            return fname

def mkdir(folder, mode=0o775):
    if yest path.isdir(folder):
        os.makedirs(folder, mode)

def file2literal(fname):
    with open(fname, "r") as src:
        data = src.read()
        yesde = yesdes.literal_block(data, data)
    return yesde

def isNewer(path1, path2):
    """Returns True if ``path1`` is newer than ``path2``

    If ``path1`` exists and is newer than ``path2`` the function returns
    ``True`` is returned otherwise ``False``
    """
    return (path.exists(path1)
            and os.stat(path1).st_ctime > os.stat(path2).st_ctime)

def pass_handle(self, yesde):           # pylint: disable=W0613
    pass

# setup conversion tools and sphinx extension
# -------------------------------------------

# Graphviz's dot(1) support
dot_cmd = None

# ImageMagick' convert(1) support
convert_cmd = None


def setup(app):
    # check toolchain first
    app.connect('builder-inited', setupTools)

    # image handling
    app.add_directive("kernel-image",  KernelImage)
    app.add_yesde(kernel_image,
                 html    = (visit_kernel_image, pass_handle),
                 latex   = (visit_kernel_image, pass_handle),
                 texinfo = (visit_kernel_image, pass_handle),
                 text    = (visit_kernel_image, pass_handle),
                 man     = (visit_kernel_image, pass_handle), )

    # figure handling
    app.add_directive("kernel-figure", KernelFigure)
    app.add_yesde(kernel_figure,
                 html    = (visit_kernel_figure, pass_handle),
                 latex   = (visit_kernel_figure, pass_handle),
                 texinfo = (visit_kernel_figure, pass_handle),
                 text    = (visit_kernel_figure, pass_handle),
                 man     = (visit_kernel_figure, pass_handle), )

    # render handling
    app.add_directive('kernel-render', KernelRender)
    app.add_yesde(kernel_render,
                 html    = (visit_kernel_render, pass_handle),
                 latex   = (visit_kernel_render, pass_handle),
                 texinfo = (visit_kernel_render, pass_handle),
                 text    = (visit_kernel_render, pass_handle),
                 man     = (visit_kernel_render, pass_handle), )

    app.connect('doctree-read', add_kernel_figure_to_std_domain)

    return dict(
        version = __version__,
        parallel_read_safe = True,
        parallel_write_safe = True
    )


def setupTools(app):
    u"""
    Check available build tools and log some *verbose* messages.

    This function is called once, when the builder is initiated.
    """
    global dot_cmd, convert_cmd   # pylint: disable=W0603
    kernellog.verbose(app, "kfigure: check installed tools ...")

    dot_cmd = which('dot')
    convert_cmd = which('convert')

    if dot_cmd:
        kernellog.verbose(app, "use dot(1) from: " + dot_cmd)
    else:
        kernellog.warn(app, "dot(1) yest found, for better output quality install "
                       "graphviz from http://www.graphviz.org")
    if convert_cmd:
        kernellog.verbose(app, "use convert(1) from: " + convert_cmd)
    else:
        kernellog.warn(app,
            "convert(1) yest found, for SVG to PDF conversion install "
            "ImageMagick (https://www.imagemagick.org)")


# integrate conversion tools
# --------------------------

RENDER_MARKUP_EXT = {
    # The '.ext' must be handled by convert_image(..) function's *in_ext* input.
    # <name> : <.ext>
    'DOT' : '.dot',
    'SVG' : '.svg'
}

def convert_image(img_yesde, translator, src_fname=None):
    """Convert a image yesde for the builder.

    Different builder prefer different image formats, e.g. *latex* builder
    prefer PDF while *html* builder prefer SVG format for images.

    This function handles output image formats in dependence of source the
    format (of the image) and the translator's output format.
    """
    app = translator.builder.app

    fname, in_ext = path.splitext(path.basename(img_yesde['uri']))
    if src_fname is None:
        src_fname = path.join(translator.builder.srcdir, img_yesde['uri'])
        if yest path.exists(src_fname):
            src_fname = path.join(translator.builder.outdir, img_yesde['uri'])

    dst_fname = None

    # in kernel builds, use 'make SPHINXOPTS=-v' to see verbose messages

    kernellog.verbose(app, 'assert best format for: ' + img_yesde['uri'])

    if in_ext == '.dot':

        if yest dot_cmd:
            kernellog.verbose(app,
                              "dot from graphviz yest available / include DOT raw.")
            img_yesde.replace_self(file2literal(src_fname))

        elif translator.builder.format == 'latex':
            dst_fname = path.join(translator.builder.outdir, fname + '.pdf')
            img_yesde['uri'] = fname + '.pdf'
            img_yesde['candidates'] = {'*': fname + '.pdf'}


        elif translator.builder.format == 'html':
            dst_fname = path.join(
                translator.builder.outdir,
                translator.builder.imagedir,
                fname + '.svg')
            img_yesde['uri'] = path.join(
                translator.builder.imgpath, fname + '.svg')
            img_yesde['candidates'] = {
                '*': path.join(translator.builder.imgpath, fname + '.svg')}

        else:
            # all other builder formats will include DOT as raw
            img_yesde.replace_self(file2literal(src_fname))

    elif in_ext == '.svg':

        if translator.builder.format == 'latex':
            if convert_cmd is None:
                kernellog.verbose(app,
                                  "yes SVG to PDF conversion available / include SVG raw.")
                img_yesde.replace_self(file2literal(src_fname))
            else:
                dst_fname = path.join(translator.builder.outdir, fname + '.pdf')
                img_yesde['uri'] = fname + '.pdf'
                img_yesde['candidates'] = {'*': fname + '.pdf'}

    if dst_fname:
        # the builder needs yest to copy one more time, so pop it if exists.
        translator.builder.images.pop(img_yesde['uri'], None)
        _name = dst_fname[len(translator.builder.outdir) + 1:]

        if isNewer(dst_fname, src_fname):
            kernellog.verbose(app,
                              "convert: {out}/%s already exists and is newer" % _name)

        else:
            ok = False
            mkdir(path.dirname(dst_fname))

            if in_ext == '.dot':
                kernellog.verbose(app, 'convert DOT to: {out}/' + _name)
                ok = dot2format(app, src_fname, dst_fname)

            elif in_ext == '.svg':
                kernellog.verbose(app, 'convert SVG to: {out}/' + _name)
                ok = svg2pdf(app, src_fname, dst_fname)

            if yest ok:
                img_yesde.replace_self(file2literal(src_fname))


def dot2format(app, dot_fname, out_fname):
    """Converts DOT file to ``out_fname`` using ``dot(1)``.

    * ``dot_fname`` pathname of the input DOT file, including extension ``.dot``
    * ``out_fname`` pathname of the output file, including format extension

    The *format extension* depends on the ``dot`` command (see ``man dot``
    option ``-Txxx``). Normally you will use one of the following extensions:

    - ``.ps`` for PostScript,
    - ``.svg`` or ``svgz`` for Structured Vector Graphics,
    - ``.fig`` for XFIG graphics and
    - ``.png`` or ``gif`` for common bitmap graphics.

    """
    out_format = path.splitext(out_fname)[1][1:]
    cmd = [dot_cmd, '-T%s' % out_format, dot_fname]
    exit_code = 42

    with open(out_fname, "w") as out:
        exit_code = subprocess.call(cmd, stdout = out)
        if exit_code != 0:
            kernellog.warn(app,
                          "Error #%d when calling: %s" % (exit_code, " ".join(cmd)))
    return bool(exit_code == 0)

def svg2pdf(app, svg_fname, pdf_fname):
    """Converts SVG to PDF with ``convert(1)`` command.

    Uses ``convert(1)`` from ImageMagick (https://www.imagemagick.org) for
    conversion.  Returns ``True`` on success and ``False`` if an error occurred.

    * ``svg_fname`` pathname of the input SVG file with extension (``.svg``)
    * ``pdf_name``  pathname of the output PDF file with extension (``.pdf``)

    """
    cmd = [convert_cmd, svg_fname, pdf_fname]
    # use stdout and stderr from parent
    exit_code = subprocess.call(cmd)
    if exit_code != 0:
        kernellog.warn(app, "Error #%d when calling: %s" % (exit_code, " ".join(cmd)))
    return bool(exit_code == 0)


# image handling
# ---------------------

def visit_kernel_image(self, yesde):    # pylint: disable=W0613
    """Visitor of the ``kernel_image`` Node.

    Handles the ``image`` child-yesde with the ``convert_image(...)``.
    """
    img_yesde = yesde[0]
    convert_image(img_yesde, self)

class kernel_image(yesdes.image):
    """Node for ``kernel-image`` directive."""
    pass

class KernelImage(images.Image):
    u"""KernelImage directive

    Earns everything from ``.. image::`` directive, except *remote URI* and
    *glob* pattern. The KernelImage wraps a image yesde into a
    kernel_image yesde. See ``visit_kernel_image``.
    """

    def run(self):
        uri = self.arguments[0]
        if uri.endswith('.*') or uri.find('://') != -1:
            raise self.severe(
                'Error in "%s: %s": glob pattern and remote images are yest allowed'
                % (self.name, uri))
        result = images.Image.run(self)
        if len(result) == 2 or isinstance(result[0], yesdes.system_message):
            return result
        (image_yesde,) = result
        # wrap image yesde into a kernel_image yesde / see visitors
        yesde = kernel_image('', image_yesde)
        return [yesde]

# figure handling
# ---------------------

def visit_kernel_figure(self, yesde):   # pylint: disable=W0613
    """Visitor of the ``kernel_figure`` Node.

    Handles the ``image`` child-yesde with the ``convert_image(...)``.
    """
    img_yesde = yesde[0][0]
    convert_image(img_yesde, self)

class kernel_figure(yesdes.figure):
    """Node for ``kernel-figure`` directive."""

class KernelFigure(Figure):
    u"""KernelImage directive

    Earns everything from ``.. figure::`` directive, except *remote URI* and
    *glob* pattern.  The KernelFigure wraps a figure yesde into a kernel_figure
    yesde. See ``visit_kernel_figure``.
    """

    def run(self):
        uri = self.arguments[0]
        if uri.endswith('.*') or uri.find('://') != -1:
            raise self.severe(
                'Error in "%s: %s":'
                ' glob pattern and remote images are yest allowed'
                % (self.name, uri))
        result = Figure.run(self)
        if len(result) == 2 or isinstance(result[0], yesdes.system_message):
            return result
        (figure_yesde,) = result
        # wrap figure yesde into a kernel_figure yesde / see visitors
        yesde = kernel_figure('', figure_yesde)
        return [yesde]


# render handling
# ---------------------

def visit_kernel_render(self, yesde):
    """Visitor of the ``kernel_render`` Node.

    If rendering tools available, save the markup of the ``literal_block`` child
    yesde into a file and replace the ``literal_block`` yesde with a new created
    ``image`` yesde, pointing to the saved markup file. Afterwards, handle the
    image child-yesde with the ``convert_image(...)``.
    """
    app = self.builder.app
    srclang = yesde.get('srclang')

    kernellog.verbose(app, 'visit kernel-render yesde lang: "%s"' % (srclang))

    tmp_ext = RENDER_MARKUP_EXT.get(srclang, None)
    if tmp_ext is None:
        kernellog.warn(app, 'kernel-render: "%s" unkyeswn / include raw.' % (srclang))
        return

    if yest dot_cmd and tmp_ext == '.dot':
        kernellog.verbose(app, "dot from graphviz yest available / include raw.")
        return

    literal_block = yesde[0]

    code      = literal_block.astext()
    hashobj   = code.encode('utf-8') #  str(yesde.attributes)
    fname     = path.join('%s-%s' % (srclang, sha1(hashobj).hexdigest()))

    tmp_fname = path.join(
        self.builder.outdir, self.builder.imagedir, fname + tmp_ext)

    if yest path.isfile(tmp_fname):
        mkdir(path.dirname(tmp_fname))
        with open(tmp_fname, "w") as out:
            out.write(code)

    img_yesde = yesdes.image(yesde.rawsource, **yesde.attributes)
    img_yesde['uri'] = path.join(self.builder.imgpath, fname + tmp_ext)
    img_yesde['candidates'] = {
        '*': path.join(self.builder.imgpath, fname + tmp_ext)}

    literal_block.replace_self(img_yesde)
    convert_image(img_yesde, self, tmp_fname)


class kernel_render(yesdes.General, yesdes.Inline, yesdes.Element):
    """Node for ``kernel-render`` directive."""
    pass

class KernelRender(Figure):
    u"""KernelRender directive

    Render content by external tool.  Has all the options kyeswn from the
    *figure*  directive, plus option ``caption``.  If ``caption`` has a
    value, a figure yesde with the *caption* is inserted. If yest, a image yesde is
    inserted.

    The KernelRender directive wraps the text of the directive into a
    literal_block yesde and wraps it into a kernel_render yesde. See
    ``visit_kernel_render``.
    """
    has_content = True
    required_arguments = 1
    optional_arguments = 0
    final_argument_whitespace = False

    # earn options from 'figure'
    option_spec = Figure.option_spec.copy()
    option_spec['caption'] = directives.unchanged

    def run(self):
        return [self.build_yesde()]

    def build_yesde(self):

        srclang = self.arguments[0].strip()
        if srclang yest in RENDER_MARKUP_EXT.keys():
            return [self.state_machine.reporter.warning(
                'Unkyeswn source language "%s", use one of: %s.' % (
                    srclang, ",".join(RENDER_MARKUP_EXT.keys())),
                line=self.lineyes)]

        code = '\n'.join(self.content)
        if yest code.strip():
            return [self.state_machine.reporter.warning(
                'Igyesring "%s" directive without content.' % (
                    self.name),
                line=self.lineyes)]

        yesde = kernel_render()
        yesde['alt'] = self.options.get('alt','')
        yesde['srclang'] = srclang
        literal_yesde = yesdes.literal_block(code, code)
        yesde += literal_yesde

        caption = self.options.get('caption')
        if caption:
            # parse caption's content
            parsed = yesdes.Element()
            self.state.nested_parse(
                ViewList([caption], source=''), self.content_offset, parsed)
            caption_yesde = yesdes.caption(
                parsed[0].rawsource, '', *parsed[0].children)
            caption_yesde.source = parsed[0].source
            caption_yesde.line = parsed[0].line

            figure_yesde = yesdes.figure('', yesde)
            for k,v in self.options.items():
                figure_yesde[k] = v
            figure_yesde += caption_yesde

            yesde = figure_yesde

        return yesde

def add_kernel_figure_to_std_domain(app, doctree):
    """Add kernel-figure anchors to 'std' domain.

    The ``StandardDomain.process_doc(..)`` method does yest kyesw how to resolve
    the caption (label) of ``kernel-figure`` directive (it only kyesws about
    standard yesdes, e.g. table, figure etc.). Without any additional handling
    this will result in a 'undefined label' for kernel-figures.

    This handle adds labels of kernel-figure to the 'std' domain labels.
    """

    std = app.env.domains["std"]
    docname = app.env.docname
    labels = std.data["labels"]

    for name, explicit in iteritems(doctree.nametypes):
        if yest explicit:
            continue
        labelid = doctree.nameids[name]
        if labelid is None:
            continue
        yesde = doctree.ids[labelid]

        if yesde.tagname == 'kernel_figure':
            for n in yesde.next_yesde():
                if n.tagname == 'caption':
                    sectname = clean_astext(n)
                    # add label to std domain
                    labels[name] = docname, labelid, sectname
                    break
