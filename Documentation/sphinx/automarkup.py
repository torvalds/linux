# SPDX-License-Identifier: GPL-2.0
# Copyright 2019 Jonathan Corbet <corbet@lwn.net>
#
# Apply kernel-specific tweaks after the initial document processing
# has been done.
#
from docutils import nodes
import sphinx
from sphinx import addnodes
if sphinx.version_info[0] < 2 or \
   sphinx.version_info[0] == 2 and sphinx.version_info[1] < 1:
    from sphinx.environment import NoUri
else:
    from sphinx.errors import NoUri
import re
from itertools import chain

#
# Regex nastiness.  Of course.
# Try to identify "function()" that's not already marked up some
# other way.  Sphinx doesn't like a lot of stuff right after a
# :c:func: block (i.e. ":c:func:`mmap()`s" flakes out), so the last
# bit tries to restrict matches to things that won't create trouble.
#
RE_function = re.compile(r'(([\w_][\w\d_]+)\(\))')
RE_type = re.compile(r'(struct|union|enum|typedef)\s+([\w_][\w\d_]+)')
#
# Detects a reference to a documentation page of the form Documentation/... with
# an optional extension
#
RE_doc = re.compile(r'Documentation(/[\w\-_/]+)(\.\w+)*')

#
# Many places in the docs refer to common system calls.  It is
# pointless to try to cross-reference them and, as has been known
# to happen, somebody defining a function by these names can lead
# to the creation of incorrect and confusing cross references.  So
# just don't even try with these names.
#
Skipfuncs = [ 'open', 'close', 'read', 'write', 'fcntl', 'mmap',
              'select', 'poll', 'fork', 'execve', 'clone', 'ioctl',
              'socket' ]

def markup_refs(docname, app, node):
    t = node.astext()
    done = 0
    repl = [ ]
    #
    # Associate each regex with the function that will markup its matches
    #
    markup_func = {RE_type: markup_c_ref,
                   RE_function: markup_c_ref,
                   RE_doc: markup_doc_ref}
    match_iterators = [regex.finditer(t) for regex in markup_func]
    #
    # Sort all references by the starting position in text
    #
    sorted_matches = sorted(chain(*match_iterators), key=lambda m: m.start())
    for m in sorted_matches:
        #
        # Include any text prior to match as a normal text node.
        #
        if m.start() > done:
            repl.append(nodes.Text(t[done:m.start()]))

        #
        # Call the function associated with the regex that matched this text and
        # append its return to the text
        #
        repl.append(markup_func[m.re](docname, app, m))

        done = m.end()
    if done < len(t):
        repl.append(nodes.Text(t[done:]))
    return repl

#
# Try to replace a C reference (function() or struct/union/enum/typedef
# type_name) with an appropriate cross reference.
#
def markup_c_ref(docname, app, match):
    class_str = {RE_function: 'c-func', RE_type: 'c-type'}
    reftype_str = {RE_function: 'function', RE_type: 'type'}

    cdom = app.env.domains['c']
    #
    # Go through the dance of getting an xref out of the C domain
    #
    target = match.group(2)
    target_text = nodes.Text(match.group(0))
    xref = None
    if not (match.re == RE_function and target in Skipfuncs):
        lit_text = nodes.literal(classes=['xref', 'c', class_str[match.re]])
        lit_text += target_text
        pxref = addnodes.pending_xref('', refdomain = 'c',
                                      reftype = reftype_str[match.re],
                                      reftarget = target, modname = None,
                                      classname = None)
        #
        # XXX The Latex builder will throw NoUri exceptions here,
        # work around that by ignoring them.
        #
        try:
            xref = cdom.resolve_xref(app.env, docname, app.builder,
                                     reftype_str[match.re], target, pxref,
                                     lit_text)
        except NoUri:
            xref = None
    #
    # Return the xref if we got it; otherwise just return the plain text.
    #
    if xref:
        return xref
    else:
        return target_text

#
# Try to replace a documentation reference of the form Documentation/... with a
# cross reference to that page
#
def markup_doc_ref(docname, app, match):
    stddom = app.env.domains['std']
    #
    # Go through the dance of getting an xref out of the std domain
    #
    target = match.group(1)
    xref = None
    pxref = addnodes.pending_xref('', refdomain = 'std', reftype = 'doc',
                                  reftarget = target, modname = None,
                                  classname = None, refexplicit = False)
    #
    # XXX The Latex builder will throw NoUri exceptions here,
    # work around that by ignoring them.
    #
    try:
        xref = stddom.resolve_xref(app.env, docname, app.builder, 'doc',
                                   target, pxref, None)
    except NoUri:
        xref = None
    #
    # Return the xref if we got it; otherwise just return the plain text.
    #
    if xref:
        return xref
    else:
        return nodes.Text(match.group(0))

def auto_markup(app, doctree, name):
    #
    # This loop could eventually be improved on.  Someday maybe we
    # want a proper tree traversal with a lot of awareness of which
    # kinds of nodes to prune.  But this works well for now.
    #
    # The nodes.literal test catches ``literal text``, its purpose is to
    # avoid adding cross-references to functions that have been explicitly
    # marked with cc:func:.
    #
    for para in doctree.traverse(nodes.paragraph):
        for node in para.traverse(nodes.Text):
            if not isinstance(node.parent, nodes.literal):
                node.parent.replace(node, markup_refs(name, app, node))

def setup(app):
    app.connect('doctree-resolved', auto_markup)
    return {
        'parallel_read_safe': True,
        'parallel_write_safe': True,
        }
