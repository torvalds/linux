# Welcome to libarchive!

The libarchive project develops a portable, efficient C library that
can read and write streaming archives in a variety of formats.  It
also includes implementations of the common `tar`, `cpio`, and `zcat`
command-line tools that use the libarchive library.

## Questions?  Issues?

* http://www.libarchive.org is the home for ongoing
  libarchive development, including documentation,
  and links to the libarchive mailing lists.
* To report an issue, use the issue tracker at
  https://github.com/libarchive/libarchive/issues
* To submit an enhancement to libarchive, please
  submit a pull request via GitHub: https://github.com/libarchive/libarchive/pulls

## Contents of the Distribution

This distribution bundle includes the following major components:

* **libarchive**: a library for reading and writing streaming archives
* **tar**: the 'bsdtar' program is a full-featured 'tar' implementation built on libarchive
* **cpio**: the 'bsdcpio' program is a different interface to essentially the same functionality
* **cat**: the 'bsdcat' program is a simple replacement tool for zcat, bzcat, xzcat, and such
* **examples**: Some small example programs that you may find useful.
* **examples/minitar**: a compact sample demonstrating use of libarchive.
* **contrib**:  Various items sent to me by third parties; please contact the authors with any questions.

The top-level directory contains the following information files:

* **NEWS** - highlights of recent changes
* **COPYING** - what you can do with this
* **INSTALL** - installation instructions
* **README** - this file
* **CMakeLists.txt** - input for "cmake" build tool, see INSTALL
* **configure** - configuration script, see INSTALL for details.  If your copy of the source lacks a `configure` script, you can try to construct it by running the script in `build/autogen.sh` (or use `cmake`).

The following files in the top-level directory are used by the 'configure' script:
* `Makefile.am`, `aclocal.m4`, `configure.ac` - used to build this distribution, only needed by maintainers
* `Makefile.in`, `config.h.in` - templates used by configure script

## Documentation

In addition to the informational articles and documentation
in the online [libarchive Wiki](https://github.com/libarchive/libarchive/wiki),
the distribution also includes a number of manual pages:

 * bsdtar.1 explains the use of the bsdtar program
 * bsdcpio.1 explains the use of the bsdcpio program
 * bsdcat.1 explains the use of the bsdcat program
 * libarchive.3 gives an overview of the library as a whole
 * archive_read.3, archive_write.3, archive_write_disk.3, and
   archive_read_disk.3 provide detailed calling sequences for the read
   and write APIs
 * archive_entry.3 details the "struct archive_entry" utility class
 * archive_internals.3 provides some insight into libarchive's
   internal structure and operation.
 * libarchive-formats.5 documents the file formats supported by the library
 * cpio.5, mtree.5, and tar.5 provide detailed information about these
   popular archive formats, including hard-to-find details about
   modern cpio and tar variants.

The manual pages above are provided in the 'doc' directory in
a number of different formats.

You should also read the copious comments in `archive.h` and the
source code for the sample programs for more details.  Please let us
know about any errors or omissions you find.

## Supported Formats

Currently, the library automatically detects and reads the following fomats:
  * Old V7 tar archives
  * POSIX ustar
  * GNU tar format (including GNU long filenames, long link names, and sparse files)
  * Solaris 9 extended tar format (including ACLs)
  * POSIX pax interchange format
  * POSIX octet-oriented cpio
  * SVR4 ASCII cpio
  * Binary cpio (big-endian or little-endian)
  * ISO9660 CD-ROM images (with optional Rockridge or Joliet extensions)
  * ZIP archives (with uncompressed or "deflate" compressed entries, including support for encrypted Zip archives)
  * GNU and BSD 'ar' archives
  * 'mtree' format
  * 7-Zip archives
  * Microsoft CAB format
  * LHA and LZH archives
  * RAR and RAR 5.0 archives (with some limitations due to RAR's proprietary status)
  * XAR archives

The library also detects and handles any of the following before evaluating the archive:
  * uuencoded files
  * files with RPM wrapper
  * gzip compression
  * bzip2 compression
  * compress/LZW compression
  * lzma, lzip, and xz compression
  * lz4 compression
  * lzop compression
  * zstandard compression

The library can create archives in any of the following formats:
  * POSIX ustar
  * POSIX pax interchange format
  * "restricted" pax format, which will create ustar archives except for
    entries that require pax extensions (for long filenames, ACLs, etc).
  * Old GNU tar format
  * Old V7 tar format
  * POSIX octet-oriented cpio
  * SVR4 "newc" cpio
  * shar archives
  * ZIP archives (with uncompressed or "deflate" compressed entries)
  * GNU and BSD 'ar' archives
  * 'mtree' format
  * ISO9660 format
  * 7-Zip archives
  * XAR archives

When creating archives, the result can be filtered with any of the following:
  * uuencode
  * gzip compression
  * bzip2 compression
  * compress/LZW compression
  * lzma, lzip, and xz compression
  * lz4 compression
  * lzop compression
  * zstandard compression

## Notes about the Library Design

The following notes address many of the most common
questions we are asked about libarchive:

* This is a heavily stream-oriented system.  That means that
  it is optimized to read or write the archive in a single
  pass from beginning to end.  For example, this allows
  libarchive to process archives too large to store on disk
  by processing them on-the-fly as they are read from or
  written to a network or tape drive.  This also makes
  libarchive useful for tools that need to produce
  archives on-the-fly (such as webservers that provide
  archived contents of a users account).

* In-place modification and random access to the contents
  of an archive are not directly supported.  For some formats,
  this is not an issue: For example, tar.gz archives are not
  designed for random access.  In some other cases, libarchive
  can re-open an archive and scan it from the beginning quickly
  enough to provide the needed abilities even without true
  random access.  Of course, some applications do require true
  random access; those applications should consider alternatives
  to libarchive.

* The library is designed to be extended with new compression and
  archive formats.  The only requirement is that the format be
  readable or writable as a stream and that each archive entry be
  independent.  There are articles on the libarchive Wiki explaining
  how to extend libarchive.

* On read, compression and format are always detected automatically.

* The same API is used for all formats; it should be very
  easy for software using libarchive to transparently handle
  any of libarchive's archiving formats.

* Libarchive's automatic support for decompression can be used
  without archiving by explicitly selecting the "raw" and "empty"
  formats.

* I've attempted to minimize static link pollution.  If you don't
  explicitly invoke a particular feature (such as support for a
  particular compression or format), it won't get pulled in to
  statically-linked programs.  In particular, if you don't explicitly
  enable a particular compression or decompression support, you won't
  need to link against the corresponding compression or decompression
  libraries.  This also reduces the size of statically-linked
  binaries in environments where that matters.

* The library is generally _thread safe_ depending on the platform:
  it does not define any global variables of its own.  However, some
  platforms do not provide fully thread-safe versions of key C library
  functions.  On those platforms, libarchive will use the non-thread-safe
  functions.  Patches to improve this are of great interest to us.

* In particular, libarchive's modules to read or write a directory
  tree do use `chdir()` to optimize the directory traversals.  This
  can cause problems for programs that expect to do disk access from
  multiple threads.  Of course, those modules are completely
  optional and you can use the rest of libarchive without them.

* The library is _not_ thread aware, however.  It does no locking
  or thread management of any kind.  If you create a libarchive
  object and need to access it from multiple threads, you will
  need to provide your own locking.

* On read, the library accepts whatever blocks you hand it.
  Your read callback is free to pass the library a byte at a time
  or mmap the entire archive and give it to the library at once.
  On write, the library always produces correctly-blocked output.

* The object-style approach allows you to have multiple archive streams
  open at once.  bsdtar uses this in its "@archive" extension.

* The archive itself is read/written using callback functions.
  You can read an archive directly from an in-memory buffer or
  write it to a socket, if you wish.  There are some utility
  functions to provide easy-to-use "open file," etc, capabilities.

* The read/write APIs are designed to allow individual entries
  to be read or written to any data source:  You can create
  a block of data in memory and add it to a tar archive without
  first writing a temporary file.  You can also read an entry from
  an archive and write the data directly to a socket.  If you want
  to read/write entries to disk, there are convenience functions to
  make this especially easy.

* Note: The "pax interchange format" is a POSIX standard extended tar
  format that should be used when the older _ustar_ format is not
  appropriate.  It has many advantages over other tar formats
  (including the legacy GNU tar format) and is widely supported by
  current tar implementations.

