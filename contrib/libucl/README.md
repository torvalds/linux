# LIBUCL

[![Build Status](https://travis-ci.org/vstakhov/libucl.svg?branch=master)](https://travis-ci.org/vstakhov/libucl)
[![Coverity](https://scan.coverity.com/projects/4138/badge.svg)](https://scan.coverity.com/projects/4138)
[![Coverage Status](https://coveralls.io/repos/github/vstakhov/libucl/badge.svg?branch=master)](https://coveralls.io/github/vstakhov/libucl?branch=master)

**Table of Contents**  *generated with [DocToc](http://doctoc.herokuapp.com/)*

- [Introduction](#introduction)
- [Basic structure](#basic-structure)
- [Improvements to the json notation](#improvements-to-the-json-notation)
	- [General syntax sugar](#general-syntax-sugar)
	- [Automatic arrays creation](#automatic-arrays-creation)
	- [Named keys hierarchy](#named-keys-hierarchy)
	- [Convenient numbers and booleans](#convenient-numbers-and-booleans)
- [General improvements](#general-improvements)
	- [Comments](#comments)
	- [Macros support](#macros-support)
	- [Variables support](#variables-support)
	- [Multiline strings](#multiline-strings)
- [Emitter](#emitter)
- [Validation](#validation)
- [Performance](#performance)
- [Conclusion](#conclusion)

## Introduction

This document describes the main features and principles of the configuration
language called `UCL` - universal configuration language.

If you are looking for the libucl API documentation you can find it at [this page](doc/api.md).

## Basic structure

UCL is heavily infused by `nginx` configuration as the example of a convenient configuration
system. However, UCL is fully compatible with `JSON` format and is able to parse json files.
For example, you can write the same configuration in the following ways:

* in nginx like:

```nginx
param = value;
section {
    param = value;
    param1 = value1;
    flag = true;
    number = 10k;
    time = 0.2s;
    string = "something";
    subsection {
        host = {
            host = "hostname";
            port = 900;
        }
        host = {
            host = "hostname";
            port = 901;
        }
    }
}
```

* or in JSON:

```json
{
    "param": "value",
    "param1": "value1",
    "flag": true,
    "subsection": {
        "host": [
        {
            "host": "hostname",
            "port": 900
        },
        {
            "host": "hostname",
            "port": 901
        }
        ]
    }
}
```

## Improvements to the json notation.

There are various things that make ucl configuration more convenient for editing than strict json:

### General syntax sugar

* Braces are not necessary to enclose a top object: it is automatically treated as an object:

```json
"key": "value"
```
is equal to:
```json
{"key": "value"}
```

* There is no requirement of quotes for strings and keys, moreover, `:` may be replaced `=` or even be skipped for objects:

```nginx
key = value;
section {
    key = value;
}
```
is equal to:
```json
{
    "key": "value",
    "section": {
        "key": "value"
    }
}
```

* No commas mess: you can safely place a comma or semicolon for the last element in an array or an object:

```json
{
    "key1": "value",
    "key2": "value",
}
```
### Automatic arrays creation

* Non-unique keys in an object are allowed and are automatically converted to the arrays internally:

```json
{
    "key": "value1",
    "key": "value2"
}
```
is converted to:
```json
{
    "key": ["value1", "value2"]
}
```

### Named keys hierarchy

UCL accepts named keys and organize them into objects hierarchy internally. Here is an example of this process:
```nginx
section "blah" {
	key = value;
}
section foo {
	key = value;
}
```

is converted to the following object:

```nginx
section {
	blah {
		key = value;
	}
	foo {
		key = value;
	}
}
```

Plain definitions may be more complex and contain more than a single level of nested objects:

```nginx
section "blah" "foo" {
	key = value;
}
```

is presented as:

```nginx
section {
	blah {
		foo {
			key = value;
		}
	}
}
```

### Convenient numbers and booleans

* Numbers can have suffixes to specify standard multipliers:
    + `[kKmMgG]` - standard 10 base multipliers (so `1k` is translated to 1000)
    + `[kKmMgG]b` - 2 power multipliers (so `1kb` is translated to 1024)
    + `[s|min|d|w|y]` - time multipliers, all time values are translated to float number of seconds, for example `10min` is translated to 600.0 and `10ms` is translated to 0.01
* Hexadecimal integers can be used by `0x` prefix, for example `key = 0xff`. However, floating point values can use decimal base only.
* Booleans can be specified as `true` or `yes` or `on` and `false` or `no` or `off`.
* It is still possible to treat numbers and booleans as strings by enclosing them in double quotes.

## General improvements

### Comments

UCL supports different style of comments:

* single line: `#`
* multiline: `/* ... */`

Multiline comments may be nested:
```c
# Sample single line comment
/*
 some comment
 /* nested comment */
 end of comment
*/
```

### Macros support

UCL supports external macros both multiline and single line ones:
```nginx
.macro_name "sometext";
.macro_name {
    Some long text
    ....
};
```

Moreover, each macro can accept an optional list of arguments in braces. These
arguments themselves are the UCL object that is parsed and passed to a macro as
options:

```nginx
.macro_name(param=value) "something";
.macro_name(param={key=value}) "something";
.macro_name(.include "params.conf") "something";
.macro_name(#this is multiline macro
param = [value1, value2]) "something";
.macro_name(key="()") "something";
```

UCL also provide a convenient `include` macro to load content from another files
to the current UCL object. This macro accepts either path to file:

```nginx
.include "/full/path.conf"
.include "./relative/path.conf"
.include "${CURDIR}/path.conf"
```

or URL (if ucl is built with url support provided by either `libcurl` or `libfetch`):

	.include "http://example.com/file.conf"

`.include` macro supports a set of options:

* `try` (default: **false**) - if this option is `true` than UCL treats errors on loading of
this file as non-fatal. For example, such a file can be absent but it won't stop the parsing
of the top-level document.
* `sign` (default: **false**) - if this option is `true` UCL loads and checks the signature for
a file from path named `<FILEPATH>.sig`. Trusted public keys should be provided for UCL API after
parser is created but before any configurations are parsed.
* `glob` (default: **false**) - if this option is `true` UCL treats the filename as GLOB pattern and load
all files that matches the specified pattern (normally the format of patterns is defined in `glob` manual page
for your operating system). This option is meaningless for URL includes.
* `url` (default: **true**) - allow URL includes.
* `path` (default: empty) - A UCL_ARRAY of directories to search for the include file.
Search ends after the first match, unless `glob` is true, then all matches are included.
* `prefix` (default false) - Put included contents inside an object, instead
of loading them into the root. If no `key` is provided, one is automatically generated based on each files basename()
* `key` (default: <empty string>) - Key to load contents of include into. If
the key already exists, it must be the correct type
* `target` (default: object) - Specify if the `prefix` `key` should be an
object or an array.
* `priority` (default: 0) - specify priority for the include (see below).
* `duplicate` (default: 'append') - specify policy of duplicates resolving:
	- `append` - default strategy, if we have new object of higher priority then it replaces old one, if we have new object with less priority it is ignored completely, and if we have two duplicate objects with the same priority then we have a multi-value key (implicit array)
	- `merge` - if we have object or array, then new keys are merged inside, if we have a plain object then an implicit array is formed (regardless of priorities)
	- `error` - create error on duplicate keys and stop parsing
	- `rewrite` - always rewrite an old value with new one (ignoring priorities)

Priorities are used by UCL parser to manage the policy of objects rewriting during including other files
as following:

* If we have two objects with the same priority then we form an implicit array
* If a new object has bigger priority then we overwrite an old one
* If a new object has lower priority then we ignore it

By default, the priority of top-level object is set to zero (lowest priority). Currently,
you can define up to 16 priorities (from 0 to 15). Includes with bigger priorities will
rewrite keys from the objects with lower priorities as specified by the policy.

### Variables support

UCL supports variables in input. Variables are registered by a user of the UCL parser and can be presented in the following forms:

* `${VARIABLE}`
* `$VARIABLE`

UCL currently does not support nested variables. To escape variables one could use double dollar signs:

* `$${VARIABLE}` is converted to `${VARIABLE}`
* `$$VARIABLE` is converted to `$VARIABLE`

However, if no valid variables are found in a string, no expansion will be performed (and `$$` thus remains unchanged). This may be a subject
to change in future libucl releases.

### Multiline strings

UCL can handle multiline strings as well as single line ones. It uses shell/perl like notation for such objects:
```
key = <<EOD
some text
splitted to
lines
EOD
```

In this example `key` will be interpreted as the following string: `some text\nsplitted to\nlines`.
Here are some rules for this syntax:

* Multiline terminator must start just after `<<` symbols and it must consist of capital letters only (e.g. `<<eof` or `<< EOF` won't work);
* Terminator must end with a single newline character (and no spaces are allowed between terminator and newline character);
* To finish multiline string you need to include a terminator string just after newline and followed by a newline (no spaces or other characters are allowed as well);
* The initial and the final newlines are not inserted to the resulting string, but you can still specify newlines at the beginning and at the end of a value, for example:

```
key <<EOD

some
text

EOD
```

## Emitter

Each UCL object can be serialized to one of the three supported formats:

* `JSON` - canonic json notation (with spaces indented structure);
* `Compacted JSON` - compact json notation (without spaces or newlines);
* `Configuration` - nginx like notation;
* `YAML` - yaml inlined notation.

## Validation

UCL allows validation of objects. It uses the same schema that is used for json: [json schema v4](http://json-schema.org). UCL supports the full set of json schema with the exception of remote references. This feature is unlikely useful for configuration objects. Of course, a schema definition can be in UCL format instead of JSON that simplifies schemas writing. Moreover, since UCL supports multiple values for keys in an object it is possible to specify generic integer constraints `maxValues` and `minValues` to define the limits of values count in a single key. UCL currently is not absolutely strict about validation schemas themselves, therefore UCL users should supply valid schemas (as it is defined in json-schema draft v4) to ensure that the input objects are validated properly.

## Performance

Are UCL parser and emitter fast enough? Well, there are some numbers.
I got a 19Mb file that consist of ~700 thousand lines of json (obtained via
http://www.json-generator.com/). Then I checked jansson library that performs json
parsing and emitting and compared it with UCL. Here are results:

```
jansson: parsed json in 1.3899 seconds
jansson: emitted object in 0.2609 seconds

ucl: parsed input in 0.6649 seconds
ucl: emitted config in 0.2423 seconds
ucl: emitted json in 0.2329 seconds
ucl: emitted compact json in 0.1811 seconds
ucl: emitted yaml in 0.2489 seconds
```

So far, UCL seems to be significantly faster than jansson on parsing and slightly faster on emitting. Moreover,
UCL compiled with optimizations (-O3) performs significantly faster:
```
ucl: parsed input in 0.3002 seconds
ucl: emitted config in 0.1174 seconds
ucl: emitted json in 0.1174 seconds
ucl: emitted compact json in 0.0991 seconds
ucl: emitted yaml in 0.1354 seconds
```

You can do your own benchmarks by running `make check` in libucl top directory.

## Conclusion

UCL has clear design that should be very convenient for reading and writing. At the same time it is compatible with
JSON language and therefore can be used as a simple JSON parser. Macro logic provides an ability to extend configuration
language (for example by including some lua code) and comments allow to disable or enable the parts of a configuration
quickly.
