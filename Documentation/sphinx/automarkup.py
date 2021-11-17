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
# Python 2 lacks re.ASCII...
#
try:
    ascii_p3 = re.ASCII
except AttributeError:
    ascii_p3 = 0

#
# Regex nastiness.  Of course.
# Try to identify "function()" that's not already marked up some
# other way.  Sphinx doesn't like a lot of stuff right after a
# :c:func: block (i.e. ":c:func:`mmap()`s" flakes out), so the last
# bit tries to restrict matches to things that won't create trouble.
#
RE_function = re.compile(r'\b(([a-zA-Z_]\w+)\(\))', flags=ascii_p3)

#
# Sphinx 2 uses the same :c:type role for struct, union, enum and typedef
#
RE_generic_type = re.compile(r'\b(struct|union|enum|typedef)\s+([a-zA-Z_]\w+)',
                             flags=ascii_p3)

#
# Sphinx 3 uses a different C role for each one of struct, union, enum and
# typedef
#
RE_struct = re.compile(r'\b(struct)\s+([a-zA-Z_]\w+)', flags=ascii_p3)
RE_union = re.compile(r'\b(union)\s+([a-zA-Z_]\w+)', flags=ascii_p3)
RE_enum = re.compile(r'\b(enum)\s+([a-zA-Z_]\w+)', flags=ascii_p3)
RE_typedef = re.compile(r'\b(typedef)\s+([a-zA-Z_]\w+)', flags=ascii_p3)

#
# Detects a reference to a documentation page of the form Documentation/... with
# an optional extension
#
RE_doc = re.compile(r'(\bDocumentation/)?((\.\./)*[\w\-/]+)\.(rst|txt)')

RE_namespace = re.compile(r'^\s*..\s*c:namespace::\s*(\S+)\s*$')

#
# Reserved C words that we should skip when cross-referencing
#
Skipnames = [ 'for', 'if', 'register', 'sizeof', 'struct', 'unsigned' ]


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

c_namespace = ''

def markup_refs(docname, app, node):
    t = node.astext()
    done = 0
    repl = [ ]
    #
    # Associate each regex with the function that will markup its matches
    #
    markup_func_sphinx2 = {RE_doc: markup_doc_ref,
                           RE_function: markup_c_ref,
                           RE_generic_type: markup_c_ref}

    markup_func_sphinx3 = {RE_doc: markup_doc_ref,
                           RE_function: markup_func_ref_sphinx3,
                           RE_struct: markup_c_ref,
                           RE_union: markup_c_ref,
                           RE_enum: markup_c_ref,
                           RE_typedef: markup_c_ref}

    if sphinx.version_info[0] >= 3:
        markup_func = markup_func_sphinx3
    else:
        markup_func = markup_func_sphinx2

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
# In sphinx3 we can cross-reference to C macro and function, each one with its
# own C role, but both match the same regex, so we try both.
#
def markup_func_ref_sphinx3(docname, app, match):
    class_str = ['c-func', 'c-macro']
    reftype_str = ['function', 'macro']

    cdom = app.env.domains['c']
    #
    # Go through the dance of getting an xref out of the C domain
    #
    base_target = match.group(2)
    target_text = nodes.Text(match.group(0))
    xref = None
    possible_targets = [base_target]
    # Check if this document has a namespace, and if so, try
    # cross-referencing inside it first.
    if c_namespace:
        possible_targets.insert(0, c_namespace + "." + base_target)

    if base_target not in Skipnames:
        for target in possible_targets:
            if target not in Skipfuncs:
                for class_s, reftype_s in zip(class_str, reftype_str):
                    lit_text = nodes.literal(classes=['xref', 'c', class_s])
                    lit_text += target_text
                    pxref = addnodes.pending_xref('', refdomain = 'c',
                                                  reftype = reftype_s,
                                                  reftarget = target, modname = None,
                                                  classname = None)
                    #
                    # XXX The Latex builder will throw NoUri exceptions here,
                    # work around that by ignoring them.
                    #
                    try:
                        xref = cdom.resolve_xref(app.env, docname, app.builder,
                                                 reftype_s, target, pxref,
                                                 lit_text)
                    except NoUri:
                        xref = None

                    if xref:
                        return xref

    return target_text

def markup_c_ref(docname, app, match):
    class_str = {# Sphinx 2 only
                 RE_function: 'c-func',
                 RE_generic_type: 'c-type',
                 # Sphinx 3+ only
                 RE_struct: 'c-struct',
                 RE_union: 'c-union',
                 RE_enum: 'c-enum',
                 RE_typedef: 'c-type',
                 }
    reftype_str = {# Sphinx 2 only
                   RE_function: 'function',
                   RE_generic_type: 'type',
                   # Sphinx 3+ only
                   RE_struct: 'struct',
                   RE_union: 'union',
                   RE_enum: 'enum',
                   RE_typedef: 'type',
                   }

    cdom = app.env.domains['c']
    #
    # Go through the dance of getting an xref out of the C domain
    #
    base_target = match.group(2)
    target_text = nodes.Text(match.group(0))
    xref = None
    possible_targets = [base_target]
    # Check if this document has a namespace, and if so, try
    # cross-referencing inside it first.
    if c_namespace:
        possible_targets.insert(0, c_namespace + "." + base_target)

    if base_target not in Skipnames:
        for target in possible_targets:
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

                if xref:
                    return xref

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
    absolute = match.group(1)
    target = match.group(2)
    if absolute:
       target = "/" + target
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

def get_c_namespace(app, docname):
    source = app.env.doc2path(docname)
    with open(source) as f:
        for l in f:
            match = RE_namespace.search(l)
            if match:
                return match.group(1)
    return ''

def auto_markup(app, doctree, name):
    global c_namespace
    c_namespace = get_c_namespace(app, name)
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
