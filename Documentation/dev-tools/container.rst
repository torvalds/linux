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
problems once and for all (e.g. user id management).  It also makes it easier
to share an exact command line leading to a particular result.  The main use
case is likely to be kernel builds but virtually anything can be run: KUnit,
checkpatch etc. provided a suitable image is available.


Options
=======

Command line syntax::

  scripts/container -i IMAGE [OPTION]... CMD...

Available options:

``-e, --env-file ENV_FILE``

    Path to an environment file to load in the container.

``-g, --gid GID``

    Group id to use inside the container.

``-i, --image IMAGE``

    Container image name (required).

``-r, --runtime RUNTIME``

    Container runtime name.  Supported runtimes: ``docker``, ``podman``.

    If not specified, the first one found on the system will be used
    i.e. Podman if present, otherwise Docker.

``-s, --shell``

    Run the container in an interactive shell.

``-u, --uid UID``

    User id to use inside the container.

    If the ``-g`` option is not specified, the user id will also be used for
    the group id.

``-v, --verbose``

    Enable verbose output.

``-h, --help``

    Show the help message and exit.


Usage
=====

It's entirely up to the user to choose which image to use and the ``CMD``
arguments are passed directly as an arbitrary command line to run in the
container.  The tool will take care of mounting the source tree as the current
working directory and adjust the user and group id as needed.

The container image which would typically include a compiler toolchain is
provided by the user and selected via the ``-i`` option.  The container runtime
can be selected with the ``-r`` option, which can be either ``docker`` or
``podman``.  If none is specified, the first one found on the system will be
used while giving priority to Podman.  Support for other runtimes may be added
later depending on their popularity among users.

By default, commands are run non-interactively.  The user can abort a running
container with SIGINT (Ctrl-C).  To run commands interactively with a TTY, the
``--shell`` or ``-s`` option can be used.  Signals will then be received by the
shell directly rather than the parent ``container`` process.  To exit an
interactive shell, use Ctrl-D or ``exit``.

.. note::

   The only host requirement aside from a container runtime is Python 3.10 or
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
With Podman, a namespace is created to map the current user id to a different
one in the container (1000 by default).  With Docker, while this is also
possible with recent versions it requires a special feature to be enabled in
the daemon so it's not used here for simplicity.  Instead, the container is run
with the current user id directly.  In both cases, this will provide the same
file permissions for the kernel source tree mounted as a volume.  The only
difference is that when using Docker without a namespace, the user id may not
be the same as the default one set in the image.

Say, we're using an image which sets up a default user with id 1000 and the
current user calling the ``container`` tool has id 1234.  The kernel source
tree was checked out by this same user so the files belong to user 1234.  With
Podman, the container will be running as user id 1000 with a mapping to id 1234
so that the files from the mounted volume appear to belong to id 1000 inside
the container.  With Docker and no namespace, the container will be running
with user id 1234 which can access the files in the volume but not in the user
1000 home directory.  This shouldn't be an issue when running commands only in
the kernel tree but it is worth highlighting here as it might matter for
special corner cases.

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
