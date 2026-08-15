.. SPDX-License-Identifier: GPL-2.0-only OR MIT
.. Copyright (C) 2025 TNG Technology Consulting GmbH

KernelSbom
==========

Introduction
------------

KernelSbom is a Python script ``scripts/sbom/sbom.py`` that can be
executed after a successful kernel build. When invoked, KernelSbom
analyzes all files involved in the build and generates Software Bill of
Materials (SBOM) documents in SPDX 3.0.1 format.
The generated SBOM documents capture:

* **Final output artifacts**, typically the kernel image and modules
* **All source files** that contributed to the build with metadata
  and licensing information
* **Details of the build process**, including intermediate artifacts
  and the build commands linking source files to the final output
  artifacts

KernelSbom is originally developed in the
`KernelSbom repository <https://github.com/TNG/KernelSbom>`_.

Requirements
------------

Python 3.10 or later. No libraries or other dependencies are required.

Basic Usage
-----------

Run the ``make sbom`` target.
For example::

    $ make defconfig O=kernel_build
    $ make sbom O=kernel_build -j$(nproc)

This will trigger a kernel build. After all build outputs have been
generated, KernelSbom produces three SPDX documents in the root
directory of the object tree:

* ``sbom-source.spdx.json``
  Describes all source files involved in the build and
  associates each file with its corresponding license expression.

* ``sbom-output.spdx.json``
  Captures all final build outputs (kernel image and ``.ko`` module files)
  and includes build metadata such as environment variables and
  a hash of the ``.config`` file used for the build.

* ``sbom-build.spdx.json``
  Imports files from the source and output documents and describes every
  intermediate build artifact. For each artifact, it records the exact
  build command used and establishes the relationship between
  input files and generated outputs.

When invoking the sbom target, it is recommended to perform
out-of-tree builds using ``O=<objtree>``. KernelSbom classifies files as
source files when they are located in the source tree and not in the
object tree. For in-tree builds, where the source and object trees are
the same directory, this distinction can no longer be made reliably.
In that case, KernelSbom does not generate a dedicated source SBOM.
Instead, source files are included in the build SBOM.

Standalone Usage
----------------

KernelSbom can also be used as a standalone script to generate
SPDX documents for specific build outputs. For example, after a
successful x86 kernel build, KernelSbom can generate SPDX documents
for the ``bzImage`` kernel image::

    $ SRCARCH=x86 python3 scripts/sbom/sbom.py \
        --src-tree . \
        --obj-tree ./kernel_build \
        --roots arch/x86/boot/bzImage \
        --generate-spdx \
        --generate-used-files \
        --prettify-json \
        --debug

Note that when KernelSbom is invoked outside of the ``make`` process,
the environment variables used during compilation are not available and
therefore cannot be included in the generated SPDX documents. It is
recommended to set at least the ``SRCARCH`` environment variable to the
architecture for which the build was performed.

For a full list of command-line options, run::

    $ python3 scripts/sbom/sbom.py --help

Output Format
-------------

KernelSbom generates documents conforming to the
`SPDX 3.0.1 specification <https://spdx.github.io/spdx-spec/v3.0.1/>`_
serialized as JSON-LD.

To reduce file size, the output documents use the JSON-LD ``@context``
to define custom prefixes for ``spdxId`` values. While this is compliant
with the SPDX specification, only a limited number of tools in the
current SPDX ecosystem support custom JSON-LD contexts. To use such
tools with the generated documents, the custom JSON-LD context must
be expanded before providing the documents.
See https://lists.spdx.org/g/Spdx-tech/message/6064 for more information.

How it Works
------------

KernelSbom operates in two major phases:

1. **Generate the cmd graph**, an acyclic directed dependency graph.
2. **Generate SPDX documents** based on the cmd graph.

KernelSbom begins from the root artifacts specified by the user, e.g.,
``arch/x86/boot/bzImage``. For each root artifact, it collects all
dependencies required to build that artifact. The dependencies come
from multiple sources:

* **.cmd files**: The primary source is the ``.cmd`` file of the
  generated artifact, e.g., ``arch/x86/boot/.bzImage.cmd``. These files
  contain the exact command used to build the artifact and often include
  an explicit list of input dependencies. By parsing the ``.cmd``
  file, the full list of dependencies can be obtained.

* **.incbin statements**: The second source are include binary
  ``.incbin`` statements in ``.S`` assembly files.

* **Hardcoded dependencies**: Unfortunately, not all build dependencies
  can be found via ``.cmd`` files and ``.incbin`` statements. Some build
  dependencies are directly defined in Makefiles or Kbuild files.
  Parsing these files is considered too complex for the scope of this
  project. Instead, the remaining gaps of the graph are filled using a
  list of manually defined dependencies, see
  ``scripts/sbom/sbom/cmd_graph/hardcoded_dependencies.py``. This list is
  known to be incomplete. However, analysis of the cmd graph indicates a
  ~99% completeness. For more information about the completeness analysis,
  see `KernelSbom #95 <https://github.com/TNG/KernelSbom/issues/95>`_.

Given the list of dependency files, KernelSbom recursively processes
each file, expanding the dependency chain all the way to the version
controlled source files. The result is a complete dependency graph
where nodes represent files, and edges represent "file A was used to
build file B" relationships.

Using the cmd graph, KernelSbom produces three SPDX documents.
For every file in the graph, KernelSbom:

* Parses ``SPDX-License-Identifier`` headers,
* Computes file hashes,
* Estimates the file type based on extension and path,
* Records build relationships between files.

Each root output file is additionally associated with an SPDX Package
element that captures version information, license data, and copyright.

Advanced Usage
--------------

Including Kernel Modules
~~~~~~~~~~~~~~~~~~~~~~~~

The list of all ``.ko`` kernel modules produced during a build can be
extracted from the ``modules.order`` file within the object tree.
For example::

    $ echo "arch/x86/boot/bzImage" > sbom-roots.txt
    $ sed 's/\.o$/.ko/' ./kernel_build/modules.order >> sbom-roots.txt

Then use the generated roots file::

    $ SRCARCH=x86 python3 scripts/sbom/sbom.py \
        --src-tree . \
        --obj-tree ./kernel_build \
        --roots-file sbom-roots.txt \
        --generate-spdx

Equal Source and Object Trees
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When the source tree and object tree are identical (for example, when
building in-tree), source files can no longer be reliably distinguished
from generated files.
In this scenario, KernelSbom does not produce a dedicated
``sbom-source.spdx.json`` document. Instead, both source files and build
artifacts are included together in ``sbom-build.spdx.json``, and
``sbom.used-files.txt`` lists all files referenced in the build document.

Unknown Build Commands
~~~~~~~~~~~~~~~~~~~~~~

Because the kernel supports a wide range of configurations and versions,
KernelSbom may encounter build commands in ``.cmd`` files that it does
not yet support. By default, KernelSbom will fail if an unknown build
command is encountered.

If you still wish to generate SPDX documents despite unsupported
commands, you can use the ``--do-not-fail-on-unknown-build-command``
option. KernelSbom will continue and produce the documents, although
the resulting SBOM will be incomplete.

This option should only be used when the missing portion of the
dependency graph is small and an incomplete SBOM is acceptable for
your use case.
