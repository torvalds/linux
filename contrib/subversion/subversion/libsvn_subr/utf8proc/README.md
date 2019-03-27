# utf8proc
[![Travis CI Status](https://travis-ci.org/JuliaLang/utf8proc.png)](https://travis-ci.org/JuliaLang/utf8proc)
[![AppVeyor Status](https://ci.appveyor.com/api/projects/status/aou20lfkyhj8xbwq/branch/master?svg=true)](https://ci.appveyor.com/project/tkelman/utf8proc/branch/master)


[utf8proc](http://julialang.org/utf8proc/) is a small, clean C
library that provides Unicode normalization, case-folding, and other
operations for data in the [UTF-8
encoding](http://en.wikipedia.org/wiki/UTF-8).  It was [initially
developed](http://www.public-software-group.org/utf8proc) by Jan
Behrens and the rest of the [Public Software
Group](http://www.public-software-group.org/), who deserve *nearly all
of the credit* for this package.  With the blessing of the Public
Software Group, the [Julia developers](http://julialang.org/) have
taken over development of utf8proc, since the original developers have
moved to other projects.

(utf8proc is used for basic Unicode
support in the [Julia language](http://julialang.org/), and the Julia
developers became involved because they wanted to add Unicode 7 support and other features.)

(The original utf8proc package also includes Ruby and PostgreSQL plug-ins.
We removed those from utf8proc in order to focus exclusively on the C
library for the time being, but plan to add them back in or release them as separate packages.)

The utf8proc package is licensed under the
free/open-source [MIT "expat"
license](http://opensource.org/licenses/MIT) (plus certain Unicode
data governed by the similarly permissive [Unicode data
license](http://www.unicode.org/copyright.html#Exhibit1)); please see
the included `LICENSE.md` file for more detailed information.

## Quick Start

For compilation of the C library run `make`.

## General Information

The C library is found in this directory after successful compilation
and is named `libutf8proc.a` (for the static library) and
`libutf8proc.so` (for the dynamic library).

The Unicode version supported is 9.0.0.

For Unicode normalizations, the following options are used:

* Normalization Form C:  `STABLE`, `COMPOSE`
* Normalization Form D:  `STABLE`, `DECOMPOSE`
* Normalization Form KC: `STABLE`, `COMPOSE`, `COMPAT`
* Normalization Form KD: `STABLE`, `DECOMPOSE`, `COMPAT`

## C Library

The documentation for the C library is found in the `utf8proc.h` header file.
`utf8proc_map` is function you will most likely be using for mapping UTF-8
strings, unless you want to allocate memory yourself.

## To Do

See the Github [issues list](https://github.com/JuliaLang/utf8proc/issues).

## Contact

Bug reports, feature requests, and other queries can be filed at
the [utf8proc issues page on Github](https://github.com/JuliaLang/utf8proc/issues).

## See also

An independent Lua translation of this library, [lua-mojibake](https://github.com/differentprogramming/lua-mojibake), is also available.
