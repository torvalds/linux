=========================
Building External Modules
=========================

This document describes how to build an out-of-tree kernel module.

Introduction
============

"kbuild" is the build system used by the Linux kernel. Modules must use
kbuild to stay compatible with changes in the build infrastructure and
to pick up the right flags to the compiler. Functionality for building modules
both in-tree and out-of-tree is provided. The method for building
either is similar, and all modules are initially developed and built
out-of-tree.

Covered in this document is information aimed at developers interested
in building out-of-tree (or "external") modules. The author of an
external module should supply a makefile that hides most of the
complexity, so one only has to type "make" to build the module. This is
easily accomplished, and a complete example will be presented in
section `Creating a Kbuild File for an External Module`_.


How to Build External Modules
=============================

To build external modules, you must have a prebuilt kernel available
that contains the configuration and header files used in the build.
Also, the kernel must have been built with modules enabled. If you are
using a distribution kernel, there will be a package for the kernel you
are running provided by your distribution.

An alternative is to use the "make" target "modules_prepare." This will
make sure the kernel contains the information required. The target
exists solely as a simple way to prepare a kernel source tree for
building external modules.

NOTE: "modules_prepare" will not build Module.symvers even if
CONFIG_MODVERSIONS is set; therefore, a full kernel build needs to be
executed to make module versioning work.

Command Syntax
--------------

	The command to build an external module is::

		$ make -C <path_to_kernel_dir> M=$PWD

	The kbuild system knows that an external module is being built
	due to the "M=<dir>" option given in the command.

	To build against the running kernel use::

		$ make -C /lib/modules/`uname -r`/build M=$PWD

	Then to install the module(s) just built, add the target
	"modules_install" to the command::

		$ make -C /lib/modules/`uname -r`/build M=$PWD modules_install

	Starting from Linux 6.13, you can use the -f option instead of -C. This
	will avoid unnecessary change of the working directory. The external
	module will be output to the directory where you invoke make.

		$ make -f /lib/modules/`uname -r`/build/Makefile M=$PWD

Options
-------

	($KDIR refers to the path of the kernel source directory, or the path
	of the kernel output directory if the kernel was built in a separate
	build directory.)

	You can optionally pass MO= option if you want to build the modules in
	a separate directory.

	make -C $KDIR M=$PWD [MO=$BUILD_DIR]

	-C $KDIR
		The directory that contains the kernel and relevant build
		artifacts used for building an external module.
		"make" will actually change to the specified directory
		when executing and will change back when finished.

	M=$PWD
		Informs kbuild that an external module is being built.
		The value given to "M" is the absolute path of the
		directory where the external module (kbuild file) is
		located.

	MO=$BUILD_DIR
		Specifies a separate output directory for the external module.

Targets
-------

	When building an external module, only a subset of the "make"
	targets are available.

	make -C $KDIR M=$PWD [target]

	The default will build the module(s) located in the current
	directory, so a target does not need to be specified. All
	output files will also be generated in this directory. No
	attempts are made to update the kernel source, and it is a
	precondition that a successful "make" has been executed for the
	kernel.

	modules
		The default target for external modules. It has the
		same functionality as if no target was specified. See
		description above.

	modules_install
		Install the external module(s). The default location is
		/lib/modules/<kernel_release>/updates/, but a prefix may
		be added with INSTALL_MOD_PATH (discussed in section
		`Module Installation`_).

	clean
		Remove all generated files in the module directory only.

	help
		List the available targets for external modules.

Building Separate Files
-----------------------

	It is possible to build single files that are part of a module.
	This works equally well for the kernel, a module, and even for
	external modules.

	Example (The module foo.ko, consist of bar.o and baz.o)::

		make -C $KDIR M=$PWD bar.lst
		make -C $KDIR M=$PWD baz.o
		make -C $KDIR M=$PWD foo.ko
		make -C $KDIR M=$PWD ./


Creating a Kbuild File for an External Module
=============================================

In the last section we saw the command to build a module for the
running kernel. The module is not actually built, however, because a
build file is required. Contained in this file will be the name of
the module(s) being built, along with the list of requisite source
files. The file may be as simple as a single line::

	obj-m := <module_name>.o

The kbuild system will build <module_name>.o from <module_name>.c,
and, after linking, will result in the kernel module <module_name>.ko.
The above line can be put in either a "Kbuild" file or a "Makefile."
When the module is built from multiple sources, an additional line is
needed listing the files::

	<module_name>-y := <src1>.o <src2>.o ...

NOTE: Further documentation describing the syntax used by kbuild is
located in Documentation/kbuild/makefiles.rst.

The examples below demonstrate how to create a build file for the
module 8123.ko, which is built from the following files::

	8123_if.c
	8123_if.h
	8123_pci.c

Shared Makefile
---------------

	An external module always includes a wrapper makefile that
	supports building the module using "make" with no arguments.
	This target is not used by kbuild; it is only for convenience.
	Additional functionality, such as test targets, can be included
	but should be filtered out from kbuild due to possible name
	clashes.

	Example 1::

		--> filename: Makefile
		ifneq ($(KERNELRELEASE),)
		# kbuild part of makefile
		obj-m  := 8123.o
		8123-y := 8123_if.o 8123_pci.o

		else
		# normal makefile
		KDIR ?= /lib/modules/`uname -r`/build

		default:
			$(MAKE) -C $(KDIR) M=$$PWD

		endif

	The check for KERNELRELEASE is used to separate the two parts
	of the makefile. In the example, kbuild will only see the two
	assignments, whereas "make" will see everything except these
	two assignments. This is due to two passes made on the file:
	the first pass is by the "make" instance run on the command
	line; the second pass is by the kbuild system, which is
	initiated by the parameterized "make" in the default target.

Separate Kbuild File and Makefile
---------------------------------

	Kbuild will first look for a file named "Kbuild", and if it is not
	found, it will then look for "Makefile". Utilizing a "Kbuild" file
	allows us to split up the "Makefile" from example 1 into two files:

	Example 2::

		--> filename: Kbuild
		obj-m  := 8123.o
		8123-y := 8123_if.o 8123_pci.o

		--> filename: Makefile
		KDIR ?= /lib/modules/`uname -r`/build

		default:
			$(MAKE) -C $(KDIR) M=$$PWD

	The split in example 2 is questionable due to the simplicity of
	each file; however, some external modules use makefiles
	consisting of several hundred lines, and here it really pays
	off to separate the kbuild part from the rest.

	Linux 6.13 and later support another way. The external module Makefile
	can include the kernel Makefile directly, rather than invoking sub Make.

	Example 3::

		--> filename: Kbuild
		obj-m  := 8123.o
		8123-y := 8123_if.o 8123_pci.o

		--> filename: Makefile
		KDIR ?= /lib/modules/$(shell uname -r)/build
		export KBUILD_EXTMOD := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
		include $(KDIR)/Makefile


Building Multiple Modules
-------------------------

	kbuild supports building multiple modules with a single build
	file. For example, if you wanted to build two modules, foo.ko
	and bar.ko, the kbuild lines would be::

		obj-m := foo.o bar.o
		foo-y := <foo_srcs>
		bar-y := <bar_srcs>

	It is that simple!


Include Files
=============

Within the kernel, header files are kept in standard locations
according to the following rule:

	* If the header file only describes the internal interface of a
	  module, then the file is placed in the same directory as the
	  source files.
	* If the header file describes an interface used by other parts
	  of the kernel that are located in different directories, then
	  the file is placed in include/linux/.

	  NOTE:
	      There are two notable exceptions to this rule: larger
	      subsystems have their own directory under include/, such as
	      include/scsi; and architecture specific headers are located
	      under arch/$(SRCARCH)/include/.

Kernel Includes
---------------

	To include a header file located under include/linux/, simply
	use::

		#include <linux/module.h>

	kbuild will add options to the compiler so the relevant directories
	are searched.

Single Subdirectory
-------------------

	External modules tend to place header files in a separate
	include/ directory where their source is located, although this
	is not the usual kernel style. To inform kbuild of the
	directory, use either ccflags-y or CFLAGS_<filename>.o.

	Using the example from section 3, if we moved 8123_if.h to a
	subdirectory named include, the resulting kbuild file would
	look like::

		--> filename: Kbuild
		obj-m := 8123.o

		ccflags-y := -I $(src)/include
		8123-y := 8123_if.o 8123_pci.o

Several Subdirectories
----------------------

	kbuild can handle files that are spread over several directories.
	Consider the following example::

		.
		|__ src
		|   |__ complex_main.c
		|   |__ hal
		|	|__ hardwareif.c
		|	|__ include
		|	    |__ hardwareif.h
		|__ include
			|__ complex.h

	To build the module complex.ko, we then need the following
	kbuild file::

		--> filename: Kbuild
		obj-m := complex.o
		complex-y := src/complex_main.o
		complex-y += src/hal/hardwareif.o

		ccflags-y := -I$(src)/include
		ccflags-y += -I$(src)/src/hal/include

	As you can see, kbuild knows how to handle object files located
	in other directories. The trick is to specify the directory
	relative to the kbuild file's location. That being said, this
	is NOT recommended practice.

	For the header files, kbuild must be explicitly told where to
	look. When kbuild executes, the current directory is always the
	root of the kernel tree (the argument to "-C") and therefore an
	absolute path is needed. $(src) provides the absolute path by
	pointing to the directory where the currently executing kbuild
	file is located.


Module Installation
===================

Modules which are included in the kernel are installed in the
directory:

	/lib/modules/$(KERNELRELEASE)/kernel/

And external modules are installed in:

	/lib/modules/$(KERNELRELEASE)/updates/

INSTALL_MOD_PATH
----------------

	Above are the default directories but as always some level of
	customization is possible. A prefix can be added to the
	installation path using the variable INSTALL_MOD_PATH::

		$ make INSTALL_MOD_PATH=/frodo modules_install
		=> Install dir: /frodo/lib/modules/$(KERNELRELEASE)/kernel/

	INSTALL_MOD_PATH may be set as an ordinary shell variable or,
	as shown above, can be specified on the command line when
	calling "make." This has effect when installing both in-tree
	and out-of-tree modules.

INSTALL_MOD_DIR
---------------

	External modules are by default installed to a directory under
	/lib/modules/$(KERNELRELEASE)/updates/, but you may wish to
	locate modules for a specific functionality in a separate
	directory. For this purpose, use INSTALL_MOD_DIR to specify an
	alternative name to "updates."::

		$ make INSTALL_MOD_DIR=gandalf -C $KDIR \
		       M=$PWD modules_install
		=> Install dir: /lib/modules/$(KERNELRELEASE)/gandalf/


Module Versioning
=================

Module versioning is enabled by the CONFIG_MODVERSIONS tag, and is used
as a simple ABI consistency check. A CRC value of the full prototype
for an exported symbol is created. When a module is loaded/used, the
CRC values contained in the kernel are compared with similar values in
the module; if they are not equal, the kernel refuses to load the
module.

Module.symvers contains a list of all exported symbols from a kernel
build.

Symbols From the Kernel (vmlinux + modules)
-------------------------------------------

	During a kernel build, a file named Module.symvers will be
	generated. Module.symvers contains all exported symbols from
	the kernel and compiled modules. For each symbol, the
	corresponding CRC value is also stored.

	The syntax of the Module.symvers file is::

		<CRC>       <Symbol>         <Module>                         <Export Type>     <Namespace>

		0xe1cc2a05  usb_stor_suspend drivers/usb/storage/usb-storage  EXPORT_SYMBOL_GPL USB_STORAGE

	The fields are separated by tabs and values may be empty (e.g.
	if no namespace is defined for an exported symbol).

	For a kernel build without CONFIG_MODVERSIONS enabled, the CRC
	would read 0x00000000.

	Module.symvers serves two purposes:

	1) It lists all exported symbols from vmlinux and all modules.
	2) It lists the CRC if CONFIG_MODVERSIONS is enabled.

Version Information Formats
---------------------------

	Exported symbols have information stored in __ksymtab or __ksymtab_gpl
	sections. Symbol names and namespaces are stored in __ksymtab_strings,
	using a format similar to the string table used for ELF. If
	CONFIG_MODVERSIONS is enabled, the CRCs corresponding to exported
	symbols will be added to the __kcrctab or __kcrctab_gpl.

	If CONFIG_BASIC_MODVERSIONS is enabled (default with
	CONFIG_MODVERSIONS), imported symbols will have their symbol name and
	CRC stored in the __versions section of the importing module. This
	mode only supports symbols of length up to 64 bytes.

	If CONFIG_EXTENDED_MODVERSIONS is enabled (required to enable both
	CONFIG_MODVERSIONS and CONFIG_RUST at the same time), imported symbols
	will have their symbol name recorded in the __version_ext_names
	section as a series of concatenated, null-terminated strings. CRCs for
	these symbols will be recorded in the __version_ext_crcs section.

Symbols and External Modules
----------------------------

	When building an external module, the build system needs access
	to the symbols from the kernel to check if all external symbols
	are defined. This is done in the MODPOST step. modpost obtains
	the symbols by reading Module.symvers from the kernel source
	tree. During the MODPOST step, a new Module.symvers file will be
	written containing all exported symbols from that external module.

Symbols From Another External Module
------------------------------------

	Sometimes, an external module uses exported symbols from
	another external module. Kbuild needs to have full knowledge of
	all symbols to avoid spitting out warnings about undefined
	symbols. Two solutions exist for this situation.

	NOTE: The method with a top-level kbuild file is recommended
	but may be impractical in certain situations.

	Use a top-level kbuild file
		If you have two modules, foo.ko and bar.ko, where
		foo.ko needs symbols from bar.ko, you can use a
		common top-level kbuild file so both modules are
		compiled in the same build. Consider the following
		directory layout::

			./foo/ <= contains foo.ko
			./bar/ <= contains bar.ko

		The top-level kbuild file would then look like::

			#./Kbuild (or ./Makefile):
				obj-m := foo/ bar/

		And executing::

			$ make -C $KDIR M=$PWD

		will then do the expected and compile both modules with
		full knowledge of symbols from either module.

	Use "make" variable KBUILD_EXTRA_SYMBOLS
		If it is impractical to add a top-level kbuild file,
		you can assign a space separated list
		of files to KBUILD_EXTRA_SYMBOLS in your build file.
		These files will be loaded by modpost during the
		initialization of its symbol tables.


Tips & Tricks
=============

Testing for CONFIG_FOO_BAR
--------------------------

	Modules often need to check for certain `CONFIG_` options to
	decide if a specific feature is included in the module. In
	kbuild this is done by referencing the `CONFIG_` variable
	directly::

		#fs/ext2/Makefile
		obj-$(CONFIG_EXT2_FS) += ext2.o

		ext2-y := balloc.o bitmap.o dir.o
		ext2-$(CONFIG_EXT2_FS_XATTR) += xattr.o
