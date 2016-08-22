# -*- coding: utf-8; mode: python -*-
u"""
    cdomain
    ~~~~~~~

    Replacement for the sphinx c-domain.

    :copyright:  Copyright (C) 2016  Markus Heiser
    :license:    GPL Version 2, June 1991 see Linux/COPYING for details.
"""

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
