# -*- coding: utf-8; mode: python -*-
# pylint: disable=W0141,C0113,C0103,C0325
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

     * Handle signatures of function-like macros well. Don't try to deduce
       arguments types of function-like macros.

"""

from docutils import nodes
from docutils.parsers.rst import directives

import sphinx
from sphinx import addnodes
from sphinx.domains.c import c_funcptr_sig_re, c_sig_re
from sphinx.domains.c import CObject as Base_CObject
from sphinx.domains.c import CDomain as Base_CDomain
from itertools import chain
import re

__version__  = '1.1'

# Get Sphinx version
major, minor, patch = sphinx.version_info[:3]

# Namespace to be prepended to the full name
namespace = None

#
# Handle trivial newer c domain tags that are part of Sphinx 3.1 c domain tags
# - Store the namespace if ".. c:namespace::" tag is found
#
RE_namespace = re.compile(r'^\s*..\s*c:namespace::\s*(\S+)\s*$')

def markup_namespace(match):
    global namespace

    namespace = match.group(1)

    return ""

#
# Handle c:macro for function-style declaration
#
RE_macro = re.compile(r'^\s*..\s*c:macro::\s*(\S+)\s+(\S.*)\s*$')
def markup_macro(match):
    return ".. c:function:: " + match.group(1) + ' ' + match.group(2)

#
# Handle newer c domain tags that are evaluated as .. c:type: for
# backward-compatibility with Sphinx < 3.0
#
RE_ctype = re.compile(r'^\s*..\s*c:(struct|union|enum|enumerator|alias)::\s*(.*)$')

def markup_ctype(match):
    return ".. c:type:: " + match.group(2)

#
# Handle newer c domain tags that are evaluated as :c:type: for
# backward-compatibility with Sphinx < 3.0
#
RE_ctype_refs = re.compile(r':c:(var|struct|union|enum|enumerator)::`([^\`]+)`')
def markup_ctype_refs(match):
    return ":c:type:`" + match.group(2) + '`'

#
# Simply convert :c:expr: and :c:texpr: into a literal block.
#
RE_expr = re.compile(r':c:(expr|texpr):`([^\`]+)`')
def markup_c_expr(match):
    return '\\ ``' + match.group(2) + '``\\ '

#
# Parse Sphinx 3.x C markups, replacing them by backward-compatible ones
#
def c_markups(app, docname, source):
    result = ""
    markup_func = {
        RE_namespace: markup_namespace,
        RE_expr: markup_c_expr,
        RE_macro: markup_macro,
        RE_ctype: markup_ctype,
        RE_ctype_refs: markup_ctype_refs,
    }

    lines = iter(source[0].splitlines(True))
    for n in lines:
        match_iterators = [regex.finditer(n) for regex in markup_func]
        matches = sorted(chain(*match_iterators), key=lambda m: m.start())
        for m in matches:
            n = n[:m.start()] + markup_func[m.re](m) + n[m.end():]

        result = result + n

    source[0] = result

#
# Now implements support for the cdomain namespacing logic
#

def setup(app):

    # Handle easy Sphinx 3.1+ simple new tags: :c:expr and .. c:namespace::
    app.connect('source-read', c_markups)
    app.add_domain(CDomain, override=True)

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

    def handle_func_like_macro(self, sig, signode):
        u"""Handles signatures of function-like macros.

        If the objtype is 'function' and the signature ``sig`` is a
        function-like macro, the name of the macro is returned. Otherwise
        ``False`` is returned.  """

        global namespace

        if not self.objtype == 'function':
            return False

        m = c_funcptr_sig_re.match(sig)
        if m is None:
            m = c_sig_re.match(sig)
            if m is None:
                raise ValueError('no match')

        rettype, fullname, arglist, _const = m.groups()
        arglist = arglist.strip()
        if rettype or not arglist:
            return False

        arglist = arglist.replace('`', '').replace('\\ ', '') # remove markup
        arglist = [a.strip() for a in arglist.split(",")]

        # has the first argument a type?
        if len(arglist[0].split(" ")) > 1:
            return False

        # This is a function-like macro, its arguments are typeless!
        signode  += addnodes.desc_name(fullname, fullname)
        paramlist = addnodes.desc_parameterlist()
        signode  += paramlist

        for argname in arglist:
            param = addnodes.desc_parameter('', '', noemph=True)
            # separate by non-breaking space in the output
            param += nodes.emphasis(argname, argname)
            paramlist += param

        if namespace:
            fullname = namespace + "." + fullname

        return fullname

    def handle_signature(self, sig, signode):
        """Transform a C signature into RST nodes."""

        global namespace

        fullname = self.handle_func_like_macro(sig, signode)
        if not fullname:
            fullname = super(CObject, self).handle_signature(sig, signode)

        if "name" in self.options:
            if self.objtype == 'function':
                fullname = self.options["name"]
            else:
                # FIXME: handle :name: value of other declaration types?
                pass
        else:
            if namespace:
                fullname = namespace + "." + fullname

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
            self.indexnode['entries'].append(
                    ('single', indextext, targetname, '', None))

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
