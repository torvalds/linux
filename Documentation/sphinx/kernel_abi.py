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
    scripts/get_abi.py script to parse the Kernel ABI files.

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

import os
import re
import sys

from docutils import nodes, statemachine
from docutils.statemachine import ViewList
from docutils.parsers.rst import directives, Directive
from sphinx.util.docutils import switch_source_input
from sphinx.util import logging

srctree = os.path.abspath(os.environ["srctree"])
sys.path.insert(0, os.path.join(srctree, "scripts/lib/abi"))

from abi_parser import AbiParser

__version__ = "1.0"


def setup(app):

    app.add_directive("kernel-abi", KernelCmd)
    return {
        "version": __version__,
        "parallel_read_safe": True,
        "parallel_write_safe": True
    }


class KernelCmd(Directive):
    u"""KernelABI (``kernel-abi``) directive"""

    required_arguments = 1
    optional_arguments = 2
    has_content = False
    final_argument_whitespace = True
    logger = logging.getLogger('kernel_abi')
    parser = None

    option_spec = {
        "debug": directives.flag,
    }

    def run(self):
        doc = self.state.document
        if not doc.settings.file_insertion_enabled:
            raise self.warning("docutils: file insertion disabled")

        path = os.path.join(srctree, "Documentation", self.arguments[0])
        self.parser = AbiParser(path, logger=self.logger)
        self.parser.parse_abi()
        self.parser.check_issues()

        node = self.nested_parse(None, self.arguments[0])
        return node

    def nested_parse(self, data, fname):
        env = self.state.document.settings.env
        content = ViewList()
        node = nodes.section()

        if data is not None:
            # Handles the .rst file
            for line in data.split("\n"):
                content.append(line, fname, 0)

            self.do_parse(content, node)

        else:
            # Handles the ABI parser content, symbol by symbol

            old_f = fname
            n = 0
            for msg, f, ln in self.parser.doc():
                msg_list = statemachine.string2lines(msg, tab_width,
                                                     convert_whitespace=True)
                if "debug" in self.options:
                    lines = [
                        "", "",  ".. code-block:: rst",
                        "    :linenos:", ""
                    ]
                    for m in msg_list:
                        lines.append("    " + m)
                else:
                    lines = msg_list

                for line in lines:
                    # sphinx counts lines from 0
                    content.append(line, f, ln - 1)
                    n += 1

                if f != old_f:
                    # Add the file to Sphinx build dependencies
                    env.note_dependency(os.path.abspath(f))

                    old_f = f

                # Sphinx doesn't like to parse big messages. So, let's
                # add content symbol by symbol
                if content:
                    self.do_parse(content, node)
                    content = ViewList()

            self.logger.info("%s: parsed %i lines" % (fname, n))

        return node.children

    def do_parse(self, content, node):
        with switch_source_input(self.state, content):
            self.state.nested_parse(content, 0, node, match_titles=1)
