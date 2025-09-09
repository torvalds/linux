#!/usr/bin/env python
# SPDX-License-Identifier: GPL-2.0
# -*- coding: utf-8; mode: python -*-
# pylint: disable=R0903, C0330, R0914, R0912, E0401

"""
    maintainers-include
    ~~~~~~~~~~~~~~~~~~~

    Implementation of the ``maintainers-include`` reST-directive.

    :copyright:  Copyright (C) 2019  Kees Cook <keescook@chromium.org>
    :license:    GPL Version 2, June 1991 see linux/COPYING for details.

    The ``maintainers-include`` reST-directive performs extensive parsing
    specific to the Linux kernel's standard "MAINTAINERS" file, in an
    effort to avoid needing to heavily mark up the original plain text.
"""

import sys
import re
import os.path

from docutils import statemachine
from docutils.parsers.rst import Directive
from docutils.parsers.rst.directives.misc import Include

def ErrorString(exc):  # Shamelessly stolen from docutils
    return f'{exc.__class__.__name}: {exc}'

__version__  = '1.0'

def setup(app):
    app.add_directive("maintainers-include", MaintainersInclude)
    return dict(
        version = __version__,
        parallel_read_safe = True,
        parallel_write_safe = True
    )

class MaintainersInclude(Include):
    """MaintainersInclude (``maintainers-include``) directive"""
    required_arguments = 0

    def parse_maintainers(self, path):
        """Parse all the MAINTAINERS lines into ReST for human-readability"""

        result = list()
        result.append(".. _maintainers:")
        result.append("")

        # Poor man's state machine.
        descriptions = False
        maintainers = False
        subsystems = False

        # Field letter to field name mapping.
        field_letter = None
        fields = dict()

        prev = None
        field_prev = ""
        field_content = ""

        for line in open(path):
            # Have we reached the end of the preformatted Descriptions text?
            if descriptions and line.startswith('Maintainers'):
                descriptions = False
                # Ensure a blank line following the last "|"-prefixed line.
                result.append("")

            # Start subsystem processing? This is to skip processing the text
            # between the Maintainers heading and the first subsystem name.
            if maintainers and not subsystems:
                if re.search('^[A-Z0-9]', line):
                    subsystems = True

            # Drop needless input whitespace.
            line = line.rstrip()

            # Linkify all non-wildcard refs to ReST files in Documentation/.
            pat = r'(Documentation/([^\s\?\*]*)\.rst)'
            m = re.search(pat, line)
            if m:
                # maintainers.rst is in a subdirectory, so include "../".
                line = re.sub(pat, ':doc:`%s <../%s>`' % (m.group(2), m.group(2)), line)

            # Check state machine for output rendering behavior.
            output = None
            if descriptions:
                # Escape the escapes in preformatted text.
                output = "| %s" % (line.replace("\\", "\\\\"))
                # Look for and record field letter to field name mappings:
                #   R: Designated *reviewer*: FullName <address@domain>
                m = re.search(r"\s(\S):\s", line)
                if m:
                    field_letter = m.group(1)
                if field_letter and not field_letter in fields:
                    m = re.search(r"\*([^\*]+)\*", line)
                    if m:
                        fields[field_letter] = m.group(1)
            elif subsystems:
                # Skip empty lines: subsystem parser adds them as needed.
                if len(line) == 0:
                    continue
                # Subsystem fields are batched into "field_content"
                if line[1] != ':':
                    # Render a subsystem entry as:
                    #   SUBSYSTEM NAME
                    #   ~~~~~~~~~~~~~~

                    # Flush pending field content.
                    output = field_content + "\n\n"
                    field_content = ""

                    # Collapse whitespace in subsystem name.
                    heading = re.sub(r"\s+", " ", line)
                    output = output + "%s\n%s" % (heading, "~" * len(heading))
                    field_prev = ""
                else:
                    # Render a subsystem field as:
                    #   :Field: entry
                    #           entry...
                    field, details = line.split(':', 1)
                    details = details.strip()

                    # Mark paths (and regexes) as literal text for improved
                    # readability and to escape any escapes.
                    if field in ['F', 'N', 'X', 'K']:
                        # But only if not already marked :)
                        if not ':doc:' in details:
                            details = '``%s``' % (details)

                    # Comma separate email field continuations.
                    if field == field_prev and field_prev in ['M', 'R', 'L']:
                        field_content = field_content + ","

                    # Do not repeat field names, so that field entries
                    # will be collapsed together.
                    if field != field_prev:
                        output = field_content + "\n"
                        field_content = ":%s:" % (fields.get(field, field))
                    field_content = field_content + "\n\t%s" % (details)
                    field_prev = field
            else:
                output = line

            # Re-split on any added newlines in any above parsing.
            if output != None:
                for separated in output.split('\n'):
                    result.append(separated)

            # Update the state machine when we find heading separators.
            if line.startswith('----------'):
                if prev.startswith('Descriptions'):
                    descriptions = True
                if prev.startswith('Maintainers'):
                    maintainers = True

            # Retain previous line for state machine transitions.
            prev = line

        # Flush pending field contents.
        if field_content != "":
            for separated in field_content.split('\n'):
                result.append(separated)

        output = "\n".join(result)
        # For debugging the pre-rendered results...
        #print(output, file=open("/tmp/MAINTAINERS.rst", "w"))

        self.state_machine.insert_input(
          statemachine.string2lines(output), path)

    def run(self):
        """Include the MAINTAINERS file as part of this reST file."""
        if not self.state.document.settings.file_insertion_enabled:
            raise self.warning('"%s" directive disabled.' % self.name)

        # Walk up source path directories to find Documentation/../
        path = self.state_machine.document.attributes['source']
        path = os.path.realpath(path)
        tail = path
        while tail != "Documentation" and tail != "":
            (path, tail) = os.path.split(path)

        # Append "MAINTAINERS"
        path = os.path.join(path, "MAINTAINERS")

        try:
            self.state.document.settings.record_dependencies.add(path)
            lines = self.parse_maintainers(path)
        except IOError as error:
            raise self.severe('Problems with "%s" directive path:\n%s.' %
                      (self.name, ErrorString(error)))

        return []
