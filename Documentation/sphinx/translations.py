# SPDX-License-Identifier: GPL-2.0
#
# Copyright © 2023, Oracle and/or its affiliates.
# Author: Vegard Nossum <vegard.nossum@oracle.com>
#
# Add translation links to the top of the document.
#

import os

from docutils import nodes
from docutils.transforms import Transform

import sphinx
from sphinx import addnodes
from sphinx.errors import NoUri

all_languages = {
    # English is always first
    None: 'English',

    # Keep the rest sorted alphabetically
    'zh_CN': 'Chinese (Simplified)',
    'zh_TW': 'Chinese (Traditional)',
    'it_IT': 'Italian',
    'ja_JP': 'Japanese',
    'ko_KR': 'Korean',
    'sp_SP': 'Spanish',
}

class LanguagesNode(nodes.Element):
    pass

class TranslationsTransform(Transform):
    default_priority = 900

    def apply(self):
        app = self.document.settings.env.app
        docname = self.document.settings.env.docname

        this_lang_code = None
        components = docname.split(os.sep)
        if components[0] == 'translations' and len(components) > 2:
            this_lang_code = components[1]

            # normalize docname to be the untranslated one
            docname = os.path.join(*components[2:])

        new_nodes = LanguagesNode()
        new_nodes['current_language'] = all_languages[this_lang_code]

        for lang_code, lang_name in all_languages.items():
            if lang_code == this_lang_code:
                continue

            if lang_code is None:
                target_name = docname
            else:
                target_name = os.path.join('translations', lang_code, docname)

            pxref = addnodes.pending_xref('', refdomain='std',
                reftype='doc', reftarget='/' + target_name, modname=None,
                classname=None, refexplicit=True)
            pxref += nodes.Text(lang_name)
            new_nodes += pxref

        self.document.insert(0, new_nodes)

def process_languages(app, doctree, docname):
    for node in doctree.traverse(LanguagesNode):
        if app.builder.format not in ['html']:
            node.parent.remove(node)
            continue

        languages = []

        # Iterate over the child nodes; any resolved links will have
        # the type 'nodes.reference', while unresolved links will be
        # type 'nodes.Text'.
        languages = list(filter(lambda xref:
            isinstance(xref, nodes.reference), node.children))

        html_content = app.builder.templates.render('translations.html',
            context={
                'current_language': node['current_language'],
                'languages': languages,
            })

        node.replace_self(nodes.raw('', html_content, format='html'))

def setup(app):
    app.add_node(LanguagesNode)
    app.add_transform(TranslationsTransform)
    app.connect('doctree-resolved', process_languages)

    return {
        'parallel_read_safe': True,
        'parallel_write_safe': True,
    }
