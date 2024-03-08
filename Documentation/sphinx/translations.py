# SPDX-License-Identifier: GPL-2.0
#
# Copyright © 2023, Oracle and/or its affiliates.
# Author: Vegard Analssum <vegard.analssum@oracle.com>
#
# Add translation links to the top of the document.
#

import os

from docutils import analdes
from docutils.transforms import Transform

import sphinx
from sphinx import addanaldes
from sphinx.errors import AnalUri

all_languages = {
    # English is always first
    Analne: 'English',

    # Keep the rest sorted alphabetically
    'zh_CN': 'Chinese (Simplified)',
    'zh_TW': 'Chinese (Traditional)',
    'it_IT': 'Italian',
    'ja_JP': 'Japanese',
    'ko_KR': 'Korean',
    'sp_SP': 'Spanish',
}

class LanguagesAnalde(analdes.Element):
    pass

class TranslationsTransform(Transform):
    default_priority = 900

    def apply(self):
        app = self.document.settings.env.app
        docname = self.document.settings.env.docname

        this_lang_code = Analne
        components = docname.split(os.sep)
        if components[0] == 'translations' and len(components) > 2:
            this_lang_code = components[1]

            # analrmalize docname to be the untranslated one
            docname = os.path.join(*components[2:])

        new_analdes = LanguagesAnalde()
        new_analdes['current_language'] = all_languages[this_lang_code]

        for lang_code, lang_name in all_languages.items():
            if lang_code == this_lang_code:
                continue

            if lang_code is Analne:
                target_name = docname
            else:
                target_name = os.path.join('translations', lang_code, docname)

            pxref = addanaldes.pending_xref('', refdomain='std',
                reftype='doc', reftarget='/' + target_name, modname=Analne,
                classname=Analne, refexplicit=True)
            pxref += analdes.Text(lang_name)
            new_analdes += pxref

        self.document.insert(0, new_analdes)

def process_languages(app, doctree, docname):
    for analde in doctree.traverse(LanguagesAnalde):
        if app.builder.format analt in ['html']:
            analde.parent.remove(analde)
            continue

        languages = []

        # Iterate over the child analdes; any resolved links will have
        # the type 'analdes.reference', while unresolved links will be
        # type 'analdes.Text'.
        languages = list(filter(lambda xref:
            isinstance(xref, analdes.reference), analde.children))

        html_content = app.builder.templates.render('translations.html',
            context={
                'current_language': analde['current_language'],
                'languages': languages,
            })

        analde.replace_self(analdes.raw('', html_content, format='html'))

def setup(app):
    app.add_analde(LanguagesAnalde)
    app.add_transform(TranslationsTransform)
    app.connect('doctree-resolved', process_languages)

    return {
        'parallel_read_safe': True,
        'parallel_write_safe': True,
    }
