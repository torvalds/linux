.. SPDX-License-Identifier: GPL-2.0-only
.. Copyright (C) 2025 Guillaume Tucker

====================
Containerized Builds
====================

The ``container`` tool can be used to run any command in the kernel source tree
from within a container.  Doing so facilitates reproducing builds across
various platforms, for example when a test bot has reported an issue which
requires a specific version of a compiler or an external test suite.  While
this can already be done by users who are familiar with containers, having a
dedicated tool in the kernel tree lowers the barrier to entry by solving common
problems once and for all (e.g. user ID management).  It also makes it easier
to share an exact command line leading to a particular result.  The main use
case is likely to be kernel builds but virtually anything can be run: KUnit,
checkpatch etc. provided a suitable image is available.


Options
=======

Command line syntax::

  scripts/container -i IMAGE [OPTION]... CMD...

Available options:

``-c, --config-file CONFIG_FILE``

    Path to the config file.  If not specified, the default is to look for
    ``.container.toml`` in the current working directory.

``-e, --env-file ENV_FILE``

    Path to an environment file to load in the container.

``-g, --gid GID``

    Group ID to use inside the container.

``-i, --image IMAGE``

    Container image name (required).

``-p, --config-profile CONFIG_PROFILE``

    Profile section to use in the config file.  This will override any values
    defined in the ``DEFAULT`` section.

``-r, --runtime RUNTIME``

    Container runtime name.  Supported runtimes: ``podman``, ``docker``.

    If not specified, the first one found on the system will be used
    i.e. Podman if present, otherwise Docker.

``-s, --shell``

    Run the container in an interactive shell.

``-u, --uid UID``

    User ID to use inside the container.

    If the ``-g`` option is not specified and no group ID is defined in the
    configuration file, the user ID will also be set as the group ID.

``-v, --verbose``

    Enable verbose output.

``-h, --help``

    Show the help message and exit.


Usage
=====

It's entirely up to the user to choose which image to use and the ``CMD``
arguments are passed directly as an arbitrary command line to run in the
container.  The tool will take care of mounting the source tree as the current
working directory and adjust the user and group ID as needed.

The container image which would typically include a compiler toolchain is
provided by the user and selected via the ``-i`` option.  The container runtime
can be selected with the ``-r`` option, which can be either ``podman`` or
``docker``.  If none is specified, the first one found on the system will be
used while giving priority to Podman.  Support for other runtimes may be added
later depending on their popularity among users.

By default, commands are run non-interactively.  The user can abort a running
container with SIGINT (Ctrl-C).  To run commands interactively with a TTY, the
``--shell`` or ``-s`` option can be used.  Signals will then be received by the
shell directly rather than the parent ``container`` process.  To exit an
interactive shell, use Ctrl-D or ``exit``.

A :ref:`configuration file<config_file>` may be used to facilitate running
containers for various use cases.  It also removes the burden of repeatedly
providing the same options on the command line.

.. note::

   The only host requirement aside from a container runtime is Python 3.11 or
   later.

.. note::

   Out-of-tree builds are not fully supported yet.  The ``O=`` option can
   however already be used with a relative path inside the source tree to keep
   separate build outputs.  A workaround to build outside the tree is to use
   ``mount --bind``, see the examples section further down.


Environment Variables
=====================

Environment variables are not propagated to the container so they have to be
either defined in the image itself or via the ``-e`` option using an
environment file.  In some cases it makes more sense to have them defined in
the Containerfile used to create the image.  For example, a Clang-only compiler
toolchain image may have ``LLVM=1`` defined.

The local environment file is more useful for user-specific variables added
during development.  It is passed as-is to the container runtime so its format
may vary.  Typically, it will look like the output of ``env``.  For example::

  INSTALL_MOD_STRIP=1
  SOME_RANDOM_TEXT=One upon a time

Please also note that ``make`` options can still be passed on the command line,
so while this can't be done since the first argument needs to be the
executable::

  scripts/container -i docker.io/tuxmake/korg-clang LLVM=1 make  # won't work

this will work::

  scripts/container -i docker.io/tuxmake/korg-clang make LLVM=1


User IDs
========

This is an area where the behaviour will vary slightly depending on the
container runtime.  The goal is to run commands as the user invoking the tool.
With Podman, a namespace is created to map the current user ID to a different
one in the container (1000 by default).  With Docker, while this is also
possible with recent versions it requires a special feature to be enabled in
the daemon so it's not used here for simplicity.  Instead, the container is run
with the current user ID directly.  In both cases, this will provide the same
file permissions for the kernel source tree mounted as a volume.  The only
difference is that when using Docker without a namespace, the user ID may not
be the same as the default one set in the image.

Say, we're using an image which sets up a default user with ID 1000 and the
current user calling the ``container`` tool has ID 1234.  The kernel source
tree was checked out by this same user so the files belong to user 1234.  With
Podman, the container will be running as user ID 1000 with a mapping to ID 1234
so that the files from the mounted volume appear to belong to ID 1000 inside
the container.  With Docker and no namespace, the container will be running
with user ID 1234 which can access the files in the volume but not in the user
1000 home directory.  This shouldn't be an issue when running commands only in
the kernel tree but it is worth highlighting here as it might matter for
special corner cases.

Group IDs (GID) follow the same logic as user IDs (UID).  When specifying a UID
via the ``--uid`` option or in the configuration file, the GID takes the same
value as the UID by default.  A specific GID can be set via ``--gid`` or the
configuration file.

.. note::

   Podman's `Docker compatibility
   <https://podman-desktop.io/docs/migrating-from-docker/managing-docker-compatibility>`__
   mode to run ``docker`` commands on top of a Podman backend is more complex
   and not fully supported yet.  As such, Podman will take priority if both
   runtimes are available on the system.


Examples
========

The TuxMake project provides a variety of prebuilt container images available
on `Docker Hub <https://hub.docker.com/u/tuxmake>`__.  Here's the shortest
example to build a kernel using a TuxMake Clang image::

  scripts/container -i docker.io/tuxmake/korg-clang -- make LLVM=1 defconfig
  scripts/container -i docker.io/tuxmake/korg-clang -- make LLVM=1 -j$(nproc)

.. note::

   When running a command with options within the container, it should be
   separated with a double dash ``--`` to not confuse them with the
   ``container`` tool options.  Plain commands with no options don't strictly
   require the double dashes e.g.::

     scripts/container -i docker.io/tuxmake/korg-clang make mrproper

To run ``checkpatch.pl`` in a ``patches`` directory with a generic Perl image::

  scripts/container -i perl:slim-trixie scripts/checkpatch.pl patches/*

As an alternative to the TuxMake images, the examples below refer to
``kernel.org`` images which are based on the `kernel.org compiler toolchains
<https://mirrors.edge.kernel.org/pub/tools/>`__.  These aren't (yet) officially
available in any public registry but users can build their own locally instead
using this `experimental repository
<https://gitlab.com/gtucker/korg-containers>`__ by running ``make
PREFIX=kernel.org/``.

To build just ``bzImage`` using Clang::

  scripts/container -i kernel.org/clang -- make bzImage -j$(nproc)

Same with GCC 15 as a particular version tag::

  scripts/container -i kernel.org/gcc:15 -- make bzImage -j$(nproc)

For an out-of-tree build, a trick is to bind-mount the destination directory to
a relative path inside the source tree::

  mkdir -p $HOME/tmp/my-kernel-build
  mkdir -p build
  sudo mount --bind $HOME/tmp/my-kernel-build build
  scripts/container -i kernel.org/gcc -- make mrproper
  scripts/container -i kernel.org/gcc -- make O=build defconfig
  scripts/container -i kernel.org/gcc -- make O=build -j$(nproc)

To run KUnit in an interactive shell and get the full output::

  scripts/container -s -i kernel.org/gcc:kunit -- \
      tools/testing/kunit/kunit.py \
          run \
          --arch=x86_64 \
          --cross_compile=x86_64-linux-

To just start an interactive shell::

  scripts/container -si kernel.org/gcc bash

To build the HTML documentation, which requires the ``kdocs`` image built with
``make PREFIX=kernel.org/ extra`` as it's not a compiler toolchain::

  scripts/container -i kernel.org/kdocs make htmldocs

.. _config_file:

Configuration File
==================

By default, the tool will look for a configuration file named
``.container.toml`` in the current working directory.  If not found, it will be
silently ignored as it's not required.  Alternatively, any other path can be
specified with the ``-c`` option in which case the file needs to be present or
an error will be raised.

Its data follows the standard TOML format and is made up of different sections
with ``DEFAULT`` as the default one.  Other sections may be used to define
alternative profiles under arbitrary names which can be selected by the ``-p``
option.  Command line options take precedence over configuration values, and
profile sections take precedence over the default one.

Supported options in each section are:

``env_file``

    Path to an environment file to load in the container, equivalent to the
    ``-e`` command line option.

``image``

    Name of the container image to use, equivalent to the ``-i`` command line
    option.

``runtime``

    Name of the container runtime, equivalent to the ``-r`` command line option.

``uid`` / ``gid`` (integers)

    User and group ID numbers to use inside the container, equivalents to the
    ``-u`` and ``-g`` command line options respectively.


Here's a sample configuration with an extra ``clang`` profile::

  [DEFAULT]
  runtime = "podman"
  image = "tuxmake/korg-gcc"

  [clang]
  image = "tuxmake/korg-clang"
  env_file = ".clang.env"

It mentions a ``.clang.env`` file which simply contains this flag::

  LLVM=1

Let's take a look again at this example mentioned earlier::

  scripts/container -i docker.io/tuxmake/korg-clang -- make LLVM=1 defconfig

Using the configuration file, it can now be simplified into this::

  scripts/container -p clang -- make defconfig

Then to override the default user, the ``-u`` option can still be used::

  scripts/container -p clang -u0 -- make defconfig

This also illustrates how values take precedence over each other: the runtime
is loaded from the ``DEFAULT`` section, the image from the ``clang`` section
and the user ID from the command line ``-u`` option.
