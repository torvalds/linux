.. _sphinx_intro:

Sphinx Introduction for LLVM Developers
=======================================

This document is intended as a short and simple introduction to the Sphinx
documentation generation system for LLVM developers.

Quickstart
----------

To get started writing documentation, you will need to:

 1. Have the Sphinx tools :ref:`installed <installing_sphinx>`.

 2. Understand how to :ref:`build the documentation
    <building_the_documentation>`.

 3. Start :ref:`writing documentation <writing_documentation>`!

.. _installing_sphinx:

Installing Sphinx
~~~~~~~~~~~~~~~~~

You should be able to install Sphinx using the standard Python package
installation tool ``easy_install``, as follows::

  $ sudo easy_install sphinx
  Searching for sphinx
  Reading http://pypi.python.org/simple/sphinx/
  Reading http://sphinx.pocoo.org/
  Best match: Sphinx 1.1.3
  ... more lines here ..

If you do not have root access (or otherwise want to avoid installing Sphinx in
system directories) see the section on :ref:`installing_sphinx_in_a_venv` .

If you do not have the ``easy_install`` tool on your system, you should be able
to install it using:

  Linux
    Use your distribution's standard package management tool to install it,
    i.e., ``apt-get install easy_install`` or ``yum install easy_install``.

  Mac OS X
    All modern Mac OS X systems come with ``easy_install`` as part of the base
    system.

  Windows
    See the `setuptools <http://pypi.python.org/pypi/setuptools>`_ package web
    page for instructions.


.. _building_the_documentation:

Building the documentation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In order to build the documentation need to add ``-DLLVM_ENABLE_SPHINX=ON`` to
your ``cmake`` command.  Once you do this you can build the docs using
``docs-lld-html`` build (``ninja`` or ``make``) target.

That build target will invoke ``sphinx-build`` with the appropriate options for
the project, and generate the HTML documentation in a ``tools/lld/docs/html``
subdirectory.

.. _writing_documentation:

Writing documentation
~~~~~~~~~~~~~~~~~~~~~

The documentation itself is written in the reStructuredText (ReST) format, and
Sphinx defines additional tags to support features like cross-referencing.

The ReST format itself is organized around documents mostly being readable
plaintext documents. You should generally be able to write new documentation
easily just by following the style of the existing documentation.

If you want to understand the formatting of the documents more, the best place
to start is Sphinx's own `ReST Primer <http://sphinx.pocoo.org/rest.html>`_.


Learning More
-------------

If you want to learn more about the Sphinx system, the best place to start is
the Sphinx documentation itself, available `here
<http://sphinx.pocoo.org/contents.html>`_.


.. _installing_sphinx_in_a_venv:

Installing Sphinx in a Virtual Environment
------------------------------------------

Most Python developers prefer to work with tools inside a *virtualenv* (virtual
environment) instance, which functions as an application sandbox. This avoids
polluting your system installation with different packages used by various
projects (and ensures that dependencies for different packages don't conflict
with one another). Of course, you need to first have the virtualenv software
itself which generally would be installed at the system level::

  $ sudo easy_install virtualenv

but after that you no longer need to install additional packages in the system
directories.

Once you have the *virtualenv* tool itself installed, you can create a
virtualenv for Sphinx using::

  $ virtualenv ~/my-sphinx-install
  New python executable in /Users/dummy/my-sphinx-install/bin/python
  Installing setuptools............done.
  Installing pip...............done.

  $ ~/my-sphinx-install/bin/easy_install sphinx
  ... install messages here ...

and from now on you can "activate" the *virtualenv* using::

  $ source ~/my-sphinx-install/bin/activate

which will change your PATH to ensure the sphinx-build tool from inside the
virtual environment will be used. See the `virtualenv website
<http://www.virtualenv.org/en/latest/index.html>`_ for more information on using
virtual environments.
