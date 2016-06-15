# coding=utf-8
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
# Please make sure this works on both python2 and python3.
#

import os
import subprocess
import sys
import re

from docutils import nodes, statemachine
from docutils.statemachine import ViewList
from docutils.parsers.rst import directives
from sphinx.util.compat import Directive

class KernelDocDirective(Directive):
    """Extract kernel-doc comments from the specified file"""
    required_argument = 1
    optional_arguments = 4
    option_spec = {
        'doc': directives.unchanged_required,
        'functions': directives.unchanged_required,
        'export': directives.flag,
        'internal': directives.flag,
    }
    has_content = False

    def run(self):
        env = self.state.document.settings.env
        cmd = [env.config.kerneldoc_bin, '-rst', '-enable-lineno']

        filename = env.config.kerneldoc_srctree + '/' + self.arguments[0]

        # Tell sphinx of the dependency
        env.note_dependency(os.path.abspath(filename))

        tab_width = self.options.get('tab-width', self.state.document.settings.tab_width)
        source = filename

        # FIXME: make this nicer and more robust against errors
        if 'export' in self.options:
            cmd += ['-export']
        elif 'internal' in self.options:
            cmd += ['-internal']
        elif 'doc' in self.options:
            cmd += ['-function', str(self.options.get('doc'))]
        elif 'functions' in self.options:
            for f in str(self.options.get('functions')).split(' '):
                cmd += ['-function', f]

        cmd += [filename]

        try:
            env.app.verbose('calling kernel-doc \'%s\'' % (" ".join(cmd)))

            p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
            out, err = p.communicate()

            # python2 needs conversion to unicode.
            # python3 with universal_newlines=True returns strings.
            if sys.version_info.major < 3:
                out, err = unicode(out, 'utf-8'), unicode(err, 'utf-8')

            if p.returncode != 0:
                sys.stderr.write(err)

                env.app.warn('kernel-doc \'%s\' failed with return code %d' % (" ".join(cmd), p.returncode))
                return [nodes.error(None, nodes.paragraph(text = "kernel-doc missing"))]
            elif env.config.kerneldoc_verbosity > 0:
                sys.stderr.write(err)

            lines = statemachine.string2lines(out, tab_width, convert_whitespace=True)
            result = ViewList()

            lineoffset = 0;
            line_regex = re.compile("^#define LINENO ([0-9]+)$")
            for line in lines:
                match = line_regex.search(line)
                if match:
                    # sphinx counts lines from 0
                    lineoffset = int(match.group(1)) - 1
                    # we must eat our comments since the upset the markup
                else:
                    result.append(line, source, lineoffset)
                    lineoffset += 1

            node = nodes.section()
            node.document = self.state.document
            self.state.nested_parse(result, self.content_offset, node)

            return node.children

        except Exception as e:
            env.app.warn('kernel-doc \'%s\' processing failed with: %s' %
                         (" ".join(cmd), str(e)))
            return [nodes.error(None, nodes.paragraph(text = "kernel-doc missing"))]

def setup(app):
    app.add_config_value('kerneldoc_bin', None, 'env')
    app.add_config_value('kerneldoc_srctree', None, 'env')
    app.add_config_value('kerneldoc_verbosity', 1, 'env')

    app.add_directive('kernel-doc', KernelDocDirective)
