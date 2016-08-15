# -*- coding: utf-8; mode: python -*-
u"""
    cdomain
    ~~~~~~~

    Replacement for the sphinx c-domain.

    :copyright:  Copyright (C) 2016  Markus Heiser
    :license:    GPL Version 2, June 1991 see Linux/COPYING for details.

    List of customizations:

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
