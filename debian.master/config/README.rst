==================
Config Annotations
==================

:Author: Andrea Righi

Overview
========

Each Ubuntu kernel needs to maintain its own .config for each supported
architecture and each flavour.

Every time a new patch is applied or a kernel is rebased on top of a new
one, we need to update the .config's accordingly (config options can be
added, removed and also renamed).

So, we need to make sure that some critical config options are always
matching the desired value in order to have a functional kernel.

State of the art
================

At the moment configs are maintained as a set of Kconfig chunks (inside
`debian.<kernel>/config/`): a global one, plus per-arch / per-flavour
chunks.

In addition to that, we need to maintain also a file called
'annotations'; the purpose of this file is to make sure that some
critical config options are not silently removed or changed when the
real .config is re-generated (for example after a rebase or after
applying a new set of patches).

The main problem with this approach is that, often, we have duplicate
information that is stored both in the Kconfig chunks *and* in the
annotations files and, at the same time, the whole .config's information
is distributed between Kconfig chunks and annotations, making it hard to
maintain, review and manage in general.

Proposed solution
=================

The proposed solution is to store all the config information into the
"annotations" format and get rid of the config chunks (basically the
real .config's can be produced "compiling" annotations).

Implementation
==============

To help the management of the annotations an helper script is provided
(`debian/scripts/misc/annotations`):

```
usage: annotations [-h] [--version] [--file FILE] [--arch ARCH] [--flavour FLAVOUR] [--config CONFIG]
                   (--query | --export | --import FILE | --update FILE | --check FILE)

Manage Ubuntu kernel .config and annotations

options:
  -h, --help            show this help message and exit
  --version, -v         show program's version number and exit
  --file FILE, -f FILE  Pass annotations or .config file to be parsed
  --arch ARCH, -a ARCH  Select architecture
  --flavour FLAVOUR, -l FLAVOUR
                        Select flavour (default is "generic")
  --config CONFIG, -c CONFIG
                        Select a specific config option

Action:
  --query, -q           Query annotations
  --export, -e          Convert annotations to .config format
  --import FILE, -i FILE
                        Import a full .config for a specific arch and flavour into annotations
  --update FILE, -u FILE
                        Import a partial .config into annotations (only resync configs specified in FILE)
  --check FILE, -k FILE
                        Validate kernel .config with annotations
```

This script allows to query config settings (per arch/flavour/config),
export them into the Kconfig format (generating the real .config files)
and check if the final .config matches the rules defined in the
annotations.

Examples (annotations is defined as an alias to `debian/scripts/annotations`):

 - Show settings for `CONFIG_DEBUG_INFO_BTF` for master kernel across all the
   supported architectures and flavours:

```
$ annotations --query --config CONFIG_DEBUG_INFO_BTF
{
    "policy": {
        "amd64": "y",
        "arm64": "y",
        "armhf": "n",
        "ppc64el": "y",
        "riscv64": "y",
        "s390x": "y"
    },
    "note": "'Needs newer pahole for armhf'"
}
```

 - Dump kernel .config for arm64 and flavour generic-64k:

```
$ annotations --arch arm64 --flavour generic-64k --export
CONFIG_DEBUG_FS=y
CONFIG_DEBUG_KERNEL=y
CONFIG_COMPAT=y
...
```

 - Update annotations file with a new kernel .config for amd64 flavour
   generic:

```
$ annotations --arch amd64 --flavour generic --import build/.config
```

Moreover, an additional kernelconfig commands are provided
(via debian/rules targets):
 - `migrateconfigs`: automatically merge all the previous configs into
   annotations (local changes still need to be committed)

Annotations headers
===================

The main annotations file should contain a header to define the architectures
and flavours that are supported.

Here is the format of the header for the generic kernel:
```
# Menu: HEADER
# FORMAT: 4
# ARCH: amd64 arm64 armhf ppc64el riscv64 s390x
# FLAVOUR: amd64-generic arm64-generic arm64-generic-64k armhf-generic armhf-generic-lpae ppc64el-generic riscv64-generic s390x-generic

```

Example header of a derivative (linux-aws):
```
# Menu: HEADER
# FORMAT: 4
# ARCH: amd64 arm64
# FLAVOUR: amd64-aws arm64-aws
# FLAVOUR_DEP: {'amd64-aws': 'amd64-generic', 'arm64-aws': 'arm64-generic'}

include "../../debian.master/config/annotations"

# Below you can define only the specific linux-aws configs that differ from linux generic

```

Pros and Cons
=============

 Pros:
  - avoid duplicate information in .config's and annotations
  - allow to easily define groups of config settings (for a specific
    environment or feature, such as annotations.clouds, annotations.ubuntu,
    annotations.snapd, etc.)
  - config options are more accessible, easy to change and review
  - we can easily document how config options are managed (and external
    contributors won't be discouraged anymore when they need to to change a
    config option)

 Cons:
  - potential regressions: the new tool/scripts can have potential bugs,
    so we could experience regressions due to some missed config changes
  - kernel team need to understand the new process (even if everything
    is transparent, kernel cranking process is the same, there might be
    corner cases that need to be addressed and resolved manually)

TODO
====

 - Migrate all flavour and arch definitions into annotations (rather
   than having this information defined in multiple places inside
   debian/scripts); right now this information is "partially" migrated,
   meaning that we need to define arches and flavours in the headers
   section of annotations (so that the annotations tool can figure out
   the list of supported arches and flavours), but arches and flavours
   are still defined elsewhere, ideally we would like to have arches and
   flavours defined only in one place: annotations.
