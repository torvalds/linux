#!/usr/bin/env python3
# -*- coding: utf-8; mode: python -*-
# pylint: disable=R0903, C0330, R0914, R0912, E0401

"""
    kernel-include
    ~~~~~~~~~~~~~~

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
"""

# ==============================================================================
# imports
# ==============================================================================

import os.path

from docutils import io, nodes, statemachine
from docutils.utils.error_reporting import SafeString, ErrorString
from docutils.parsers.rst import directives
from docutils.parsers.rst.directives.body import CodeBlock, NumberLines
from docutils.parsers.rst.directives.misc import Include

__version__  = '1.0'

# ==============================================================================
def setup(app):
# ==============================================================================

    app.add_directive("kernel-include", KernelInclude)
    return dict(
        version = __version__,
        parallel_read_safe = True,
        parallel_write_safe = True
    )

# ==============================================================================
class KernelInclude(Include):
# ==============================================================================

    """KernelInclude (``kernel-include``) directive"""

    def run(self):
        env = self.state.document.settings.env
        path = os.path.realpath(
            os.path.expandvars(self.arguments[0]))

        # to get a bit security back, prohibit /etc:
        if path.startswith(os.sep + "etc"):
            raise self.severe(
                'Problems with "%s" directive, prohibited path: %s'
                % (self.name, path))

        self.arguments[0] = path

        env.note_dependency(os.path.abspath(path))

        #return super(KernelInclude, self).run() # won't work, see HINTs in _run()
        return self._run()

    def _run(self):
        """Include a file as part of the content of this reST file."""

        # HINT: I had to copy&paste the whole Include.run method. I'am not happy
        # with this, but due to security reasons, the Include.run method does
        # not allow absolute or relative pathnames pointing to locations *above*
        # the filesystem tree where the reST document is placed.

        if not self.state.document.settings.file_insertion_enabled:
            raise self.warning('"%s" directive disabled.' % self.name)
        source = self.state_machine.input_lines.source(
            self.lineno - self.state_machine.input_offset - 1)
        source_dir = os.path.dirname(os.path.abspath(source))
        path = directives.path(self.arguments[0])
        if path.startswith('<') and path.endswith('>'):
            path = os.path.join(self.standard_include_path, path[1:-1])
        path = os.path.normpath(os.path.join(source_dir, path))

        # HINT: this is the only line I had to change / commented out:
        #path = utils.relative_path(None, path)

        encoding = self.options.get(
            'encoding', self.state.document.settings.input_encoding)
        e_handler=self.state.document.settings.input_encoding_error_handler
        tab_width = self.options.get(
            'tab-width', self.state.document.settings.tab_width)
        try:
            self.state.document.settings.record_dependencies.add(path)
            include_file = io.FileInput(source_path=path,
                                        encoding=encoding,
                                        error_handler=e_handler)
        except UnicodeEncodeError as error:
            raise self.severe('Problems with "%s" directive path:\n'
                              'Cannot encode input file path "%s" '
                              '(wrong locale?).' %
                              (self.name, SafeString(path)))
        except IOError as error:
            raise self.severe('Problems with "%s" directive path:\n%s.' %
                      (self.name, ErrorString(error)))
        startline = self.options.get('start-line', None)
        endline = self.options.get('end-line', None)
        try:
            if startline or (endline is not None):
                lines = include_file.readlines()
                rawtext = ''.join(lines[startline:endline])
            else:
                rawtext = include_file.read()
        except UnicodeError as error:
            raise self.severe('Problem with "%s" directive:\n%s' %
                              (self.name, ErrorString(error)))
        # start-after/end-before: no restrictions on newlines in match-text,
        # and no restrictions on matching inside lines vs. line boundaries
        after_text = self.options.get('start-after', None)
        if after_text:
            # skip content in rawtext before *and incl.* a matching text
            after_index = rawtext.find(after_text)
            if after_index < 0:
                raise self.severe('Problem with "start-after" option of "%s" '
                                  'directive:\nText not found.' % self.name)
            rawtext = rawtext[after_index + len(after_text):]
        before_text = self.options.get('end-before', None)
        if before_text:
            # skip content in rawtext after *and incl.* a matching text
            before_index = rawtext.find(before_text)
            if before_index < 0:
                raise self.severe('Problem with "end-before" option of "%s" '
                                  'directive:\nText not found.' % self.name)
            rawtext = rawtext[:before_index]

        include_lines = statemachine.string2lines(rawtext, tab_width,
                                                  convert_whitespace=True)
        if 'literal' in self.options:
            # Convert tabs to spaces, if `tab_width` is positive.
            if tab_width >= 0:
                text = rawtext.expandtabs(tab_width)
            else:
                text = rawtext
            literal_block = nodes.literal_block(rawtext, source=path,
                                    classes=self.options.get('class', []))
            literal_block.line = 1
            self.add_name(literal_block)
            if 'number-lines' in self.options:
                try:
                    startline = int(self.options['number-lines'] or 1)
                except ValueError:
                    raise self.error(':number-lines: with non-integer '
                                     'start value')
                endline = startline + len(include_lines)
                if text.endswith('\n'):
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
        if 'code' in self.options:
            self.options['source'] = path
            codeblock = CodeBlock(self.name,
                                  [self.options.pop('code')], # arguments
                                  self.options,
                                  include_lines, # content
                                  self.lineno,
                                  self.content_offset,
                                  self.block_text,
                                  self.state,
                                  self.state_machine)
            return codeblock.run()
        self.state_machine.insert_input(include_lines, path)
        return []
