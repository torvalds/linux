# coding=utf-8
# SPDX-License-Identifier: MIT
#
# Copyright © 2016 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
#
# Authors:
#    Jani Nikula <jani.nikula@intel.com>
#

import codecs
import os
import subprocess
import sys
import re
import glob

from docutils import nodes, statemachine
from docutils.statemachine import ViewList
from docutils.parsers.rst import directives, Directive
import sphinx
from sphinx.util.docutils import switch_source_input
from sphinx.util import logging
from pprint import pformat

srctree = os.path.abspath(os.environ["srctree"])
sys.path.insert(0, os.path.join(srctree, "tools/lib/python"))

from kdoc.kdoc_files import KernelFiles
from kdoc.kdoc_output import RestFormat

# Used when verbose is active to show how to reproduce kernel-doc
# issues via command line
kerneldoc_bin = "tools/docs/kernel-doc"

__version__  = '1.0'
kfiles = None
logger = logging.getLogger(__name__)

def cmd_str(cmd):
    """
    Helper function to output a command line that can be used to produce
    the same records via command line. Helpful to debug troubles at the
    script.
    """

    cmd_line = ""

    for w in cmd:
        if w == "" or " " in w:
            esc_cmd = "'" + w + "'"
        else:
            esc_cmd = w

        if cmd_line:
            cmd_line += " " + esc_cmd
            continue
        else:
            cmd_line = esc_cmd

    return cmd_line

class KernelDocDirective(Directive):
    """Extract kernel-doc comments from the specified file"""
    required_argument = 1
    optional_arguments = 4
    option_spec = {
        'doc': directives.unchanged_required,
        'export': directives.unchanged,
        'internal': directives.unchanged,
        'identifiers': directives.unchanged,
        'no-identifiers': directives.unchanged,
        'functions': directives.unchanged,
    }
    has_content = False
    verbose = 0

    parse_args = {}
    msg_args = {}

    def handle_args(self):

        env = self.state.document.settings.env
        cmd = [kerneldoc_bin, '-rst', '-enable-lineno']

        filename = env.config.kerneldoc_srctree + '/' + self.arguments[0]

        # Arguments used by KernelFiles.parse() function
        self.parse_args = {
            "file_list": [filename],
            "export_file": []
        }

        # Arguments used by KernelFiles.msg() function
        self.msg_args = {
            "enable_lineno": True,
            "export": False,
            "internal": False,
            "symbol": [],
            "nosymbol": [],
            "no_doc_sections": False
        }

        export_file_patterns = []

        verbose = os.environ.get("V")
        if verbose:
            try:
                self.verbose = int(verbose)
            except ValueError:
                pass

        # Tell sphinx of the dependency
        env.note_dependency(os.path.abspath(filename))

        self.tab_width = self.options.get('tab-width',
                                          self.state.document.settings.tab_width)

        # 'function' is an alias of 'identifiers'
        if 'functions' in self.options:
            self.options['identifiers'] = self.options.get('functions')

        # FIXME: make this nicer and more robust against errors
        if 'export' in self.options:
            cmd += ['-export']
            self.msg_args["export"] = True
            export_file_patterns = str(self.options.get('export')).split()
        elif 'internal' in self.options:
            cmd += ['-internal']
            self.msg_args["internal"] = True
            export_file_patterns = str(self.options.get('internal')).split()
        elif 'doc' in self.options:
            func = str(self.options.get('doc'))
            cmd += ['-function', func]
            self.msg_args["symbol"].append(func)
        elif 'identifiers' in self.options:
            identifiers = self.options.get('identifiers').split()
            if identifiers:
                for i in identifiers:
                    i = i.rstrip("\\").strip()
                    if not i:
                        continue

                    cmd += ['-function', i]
                    self.msg_args["symbol"].append(i)
            else:
                cmd += ['-no-doc-sections']
                self.msg_args["no_doc_sections"] = True

        if 'no-identifiers' in self.options:
            no_identifiers = self.options.get('no-identifiers').split()
            if no_identifiers:
                for i in no_identifiers:
                    i = i.rstrip("\\").strip()
                    if not i:
                        continue

                    cmd += ['-nosymbol', i]
                    self.msg_args["nosymbol"].append(i)

        for pattern in export_file_patterns:
            pattern = pattern.rstrip("\\").strip()
            if not pattern:
                continue

            for f in glob.glob(env.config.kerneldoc_srctree + '/' + pattern):
                env.note_dependency(os.path.abspath(f))
                cmd += ['-export-file', f]
                self.parse_args["export_file"].append(f)

            # Export file is needed by both parse and msg, as kernel-doc
            # cache exports.
            self.msg_args["export_file"] = self.parse_args["export_file"]

        cmd += [filename]

        return cmd

    def parse_msg(self, filename, node, out):
        """
        Handles a kernel-doc output for a given file
        """

        env = self.state.document.settings.env

        lines = statemachine.string2lines(out, self.tab_width,
                                            convert_whitespace=True)
        result = ViewList()

        lineoffset = 0;
        line_regex = re.compile(r"^\.\. LINENO ([0-9]+)$")
        for line in lines:
            match = line_regex.search(line)
            if match:
                # sphinx counts lines from 0
                lineoffset = int(match.group(1)) - 1
                # we must eat our comments since the upset the markup
            else:
                doc = str(env.srcdir) + "/" + env.docname + ":" + str(self.lineno)
                result.append(line, doc + ": " + filename, lineoffset)
                lineoffset += 1

        self.do_parse(result, node)

    def run_kdoc(self, kfiles):
        """
        Execute kernel-doc classes directly instead of running as a separate
        command.
        """

        env = self.state.document.settings.env

        node = nodes.section()

        kfiles.parse(**self.parse_args)
        filenames = self.parse_args["file_list"]

        for filename, out in kfiles.msg(**self.msg_args, filenames=filenames):
            self.parse_msg(filename, node, out)

        return node.children

    def run(self):
        cmd = self.handle_args()
        if self.verbose >= 1:
            logger.info(cmd_str(cmd))

        try:
            return self.run_kdoc(kfiles)
        except Exception as e:  # pylint: disable=W0703
            logger.warning("kernel-doc '%s' processing failed with: %s" %
                           (cmd_str(cmd), pformat(e)))
            return [nodes.error(None, nodes.paragraph(text = "kernel-doc missing"))]

    def do_parse(self, result, node):
        with switch_source_input(self.state, result):
            self.state.nested_parse(result, 0, node, match_titles=1)

def setup_kfiles(app):
    global kfiles
    out_style = RestFormat()
    kfiles = KernelFiles(out_style=out_style, logger=logger)


def setup(app):
    app.add_config_value('kerneldoc_srctree', None, 'env')
    app.add_config_value('kerneldoc_verbosity', 1, 'env')

    app.add_directive('kernel-doc', KernelDocDirective)

    app.connect('builder-inited', setup_kfiles)

    return dict(
        version = __version__,
        parallel_read_safe = True,
        parallel_write_safe = True
    )
