# -*- coding: utf-8; mode: python -*-
# SPDX-License-Identifier: GPL-2.0
u"""
    kernel-abi
    ~~~~~~~~~~

    Implementation of the ``kernel-abi`` reST-directive.

    :copyright:  Copyright (C) 2016  Markus Heiser
    :copyright:  Copyright (C) 2016-2020  Mauro Carvalho Chehab
    :maintained-by: Mauro Carvalho Chehab <mchehab+huawei@kernel.org>
    :license:    GPL Version 2, June 1991 see Linux/COPYING for details.

    The ``kernel-abi`` (:py:class:`KernelCmd`) directive calls the
    scripts/get_abi.pl script to parse the Kernel ABI files.

    Overview of directive's argument and options.

    .. code-block:: rst

        .. kernel-abi:: <ABI directory location>
            :debug:

    The argument ``<ABI directory location>`` is required. It contains the
    location of the ABI files to be parsed.

    ``debug``
      Inserts a code-block with the *raw* reST. Sometimes it is helpful to see
      what reST is generated.

"""

import sys
import os
from os import path
import subprocess

from sphinx.ext.autodoc import AutodocReporter

from docutils import nodes
from docutils.parsers.rst import Directive, directives
from docutils.statemachine import ViewList
from docutils.utils.error_reporting import ErrorString


__version__  = '1.0'

# We can't assume that six is installed
PY3 = sys.version_info[0] == 3
PY2 = sys.version_info[0] == 2
if PY3:
    # pylint: disable=C0103, W0622
    unicode     = str
    basestring  = str

def setup(app):

    app.add_directive("kernel-abi", KernelCmd)
    return dict(
        version = __version__
        , parallel_read_safe = True
        , parallel_write_safe = True
    )

class KernelCmd(Directive):

    u"""KernelABI (``kernel-abi``) directive"""

    required_arguments = 1
    optional_arguments = 0
    has_content = False
    final_argument_whitespace = True

    option_spec = {
        "debug"     : directives.flag
    }

    def warn(self, message, **replace):
        replace["fname"]   = self.state.document.current_source
        replace["line_no"] = replace.get("line_no", self.lineno)
        message = ("%(fname)s:%(line_no)s: [kernel-abi WARN] : " + message) % replace
        self.state.document.settings.env.app.warn(message, prefix="")

    def run(self):

        doc = self.state.document
        if not doc.settings.file_insertion_enabled:
            raise self.warning("docutils: file insertion disabled")

        env = doc.settings.env
        cwd = path.dirname(doc.current_source)
        cmd = "get_abi.pl rest --dir "
        cmd += self.arguments[0]

        srctree = path.abspath(os.environ["srctree"])

        fname = cmd

        # extend PATH with $(srctree)/scripts
        path_env = os.pathsep.join([
            srctree + os.sep + "scripts",
            os.environ["PATH"]
        ])
        shell_env = os.environ.copy()
        shell_env["PATH"]    = path_env
        shell_env["srctree"] = srctree

        lines = self.runCmd(cmd, shell=True, cwd=cwd, env=shell_env)
        nodeList = self.nestedParse(lines, fname)
        return nodeList

    def runCmd(self, cmd, **kwargs):
        u"""Run command ``cmd`` and return it's stdout as unicode."""

        try:
            proc = subprocess.Popen(
                cmd
                , stdout = subprocess.PIPE
                , stderr = subprocess.PIPE
                , universal_newlines = True
                , **kwargs
            )
            out, err = proc.communicate()
            if err:
                self.warn(err)
            if proc.returncode != 0:
                raise self.severe(
                    u"command '%s' failed with return code %d"
                    % (cmd, proc.returncode)
                )
        except OSError as exc:
            raise self.severe(u"problems with '%s' directive: %s."
                              % (self.name, ErrorString(exc)))
        return unicode(out)

    def nestedParse(self, lines, fname):
        content = ViewList()
        node    = nodes.section()

        if "debug" in self.options:
            code_block = "\n\n.. code-block:: rst\n    :linenos:\n"
            for l in lines.split("\n"):
                code_block += "\n    " + l
            lines = code_block + "\n\n"

        for c, l in enumerate(lines.split("\n")):
            content.append(l, fname, c)

        buf  = self.state.memo.title_styles, self.state.memo.section_level, self.state.memo.reporter
        self.state.memo.title_styles  = []
        self.state.memo.section_level = 0
        self.state.memo.reporter      = AutodocReporter(content, self.state.memo.reporter)
        try:
            self.state.nested_parse(content, 0, node, match_titles=1)
        finally:
            self.state.memo.title_styles, self.state.memo.section_level, self.state.memo.reporter = buf
        return node.children
