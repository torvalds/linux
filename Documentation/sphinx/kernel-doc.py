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

import os
import subprocess
import sys

from docutils import nodes, statemachine
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
        cmd = [env.config.kerneldoc_bin, '-rst']

        filename = env.config.kerneldoc_srctree + '/' + self.arguments[0]

        # Tell sphinx of the dependency
        env.note_dependency(os.path.abspath(filename))

        tab_width = self.options.get('tab-width', self.state.document.settings.tab_width)
        source = self.state_machine.input_lines.source(self.lineno - self.state_machine.input_offset - 1)

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

            # assume the kernel sources are utf-8
            out, err = unicode(out, 'utf-8'), unicode(err, 'utf-8')

            if p.returncode != 0:
                sys.stderr.write(err)

                env.app.warn('kernel-doc \'%s\' failed with return code %d' % (" ".join(cmd), p.returncode))
                return [nodes.error(None, nodes.paragraph(text = "kernel-doc missing"))]
            elif env.config.kerneldoc_verbosity > 0:
                sys.stderr.write(err)

            lines = statemachine.string2lines(out, tab_width, convert_whitespace=True)
            self.state_machine.insert_input(lines, source)
            return []
        except Exception as e:
            env.app.warn('kernel-doc \'%s\' processing failed with: %s' %
                         (" ".join(cmd), str(e)))
            return [nodes.error(None, nodes.paragraph(text = "kernel-doc missing"))]

def setup(app):
    app.add_config_value('kerneldoc_bin', None, 'env')
    app.add_config_value('kerneldoc_srctree', None, 'env')
    app.add_config_value('kerneldoc_verbosity', 1, 'env')

    app.add_directive('kernel-doc', KernelDocDirective)
