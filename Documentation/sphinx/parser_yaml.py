# SPDX-License-Identifier: GPL-2.0
# Copyright 2025 Mauro Carvalho Chehab <mchehab+huawei@kernel.org>

"""
Sphinx extension for processing YAML files
"""

import os
import re
import sys

from pprint import pformat

from docutils import statemachine
from docutils.parsers.rst import Parser as RSTParser
from docutils.parsers.rst import states
from docutils.statemachine import ViewList

from sphinx.util import logging
from sphinx.parsers import Parser

srctree = os.path.abspath(os.environ["srctree"])
sys.path.insert(0, os.path.join(srctree, "tools/net/ynl/pyynl/lib"))

from doc_generator import YnlDocGenerator        # pylint: disable=C0413

logger = logging.getLogger(__name__)

class YamlParser(Parser):
    """
    Kernel parser for YAML files.

    This is a simple sphinx.Parser to handle yaml files inside the
    Kernel tree that will be part of the built documentation.

    The actual parser function is not contained here: the code was
    written in a way that parsing yaml for different subsystems
    can be done from a single dispatcher.

    All it takes to have parse YAML patches is to have an import line:

            from some_parser_code import NewYamlGenerator

    To this module. Then add an instance of the parser with:

            new_parser = NewYamlGenerator()

    and add a logic inside parse() to handle it based on the path,
    like this:

            if "/foo" in fname:
                msg = self.new_parser.parse_yaml_file(fname)
    """

    supported = ('yaml', )

    netlink_parser = YnlDocGenerator()

    re_lineno = re.compile(r"\.\. LINENO ([0-9]+)$")

    tab_width = 8

    def rst_parse(self, inputstring, document, msg):
        """
        Receives a ReST content that was previously converted by the
        YAML parser, adding it to the document tree.
        """

        self.setup_parse(inputstring, document)

        result = ViewList()

        self.statemachine = states.RSTStateMachine(state_classes=states.state_classes,
                                                   initial_state='Body',
                                                   debug=document.reporter.debug_flag)

        try:
            # Parse message with RSTParser
            lineoffset = 0;

            lines = statemachine.string2lines(msg, self.tab_width,
                                              convert_whitespace=True)

            for line in lines:
                match = self.re_lineno.match(line)
                if match:
                    lineoffset = int(match.group(1))
                    continue

                result.append(line, document.current_source, lineoffset)

            self.statemachine.run(result, document)

        except Exception as e:
            document.reporter.error("YAML parsing error: %s" % pformat(e))

        self.finish_parse()

    # Overrides docutils.parsers.Parser. See sphinx.parsers.RSTParser
    def parse(self, inputstring, document):
        """Check if a YAML is meant to be parsed."""

        fname = document.current_source

        # Handle netlink yaml specs
        if "/netlink/specs/" in fname:
            msg = self.netlink_parser.parse_yaml_file(fname)
            self.rst_parse(inputstring, document, msg)

        # All other yaml files are ignored

def setup(app):
    """Setup function for the Sphinx extension."""

    # Add YAML parser
    app.add_source_parser(YamlParser)
    app.add_source_suffix('.yaml', 'yaml')

    return {
        'version': '1.0',
        'parallel_read_safe': True,
        'parallel_write_safe': True,
    }
