#!/usr/bin/env python
# SPDX-License-Identifier: GPL-2.0
# -*- coding: utf-8; mode: python -*-
# pylint: disable=C0209, C0301, E0401, R0022, R0902, R0903, R0912, R0914

"""
Implementation of the ``maintainers-include`` reST-directive.

:copyright:  Copyright (C) 2019  Kees Cook <keescook@chromium.org>
:license:    GPL Version 2, June 1991 see linux/COPYING for details.

The ``maintainers-include`` reST-directive performs extensive parsing
specific to the Linux kernel's standard "MAINTAINERS" file, in an
effort to avoid needing to heavily mark up the original plain text.
"""

import os.path
import re

from glob import glob

from docutils import statemachine
from docutils.parsers.rst.directives.misc import Include

#
# Base URL for intersphinx-like links to maintainer profiles
#
KERNELDOC_URL = "https://docs.kernel.org/"

__version__ = "1.0"

maint_parser = None  # pylint: disable=C0103

JS_FILTER = """
(function() {
  function filterTable(table) {
    const filter = document.getElementById("filter-table").value.trim();
    const rows = table.querySelectorAll("tbody tr");
    for (let i = 0; i < rows.length; i++) {
      const tds = rows[i].getElementsByTagName("td");
      let match = false;
      for (let j = 0; j < tds.length; j++) {
        const cellText = (tds[j].textContent || tds[j].innerText);
        if (cellText.includes(filter)) {
          match = true;
          break;
        }
      }
      rows[i].style.display = match ? "table-row" : "none";
    }
  }
  function addInput() {
    const table = document.getElementById("maintainers-table");
    if (!table) return;
    let input = document.getElementById("filter-table");
    if (!input) {
      const filt_div = document.createElement('div');
      filt_div.innerHTML = `
        <p>Filter:
          <input type="search" id="filter-table" placeholder="search string"/>
          subsystem or property (case-sensitive)
        </p>
      `;
      table.parentNode.insertBefore(filt_div, table);
      const input = document.getElementById("filter-table")
      input.addEventListener('input', () => filterTable(table));
    }
  }
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', addInput);
  } else {
    addInput();
  }
})();
"""


# Shamelessly stolen from docutils
def ErrorString(exc):  # pylint: disable=C0103, C0116
    return f"{exc.__class__.__name}: {exc}"  # pylint: disable=W0212

class MaintainersParser:
    """Parse MAINTAINERS file(s) content"""

    def __init__(self, base_dir, app_dir, path):
        self.path = path

        # Poor man's state machine.
        self.descriptions = False
        self.maintainers = False
        self.subsystems = False

        self.subsystem_name = None

        self.base_dir = base_dir
        self.app_dir = app_dir

        self.re_doc = re.compile(r'(Documentation/(\S*)\.rst)')

        #
        # Output variables with maintainers content to be stored
        #
        self.profile_toc = set()
        self.profile_entries = {}
        self.header = ""
        self.maint_entries = {}
        self.fields = {}

        prev = None
        with open(path, "r", encoding="utf-8") as fp:
            for line in fp:
                if self.descriptions:
                    self.parse_descriptions(line)
                elif self.maintainers and not self.subsystems:
                    if re.search('^[A-Z0-9]', line):
                        self.subsystems = True
                        self.parse_subsystems(line)
                    else:
                        self.header += line
                elif self.subsystems:
                    self.parse_subsystems(line)
                else:
                    self.header += line

                # Update the state machine when we find heading separators.
                if line.startswith("----------"):
                    if prev.startswith("Descriptions"):
                        self.descriptions = True
                    if prev.startswith("Maintainers"):
                        self.maintainers = True

                # Retain previous line for state machine transitions.
                prev = line

    def get_entries(self, text):
        """Generate refs to ReST files in Documentation/"""

        if "Documentation/" not in text:
            return None

        if "*" in text or "?" in text:
            m = self.re_doc.search(text)
            if not m:
                return None

            doc_list = glob(os.path.join(self.base_dir, m.group(1)))
        else:
            doc_list = [text]

        entries = {}
        for doc in doc_list:
            m = self.re_doc.search(doc)
            if m:
                fname = m.group(1)
                ename = m.group(2)

                entry = os.path.relpath(self.base_dir + fname, self.app_dir)
                entry = entry.removesuffix(".rst")

                if entry.startswith("../"):
                    html = KERNELDOC_URL + ename + ".html"
                    entries[entry] = f'`{ename} <{html}>`_'
                else:
                    entries[entry] = f':doc:`{ename} </{entry}>`'

        return entries

    def linkify(self, text):
        """Return a list of doc files converted to cross-references"""

        entries = self.get_entries(text)
        if not entries:
            return text

        return self.re_doc.sub(", ".join(entries.values()), text)

    def parse_descriptions(self, line):
        """Handle contents of the descriptions section."""

        # Have we reached the end of the preformatted Descriptions text?
        if line.startswith("Maintainers"):
            self.descriptions = False
            self.header += "\n" + line
            return

        # Look for and record field letter to field name mappings:
        #   R: Designated *reviewer*: FullName <address@domain>
        m = re.match(r"\s+(\S):\s+(\S+)", line)
        if m:
            field = m.group(1)
            details = m.group(2)

            if field not in self.fields:
                m = re.search(r"\*([^\*]+)\*", line)
                if m:
                    self.fields[field] = m.group(1)
            elif field in ['F', 'N', 'X', 'K']:
                line = line.replace(details, f'``{details}``')

        self.header += "| " + self.linkify(line)


    def parse_subsystems(self, line):
        """Handle contents of the per-subsystem sections."""

        # Drop needless input whitespace.
        line = line.rstrip()

        # Skip empty lines: subsystem parser adds them as needed.
        if not line:
            return

        if line[1] != ':':
            self.subsystem_name = re.sub(r"\s+", " ", self.linkify(line))
            return

        # Render a subsystem field as:
        #   :Field: entry
        #           entry...
        field, details = line.split(":", 1)
        details = details.strip()

        #
        # Handle profile entries - either as files or as https refs
        #
        if field == "P":
            entries = self.get_entries(details)
            if entries:
                for e, link in entries.items():
                    if "html" not in link:
                        self.profile_toc.add(e)

                    self.profile_entries[self.subsystem_name] = link

                details = ", ".join(entries.values())
            else:
                match = re.match(r"(https?://.*)", details)
                if match:
                    entry = match.group(1).strip()
                    self.profile_entries[self.subsystem_name] = entry
                else:
                    self.profile_entries[self.subsystem_name] = f"``{details}``"

                details = self.linkify(details)
        else:
            details = self.linkify(details)

        #
        # Mark paths (and regexes) as literal text for improved
        # readability and to escape any escapes.
        #
        if field in ['F', 'N', 'X', 'K']:
            # But only if not already marked :)
            if ':doc:' not in details and "http" not in details:
                details = '``%s``' % (details)

        if self.subsystem_name not in self.maint_entries:
            self.maint_entries[self.subsystem_name] = {}

        if field not in self.maint_entries[self.subsystem_name]:
            self.maint_entries[self.subsystem_name][field] = []

        self.maint_entries[self.subsystem_name][field].append(details)

        self.field_prev = field


class MaintainersInclude(Include):
    """MaintainersInclude (``maintainers-include``) directive"""

    required_arguments = 0

    def emit(self):
        """Parse all the MAINTAINERS lines into ReST for human-readability"""
        path = maint_parser.path
        output = ".. _maintainers:\n\n"
        output += maint_parser.header

        output += ".. _maintainers_table:\n\n"
        output += ".. flat-table::\n"
        output += "  :header-rows: 1\n\n"
        output += "  * - Subsystem\n"
        output += "    - Properties\n\n"

        self.state.document['maintainers_included'] = True

        # Keep the last entry ("THE REST") in the end
        entries = list(maint_parser.maint_entries.keys())
        entries = sorted(entries[:-1], key=str.casefold) + [entries[-1]]

        for name in entries:
            fields = maint_parser.maint_entries[name]
            output += f"  * - {name}\n"
            tag = "-"
            for field, lines in fields.items():
                field_name = maint_parser.fields.get(field, field)

                output += f"    {tag} :{field_name}:\n        "
                output += ",\n        ".join(lines) + "\n"
                tag = " "

            output += "\n"

        # For debugging the pre-rendered results...
        #print(output, file=open("/tmp/MAINTAINERS.rst", "w"))

        self.state.document.settings.record_dependencies.add(path)
        self.state_machine.insert_input(statemachine.string2lines(output), path)

    def run(self):
        """Include the MAINTAINERS file as part of this reST file."""
        if not self.state.document.settings.file_insertion_enabled:
            raise self.warning('"%s" directive disabled.' % self.name)

        try:
            self.emit()
        except IOError as error:
            raise self.severe('Problems with "%s" directive path:\n%s.' %
                      (self.name, ErrorString(error)))

        return []


class MaintainersProfile(Include):
    """Generate a list with all maintainer's profiles"""

    required_arguments = 0

    def emit(self):
        """Parse all the MAINTAINERS lines looking for profile entries"""
        env = self.state.document.settings.env
        docdir = os.path.dirname(os.path.join(env.srcdir, env.docname))
        path = maint_parser.path

        #
        # Produce a list with all maintainer profiles, sorted by subsystem name
        #
        output = ""
        for profile, entry in sorted(maint_parser.profile_entries.items()):
            name = profile.title()

            if entry.startswith("http"):
                output += f"- `{name} <{entry}>`_\n"
            elif entry.startswith("`"):
                output += f"- {name}: {entry}\n"
                self.warning(f"{profile}: Invalid 'P' tag: {entry}\n")
            else:
                output += f"- {entry}\n"

        #
        # Create a hidden TOC table with all profiles. That allows adding
        # profiles without needing to add them on any index.rst file.
        #
        output += "\n.. toctree::\n"
        output += "   :hidden:\n\n"

        for f in sorted(maint_parser.profile_toc):
            fname = os.path.join(maint_parser.base_dir, "Documentation", f)
            fname = os.path.relpath(fname, docdir)
            output += f"   {fname}\n"

        output += "\n"

        # For debugging the pre-rendered results...
        #print(output, file=open("/tmp/profiles.rst", "w"))

        self.state.document.settings.record_dependencies.add(path)
        self.state_machine.insert_input(statemachine.string2lines(output), path)

    def run(self):
        """Include the MAINTAINERS file as part of this reST file."""
        if not self.state.document.settings.file_insertion_enabled:
            raise self.warning('"%s" directive disabled.' % self.name)

        try:
            self.emit()
        except IOError as error:
            raise self.severe('Problems with "%s" directive path:\n%s.' %
                      (self.name, ErrorString(error)))

        return []


# pylint: disable=W0613
def add_filter_script(app, pagename, templatename, context, doctree):
    """Add Filter javascript only to maintainers page"""

    if doctree and doctree.get('maintainers_included'):
        app.add_js_file(None, body=JS_FILTER)


def setup(app):
    """Setup Sphinx extension"""
    global maint_parser  # pylint: disable=W0603

    app_dir = os.path.abspath(app.srcdir)
    match = re.match(r"(.*/)Documentation", app_dir)
    if not match:
        raise ValueError('Documentation directory not found.')

    base_dir = match.group(1)
    path = os.path.join(base_dir, "MAINTAINERS")

    maint_parser = MaintainersParser(base_dir, app_dir, path)

    app.add_directive("maintainers-include", MaintainersInclude)
    app.add_directive("maintainers-profile-toc", MaintainersProfile)

    app.connect("html-page-context", add_filter_script)

    return {
        "version": __version__,
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
