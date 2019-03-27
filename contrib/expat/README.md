[![Travis CI Build Status](https://travis-ci.org/libexpat/libexpat.svg?branch=master)](https://travis-ci.org/libexpat/libexpat)
[![AppVeyor Build Status](https://ci.appveyor.com/api/projects/status/github/libexpat/libexpat?svg=true)](https://ci.appveyor.com/project/libexpat/libexpat)


# Expat, Release 2.2.6

This is Expat, a C library for parsing XML, started by
[James Clark](https://en.wikipedia.org/wiki/James_Clark_(programmer)) in 1997.
Expat is a stream-oriented XML parser.  This means that you register
handlers with the parser before starting the parse.  These handlers
are called when the parser discovers the associated structures in the
document being parsed.  A start tag is an example of the kind of
structures for which you may register handlers.

Windows users should use the
[`expat_win32` package](https://sourceforge.net/projects/expat/files/expat_win32/),
which includes both precompiled libraries and executables, and source code for
developers.

Expat is [free software](https://www.gnu.org/philosophy/free-sw.en.html).
You may copy, distribute, and modify it under the terms of the License
contained in the file
[`COPYING`](https://github.com/libexpat/libexpat/blob/master/expat/COPYING)
distributed with this package.
This license is the same as the MIT/X Consortium license.

If you are building Expat from a check-out from the
[Git repository](https://github.com/libexpat/libexpat/),
you need to run a script that generates the configure script using the
GNU autoconf and libtool tools.  To do this, you need to have
autoconf 2.58 or newer. Run the script like this:

```console
./buildconf.sh
```

Once this has been done, follow the same instructions as for building
from a source distribution.

To build Expat from a source distribution, you first run the
configuration shell script in the top level distribution directory:

```console
./configure
```

There are many options which you may provide to configure (which you
can discover by running configure with the `--help` option).  But the
one of most interest is the one that sets the installation directory.
By default, the configure script will set things up to install
libexpat into `/usr/local/lib`, `expat.h` into `/usr/local/include`, and
`xmlwf` into `/usr/local/bin`.  If, for example, you'd prefer to install
into `/home/me/mystuff/lib`, `/home/me/mystuff/include`, and
`/home/me/mystuff/bin`, you can tell `configure` about that with:

```console
./configure --prefix=/home/me/mystuff
```

Another interesting option is to enable 64-bit integer support for
line and column numbers and the over-all byte index:

```console
./configure CPPFLAGS=-DXML_LARGE_SIZE
```

However, such a modification would be a breaking change to the ABI
and is therefore not recommended for general use &mdash; e.g. as part of
a Linux distribution &mdash; but rather for builds with special requirements.

After running the configure script, the `make` command will build
things and `make install` will install things into their proper
location.  Have a look at the `Makefile` to learn about additional
`make` options.  Note that you need to have write permission into
the directories into which things will be installed.

If you are interested in building Expat to provide document
information in UTF-16 encoding rather than the default UTF-8, follow
these instructions (after having run `make distclean`).
Please note that we configure with `--without-xmlwf` as xmlwf does not
support this mode of compilation (yet):

1. Mass-patch `Makefile.am` files to use `libexpatw.la` for a library name:
   <br/>
   `find -name Makefile.am -exec sed
       -e 's,libexpat\.la,libexpatw.la,'
       -e 's,libexpat_la,libexpatw_la,'
       -i {} +`

1. Run `automake` to re-write `Makefile.in` files:<br/>
   `automake`

1. For UTF-16 output as unsigned short (and version/error strings as char),
   run:<br/>
   `./configure CPPFLAGS=-DXML_UNICODE --without-xmlwf`<br/>
   For UTF-16 output as `wchar_t` (incl. version/error strings), run:<br/>
   `./configure CFLAGS="-g -O2 -fshort-wchar" CPPFLAGS=-DXML_UNICODE_WCHAR_T
       --without-xmlwf`
   <br/>Note: The latter requires libc compiled with `-fshort-wchar`, as well.

1. Run `make` (which excludes xmlwf).

1. Run `make install` (again, excludes xmlwf).

Using `DESTDIR` is supported.  It works as follows:

```console
make install DESTDIR=/path/to/image
```

overrides the in-makefile set `DESTDIR`, because variable-setting priority is

1. commandline
1. in-makefile
1. environment

Note: This only applies to the Expat library itself, building UTF-16 versions
of xmlwf and the tests is currently not supported.

When using Expat with a project using autoconf for configuration, you
can use the probing macro in `conftools/expat.m4` to determine how to
include Expat.  See the comments at the top of that file for more
information.

A reference manual is available in the file `doc/reference.html` in this
distribution.
