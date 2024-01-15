# coding=utf-8
# SPDX-License-Identifier: GPL-2.0
#
u"""
    kernel-feat
    ~~~~~~~~~~~

    Implementation of the ``kernel-feat`` reST-directive.

    :copyright:  Copyright (C) 2016  Markus Heiser
    :copyright:  Copyright (C) 2016-2019  Mauro Carvalho Chehab
    :maintained-by: Mauro Carvalho Chehab <mchehab+samsung@kernel.org>
    :license:    GPL Version 2, June 1991 see Linux/COPYING for details.

    The ``kernel-feat`` (:py:class:`KernelFeat`) directive calls the
    scripts/get_feat.pl script to parse the Kernel ABI files.

    Overview of directive's argument and options.

    .. code-block:: rst

        .. kernel-feat:: <ABI directory location>
            :debug:

    The argument ``<ABI directory location>`` is required. It contains the
    location of the ABI files to be parsed.

    ``debug``
      Inserts a code-block with the *raw* reST. Sometimes it is helpful to see
      what reST is generated.

"""

import codecs
import os
import re
import subprocess
import sys

from os import path

from docutils import nodes, statemachine
from docutils.statemachine import ViewList
from docutils.parsers.rst import directives, Directive
from docutils.utils.error_reporting import ErrorString
from sphinx.util.docutils import switch_source_input

__version__  = '1.0'

def setup(app):

    app.add_directive("kernel-feat", KernelFeat)
    return dict(
        version = __version__
        , parallel_read_safe = True
        , parallel_write_safe = True
    )

class KernelFeat(Directive):

    u"""KernelFeat (``kernel-feat``) directive"""

    required_arguments = 1
    optional_arguments = 2
    has_content = False
    final_argument_whitespace = True

    option_spec = {
        "debug"     : directives.flag
    }

    def warn(self, message, **replace):
        replace["fname"]   = self.state.document.current_source
        replace["line_no"] = replace.get("line_no", self.lineno)
        message = ("%(fname)s:%(line_no)s: [kernel-feat WARN] : " + message) % replace
        self.state.document.settings.env.app.warn(message, prefix="")

    def run(self):

        doc = self.state.document
        if not doc.settings.file_insertion_enabled:
            raise self.warning("docutils: file insertion disabled")

        env = doc.settings.env
        cwd = path.dirname(doc.current_source)
        cmd = "get_feat.pl rest --enable-fname --dir "
        cmd += self.arguments[0]

        if len(self.arguments) > 1:
            cmd += " --arch " + self.arguments[1]

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

        line_regex = re.compile(r"^\.\. FILE (\S+)$")

        out_lines = ""

        for line in lines.split("\n"):
            match = line_regex.search(line)
            if match:
                fname = match.group(1)

                # Add the file to Sphinx build dependencies
                env.note_dependency(os.path.abspath(fname))
            else:
                out_lines += line + "\n"

        nodeList = self.nestedParse(out_lines, fname)
        return nodeList

    def runCmd(self, cmd, **kwargs):
        u"""Run command ``cmd`` and return its stdout as unicode."""

        try:
            proc = subprocess.Popen(
                cmd
                , stdout = subprocess.PIPE
                , stderr = subprocess.PIPE
                , **kwargs
            )
            out, err = proc.communicate()

            out, err = codecs.decode(out, 'utf-8'), codecs.decode(err, 'utf-8')

            if proc.returncode != 0:
                raise self.severe(
                    u"command '%s' failed with return code %d"
                    % (cmd, proc.returncode)
                )
        except OSError as exc:
            raise self.severe(u"problems with '%s' directive: %s."
                              % (self.name, ErrorString(exc)))
        return out

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

        with switch_source_input(self.state, content):
            self.state.nested_parse(content, 0, node, match_titles=1)

        return node.children
