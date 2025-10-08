# -*- coding: utf-8; mode: python -*-
# coding=utf-8
# SPDX-License-Identifier: GPL-2.0
#
"""
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

logger = logging.getLogger('kernel_abi')
path = os.path.join(srctree, "Documentation/ABI")

_kernel_abi = None

def get_kernel_abi():
    """
    Initialize kernel_abi global var, if not initialized yet.

    This is needed to avoid warnings during Sphinx module initialization.
    """
    global _kernel_abi

    if not _kernel_abi:
        # Parse ABI symbols only once
        _kernel_abi = AbiParser(path, logger=logger)
        _kernel_abi.parse_abi()
        _kernel_abi.check_issues()

    return _kernel_abi

def setup(app):

    app.add_directive("kernel-abi", KernelCmd)
    return {
        "version": __version__,
        "parallel_read_safe": True,
        "parallel_write_safe": True
    }


class KernelCmd(Directive):
    """KernelABI (``kernel-abi``) directive"""

    required_arguments = 1
    optional_arguments = 3
    has_content = False
    final_argument_whitespace = True
    parser = None

    option_spec = {
        "debug": directives.flag,
        "no-symbols": directives.flag,
        "no-files":  directives.flag,
    }

    def run(self):
        kernel_abi = get_kernel_abi()

        doc = self.state.document
        if not doc.settings.file_insertion_enabled:
            raise self.warning("docutils: file insertion disabled")

        env = self.state.document.settings.env
        content = ViewList()
        node = nodes.section()

        abi_type = self.arguments[0]

        if "no-symbols" in self.options:
            show_symbols = False
        else:
            show_symbols = True

        if "no-files" in self.options:
            show_file = False
        else:
            show_file = True

        tab_width = self.options.get('tab-width',
                                     self.state.document.settings.tab_width)

        old_f = None
        n = 0
        n_sym = 0
        for msg, f, ln in kernel_abi.doc(show_file=show_file,
                                            show_symbols=show_symbols,
                                            filter_path=abi_type):
            n_sym += 1
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
                # Add the file to Sphinx build dependencies if the file exists
                fname = os.path.join(srctree, f)
                if os.path.isfile(fname):
                    env.note_dependency(fname)

                old_f = f

            # Sphinx doesn't like to parse big messages. So, let's
            # add content symbol by symbol
            if content:
                self.do_parse(content, node)
                content = ViewList()

        if show_symbols and not show_file:
            logger.verbose("%s ABI: %i symbols (%i ReST lines)" % (abi_type, n_sym, n))
        elif not show_symbols and show_file:
            logger.verbose("%s ABI: %i files (%i ReST lines)" % (abi_type, n_sym, n))
        else:
            logger.verbose("%s ABI: %i data (%i ReST lines)" % (abi_type, n_sym, n))

        return node.children

    def do_parse(self, content, node):
        with switch_source_input(self.state, content):
            self.state.nested_parse(content, 0, node, match_titles=1)
