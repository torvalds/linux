# utf8proc release history #

## Version 2.1 ##

2016-12-16:

- New functions `utf8proc_map_custom` and `utf8proc_decompose_custom`
  to allow user-supplied transformations of codepoints, in conjunction
  with other transformations ([#89]).

- New function `utf8proc_normalize_utf32` to apply normalizations
  directly to UTF-32 data (not just UTF-8) ([#88]).

- Fixed stack overflow that could occur due to incorrect definition
  of `UINT16_MAX` with some compilers ([#84]).

- Fixed conflict with `stdbool.h` in Visual Studio ([#90]).

- Updated font metrics to use Unifont 9.0.04.

## Version 2.0.2 ##

2016-07-27:

- Move `-Wmissing-prototypes` warning flag from `Makefile` to `.travis.yml`
  since MSVC does not understand this flag and it is occasionally useful to
  build using MSVC through the `Makefile` ([#79]).

- Use a different variable name for a nested loop in `bench/bench.c`, and
  declare it in a C89 way rather than inside the `for` to avoid "error:
  'for' loop initial declarations are only allowed in C99 mode" ([#80]).

## Version 2.0.1 ##

2016-07-13:

- Bug fix in `utf8proc_grapheme_break_stateful` ([#77]).

- Tests now use versioned Unicode files, so they will no longer
  break when a new version of Unicode is released ([#78]).

## Version 2.0 ##

2016-07-13:

- Updated for Unicode 9.0 ([#70]).

- New `utf8proc_grapheme_break_stateful` to handle the complicated
  grapheme-breaking rules in Unicode 9.  The old `utf8proc_grapheme_break`
  is still provided, but may incorrectly identify grapheme breaks
  in some Unicode-9 sequences.

- Smaller Unicode tables ([#62], [#68]).  This required changes
  in the `utf8proc_property_t` structure, which breaks backward
  compatibility if you access this `struct` directly.  The
  functions in the API remain backward-compatible, however.

- Buffer overrun fix ([#66]).

## Version 1.3.1 ##

2015-11-02:

- Do not export symbol for internal function `unsafe_encode_char()` ([#55]).

- Install relative symbolic links for shared libraries ([#58]).

- Enable and fix compiler warnings ([#55], [#58]).

- Add missing files to `make clean` ([#58]).

## Version 1.3 ##

2015-07-06:

- Updated for Unicode 8.0 ([#45]).

- New `utf8proc_tolower` and `utf8proc_toupper` functions, portable
  replacements for `towlower` and `towupper` in the C library ([#40]).

- Don't treat Unicode "non-characters" as invalid, and improved
  validity checking in general ([#35]).

- Prefix all typedefs with `utf8proc_`, e.g. `utf8proc_int32_t`,
  to avoid collisions with other libraries ([#32]).

- Rename `DLLEXPORT` to `UTF8PROC_DLLEXPORT` to prevent collisions.

- Fix build breakage in the benchmark routines.

- More fine-grained Makefile variables (`PICFLAG` etcetera), so that
  compilation flags can be selectively overridden, and in particular
  so that `CFLAGS` can be changed without accidentally eliminating
  necessary flags like `-fPIC` and `-std=c99` ([#43]).

- Updated character-width tables based on Unifont 8.0.01 ([#51]) and
  the Unicode 8 character categories ([#47]).

## Version 1.2 ##

2015-03-28:

- Updated for Unicode 7.0 ([#6]).

- New function `utf8proc_grapheme_break(c1,c2)` that returns whether
  there is a grapheme break between `c1` and `c2` ([#20]).

- New function `utf8proc_charwidth(c)` that returns the number of
  column-positions that should be required for `c`; essentially a
  portable replacment for `wcwidth(c)` ([#27]).

- New function `utf8proc_category(c)` that returns the Unicode
  category of `c` (as one of the constants `UTF8PROC_CATEGORY_xx`).
  Also, a function `utf8proc_category_string(c)` that returns the Unicode
  category of `c` as a two-character string.

- `cmake` script `CMakeLists.txt`, in addition to `Makefile`, for
  easier compilation on Windows ([#28]).

- Various `Makefile` improvements: a `make check` target to perform
  tests ([#13]), `make install`, a rule to automate updating the Unicode
  tables, etcetera.

- The shared library is now versioned (e.g. has a soname on GNU/Linux) ([#24]).

- C++/MSVC compatibility ([#17]).

- Most `#defined` constants are now `enums` ([#29]).

- New preprocessor constants `UTF8PROC_VERSION_MAJOR`,
  `UTF8PROC_VERSION_MINOR`, and `UTF8PROC_VERSION_PATCH` for compile-time
  detection of the API version.

- Doxygen-formatted documentation ([#29]).

- The Ruby and PostgreSQL plugins have been removed due to lack of testing ([#22]).

## Version 1.1.6 ##

2013-11-27:

- PostgreSQL 9.2 and 9.3 compatibility (lowercase `c` language name)

## Version 1.1.5 ##

2009-08-20:

- Use `RSTRING_PTR()` and `RSTRING_LEN()` instead of `RSTRING()->ptr` and
  `RSTRING()->len` for ruby1.9 compatibility (and `#define` them, if not
  existent)

2009-10-02:

- Patches for compatibility with Microsoft Visual Studio

2009-10-08:

- Fixes to make utf8proc usable in C++ programs

2009-10-16:

## Version 1.1.4 ##

2009-06-14:

- replaced C++ style comments for compatibility reasons
- added typecasts to suppress compiler warnings
- removed redundant source files for ruby-gemfile generation

2009-08-19:

- Changed copyright notice for Public Software Group e. V.
- Minor changes in the `README` file

## Version 1.1.3 ##

2008-10-04:

- Added a function `utf8proc_version` returning a string containing the version
  number of the library.
- Included a target `libutf8proc.dylib` for MacOSX.

2009-05-01:
- PostgreSQL 8.3 compatibility (use of `SET_VARSIZE` macro)

## Version 1.1.2 ##

2007-07-25:

- Fixed a serious bug in the data file generator, which caused characters
  being treated incorrectly, when stripping default ignorable characters or
  calculating grapheme cluster boundaries.

## Version 1.1.1 ##

2007-06-25:

- Added a new PostgreSQL function `unistrip`, which behaves like `unifold`,
  but also removes all character marks (e.g. accents).

2007-07-22:

- Changed license from BSD to MIT style.
- Added a new function `utf8proc_codepoint_valid` to the C library.
- Changed compiler flags in `Makefile` from `-g -O0` to `-O2`
- The ruby script, which was used to build the `utf8proc_data.c` file, is now
  included in the distribution.

## Version 1.0.3 ##

2007-03-16:

- Fixed a bug in the ruby library, which caused an error, when splitting an
  empty string at grapheme cluster boundaries (method `String#utf8chars`).

## Version 1.0.2 ##

2006-09-21:

- included a check in `Integer#utf8`, which raises an exception, if the given
  code-point is invalid because of being too high (this was missing yet)

2006-12-26:

- added support for PostgreSQL version 8.2

## Version 1.0.1 ##

2006-09-20:

- included a gem file for the ruby version of the library

Release of version 1.0.1

## Version 1.0 ##

2006-09-17:

- added the `LUMP` option, which lumps certain characters together (see `lump.md`) (also used for the PostgreSQL `unifold` function)
- added the `STRIPMARK` option, which strips marking characters (or marks of composed characters)
- deprecated ruby method `String#char_ary` in favour of `String#utf8chars`

## Version 0.3 ##

2006-07-18:

- changed normalization from NFC to NFKC for postgresql unifold function

2006-08-04:

- added support to mark the beginning of a grapheme cluster with 0xFF (option: `CHARBOUND`)
- added the ruby method `String#chars`, which is returning an array of UTF-8 encoded grapheme clusters
- added `NLF2LF` transformation in postgresql `unifold` function
- added the `DECOMPOSE` option, if you neither use `COMPOSE` or `DECOMPOSE`, no normalization will be performed (different from previous versions)
- using integer constants rather than C-strings for character properties
- fixed (hopefully) a problem with the ruby library on Mac OS X, which occurred when compiler optimization was switched on

## Version 0.2 ##

2006-06-05:

- changed behaviour of PostgreSQL function to return NULL in case of invalid input, rather than raising an exceptional condition
- improved efficiency of PostgreSQL function (no transformation to C string is done)

2006-06-20:

- added -fpic compiler flag in Makefile
- fixed bug in the C code for the ruby library (usage of non-existent function)

## Version 0.1 ##

2006-06-02: initial release of version 0.1

[#6]: https://github.com/JuliaLang/utf8proc/issues/6
[#13]: https://github.com/JuliaLang/utf8proc/issues/13
[#17]: https://github.com/JuliaLang/utf8proc/issues/17
[#20]: https://github.com/JuliaLang/utf8proc/issues/20
[#22]: https://github.com/JuliaLang/utf8proc/issues/22
[#24]: https://github.com/JuliaLang/utf8proc/issues/24
[#27]: https://github.com/JuliaLang/utf8proc/issues/27
[#28]: https://github.com/JuliaLang/utf8proc/issues/28
[#29]: https://github.com/JuliaLang/utf8proc/issues/29
[#32]: https://github.com/JuliaLang/utf8proc/issues/32
[#35]: https://github.com/JuliaLang/utf8proc/issues/35
[#40]: https://github.com/JuliaLang/utf8proc/issues/40
[#43]: https://github.com/JuliaLang/utf8proc/issues/43
[#45]: https://github.com/JuliaLang/utf8proc/issues/45
[#47]: https://github.com/JuliaLang/utf8proc/issues/47
[#51]: https://github.com/JuliaLang/utf8proc/issues/51
[#55]: https://github.com/JuliaLang/utf8proc/issues/55
[#58]: https://github.com/JuliaLang/utf8proc/issues/58
[#62]: https://github.com/JuliaLang/utf8proc/issues/62
[#66]: https://github.com/JuliaLang/utf8proc/issues/66
[#68]: https://github.com/JuliaLang/utf8proc/issues/68
[#70]: https://github.com/JuliaLang/utf8proc/issues/70
[#77]: https://github.com/JuliaLang/utf8proc/issues/77
[#78]: https://github.com/JuliaLang/utf8proc/issues/78
[#79]: https://github.com/JuliaLang/utf8proc/issues/79
[#80]: https://github.com/JuliaLang/utf8proc/issues/80
[#84]: https://github.com/JuliaLang/utf8proc/pull/84
[#88]: https://github.com/JuliaLang/utf8proc/pull/88
[#89]: https://github.com/JuliaLang/utf8proc/pull/89
[#90]: https://github.com/JuliaLang/utf8proc/issues/90
