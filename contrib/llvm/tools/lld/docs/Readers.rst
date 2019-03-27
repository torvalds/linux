.. _Readers:

Developing lld Readers
======================

Note: this document discuss Mach-O port of LLD. For ELF and COFF,
see :doc:`index`.

Introduction
------------

The purpose of a "Reader" is to take an object file in a particular format
and create an `lld::File`:cpp:class: (which is a graph of Atoms)
representing the object file.  A Reader inherits from
`lld::Reader`:cpp:class: which lives in
:file:`include/lld/Core/Reader.h` and
:file:`lib/Core/Reader.cpp`.

The Reader infrastructure for an object format ``Foo`` requires the
following pieces in order to fit into lld:

:file:`include/lld/ReaderWriter/ReaderFoo.h`

   .. cpp:class:: ReaderOptionsFoo : public ReaderOptions

      This Options class is the only way to configure how the Reader will
      parse any file into an `lld::Reader`:cpp:class: object.  This class
      should be declared in the `lld`:cpp:class: namespace.

   .. cpp:function:: Reader *createReaderFoo(ReaderOptionsFoo &reader)

      This factory function configures and create the Reader. This function
      should be declared in the `lld`:cpp:class: namespace.

:file:`lib/ReaderWriter/Foo/ReaderFoo.cpp`

   .. cpp:class:: ReaderFoo : public Reader

      This is the concrete Reader class which can be called to parse
      object files. It should be declared in an anonymous namespace or
      if there is shared code with the `lld::WriterFoo`:cpp:class: you
      can make a nested namespace (e.g. `lld::foo`:cpp:class:).

You may have noticed that :cpp:class:`ReaderFoo` is not declared in the
``.h`` file. An important design aspect of lld is that all Readers are
created *only* through an object-format-specific
:cpp:func:`createReaderFoo` factory function. The creation of the Reader is
parametrized through a :cpp:class:`ReaderOptionsFoo` class. This options
class is the one-and-only way to control how the Reader operates when
parsing an input file into an Atom graph. For instance, you may want the
Reader to only accept certain architectures. The options class can be
instantiated from command line options or be programmatically configured.

Where to start
--------------

The lld project already has a skeleton of source code for Readers for
``ELF``, ``PECOFF``, ``MachO``, and lld's native ``YAML`` graph format.
If your file format is a variant of one of those, you should modify the
existing Reader to support your variant. This is done by customizing the Options
class for the Reader and making appropriate changes to the ``.cpp`` file to
interpret those options and act accordingly.

If your object file format is not a variant of any existing Reader, you'll need
to create a new Reader subclass with the organization described above.

Readers are factories
---------------------

The linker will usually only instantiate your Reader once.  That one Reader will
have its loadFile() method called many times with different input files.
To support multithreaded linking, the Reader may be parsing multiple input
files in parallel. Therefore, there should be no parsing state in you Reader
object.  Any parsing state should be in ivars of your File subclass or in
some temporary object.

The key function to implement in a reader is::

  virtual error_code loadFile(LinkerInput &input,
                              std::vector<std::unique_ptr<File>> &result);

It takes a memory buffer (which contains the contents of the object file
being read) and returns an instantiated lld::File object which is
a collection of Atoms. The result is a vector of File pointers (instead of
simple a File pointer) because some file formats allow multiple object
"files" to be encoded in one file system file.


Memory Ownership
----------------

Atoms are always owned by their File object. During core linking when Atoms
are coalesced or stripped away, core linking does not delete them.
Core linking just removes those unused Atoms from its internal list.
The destructor of a File object is responsible for deleting all Atoms it
owns, and if ownership of the MemoryBuffer was passed to it, the File
destructor needs to delete that too.

Making Atoms
------------

The internal model of lld is purely Atom based.  But most object files do not
have an explicit concept of Atoms, instead most have "sections". The way
to think of this is that a section is just a list of Atoms with common
attributes.

The first step in parsing section-based object files is to cleave each
section into a list of Atoms. The technique may vary by section type. For
code sections (e.g. .text), there are usually symbols at the start of each
function. Those symbol addresses are the points at which the section is
cleaved into discrete Atoms.  Some file formats (like ELF) also include the
length of each symbol in the symbol table. Otherwise, the length of each
Atom is calculated to run to the start of the next symbol or the end of the
section.

Other sections types can be implicitly cleaved. For instance c-string literals
or unwind info (e.g. .eh_frame) can be cleaved by having the Reader look at
the content of the section.  It is important to cleave sections into Atoms
to remove false dependencies. For instance the .eh_frame section often
has no symbols, but contains "pointers" to the functions for which it
has unwind info.  If the .eh_frame section was not cleaved (but left as one
big Atom), there would always be a reference (from the eh_frame Atom) to
each function.  So the linker would be unable to coalesce or dead stripped
away the function atoms.

The lld Atom model also requires that a reference to an undefined symbol be
modeled as a Reference to an UndefinedAtom. So the Reader also needs to
create an UndefinedAtom for each undefined symbol in the object file.

Once all Atoms have been created, the second step is to create References
(recall that Atoms are "nodes" and References are "edges"). Most References
are created by looking at the "relocation records" in the object file. If
a function contains a call to "malloc", there is usually a relocation record
specifying the address in the section and the symbol table index. Your
Reader will need to convert the address to an Atom and offset and the symbol
table index into a target Atom. If "malloc" is not defined in the object file,
the target Atom of the Reference will be an UndefinedAtom.


Performance
-----------
Once you have the above working to parse an object file into Atoms and
References, you'll want to look at performance.  Some techniques that can
help performance are:

* Use llvm::BumpPtrAllocator or pre-allocate one big vector<Reference> and then
  just have each atom point to its subrange of References in that vector.
  This can be faster that allocating each Reference as separate object.
* Pre-scan the symbol table and determine how many atoms are in each section
  then allocate space for all the Atom objects at once.
* Don't copy symbol names or section content to each Atom, instead use
  StringRef and ArrayRef in each Atom to point to its name and content in the
  MemoryBuffer.


Testing
-------

We are still working on infrastructure to test Readers. The issue is that
you don't want to check in binary files to the test suite. And the tools
for creating your object file from assembly source may not be available on
every OS.

We are investigating a way to use YAML to describe the section, symbols,
and content of a file. Then have some code which will write out an object
file from that YAML description.

Once that is in place, you can write test cases that contain section/symbols
YAML and is run through the linker to produce Atom/References based YAML which
is then run through FileCheck to verify the Atoms and References are as
expected.



