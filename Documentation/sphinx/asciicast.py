import os
import shutil

from docutils import nodes, utils
from docutils.nodes import Body, Element
from docutils.parsers.rst import directives

from sphinx.util import relative_uri
from sphinx.util.nodes import set_source_info
from sphinx.util.compat import Directive


class asciicast(Body, Element):
    pass

class Asciicast(Directive):
    """Embed asciinama "movie" from specified cast file"""

    required_argument = 1
    optional_arguments = 1
    has_content = False

    def run(self):
        env = self.state.document.settings.env
        node = asciicast()

        relpath, abspath = env.relfn2path(self.arguments[0])
        node['body'] = '<asciinema-player src="{}"></asciinema-player>'.format(self.arguments[0])
        node['path'] = abspath
        node['relpath'] = relpath

        env.note_dependency(abspath)

        return [node]

def html_asciicast(self, node):
    dst = os.path.join(self.builder.outdir, node['relpath'])
    try:
        shutil.copy(node['path'], dst)
        self.body.append(node['body'])
    except shutil.Error as err:
        self.builder.warn("failed to copy file: {}".format(err))
    raise nodes.SkipNode

def setup(app):
    app.add_javascript("asciinema-player.js")
    app.add_stylesheet("asciinema-player.css")
    app.add_node(asciicast, html=(html_asciicast, None))
    app.add_directive('asciicast', Asciicast)
