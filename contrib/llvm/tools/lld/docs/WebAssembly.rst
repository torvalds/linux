WebAssembly lld port
====================

The WebAssembly version of lld takes WebAssembly binaries as inputs and produces
a WebAssembly binary as its output.  For the most part it tries to mimic the
behaviour of traditional ELF linkers and specifically the ELF lld port.  Where
possible that command line flags and the semantics should be the same.


Object file format
------------------

The format the input object files that lld expects is specified as part of the
the WebAssembly tool conventions
https://github.com/WebAssembly/tool-conventions/blob/master/Linking.md.

This is object format that the llvm will produce when run with the
``wasm32-unknown-unknown`` target.  To build llvm with WebAssembly support
currently requires enabling the experimental backed using
``-DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=WebAssembly``.


Usage
-----

The WebAssembly version of lld is installed as **wasm-ld**.  It shared many 
common linker flags with **ld.lld** but also includes several
WebAssembly-specific options:

.. option:: --no-entry

  Don't search for the entry point symbol (by default ``_start``).

.. option:: --export-table

  Export the function table to the environment.

.. option:: --import-table

  Import the function table from the environment.

.. option:: --export-all

  Export all symbols (normally combined with --no-gc-sections)

.. option:: --export-dynamic

  When building an executable, export any non-hidden symbols.  By default only
  the entry point and any symbols marked with --export/--export-all are
  exported.

.. option:: --global-base=<value>

  Address at which to place global data.

.. option:: --no-merge-data-segments

  Disable merging of data segments.

.. option:: --stack-first

  Place stack at start of linear memory rather than after data.

.. option:: --compress-relocations

  Relocation targets in the code section 5-bytes wide in order to potentially
  occomate the largest LEB128 value.  This option will cause the linker to
  shirnk the code section to remove any padding from the final output.  However
  because it effects code offset, this option is not comatible with outputing
  debug information.

.. option:: --allow-undefined

  Allow undefined symbols in linked binary.

.. option:: --import-memory

  Import memory from the environment.

.. option:: --initial-memory=<value>

  Initial size of the linear memory. Default: static data size.

.. option:: --max-memory=<value>

  Maximum size of the linear memory. Default: unlimited.

By default the function table is neither imported nor exported, but defined
for internal use only.

When building shared libraries symbols are exported if they are marked
as ``visibility=default``.  When building executables only the entry point is
exported by default.  In addition any symbol included on the command line via
``--export`` is also exported.

Since WebAssembly is designed with size in mind the linker defaults to
``--gc-sections`` which means that all unused functions and data segments will
be stripped from the binary.

The symbols which are preserved by default are:

- The entry point (by default ``_start``).
- Any symbol which is to be exported.
- Any symbol transitively referenced by the above.


Missing features
----------------

- Merging of data section similar to ``SHF_MERGE`` in the ELF world is not
  supported.
- No support for creating shared libraries.  The spec for shared libraries in
  WebAssembly is still in flux:
  https://github.com/WebAssembly/tool-conventions/blob/master/DynamicLinking.md
