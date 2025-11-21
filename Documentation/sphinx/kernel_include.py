#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# pylint: disable=R0903, R0912, R0914, R0915, C0209,W0707


"""
Implementation of the ``kernel-include`` reST-directive.

:copyright:  Copyright (C) 2016  Markus Heiser
:license:    GPL Version 2, June 1991 see linux/COPYING for details.

The ``kernel-include`` reST-directive is a replacement for the ``include``
directive. The ``kernel-include`` directive expand environment variables in
the path name and allows to include files from arbitrary locations.

.. hint::

    Including files from arbitrary locations (e.g. from ``/etc``) is a
    security risk for builders. This is why the ``include`` directive from
    docutils *prohibit* pathnames pointing to locations *above* the filesystem
    tree where the reST document with the include directive is placed.

Substrings of the form $name or ${name} are replaced by the value of
environment variable name. Malformed variable names and references to
non-existing variables are left unchanged.

**Supported Sphinx Include Options**:

:param literal:
    If present, the included file is inserted as a literal block.

:param code:
    Specify the language for syntax highlighting (e.g., 'c', 'python').

:param encoding:
    Specify the encoding of the included file (default: 'utf-8').

:param tab-width:
    Specify the number of spaces that a tab represents.

:param start-line:
    Line number at which to start including the file (1-based).

:param end-line:
    Line number at which to stop including the file (inclusive).

:param start-after:
    Include lines after the first line matching this text.

:param end-before:
    Include lines before the first line matching this text.

:param number-lines:
    Number the included lines (integer specifies start number).
    Only effective with 'literal' or 'code' options.

:param class:
    Specify HTML class attribute for the included content.

**Kernel-specific Extensions**:

:param generate-cross-refs:
    If present, instead of directly including the file, it calls
    ParseDataStructs() to convert C data structures into cross-references
    that link to comprehensive documentation in other ReST files.

:param exception-file:
    (Used with generate-cross-refs)

    Path to a file containing rules for handling special cases:
    - Ignore specific C data structures
    - Use alternative reference names
    - Specify different reference types

:param warn-broken:
    (Used with generate-cross-refs)

    Enables warnings when auto-generated cross-references don't point to
    existing documentation targets.
"""

# ==============================================================================
# imports
# ==============================================================================

import os.path
import re
import sys

from docutils import io, nodes, statemachine
from docutils.statemachine import ViewList
from docutils.parsers.rst import Directive, directives
from docutils.parsers.rst.directives.body import CodeBlock, NumberLines

from sphinx.util import logging

srctree = os.path.abspath(os.environ["srctree"])
sys.path.insert(0, os.path.join(srctree, "tools/docs/lib"))

from parse_data_structs import ParseDataStructs

__version__ = "1.0"
logger = logging.getLogger(__name__)

RE_DOMAIN_REF = re.compile(r'\\ :(ref|c:type|c:func):`([^<`]+)(?:<([^>]+)>)?`\\')
RE_SIMPLE_REF = re.compile(r'`([^`]+)`')

def ErrorString(exc):  # Shamelessly stolen from docutils
    return f'{exc.__class__.__name}: {exc}'


# ==============================================================================
class KernelInclude(Directive):
    """
    KernelInclude (``kernel-include``) directive

    Most of the stuff here came from Include directive defined at:
        docutils/parsers/rst/directives/misc.py

    Yet, overriding the class don't has any benefits: the original class
    only have run() and argument list. Not all of them are implemented,
    when checked against latest Sphinx version, as with time more arguments
    were added.

    So, keep its own list of supported arguments
    """

    required_arguments = 1
    optional_arguments = 0
    final_argument_whitespace = True
    option_spec = {
        'literal': directives.flag,
        'code': directives.unchanged,
        'encoding': directives.encoding,
        'tab-width': int,
        'start-line': int,
        'end-line': int,
        'start-after': directives.unchanged_required,
        'end-before': directives.unchanged_required,
        # ignored except for 'literal' or 'code':
        'number-lines': directives.unchanged,  # integer or None
        'class': directives.class_option,

        # Arguments that aren't from Sphinx Include directive
        'generate-cross-refs': directives.flag,
        'warn-broken': directives.flag,
        'toc': directives.flag,
        'exception-file': directives.unchanged,
    }

    def read_rawtext(self, path, encoding):
            """Read and process file content with error handling"""
            try:
                self.state.document.settings.record_dependencies.add(path)
                include_file = io.FileInput(source_path=path,
                                            encoding=encoding,
                                            error_handler=self.state.document.settings.input_encoding_error_handler)
            except UnicodeEncodeError:
                raise self.severe('Problems with directive path:\n'
                                'Cannot encode input file path "%s" '
                                '(wrong locale?).' % path)
            except IOError as error:
                raise self.severe('Problems with directive path:\n%s.' % ErrorString(error))

            try:
                return include_file.read()
            except UnicodeError as error:
                raise self.severe('Problem with directive:\n%s' % ErrorString(error))

    def apply_range(self, rawtext):
        """
        Handles start-line, end-line, start-after and end-before parameters
        """

        # Get to-be-included content
        startline = self.options.get('start-line', None)
        endline = self.options.get('end-line', None)
        try:
            if startline or (endline is not None):
                lines = rawtext.splitlines()
                rawtext = '\n'.join(lines[startline:endline])
        except UnicodeError as error:
            raise self.severe(f'Problem with "{self.name}" directive:\n'
                              + io.error_string(error))
        # start-after/end-before: no restrictions on newlines in match-text,
        # and no restrictions on matching inside lines vs. line boundaries
        after_text = self.options.get("start-after", None)
        if after_text:
            # skip content in rawtext before *and incl.* a matching text
            after_index = rawtext.find(after_text)
            if after_index < 0:
                raise self.severe('Problem with "start-after" option of "%s" '
                                  "directive:\nText not found." % self.name)
            rawtext = rawtext[after_index + len(after_text) :]
        before_text = self.options.get("end-before", None)
        if before_text:
            # skip content in rawtext after *and incl.* a matching text
            before_index = rawtext.find(before_text)
            if before_index < 0:
                raise self.severe('Problem with "end-before" option of "%s" '
                                  "directive:\nText not found." % self.name)
            rawtext = rawtext[:before_index]

        return rawtext

    def xref_text(self, env, path, tab_width):
        """
        Read and add contents from a C file parsed to have cross references.

        There are two types of supported output here:
        - A C source code with cross-references;
        - a TOC table containing cross references.
        """
        parser = ParseDataStructs()
        parser.parse_file(path)

        if 'exception-file' in self.options:
            source_dir = os.path.dirname(os.path.abspath(
                self.state_machine.input_lines.source(
                    self.lineno - self.state_machine.input_offset - 1)))
            exceptions_file = os.path.join(source_dir, self.options['exception-file'])
            parser.process_exceptions(exceptions_file)

        # Store references on a symbol dict to be used at check time
        if 'warn-broken' in self.options:
            env._xref_files.add(path)

        if "toc" not in self.options:

            rawtext = ".. parsed-literal::\n\n" + parser.gen_output()
            self.apply_range(rawtext)

            include_lines = statemachine.string2lines(rawtext, tab_width,
                                                      convert_whitespace=True)

            # Sphinx always blame the ".. <directive>", so placing
            # line numbers here won't make any difference

            self.state_machine.insert_input(include_lines, path)
            return []

        # TOC output is a ReST file, not a literal. So, we can add line
        # numbers

        rawtext = parser.gen_toc()

        include_lines = statemachine.string2lines(rawtext, tab_width,
                                                  convert_whitespace=True)

        # Append line numbers data

        startline = self.options.get('start-line', None)

        result = ViewList()
        if startline and startline > 0:
            offset = startline - 1
        else:
            offset = 0

        for ln, line in enumerate(include_lines, start=offset):
            result.append(line, path, ln)

        self.state_machine.insert_input(result, path)

        return []

    def literal(self, path, tab_width, rawtext):
        """Output a literal block"""

        # Convert tabs to spaces, if `tab_width` is positive.
        if tab_width >= 0:
            text = rawtext.expandtabs(tab_width)
        else:
            text = rawtext
        literal_block = nodes.literal_block(rawtext, source=path,
                                            classes=self.options.get("class", []))
        literal_block.line = 1
        self.add_name(literal_block)
        if "number-lines" in self.options:
            try:
                startline = int(self.options["number-lines"] or 1)
            except ValueError:
                raise self.error(":number-lines: with non-integer start value")
            endline = startline + len(include_lines)
            if text.endswith("\n"):
                text = text[:-1]
            tokens = NumberLines([([], text)], startline, endline)
            for classes, value in tokens:
                if classes:
                    literal_block += nodes.inline(value, value,
                                                    classes=classes)
                else:
                    literal_block += nodes.Text(value, value)
        else:
            literal_block += nodes.Text(text, text)
        return [literal_block]

    def code(self, path, tab_width):
        """Output a code block"""

        include_lines = statemachine.string2lines(rawtext, tab_width,
                                                  convert_whitespace=True)

        self.options["source"] = path
        codeblock = CodeBlock(self.name,
                                [self.options.pop("code")],  # arguments
                                self.options,
                                include_lines,
                                self.lineno,
                                self.content_offset,
                                self.block_text,
                                self.state,
                                self.state_machine)
        return codeblock.run()

    def run(self):
        """Include a file as part of the content of this reST file."""
        env = self.state.document.settings.env

        #
        # The include logic accepts only patches relative to the
        # Kernel source tree.  The logic does check it to prevent
        # directory traverse issues.
        #

        srctree = os.path.abspath(os.environ["srctree"])

        path = os.path.expandvars(self.arguments[0])
        src_path = os.path.join(srctree, path)

        if os.path.isfile(src_path):
            base = srctree
            path = src_path
        else:
            raise self.warning(f'File "%s" doesn\'t exist', path)

        abs_base = os.path.abspath(base)
        abs_full_path = os.path.abspath(os.path.join(base, path))

        try:
            if os.path.commonpath([abs_full_path, abs_base]) != abs_base:
                raise self.severe('Problems with "%s" directive, prohibited path: %s' %
                                  (self.name, path))
        except ValueError:
            # Paths don't have the same drive (Windows) or other incompatibility
            raise self.severe('Problems with "%s" directive, invalid path: %s' %
                            (self.name, path))

        self.arguments[0] = path

        #
        # Add path location to Sphinx dependencies to ensure proper cache
        # invalidation check.
        #

        env.note_dependency(os.path.abspath(path))

        if not self.state.document.settings.file_insertion_enabled:
            raise self.warning('"%s" directive disabled.' % self.name)
        source = self.state_machine.input_lines.source(self.lineno -
                                                       self.state_machine.input_offset - 1)
        source_dir = os.path.dirname(os.path.abspath(source))
        path = directives.path(self.arguments[0])
        if path.startswith("<") and path.endswith(">"):
            path = os.path.join(self.standard_include_path, path[1:-1])
        path = os.path.normpath(os.path.join(source_dir, path))

        # HINT: this is the only line I had to change / commented out:
        # path = utils.relative_path(None, path)

        encoding = self.options.get("encoding",
                                    self.state.document.settings.input_encoding)
        tab_width = self.options.get("tab-width",
                                     self.state.document.settings.tab_width)

        # Get optional arguments to related to cross-references generation
        if "generate-cross-refs" in self.options:
            return self.xref_text(env, path, tab_width)

        rawtext = self.read_rawtext(path, encoding)
        rawtext = self.apply_range(rawtext)

        if "code" in self.options:
            return self.code(path, tab_width, rawtext)

        return self.literal(path, tab_width, rawtext)

# ==============================================================================

reported = set()

def check_missing_refs(app, env, node, contnode):
    """Check broken refs for the files it creates xrefs"""
    if not node.source:
        return None

    try:
        xref_files = env._xref_files
    except AttributeError:
        logger.critical("FATAL: _xref_files not initialized!")
        raise

    # Only show missing references for kernel-include reference-parsed files
    if node.source not in xref_files:
        return None

    target = node.get('reftarget', '')
    domain = node.get('refdomain', 'std')
    reftype = node.get('reftype', '')

    msg = f"can't link to: {domain}:{reftype}:: {target}"

    # Don't duplicate warnings
    data = (node.source, msg)
    if data in reported:
        return None
    reported.add(data)

    logger.warning(msg, location=node, type='ref', subtype='missing')

    return None

def merge_xref_info(app, env, docnames, other):
    """
    As each process modify env._xref_files, we need to merge them back.
    """
    if not hasattr(other, "_xref_files"):
        return
    env._xref_files.update(getattr(other, "_xref_files", set()))

def init_xref_docs(app, env, docnames):
    """Initialize a list of files that we're generating cross references¨"""
    app.env._xref_files = set()

# ==============================================================================

def setup(app):
    """Setup Sphinx exension"""

    app.connect("env-before-read-docs", init_xref_docs)
    app.connect("env-merge-info", merge_xref_info)
    app.add_directive("kernel-include", KernelInclude)
    app.connect("missing-reference", check_missing_refs)

    return {
        "version": __version__,
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
