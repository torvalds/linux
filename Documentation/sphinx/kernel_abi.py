# -*- coding: utf-8; mode: python -*-
# coding=utf-8
# SPDX-License-Identifier: GPL-2.0
#
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

import codecs
import os
import subprocess
import sys
import re
import kernellog

from os import path

from docutils import nodes, statemachine
from docutils.statemachine import ViewList
from docutils.parsers.rst import directives, Directive
from docutils.utils.error_reporting import ErrorString
from sphinx.util.docutils import switch_source_input

__version__  = '1.0'

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
    optional_arguments = 2
    has_content = False
    final_argument_whitespace = True

    option_spec = {
        "debug"     : directives.flag,
        "rst"       : directives.unchanged
    }

    def run(self):

        doc = self.state.document
        if not doc.settings.file_insertion_enabled:
            raise self.warning("docutils: file insertion disabled")

        env = doc.settings.env
        cwd = path.dirname(doc.current_source)
        cmd = "get_abi.pl rest --enable-lineno --dir "
        cmd += self.arguments[0]

        if 'rst' in self.options:
            cmd += " --rst-source"

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
        nodeList = self.nestedParse(lines, self.arguments[0])
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
        node = nodes.section()

        if "debug" in self.options:
            code_block = "\n\n.. code-block:: rst\n    :linenos:\n"
            for l in lines.split("\n"):
                code_block += "\n    " + l
            lines = code_block + "\n\n"

        line_regex = re.compile("^#define LINENO (\S+)\#([0-9]+)$")
        ln = 0
        n = 0
        f = fname

        for line in lines.split("\n"):
            n = n + 1
            match = line_regex.search(line)
            if match:
                new_f = match.group(1)

                # Sphinx parser is lazy: it stops parsing contents in the
                # middle, if it is too big. So, handle it per input file
                if new_f != f and content:
                    self.do_parse(content, node)
                    content = ViewList()

                f = new_f

                # sphinx counts lines from 0
                ln = int(match.group(2)) - 1
            else:
                content.append(line, f, ln)

        kernellog.info(self.state.document.settings.env.app, "%s: parsed %i lines" % (fname, n))

        if content:
            self.do_parse(content, node)

        return node.children

    def do_parse(self, content, node):
        with switch_source_input(self.state, content):
            self.state.nested_parse(content, 0, node, match_titles=1)
