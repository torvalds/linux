# -*- coding: utf-8; mode: python -*-
u"""
    cdomain
    ~~~~~~~

    Replacement for the sphinx c-domain.

    :copyright:  Copyright (C) 2016  Markus Heiser
    :license:    GPL Version 2, June 1991 see Linux/COPYING for details.

    List of customizations:

    * Moved the *duplicate C object description* warnings for function
      declarations in the nitpicky mode. See Sphinx documentation for
      the config values for ``nitpick`` and ``nitpick_ignore``.

    * Add option 'name' to the "c:function:" directive.  With option 'name' the
      ref-name of a function can be modified. E.g.::

          .. c:function:: int ioctl( int fd, int request )
             :name: VIDIOC_LOG_STATUS

      The func-name (e.g. ioctl) remains in the output but the ref-name changed
      from 'ioctl' to 'VIDIOC_LOG_STATUS'. The function is referenced by::

          * :c:func:`VIDIOC_LOG_STATUS` or
          * :any:`VIDIOC_LOG_STATUS` (``:any:`` needs sphinx 1.3)
"""

from docutils.parsers.rst import directives

from sphinx.domains.c import CObject as Base_CObject
from sphinx.domains.c import CDomain as Base_CDomain

__version__  = '1.0'

def setup(app):

    app.override_domain(CDomain)

    return dict(
        version = __version__,
        parallel_read_safe = True,
        parallel_write_safe = True
    )

class CObject(Base_CObject):

    """
    Description of a C language object.
    """
    option_spec = {
        "name" : directives.unchanged
    }

    def handle_signature(self, sig, signode):
        """Transform a C signature into RST nodes."""
        fullname = super(CObject, self).handle_signature(sig, signode)
        if "name" in self.options:
            if self.objtype == 'function':
                fullname = self.options["name"]
            else:
                # FIXME: handle :name: value of other declaration types?
                pass
        return fullname

    def add_target_and_index(self, name, sig, signode):
        # for C API items we add a prefix since names are usually not qualified
        # by a module name and so easily clash with e.g. section titles
        targetname = 'c.' + name
        if targetname not in self.state.document.ids:
            signode['names'].append(targetname)
            signode['ids'].append(targetname)
            signode['first'] = (not self.names)
            self.state.document.note_explicit_target(signode)
            inv = self.env.domaindata['c']['objects']
            if (name in inv and self.env.config.nitpicky):
                if self.objtype == 'function':
                    if ('c:func', name) not in self.env.config.nitpick_ignore:
                        self.state_machine.reporter.warning(
                            'duplicate C object description of %s, ' % name +
                            'other instance in ' + self.env.doc2path(inv[name][0]),
                            line=self.lineno)
            inv[name] = (self.env.docname, self.objtype)

        indextext = self.get_index_text(name)
        if indextext:
            self.indexnode['entries'].append(('single', indextext,
                                              targetname, '', None))

class CDomain(Base_CDomain):

    """C language domain."""
    name = 'c'
    label = 'C'
    directives = {
        'function': CObject,
        'member':   CObject,
        'macro':    CObject,
        'type':     CObject,
        'var':      CObject,
    }
