# SPDX-License-Identifier: GPL-2.0
# Copyright 2019 Jonathan Corbet <corbet@lwn.net>
#
# Apply kernel-specific tweaks after the initial document processing
# has been done.
#
from docutils import nodes
import sphinx
from sphinx import addnodes
from sphinx.errors import NoUri
import re
from itertools import chain

from kernel_abi import get_kernel_abi

#
# Regex nastiness.  Of course.
# Try to identify "function()" that's not already marked up some
# other way.  Sphinx doesn't like a lot of stuff right after a
# :c:func: block (i.e. ":c:func:`mmap()`s" flakes out), so the last
# bit tries to restrict matches to things that won't create trouble.
#
RE_function = re.compile(r'\b(([a-zA-Z_]\w+)\(\))', flags=re.ASCII)

#
# Sphinx 3 uses a different C role for each one of struct, union, enum and
# typedef
#
RE_struct = re.compile(r'\b(struct)\s+([a-zA-Z_]\w+)', flags=re.ASCII)
RE_union = re.compile(r'\b(union)\s+([a-zA-Z_]\w+)', flags=re.ASCII)
RE_enum = re.compile(r'\b(enum)\s+([a-zA-Z_]\w+)', flags=re.ASCII)
RE_typedef = re.compile(r'\b(typedef)\s+([a-zA-Z_]\w+)', flags=re.ASCII)

#
# Detects a reference to a documentation page of the form Documentation/... with
# an optional extension
#
RE_doc = re.compile(r'(\bDocumentation/)?((\.\./)*[\w\-/]+)\.(rst|txt)')
RE_abi_file = re.compile(r'(\bDocumentation/ABI/[\w\-/]+)')
RE_abi_symbol = re.compile(r'(\b/(sys|config|proc)/[\w\-/]+)')

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

#
# Detect references to commits.
#
RE_git = re.compile(r'commit\s+(?P<rev>[0-9a-f]{12,40})(?:\s+\(".*?"\))?',
    flags=re.IGNORECASE | re.DOTALL)

def markup_refs(docname, app, node):
    t = node.astext()
    done = 0
    repl = [ ]
    #
    # Associate each regex with the function that will markup its matches
    #

    markup_func = {RE_doc: markup_doc_ref,
                           RE_abi_file: markup_abi_file_ref,
                           RE_abi_symbol: markup_abi_ref,
                           RE_function: markup_func_ref_sphinx3,
                           RE_struct: markup_c_ref,
                           RE_union: markup_c_ref,
                           RE_enum: markup_c_ref,
                           RE_typedef: markup_c_ref,
                           RE_git: markup_git}

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
# Keep track of cross-reference lookups that failed so we don't have to
# do them again.
#
failed_lookups = { }
def failure_seen(target):
    return (target) in failed_lookups
def note_failure(target):
    failed_lookups[target] = True

#
# In sphinx3 we can cross-reference to C macro and function, each one with its
# own C role, but both match the same regex, so we try both.
#
def markup_func_ref_sphinx3(docname, app, match):
    base_target = match.group(2)
    target_text = nodes.Text(match.group(0))
    possible_targets = [base_target]
    # Check if this document has a namespace, and if so, try
    # cross-referencing inside it first.
    if c_namespace:
        possible_targets.insert(0, c_namespace + "." + base_target)

    if base_target not in Skipnames:
        for target in possible_targets:
            if (target not in Skipfuncs) and not failure_seen(target):
                lit_text = nodes.literal(classes=['xref', 'c', 'c-func'])
                lit_text += target_text
                xref = add_and_resolve_xref(app, docname, 'c', 'function',
                                            target, contnode=lit_text)
                if xref:
                    return xref
                note_failure(target)

    return target_text

def markup_c_ref(docname, app, match):
    class_str = {RE_struct: 'c-struct',
                 RE_union: 'c-union',
                 RE_enum: 'c-enum',
                 RE_typedef: 'c-type',
                 }
    reftype_str = {RE_struct: 'struct',
                   RE_union: 'union',
                   RE_enum: 'enum',
                   RE_typedef: 'type',
                   }

    base_target = match.group(2)
    target_text = nodes.Text(match.group(0))
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
                xref = add_and_resolve_xref(app, docname, 'c',
                                            reftype_str[match.re], target,
                                            contnode=lit_text)
                if xref:
                    return xref

    return target_text

#
# Try to replace a documentation reference of the form Documentation/... with a
# cross reference to that page
#
def markup_doc_ref(docname, app, match):
    absolute = match.group(1)
    target = match.group(2)
    if absolute:
       target = "/" + target

    xref = add_and_resolve_xref(app, docname, 'std', 'doc', target)
    if xref:
        return xref
    else:
        return nodes.Text(match.group(0))

#
# Try to replace a documentation reference for ABI symbols and files
# with a cross reference to that page
#
def markup_abi_ref(docname, app, match, warning=False):
    kernel_abi = get_kernel_abi()

    fname = match.group(1)
    target = kernel_abi.xref(fname)

    # Kernel ABI doesn't describe such file or symbol
    if not target:
        if warning:
            kernel_abi.log.warning("%s not found", fname)
        return nodes.Text(match.group(0))

    xref = add_and_resolve_xref(app, docname, 'std', 'ref', target)
    if xref:
        return xref
    else:
        return nodes.Text(match.group(0))

def add_and_resolve_xref(app, docname, domain, reftype, target, contnode=None):
    #
    # Go through the dance of getting an xref out of the corresponding domain
    #
    dom_obj = app.env.domains[domain]
    pxref = addnodes.pending_xref('', refdomain = domain, reftype = reftype,
                                  reftarget = target, modname = None,
                                  classname = None, refexplicit = False)

    #
    # XXX The Latex builder will throw NoUri exceptions here,
    # work around that by ignoring them.
    #
    try:
        xref = dom_obj.resolve_xref(app.env, docname, app.builder, reftype,
                                    target, pxref, contnode)
    except NoUri:
        xref = None

    if xref:
        return xref
    #
    # We didn't find the xref; if a container node was supplied,
    # mark it as a broken xref
    #
    if contnode:
        contnode['classes'].append("broken_xref")
    return contnode

#
# Variant of markup_abi_ref() that warns when a reference is not found
#
def markup_abi_file_ref(docname, app, match):
    return markup_abi_ref(docname, app, match, warning=True)


def get_c_namespace(app, docname):
    source = app.env.doc2path(docname)
    with open(source) as f:
        for l in f:
            match = RE_namespace.search(l)
            if match:
                return match.group(1)
    return ''

def markup_git(docname, app, match):
    # While we could probably assume that we are running in a git
    # repository, we can't know for sure, so let's just mechanically
    # turn them into git.kernel.org links without checking their
    # validity. (Maybe we can do something in the future to warn about
    # these references if this is explicitly requested.)
    text = match.group(0)
    rev = match.group('rev')
    return nodes.reference('', nodes.Text(text),
        refuri=f'https://git.kernel.org/torvalds/c/{rev}')

def auto_markup(app, doctree, name):
    global c_namespace
    c_namespace = get_c_namespace(app, name)
    def text_but_not_a_reference(node):
        # The nodes.literal test catches ``literal text``, its purpose is to
        # avoid adding cross-references to functions that have been explicitly
        # marked with cc:func:.
        if not isinstance(node, nodes.Text) or isinstance(node.parent, nodes.literal):
            return False

        child_of_reference = False
        parent = node.parent
        while parent:
            if isinstance(parent, nodes.Referential):
                child_of_reference = True
                break
            parent = parent.parent
        return not child_of_reference

    #
    # This loop could eventually be improved on.  Someday maybe we
    # want a proper tree traversal with a lot of awareness of which
    # kinds of nodes to prune.  But this works well for now.
    #
    for para in doctree.traverse(nodes.paragraph):
        for node in para.traverse(condition=text_but_not_a_reference):
            node.parent.replace(node, markup_refs(name, app, node))

def setup(app):
    app.connect('doctree-resolved', auto_markup)
    return {
        'parallel_read_safe': True,
        'parallel_write_safe': True,
        }
