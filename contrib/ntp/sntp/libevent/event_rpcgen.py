#!/usr/bin/env python2
#
# Copyright (c) 2005-2007 Niels Provos <provos@citi.umich.edu>
# Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
# All rights reserved.
#
# Generates marshaling code based on libevent.

# TODO:
# 1) use optparse to allow the strategy shell to parse options, and
#    to allow the instantiated factory (for the specific output language)
#    to parse remaining options
# 2) move the globals into a class that manages execution (including the
#    progress outputs that space stderr at the moment)
# 3) emit other languages

import sys
import re

_NAME = "event_rpcgen.py"
_VERSION = "0.1"

# Globals
line_count = 0

white = re.compile(r'\s+')
cppcomment = re.compile(r'\/\/.*$')
nonident = re.compile(r'[^a-zA-Z0-9_]')
structref = re.compile(r'^struct\[([a-zA-Z_][a-zA-Z0-9_]*)\]$')
structdef = re.compile(r'^struct +[a-zA-Z_][a-zA-Z0-9_]* *{$')

headerdirect = []
cppdirect = []

QUIETLY = 0

def declare(s):
    if not QUIETLY:
        print s

def TranslateList(mylist, mydict):
    return map(lambda x: x % mydict, mylist)

# Exception class for parse errors
class RpcGenError(Exception):
        def __init__(self, why):
                self.why = why
        def __str__(self):
                return str(self.why)

# Holds everything that makes a struct
class Struct:
    def __init__(self, name):
        self._name = name
        self._entries = []
        self._tags = {}
        declare('  Created struct: %s' % name)

    def AddEntry(self, entry):
        if self._tags.has_key(entry.Tag()):
            raise RpcGenError(
                'Entry "%s" duplicates tag number %d from "%s" '
                'around line %d' % (entry.Name(), entry.Tag(),
                                    self._tags[entry.Tag()], line_count))
        self._entries.append(entry)
        self._tags[entry.Tag()] = entry.Name()
        declare('    Added entry: %s' % entry.Name())

    def Name(self):
        return self._name

    def EntryTagName(self, entry):
        """Creates the name inside an enumeration for distinguishing data
        types."""
        name = "%s_%s" % (self._name, entry.Name())
        return name.upper()

    def PrintIndented(self, file, ident, code):
        """Takes an array, add indentation to each entry and prints it."""
        for entry in code:
            print >>file, '%s%s' % (ident, entry)

class StructCCode(Struct):
    """ Knows how to generate C code for a struct """

    def __init__(self, name):
        Struct.__init__(self, name)

    def PrintTags(self, file):
        """Prints the tag definitions for a structure."""
        print >>file, '/* Tag definition for %s */' % self._name
        print >>file, 'enum %s_ {' % self._name.lower()
        for entry in self._entries:
            print >>file, '  %s=%d,' % (self.EntryTagName(entry),
                                        entry.Tag())
        print >>file, '  %s_MAX_TAGS' % (self._name.upper())
        print >>file, '};\n'

    def PrintForwardDeclaration(self, file):
        print >>file, 'struct %s;' % self._name

    def PrintDeclaration(self, file):
        print >>file, '/* Structure declaration for %s */' % self._name
        print >>file, 'struct %s_access_ {' % self._name
        for entry in self._entries:
            dcl = entry.AssignDeclaration('(*%s_assign)' % entry.Name())
            dcl.extend(
                entry.GetDeclaration('(*%s_get)' % entry.Name()))
            if entry.Array():
                dcl.extend(
                    entry.AddDeclaration('(*%s_add)' % entry.Name()))
            self.PrintIndented(file, '  ', dcl)
        print >>file, '};\n'

        print >>file, 'struct %s {' % self._name
        print >>file, '  struct %s_access_ *base;\n' % self._name
        for entry in self._entries:
            dcl = entry.Declaration()
            self.PrintIndented(file, '  ', dcl)
        print >>file, ''
        for entry in self._entries:
            print >>file, '  ev_uint8_t %s_set;' % entry.Name()
        print >>file, '};\n'

        print >>file, \
"""struct %(name)s *%(name)s_new(void);
struct %(name)s *%(name)s_new_with_arg(void *);
void %(name)s_free(struct %(name)s *);
void %(name)s_clear(struct %(name)s *);
void %(name)s_marshal(struct evbuffer *, const struct %(name)s *);
int %(name)s_unmarshal(struct %(name)s *, struct evbuffer *);
int %(name)s_complete(struct %(name)s *);
void evtag_marshal_%(name)s(struct evbuffer *, ev_uint32_t,
    const struct %(name)s *);
int evtag_unmarshal_%(name)s(struct evbuffer *, ev_uint32_t,
    struct %(name)s *);""" % { 'name' : self._name }


        # Write a setting function of every variable
        for entry in self._entries:
            self.PrintIndented(file, '', entry.AssignDeclaration(
                entry.AssignFuncName()))
            self.PrintIndented(file, '', entry.GetDeclaration(
                entry.GetFuncName()))
            if entry.Array():
                self.PrintIndented(file, '', entry.AddDeclaration(
                    entry.AddFuncName()))

        print >>file, '/* --- %s done --- */\n' % self._name

    def PrintCode(self, file):
        print >>file, ('/*\n'
                       ' * Implementation of %s\n'
                       ' */\n') % self._name

        print >>file, \
              'static struct %(name)s_access_ %(name)s_base__ = {' % \
              { 'name' : self._name }
        for entry in self._entries:
            self.PrintIndented(file, '  ', entry.CodeBase())
        print >>file, '};\n'

        # Creation
        print >>file, (
            'struct %(name)s *\n'
            '%(name)s_new(void)\n'
            '{\n'
            '  return %(name)s_new_with_arg(NULL);\n'
            '}\n'
            '\n'
            'struct %(name)s *\n'
            '%(name)s_new_with_arg(void *unused)\n'
            '{\n'
            '  struct %(name)s *tmp;\n'
            '  if ((tmp = malloc(sizeof(struct %(name)s))) == NULL) {\n'
            '    event_warn("%%s: malloc", __func__);\n'
            '    return (NULL);\n'
            '  }\n'
            '  tmp->base = &%(name)s_base__;\n') % { 'name' : self._name }

        for entry in self._entries:
            self.PrintIndented(file, '  ', entry.CodeInitialize('tmp'))
            print >>file, '  tmp->%s_set = 0;\n' % entry.Name()

        print >>file, (
            '  return (tmp);\n'
            '}\n')

        # Adding
        for entry in self._entries:
            if entry.Array():
                self.PrintIndented(file, '', entry.CodeAdd())
            print >>file, ''

        # Assigning
        for entry in self._entries:
            self.PrintIndented(file, '', entry.CodeAssign())
            print >>file, ''

        # Getting
        for entry in self._entries:
            self.PrintIndented(file, '', entry.CodeGet())
            print >>file, ''

        # Clearing
        print >>file, ( 'void\n'
                        '%(name)s_clear(struct %(name)s *tmp)\n'
                        '{'
                        ) % { 'name' : self._name }
        for entry in self._entries:
            self.PrintIndented(file, '  ', entry.CodeClear('tmp'))

        print >>file, '}\n'

        # Freeing
        print >>file, ( 'void\n'
                        '%(name)s_free(struct %(name)s *tmp)\n'
                        '{'
                        ) % { 'name' : self._name }

        for entry in self._entries:
            self.PrintIndented(file, '  ', entry.CodeFree('tmp'))

        print >>file, ('  free(tmp);\n'
                       '}\n')

        # Marshaling
        print >>file, ('void\n'
                       '%(name)s_marshal(struct evbuffer *evbuf, '
                       'const struct %(name)s *tmp)'
                       '{') % { 'name' : self._name }
        for entry in self._entries:
            indent = '  '
            # Optional entries do not have to be set
            if entry.Optional():
                indent += '  '
                print >>file, '  if (tmp->%s_set) {' % entry.Name()
            self.PrintIndented(
                file, indent,
                entry.CodeMarshal('evbuf', self.EntryTagName(entry),
                                  entry.GetVarName('tmp'),
                                  entry.GetVarLen('tmp')))
            if entry.Optional():
                print >>file, '  }'

        print >>file, '}\n'

        # Unmarshaling
        print >>file, ('int\n'
                       '%(name)s_unmarshal(struct %(name)s *tmp, '
                       ' struct evbuffer *evbuf)\n'
                       '{\n'
                       '  ev_uint32_t tag;\n'
                       '  while (evbuffer_get_length(evbuf) > 0) {\n'
                       '    if (evtag_peek(evbuf, &tag) == -1)\n'
                       '      return (-1);\n'
                       '    switch (tag) {\n'
                       ) % { 'name' : self._name }
        for entry in self._entries:
            print >>file, '      case %s:\n' % self.EntryTagName(entry)
            if not entry.Array():
                print >>file, (
                    '        if (tmp->%s_set)\n'
                    '          return (-1);'
                    ) % (entry.Name())

            self.PrintIndented(
                file, '        ',
                entry.CodeUnmarshal('evbuf',
                                    self.EntryTagName(entry),
                                    entry.GetVarName('tmp'),
                                    entry.GetVarLen('tmp')))

            print >>file, ( '        tmp->%s_set = 1;\n' % entry.Name() +
                            '        break;\n' )
        print >>file, ( '      default:\n'
                        '        return -1;\n'
                        '    }\n'
                        '  }\n' )
        # Check if it was decoded completely
        print >>file, ( '  if (%(name)s_complete(tmp) == -1)\n'
                        '    return (-1);'
                        ) % { 'name' : self._name }

        # Successfully decoded
        print >>file, ( '  return (0);\n'
                        '}\n')

        # Checking if a structure has all the required data
        print >>file, (
            'int\n'
            '%(name)s_complete(struct %(name)s *msg)\n'
            '{' ) % { 'name' : self._name }
        for entry in self._entries:
            if not entry.Optional():
                code = [
                    'if (!msg->%(name)s_set)',
                    '  return (-1);' ]
                code = TranslateList(code, entry.GetTranslation())
                self.PrintIndented(
                    file, '  ', code)

            self.PrintIndented(
                file, '  ',
                entry.CodeComplete('msg', entry.GetVarName('msg')))
        print >>file, (
            '  return (0);\n'
            '}\n' )

        # Complete message unmarshaling
        print >>file, (
            'int\n'
            'evtag_unmarshal_%(name)s(struct evbuffer *evbuf, '
            'ev_uint32_t need_tag, struct %(name)s *msg)\n'
            '{\n'
            '  ev_uint32_t tag;\n'
            '  int res = -1;\n'
            '\n'
            '  struct evbuffer *tmp = evbuffer_new();\n'
            '\n'
            '  if (evtag_unmarshal(evbuf, &tag, tmp) == -1'
            ' || tag != need_tag)\n'
            '    goto error;\n'
            '\n'
            '  if (%(name)s_unmarshal(msg, tmp) == -1)\n'
            '    goto error;\n'
            '\n'
            '  res = 0;\n'
            '\n'
            ' error:\n'
            '  evbuffer_free(tmp);\n'
            '  return (res);\n'
            '}\n' ) % { 'name' : self._name }

        # Complete message marshaling
        print >>file, (
            'void\n'
            'evtag_marshal_%(name)s(struct evbuffer *evbuf, ev_uint32_t tag, '
            'const struct %(name)s *msg)\n'
            '{\n'
            '  struct evbuffer *buf_ = evbuffer_new();\n'
            '  assert(buf_ != NULL);\n'
            '  %(name)s_marshal(buf_, msg);\n'
            '  evtag_marshal_buffer(evbuf, tag, buf_);\n '
            '  evbuffer_free(buf_);\n'
            '}\n' ) % { 'name' : self._name }

class Entry:
    def __init__(self, type, name, tag):
        self._type = type
        self._name = name
        self._tag = int(tag)
        self._ctype = type
        self._optional = 0
        self._can_be_array = 0
        self._array = 0
        self._line_count = -1
        self._struct = None
        self._refname = None

        self._optpointer = True
        self._optaddarg = True

    def GetInitializer(self):
        assert 0, "Entry does not provide initializer"

    def SetStruct(self, struct):
        self._struct = struct

    def LineCount(self):
        assert self._line_count != -1
        return self._line_count

    def SetLineCount(self, number):
        self._line_count = number

    def Array(self):
        return self._array

    def Optional(self):
        return self._optional

    def Tag(self):
        return self._tag

    def Name(self):
        return self._name

    def Type(self):
        return self._type

    def MakeArray(self, yes=1):
        self._array = yes

    def MakeOptional(self):
        self._optional = 1

    def Verify(self):
        if self.Array() and not self._can_be_array:
            raise RpcGenError(
                'Entry "%s" cannot be created as an array '
                'around line %d' % (self._name, self.LineCount()))
        if not self._struct:
            raise RpcGenError(
                'Entry "%s" does not know which struct it belongs to '
                'around line %d' % (self._name, self.LineCount()))
        if self._optional and self._array:
            raise RpcGenError(
                'Entry "%s" has illegal combination of optional and array '
                'around line %d' % (self._name, self.LineCount()))

    def GetTranslation(self, extradict = {}):
        mapping = {
            "parent_name" : self._struct.Name(),
            "name" : self._name,
            "ctype" : self._ctype,
            "refname" : self._refname,
            "optpointer" : self._optpointer and "*" or "",
            "optreference" : self._optpointer and "&" or "",
            "optaddarg" :
            self._optaddarg and ", const %s value" % self._ctype or ""
            }
        for (k, v) in extradict.items():
            mapping[k] = v

        return mapping

    def GetVarName(self, var):
        return '%(var)s->%(name)s_data' % self.GetTranslation({ 'var' : var })

    def GetVarLen(self, var):
        return 'sizeof(%s)' % self._ctype

    def GetFuncName(self):
        return '%s_%s_get' % (self._struct.Name(), self._name)

    def GetDeclaration(self, funcname):
        code = [ 'int %s(struct %s *, %s *);' % (
            funcname, self._struct.Name(), self._ctype ) ]
        return code

    def CodeGet(self):
        code = (
            'int',
            '%(parent_name)s_%(name)s_get(struct %(parent_name)s *msg, '
            '%(ctype)s *value)',
            '{',
            '  if (msg->%(name)s_set != 1)',
            '    return (-1);',
            '  *value = msg->%(name)s_data;',
            '  return (0);',
            '}' )
        code = '\n'.join(code)
        code = code % self.GetTranslation()
        return code.split('\n')

    def AssignFuncName(self):
        return '%s_%s_assign' % (self._struct.Name(), self._name)

    def AddFuncName(self):
        return '%s_%s_add' % (self._struct.Name(), self._name)

    def AssignDeclaration(self, funcname):
        code = [ 'int %s(struct %s *, const %s);' % (
            funcname, self._struct.Name(), self._ctype ) ]
        return code

    def CodeAssign(self):
        code = [ 'int',
                 '%(parent_name)s_%(name)s_assign(struct %(parent_name)s *msg,'
                 ' const %(ctype)s value)',
                 '{',
                 '  msg->%(name)s_set = 1;',
                 '  msg->%(name)s_data = value;',
                 '  return (0);',
                 '}' ]
        code = '\n'.join(code)
        code = code % self.GetTranslation()
        return code.split('\n')

    def CodeClear(self, structname):
        code = [ '%s->%s_set = 0;' % (structname, self.Name()) ]

        return code

    def CodeComplete(self, structname, var_name):
        return []

    def CodeFree(self, name):
        return []

    def CodeBase(self):
        code = [
            '%(parent_name)s_%(name)s_assign,',
            '%(parent_name)s_%(name)s_get,'
            ]
        if self.Array():
            code.append('%(parent_name)s_%(name)s_add,')

        code = '\n'.join(code)
        code = code % self.GetTranslation()
        return code.split('\n')

class EntryBytes(Entry):
    def __init__(self, type, name, tag, length):
        # Init base class
        Entry.__init__(self, type, name, tag)

        self._length = length
        self._ctype = 'ev_uint8_t'

    def GetInitializer(self):
        return "NULL"

    def GetVarLen(self, var):
        return '(%s)' % self._length

    def CodeArrayAdd(self, varname, value):
        # XXX: copy here
        return [ '%(varname)s = NULL;' % { 'varname' : varname } ]

    def GetDeclaration(self, funcname):
        code = [ 'int %s(struct %s *, %s **);' % (
            funcname, self._struct.Name(), self._ctype ) ]
        return code

    def AssignDeclaration(self, funcname):
        code = [ 'int %s(struct %s *, const %s *);' % (
            funcname, self._struct.Name(), self._ctype ) ]
        return code

    def Declaration(self):
        dcl  = ['ev_uint8_t %s_data[%s];' % (self._name, self._length)]

        return dcl

    def CodeGet(self):
        name = self._name
        code = [ 'int',
                 '%s_%s_get(struct %s *msg, %s **value)' % (
            self._struct.Name(), name,
            self._struct.Name(), self._ctype),
                 '{',
                 '  if (msg->%s_set != 1)' % name,
                 '    return (-1);',
                 '  *value = msg->%s_data;' % name,
                 '  return (0);',
                 '}' ]
        return code

    def CodeAssign(self):
        name = self._name
        code = [ 'int',
                 '%s_%s_assign(struct %s *msg, const %s *value)' % (
            self._struct.Name(), name,
            self._struct.Name(), self._ctype),
                 '{',
                 '  msg->%s_set = 1;' % name,
                 '  memcpy(msg->%s_data, value, %s);' % (
            name, self._length),
                 '  return (0);',
                 '}' ]
        return code

    def CodeUnmarshal(self, buf, tag_name, var_name, var_len):
        code = [  'if (evtag_unmarshal_fixed(%(buf)s, %(tag)s, '
                  '%(var)s, %(varlen)s) == -1) {',
                  '  event_warnx("%%s: failed to unmarshal %(name)s", __func__);',
                  '  return (-1);',
                  '}'
                  ]
        return TranslateList(code,
                             self.GetTranslation({
            'var' : var_name,
            'varlen' : var_len,
            'buf' : buf,
            'tag' : tag_name }))

    def CodeMarshal(self, buf, tag_name, var_name, var_len):
        code = ['evtag_marshal(%s, %s, %s, %s);' % (
            buf, tag_name, var_name, var_len)]
        return code

    def CodeClear(self, structname):
        code = [ '%s->%s_set = 0;' % (structname, self.Name()),
                 'memset(%s->%s_data, 0, sizeof(%s->%s_data));' % (
            structname, self._name, structname, self._name)]

        return code

    def CodeInitialize(self, name):
        code  = ['memset(%s->%s_data, 0, sizeof(%s->%s_data));' % (
            name, self._name, name, self._name)]
        return code

    def Verify(self):
        if not self._length:
            raise RpcGenError(
                'Entry "%s" needs a length '
                'around line %d' % (self._name, self.LineCount()))

        Entry.Verify(self)

class EntryInt(Entry):
    def __init__(self, type, name, tag, bits=32):
        # Init base class
        Entry.__init__(self, type, name, tag)

        self._can_be_array = 1
        if bits == 32:
            self._ctype = 'ev_uint32_t'
            self._marshal_type = 'int'
        if bits == 64:
            self._ctype = 'ev_uint64_t'
            self._marshal_type = 'int64'

    def GetInitializer(self):
        return "0"

    def CodeArrayFree(self, var):
        return []

    def CodeArrayAssign(self, varname, srcvar):
        return [ '%(varname)s = %(srcvar)s;' % { 'varname' : varname,
                                                'srcvar' : srcvar } ]

    def CodeArrayAdd(self, varname, value):
        """Returns a new entry of this type."""
        return [ '%(varname)s = %(value)s;' % { 'varname' : varname,
                                              'value' : value } ]

    def CodeUnmarshal(self, buf, tag_name, var_name, var_len):
        code = [
            'if (evtag_unmarshal_%(ma)s(%(buf)s, %(tag)s, &%(var)s) == -1) {',
            '  event_warnx("%%s: failed to unmarshal %(name)s", __func__);',
            '  return (-1);',
            '}' ]
        code = '\n'.join(code) % self.GetTranslation({
            'ma'  : self._marshal_type,
            'buf' : buf,
            'tag' : tag_name,
            'var' : var_name })
        return code.split('\n')

    def CodeMarshal(self, buf, tag_name, var_name, var_len):
        code = [
            'evtag_marshal_%s(%s, %s, %s);' % (
            self._marshal_type, buf, tag_name, var_name)]
        return code

    def Declaration(self):
        dcl  = ['%s %s_data;' % (self._ctype, self._name)]

        return dcl

    def CodeInitialize(self, name):
        code = ['%s->%s_data = 0;' % (name, self._name)]
        return code

class EntryString(Entry):
    def __init__(self, type, name, tag):
        # Init base class
        Entry.__init__(self, type, name, tag)

        self._can_be_array = 1
        self._ctype = 'char *'

    def GetInitializer(self):
        return "NULL"

    def CodeArrayFree(self, varname):
        code = [
            'if (%(var)s != NULL) free(%(var)s);' ]

        return TranslateList(code, { 'var' : varname })

    def CodeArrayAssign(self, varname, srcvar):
        code = [
            'if (%(var)s != NULL)',
            '  free(%(var)s);',
            '%(var)s = strdup(%(srcvar)s);',
            'if (%(var)s == NULL) {',
            '  event_warnx("%%s: strdup", __func__);',
            '  return (-1);',
            '}' ]

        return TranslateList(code, { 'var' : varname,
                                     'srcvar' : srcvar })

    def CodeArrayAdd(self, varname, value):
        code = [
            'if (%(value)s != NULL) {',
            '  %(var)s = strdup(%(value)s);',
            '  if (%(var)s == NULL) {',
            '    goto error;',
            '  }',
            '} else {',
            '  %(var)s = NULL;',
            '}' ]

        return TranslateList(code, { 'var' : varname,
                                     'value' : value })

    def GetVarLen(self, var):
        return 'strlen(%s)' % self.GetVarName(var)

    def CodeMakeInitalize(self, varname):
        return '%(varname)s = NULL;' % { 'varname' : varname }

    def CodeAssign(self):
        name = self._name
        code = """int
%(parent_name)s_%(name)s_assign(struct %(parent_name)s *msg,
    const %(ctype)s value)
{
  if (msg->%(name)s_data != NULL)
    free(msg->%(name)s_data);
  if ((msg->%(name)s_data = strdup(value)) == NULL)
    return (-1);
  msg->%(name)s_set = 1;
  return (0);
}""" % self.GetTranslation()

        return code.split('\n')

    def CodeUnmarshal(self, buf, tag_name, var_name, var_len):
        code = ['if (evtag_unmarshal_string(%(buf)s, %(tag)s, &%(var)s) == -1) {',
                '  event_warnx("%%s: failed to unmarshal %(name)s", __func__);',
                '  return (-1);',
                '}'
                ]
        code = '\n'.join(code) % self.GetTranslation({
            'buf' : buf,
            'tag' : tag_name,
            'var' : var_name })
        return code.split('\n')

    def CodeMarshal(self, buf, tag_name, var_name, var_len):
        code = ['evtag_marshal_string(%s, %s, %s);' % (
            buf, tag_name, var_name)]
        return code

    def CodeClear(self, structname):
        code = [ 'if (%s->%s_set == 1) {' % (structname, self.Name()),
                 '  free(%s->%s_data);' % (structname, self.Name()),
                 '  %s->%s_data = NULL;' % (structname, self.Name()),
                 '  %s->%s_set = 0;' % (structname, self.Name()),
                 '}'
                 ]

        return code

    def CodeInitialize(self, name):
        code  = ['%s->%s_data = NULL;' % (name, self._name)]
        return code

    def CodeFree(self, name):
        code  = ['if (%s->%s_data != NULL)' % (name, self._name),
                 '    free (%s->%s_data);' % (name, self._name)]

        return code

    def Declaration(self):
        dcl  = ['char *%s_data;' % self._name]

        return dcl

class EntryStruct(Entry):
    def __init__(self, type, name, tag, refname):
        # Init base class
        Entry.__init__(self, type, name, tag)

        self._optpointer = False
        self._can_be_array = 1
        self._refname = refname
        self._ctype = 'struct %s*' % refname
        self._optaddarg = False

    def GetInitializer(self):
        return "NULL"

    def GetVarLen(self, var):
        return '-1'

    def CodeArrayAdd(self, varname, value):
        code = [
            '%(varname)s = %(refname)s_new();',
            'if (%(varname)s == NULL)',
            '  goto error;' ]

        return TranslateList(code, self.GetTranslation({ 'varname' : varname }))

    def CodeArrayFree(self, var):
        code = [ '%(refname)s_free(%(var)s);' % self.GetTranslation(
            { 'var' : var }) ]
        return code

    def CodeArrayAssign(self, var, srcvar):
        code = [
            'int had_error = 0;',
            'struct evbuffer *tmp = NULL;',
            '%(refname)s_clear(%(var)s);',
            'if ((tmp = evbuffer_new()) == NULL) {',
            '  event_warn("%%s: evbuffer_new()", __func__);',
            '  had_error = 1;',
            '  goto done;',
            '}',
            '%(refname)s_marshal(tmp, %(srcvar)s);',
            'if (%(refname)s_unmarshal(%(var)s, tmp) == -1) {',
            '  event_warnx("%%s: %(refname)s_unmarshal", __func__);',
            '  had_error = 1;',
            '  goto done;',
            '}',
            'done:'
            'if (tmp != NULL)',
            '  evbuffer_free(tmp);',
            'if (had_error) {',
            '  %(refname)s_clear(%(var)s);',
            '  return (-1);',
            '}' ]

        return TranslateList(code, self.GetTranslation({
            'var' : var,
            'srcvar' : srcvar}))

    def CodeGet(self):
        name = self._name
        code = [ 'int',
                 '%s_%s_get(struct %s *msg, %s *value)' % (
            self._struct.Name(), name,
            self._struct.Name(), self._ctype),
                 '{',
                 '  if (msg->%s_set != 1) {' % name,
                 '    msg->%s_data = %s_new();' % (name, self._refname),
                 '    if (msg->%s_data == NULL)' % name,
                 '      return (-1);',
                 '    msg->%s_set = 1;' % name,
                 '  }',
                 '  *value = msg->%s_data;' % name,
                 '  return (0);',
                 '}' ]
        return code

    def CodeAssign(self):
        name = self._name
        code = """int
%(parent_name)s_%(name)s_assign(struct %(parent_name)s *msg,
    const %(ctype)s value)
{
   struct evbuffer *tmp = NULL;
   if (msg->%(name)s_set) {
     %(refname)s_clear(msg->%(name)s_data);
     msg->%(name)s_set = 0;
   } else {
     msg->%(name)s_data = %(refname)s_new();
     if (msg->%(name)s_data == NULL) {
       event_warn("%%s: %(refname)s_new()", __func__);
       goto error;
     }
   }
   if ((tmp = evbuffer_new()) == NULL) {
     event_warn("%%s: evbuffer_new()", __func__);
     goto error;
   }
   %(refname)s_marshal(tmp, value);
   if (%(refname)s_unmarshal(msg->%(name)s_data, tmp) == -1) {
     event_warnx("%%s: %(refname)s_unmarshal", __func__);
     goto error;
   }
   msg->%(name)s_set = 1;
   evbuffer_free(tmp);
   return (0);
 error:
   if (tmp != NULL)
     evbuffer_free(tmp);
   if (msg->%(name)s_data != NULL) {
     %(refname)s_free(msg->%(name)s_data);
     msg->%(name)s_data = NULL;
   }
   return (-1);
}""" % self.GetTranslation()
        return code.split('\n')

    def CodeComplete(self, structname, var_name):
        code = [ 'if (%(structname)s->%(name)s_set && '
                 '%(refname)s_complete(%(var)s) == -1)',
                 '  return (-1);' ]

        return TranslateList(code, self.GetTranslation({
            'structname' : structname,
            'var' : var_name }))

    def CodeUnmarshal(self, buf, tag_name, var_name, var_len):
        code = ['%(var)s = %(refname)s_new();',
                'if (%(var)s == NULL)',
                '  return (-1);',
                'if (evtag_unmarshal_%(refname)s(%(buf)s, %(tag)s, '
                '%(var)s) == -1) {',
                  '  event_warnx("%%s: failed to unmarshal %(name)s", __func__);',
                '  return (-1);',
                '}'
                ]
        code = '\n'.join(code) % self.GetTranslation({
            'buf' : buf,
            'tag' : tag_name,
            'var' : var_name })
        return code.split('\n')

    def CodeMarshal(self, buf, tag_name, var_name, var_len):
        code = ['evtag_marshal_%s(%s, %s, %s);' % (
            self._refname, buf, tag_name, var_name)]
        return code

    def CodeClear(self, structname):
        code = [ 'if (%s->%s_set == 1) {' % (structname, self.Name()),
                 '  %s_free(%s->%s_data);' % (
            self._refname, structname, self.Name()),
                 '  %s->%s_data = NULL;' % (structname, self.Name()),
                 '  %s->%s_set = 0;' % (structname, self.Name()),
                 '}'
                 ]

        return code

    def CodeInitialize(self, name):
        code  = ['%s->%s_data = NULL;' % (name, self._name)]
        return code

    def CodeFree(self, name):
        code  = ['if (%s->%s_data != NULL)' % (name, self._name),
                 '    %s_free(%s->%s_data);' % (
            self._refname, name, self._name)]

        return code

    def Declaration(self):
        dcl  = ['%s %s_data;' % (self._ctype, self._name)]

        return dcl

class EntryVarBytes(Entry):
    def __init__(self, type, name, tag):
        # Init base class
        Entry.__init__(self, type, name, tag)

        self._ctype = 'ev_uint8_t *'

    def GetInitializer(self):
        return "NULL"

    def GetVarLen(self, var):
        return '%(var)s->%(name)s_length' % self.GetTranslation({ 'var' : var })

    def CodeArrayAdd(self, varname, value):
        # xxx: copy
        return [ '%(varname)s = NULL;' % { 'varname' : varname } ]

    def GetDeclaration(self, funcname):
        code = [ 'int %s(struct %s *, %s *, ev_uint32_t *);' % (
            funcname, self._struct.Name(), self._ctype ) ]
        return code

    def AssignDeclaration(self, funcname):
        code = [ 'int %s(struct %s *, const %s, ev_uint32_t);' % (
            funcname, self._struct.Name(), self._ctype ) ]
        return code

    def CodeAssign(self):
        name = self._name
        code = [ 'int',
                 '%s_%s_assign(struct %s *msg, '
                 'const %s value, ev_uint32_t len)' % (
            self._struct.Name(), name,
            self._struct.Name(), self._ctype),
                 '{',
                 '  if (msg->%s_data != NULL)' % name,
                 '    free (msg->%s_data);' % name,
                 '  msg->%s_data = malloc(len);' % name,
                 '  if (msg->%s_data == NULL)' % name,
                 '    return (-1);',
                 '  msg->%s_set = 1;' % name,
                 '  msg->%s_length = len;' % name,
                 '  memcpy(msg->%s_data, value, len);' % name,
                 '  return (0);',
                 '}' ]
        return code

    def CodeGet(self):
        name = self._name
        code = [ 'int',
                 '%s_%s_get(struct %s *msg, %s *value, ev_uint32_t *plen)' % (
            self._struct.Name(), name,
            self._struct.Name(), self._ctype),
                 '{',
                 '  if (msg->%s_set != 1)' % name,
                 '    return (-1);',
                 '  *value = msg->%s_data;' % name,
                 '  *plen = msg->%s_length;' % name,
                 '  return (0);',
                 '}' ]
        return code

    def CodeUnmarshal(self, buf, tag_name, var_name, var_len):
        code = ['if (evtag_payload_length(%(buf)s, &%(varlen)s) == -1)',
                '  return (-1);',
                # We do not want DoS opportunities
                'if (%(varlen)s > evbuffer_get_length(%(buf)s))',
                '  return (-1);',
                'if ((%(var)s = malloc(%(varlen)s)) == NULL)',
                '  return (-1);',
                'if (evtag_unmarshal_fixed(%(buf)s, %(tag)s, %(var)s, '
                '%(varlen)s) == -1) {',
                '  event_warnx("%%s: failed to unmarshal %(name)s", __func__);',
                '  return (-1);',
                '}'
                ]
        code = '\n'.join(code) % self.GetTranslation({
            'buf' : buf,
            'tag' : tag_name,
            'var' : var_name,
            'varlen' : var_len })
        return code.split('\n')

    def CodeMarshal(self, buf, tag_name, var_name, var_len):
        code = ['evtag_marshal(%s, %s, %s, %s);' % (
            buf, tag_name, var_name, var_len)]
        return code

    def CodeClear(self, structname):
        code = [ 'if (%s->%s_set == 1) {' % (structname, self.Name()),
                 '  free (%s->%s_data);' % (structname, self.Name()),
                 '  %s->%s_data = NULL;' % (structname, self.Name()),
                 '  %s->%s_length = 0;' % (structname, self.Name()),
                 '  %s->%s_set = 0;' % (structname, self.Name()),
                 '}'
                 ]

        return code

    def CodeInitialize(self, name):
        code  = ['%s->%s_data = NULL;' % (name, self._name),
                 '%s->%s_length = 0;' % (name, self._name) ]
        return code

    def CodeFree(self, name):
        code  = ['if (%s->%s_data != NULL)' % (name, self._name),
                 '    free(%s->%s_data);' % (name, self._name)]

        return code

    def Declaration(self):
        dcl  = ['ev_uint8_t *%s_data;' % self._name,
                'ev_uint32_t %s_length;' % self._name]

        return dcl

class EntryArray(Entry):
    def __init__(self, entry):
        # Init base class
        Entry.__init__(self, entry._type, entry._name, entry._tag)

        self._entry = entry
        self._refname = entry._refname
        self._ctype = self._entry._ctype
        self._optional = True
        self._optpointer = self._entry._optpointer
        self._optaddarg = self._entry._optaddarg

        # provide a new function for accessing the variable name
        def GetVarName(var_name):
            return '%(var)s->%(name)s_data[%(index)s]' % \
                   self._entry.GetTranslation({'var' : var_name,
                                               'index' : self._index})
        self._entry.GetVarName = GetVarName

    def GetInitializer(self):
        return "NULL"

    def GetVarName(self, var_name):
        return var_name

    def GetVarLen(self, var_name):
        return '-1'

    def GetDeclaration(self, funcname):
        """Allows direct access to elements of the array."""
        code = [
            'int %(funcname)s(struct %(parent_name)s *, int, %(ctype)s *);' %
            self.GetTranslation({ 'funcname' : funcname }) ]
        return code

    def AssignDeclaration(self, funcname):
        code = [ 'int %s(struct %s *, int, const %s);' % (
            funcname, self._struct.Name(), self._ctype ) ]
        return code

    def AddDeclaration(self, funcname):
        code = [
            '%(ctype)s %(optpointer)s '
            '%(funcname)s(struct %(parent_name)s *msg%(optaddarg)s);' % \
            self.GetTranslation({ 'funcname' : funcname }) ]
        return code

    def CodeGet(self):
        code = """int
%(parent_name)s_%(name)s_get(struct %(parent_name)s *msg, int offset,
    %(ctype)s *value)
{
  if (!msg->%(name)s_set || offset < 0 || offset >= msg->%(name)s_length)
    return (-1);
  *value = msg->%(name)s_data[offset];
  return (0);
}""" % self.GetTranslation()

        return code.split('\n')

    def CodeAssign(self):
        code = [
            'int',
            '%(parent_name)s_%(name)s_assign(struct %(parent_name)s *msg, int off,',
            '    const %(ctype)s value)',
            '{',
            '  if (!msg->%(name)s_set || off < 0 || off >= msg->%(name)s_length)',
            '    return (-1);\n',
            '  {' ]
        code = TranslateList(code, self.GetTranslation())

        codearrayassign = self._entry.CodeArrayAssign(
            'msg->%(name)s_data[off]' % self.GetTranslation(), 'value')
        code += map(lambda x: '    ' + x, codearrayassign)

        code += TranslateList([
            '  }',
            '  return (0);',
            '}' ], self.GetTranslation())

        return code

    def CodeAdd(self):
        codearrayadd = self._entry.CodeArrayAdd(
            'msg->%(name)s_data[msg->%(name)s_length - 1]' % self.GetTranslation(),
            'value')
        code = [
            'static int',
            '%(parent_name)s_%(name)s_expand_to_hold_more('
            'struct %(parent_name)s *msg)',
            '{',
            '  int tobe_allocated = msg->%(name)s_num_allocated;',
            '  %(ctype)s* new_data = NULL;',
            '  tobe_allocated = !tobe_allocated ? 1 : tobe_allocated << 1;',
            '  new_data = (%(ctype)s*) realloc(msg->%(name)s_data,',
            '      tobe_allocated * sizeof(%(ctype)s));',
            '  if (new_data == NULL)',
            '    return -1;',
            '  msg->%(name)s_data = new_data;',
            '  msg->%(name)s_num_allocated = tobe_allocated;',
            '  return 0;'
            '}',
            '',
            '%(ctype)s %(optpointer)s',
            '%(parent_name)s_%(name)s_add('
            'struct %(parent_name)s *msg%(optaddarg)s)',
            '{',
            '  if (++msg->%(name)s_length >= msg->%(name)s_num_allocated) {',
            '    if (%(parent_name)s_%(name)s_expand_to_hold_more(msg)<0)',
            '      goto error;',
            '  }' ]

        code = TranslateList(code, self.GetTranslation())

        code += map(lambda x: '  ' + x, codearrayadd)

        code += TranslateList([
            '  msg->%(name)s_set = 1;',
            '  return %(optreference)s(msg->%(name)s_data['
            'msg->%(name)s_length - 1]);',
            'error:',
            '  --msg->%(name)s_length;',
            '  return (NULL);',
            '}' ], self.GetTranslation())

        return code

    def CodeComplete(self, structname, var_name):
        self._index = 'i'
        tmp = self._entry.CodeComplete(structname, self._entry.GetVarName(var_name))
        # skip the whole loop if there is nothing to check
        if not tmp:
            return []

        translate = self.GetTranslation({ 'structname' : structname })
        code = [
            '{',
            '  int i;',
            '  for (i = 0; i < %(structname)s->%(name)s_length; ++i) {' ]

        code = TranslateList(code, translate)

        code += map(lambda x: '    ' + x, tmp)

        code += [
            '  }',
            '}' ]

        return code

    def CodeUnmarshal(self, buf, tag_name, var_name, var_len):
        translate = self.GetTranslation({ 'var' : var_name,
                                          'buf' : buf,
                                          'tag' : tag_name,
                                          'init' : self._entry.GetInitializer()})
        code = [
            'if (%(var)s->%(name)s_length >= %(var)s->%(name)s_num_allocated &&',
            '    %(parent_name)s_%(name)s_expand_to_hold_more(%(var)s) < 0) {',
            '  puts("HEY NOW");',
            '  return (-1);',
            '}']

        # the unmarshal code directly returns
        code = TranslateList(code, translate)

        self._index = '%(var)s->%(name)s_length' % translate
        code += self._entry.CodeUnmarshal(buf, tag_name,
                                        self._entry.GetVarName(var_name),
                                        self._entry.GetVarLen(var_name))

        code += [ '++%(var)s->%(name)s_length;' % translate ]

        return code

    def CodeMarshal(self, buf, tag_name, var_name, var_len):
        code = ['{',
                '  int i;',
                '  for (i = 0; i < %(var)s->%(name)s_length; ++i) {' ]

        self._index = 'i'
        code += self._entry.CodeMarshal(buf, tag_name,
                                        self._entry.GetVarName(var_name),
                                        self._entry.GetVarLen(var_name))
        code += ['  }',
                 '}'
                 ]

        code = "\n".join(code) % self.GetTranslation({ 'var' : var_name })

        return code.split('\n')

    def CodeClear(self, structname):
        translate = self.GetTranslation({ 'structname' : structname })
        codearrayfree = self._entry.CodeArrayFree(
            '%(structname)s->%(name)s_data[i]' % self.GetTranslation(
            { 'structname' : structname } ))

        code = [ 'if (%(structname)s->%(name)s_set == 1) {' ]

        if codearrayfree:
            code += [
                '  int i;',
                '  for (i = 0; i < %(structname)s->%(name)s_length; ++i) {' ]

        code = TranslateList(code, translate)

        if codearrayfree:
            code += map(lambda x: '    ' + x, codearrayfree)
            code += [
                '  }' ]

        code += TranslateList([
                 '  free(%(structname)s->%(name)s_data);',
                 '  %(structname)s->%(name)s_data = NULL;',
                 '  %(structname)s->%(name)s_set = 0;',
                 '  %(structname)s->%(name)s_length = 0;',
                 '  %(structname)s->%(name)s_num_allocated = 0;',
                 '}'
                 ], translate)

        return code

    def CodeInitialize(self, name):
        code  = ['%s->%s_data = NULL;' % (name, self._name),
                 '%s->%s_length = 0;' % (name, self._name),
                 '%s->%s_num_allocated = 0;' % (name, self._name)]
        return code

    def CodeFree(self, structname):
        code = self.CodeClear(structname);

        code += TranslateList([
            'free(%(structname)s->%(name)s_data);' ],
                              self.GetTranslation({'structname' : structname }))

        return code

    def Declaration(self):
        dcl  = ['%s *%s_data;' % (self._ctype, self._name),
                'int %s_length;' % self._name,
                'int %s_num_allocated;' % self._name ]

        return dcl

def NormalizeLine(line):
    global white
    global cppcomment

    line = cppcomment.sub('', line)
    line = line.strip()
    line = white.sub(' ', line)

    return line

def ProcessOneEntry(factory, newstruct, entry):
    optional = 0
    array = 0
    entry_type = ''
    name = ''
    tag = ''
    tag_set = None
    separator = ''
    fixed_length = ''

    tokens = entry.split(' ')
    while tokens:
        token = tokens[0]
        tokens = tokens[1:]

        if not entry_type:
            if not optional and token == 'optional':
                optional = 1
                continue

            if not array and token == 'array':
                array = 1
                continue

        if not entry_type:
            entry_type = token
            continue

        if not name:
            res = re.match(r'^([^\[\]]+)(\[.*\])?$', token)
            if not res:
                 raise RpcGenError(
                     'Cannot parse name: \"%s\" '
                     'around line %d' % (entry, line_count))
            name = res.group(1)
            fixed_length = res.group(2)
            if fixed_length:
                fixed_length = fixed_length[1:-1]
            continue

        if not separator:
            separator = token
            if separator != '=':
                 raise RpcGenError('Expected "=" after name \"%s\" got %s'
                                   % (name, token))
            continue

        if not tag_set:
            tag_set = 1
            if not re.match(r'^(0x)?[0-9]+$', token):
                raise RpcGenError('Expected tag number: \"%s\"' % entry)
            tag = int(token, 0)
            continue

        raise RpcGenError('Cannot parse \"%s\"' % entry)

    if not tag_set:
        raise RpcGenError('Need tag number: \"%s\"' % entry)

    # Create the right entry
    if entry_type == 'bytes':
        if fixed_length:
            newentry = factory.EntryBytes(entry_type, name, tag, fixed_length)
        else:
            newentry = factory.EntryVarBytes(entry_type, name, tag)
    elif entry_type == 'int' and not fixed_length:
        newentry = factory.EntryInt(entry_type, name, tag)
    elif entry_type == 'int64' and not fixed_length:
        newentry = factory.EntryInt(entry_type, name, tag, bits=64)
    elif entry_type == 'string' and not fixed_length:
        newentry = factory.EntryString(entry_type, name, tag)
    else:
        res = structref.match(entry_type)
        if res:
            # References another struct defined in our file
            newentry = factory.EntryStruct(entry_type, name, tag, res.group(1))
        else:
            raise RpcGenError('Bad type: "%s" in "%s"' % (entry_type, entry))

    structs = []

    if optional:
        newentry.MakeOptional()
    if array:
        newentry.MakeArray()

    newentry.SetStruct(newstruct)
    newentry.SetLineCount(line_count)
    newentry.Verify()

    if array:
        # We need to encapsulate this entry into a struct
        newname = newentry.Name()+ '_array'

        # Now borgify the new entry.
        newentry = factory.EntryArray(newentry)
        newentry.SetStruct(newstruct)
        newentry.SetLineCount(line_count)
        newentry.MakeArray()

    newstruct.AddEntry(newentry)

    return structs

def ProcessStruct(factory, data):
    tokens = data.split(' ')

    # First three tokens are: 'struct' 'name' '{'
    newstruct = factory.Struct(tokens[1])

    inside = ' '.join(tokens[3:-1])

    tokens = inside.split(';')

    structs = []

    for entry in tokens:
        entry = NormalizeLine(entry)
        if not entry:
            continue

        # It's possible that new structs get defined in here
        structs.extend(ProcessOneEntry(factory, newstruct, entry))

    structs.append(newstruct)
    return structs

def GetNextStruct(file):
    global line_count
    global cppdirect

    got_struct = 0

    processed_lines = []

    have_c_comment = 0
    data = ''
    while 1:
        line = file.readline()
        if not line:
            break

        line_count += 1
        line = line[:-1]

        if not have_c_comment and re.search(r'/\*', line):
            if re.search(r'/\*.*?\*/', line):
                line = re.sub(r'/\*.*?\*/', '', line)
            else:
                line = re.sub(r'/\*.*$', '', line)
                have_c_comment = 1

        if have_c_comment:
            if not re.search(r'\*/', line):
                continue
            have_c_comment = 0
            line = re.sub(r'^.*\*/', '', line)

        line = NormalizeLine(line)

        if not line:
            continue

        if not got_struct:
            if re.match(r'#include ["<].*[>"]', line):
                cppdirect.append(line)
                continue

            if re.match(r'^#(if( |def)|endif)', line):
                cppdirect.append(line)
                continue

            if re.match(r'^#define', line):
                headerdirect.append(line)
                continue

            if not structdef.match(line):
                raise RpcGenError('Missing struct on line %d: %s'
                                  % (line_count, line))
            else:
                got_struct = 1
                data += line
            continue

        # We are inside the struct
        tokens = line.split('}')
        if len(tokens) == 1:
            data += ' ' + line
            continue

        if len(tokens[1]):
            raise RpcGenError('Trailing garbage after struct on line %d'
                              % line_count)

        # We found the end of the struct
        data += ' %s}' % tokens[0]
        break

    # Remove any comments, that might be in there
    data = re.sub(r'/\*.*\*/', '', data)

    return data


def Parse(factory, file):
    """
    Parses the input file and returns C code and corresponding header file.
    """

    entities = []

    while 1:
        # Just gets the whole struct nicely formatted
        data = GetNextStruct(file)

        if not data:
            break

        entities.extend(ProcessStruct(factory, data))

    return entities

class CCodeGenerator:
    def __init__(self):
        pass

    def GuardName(self, name):
        # Use the complete provided path to the input file, with all
        # non-identifier characters replaced with underscores, to
        # reduce the chance of a collision between guard macros.
        return 'EVENT_RPCOUT_' + nonident.sub('_', name).upper() + '_'

    def HeaderPreamble(self, name):
        guard = self.GuardName(name)
        pre = (
            '/*\n'
            ' * Automatically generated from %s\n'
            ' */\n\n'
            '#ifndef %s\n'
            '#define %s\n\n' ) % (
            name, guard, guard)

        for statement in headerdirect:
            pre += '%s\n' % statement
        if headerdirect:
            pre += '\n'

        pre += (
            '#include <event2/util.h> /* for ev_uint*_t */\n'
            '#include <event2/rpc.h>\n'
        )

        return pre

    def HeaderPostamble(self, name):
        guard = self.GuardName(name)
        return '#endif  /* %s */' % guard

    def BodyPreamble(self, name, header_file):
        global _NAME
        global _VERSION

        slash = header_file.rfind('/')
        if slash != -1:
            header_file = header_file[slash+1:]

        pre = ( '/*\n'
                ' * Automatically generated from %s\n'
                ' * by %s/%s.  DO NOT EDIT THIS FILE.\n'
                ' */\n\n' ) % (name, _NAME, _VERSION)
        pre += ( '#include <stdlib.h>\n'
                 '#include <string.h>\n'
                 '#include <assert.h>\n'
                 '#include <event2/event-config.h>\n'
                 '#include <event2/event.h>\n'
                 '#include <event2/buffer.h>\n'
                 '#include <event2/tag.h>\n\n'
                 '#ifdef EVENT____func__\n'
                 '#define __func__ EVENT____func__\n'
                 '#endif\n\n'
                 )

        for statement in cppdirect:
            pre += '%s\n' % statement

        pre += '\n#include "%s"\n\n' % header_file

        pre += 'void event_warn(const char *fmt, ...);\n'
        pre += 'void event_warnx(const char *fmt, ...);\n\n'

        return pre

    def HeaderFilename(self, filename):
        return '.'.join(filename.split('.')[:-1]) + '.h'

    def CodeFilename(self, filename):
        return '.'.join(filename.split('.')[:-1]) + '.gen.c'

    def Struct(self, name):
        return StructCCode(name)

    def EntryBytes(self, entry_type, name, tag, fixed_length):
        return EntryBytes(entry_type, name, tag, fixed_length)

    def EntryVarBytes(self, entry_type, name, tag):
        return EntryVarBytes(entry_type, name, tag)

    def EntryInt(self, entry_type, name, tag, bits=32):
        return EntryInt(entry_type, name, tag, bits)

    def EntryString(self, entry_type, name, tag):
        return EntryString(entry_type, name, tag)

    def EntryStruct(self, entry_type, name, tag, struct_name):
        return EntryStruct(entry_type, name, tag, struct_name)

    def EntryArray(self, entry):
        return EntryArray(entry)

class Usage(RpcGenError):
    def __init__(self, argv0):
        RpcGenError.__init__("usage: %s input.rpc [[output.h] output.c]"
                             % argv0)

class CommandLine:
    def __init__(self, argv):
        """Initialize a command-line to launch event_rpcgen, as if
           from a command-line with CommandLine(sys.argv).  If you're
           calling this directly, remember to provide a dummy value
           for sys.argv[0]
        """
        self.filename = None
        self.header_file = None
        self.impl_file = None
        self.factory = CCodeGenerator()

        if len(argv) >= 2 and argv[1] == '--quiet':
            global QUIETLY
            QUIETLY = 1
            del argv[1]

        if len(argv) < 2 or len(argv) > 4:
            raise Usage(argv[0])

        self.filename = argv[1].replace('\\', '/')
        if len(argv) == 3:
            self.impl_file = argv[2].replace('\\', '/')
        if len(argv) == 4:
            self.header_file = argv[2].replace('\\', '/')
            self.impl_file = argv[3].replace('\\', '/')

        if not self.filename:
            raise Usage(argv[0])

        if not self.impl_file:
            self.impl_file = self.factory.CodeFilename(self.filename)

        if not self.header_file:
            self.header_file = self.factory.HeaderFilename(self.impl_file)

        if not self.impl_file.endswith('.c'):
            raise RpcGenError("can only generate C implementation files")
        if not self.header_file.endswith('.h'):
            raise RpcGenError("can only generate C header files")

    def run(self):
        filename = self.filename
        header_file = self.header_file
        impl_file = self.impl_file
        factory = self.factory

        declare('Reading \"%s\"' % filename)

        fp = open(filename, 'r')
        entities = Parse(factory, fp)
        fp.close()

        declare('... creating "%s"' % header_file)
        header_fp = open(header_file, 'w')
        print >>header_fp, factory.HeaderPreamble(filename)

        # Create forward declarations: allows other structs to reference
        # each other
        for entry in entities:
            entry.PrintForwardDeclaration(header_fp)
        print >>header_fp, ''

        for entry in entities:
            entry.PrintTags(header_fp)
            entry.PrintDeclaration(header_fp)
        print >>header_fp, factory.HeaderPostamble(filename)
        header_fp.close()

        declare('... creating "%s"' % impl_file)
        impl_fp = open(impl_file, 'w')
        print >>impl_fp, factory.BodyPreamble(filename, header_file)
        for entry in entities:
            entry.PrintCode(impl_fp)
        impl_fp.close()

if __name__ == '__main__':
    try:
        CommandLine(sys.argv).run()
        sys.exit(0)

    except RpcGenError, e:
        print >>sys.stderr, e
        sys.exit(1)

    except EnvironmentError, e:
        if e.filename and e.strerror:
            print >>sys.stderr, "%s: %s" % (e.filename, e.strerror)
            sys.exit(1)
        elif e.strerror:
            print >> sys.stderr, e.strerror
            sys.exit(1)
        else:
            raise
