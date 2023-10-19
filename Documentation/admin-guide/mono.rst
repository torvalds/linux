Mono(tm) Binary Kernel Support for Linux
-----------------------------------------

To configure Linux to automatically execute Mono-based .NET binaries
(in the form of .exe files) without the need to use the mono CLR
wrapper, you can use the BINFMT_MISC kernel support.

This will allow you to execute Mono-based .NET binaries just like any
other program after you have done the following:

1) You MUST FIRST install the Mono CLR support, either by downloading
   a binary package, a source tarball or by installing from Git. Binary
   packages for several distributions can be found at:

	https://www.mono-project.com/download/

   Instructions for compiling Mono can be found at:

	https://www.mono-project.com/docs/compiling-mono/linux/

   Once the Mono CLR support has been installed, just check that
   ``/usr/bin/mono`` (which could be located elsewhere, for example
   ``/usr/local/bin/mono``) is working.

2) You have to compile BINFMT_MISC either as a module or into
   the kernel (``CONFIG_BINFMT_MISC``) and set it up properly.
   If you choose to compile it as a module, you will have
   to insert it manually with modprobe/insmod, as kmod
   cannot be easily supported with binfmt_misc.
   Read the file ``binfmt_misc.txt`` in this directory to know
   more about the configuration process.

3) Add the following entries to ``/etc/rc.local`` or similar script
   to be run at system startup:

   .. code-block:: sh

    # Insert BINFMT_MISC module into the kernel
    if [ ! -e /proc/sys/fs/binfmt_misc/register ]; then
        /sbin/modprobe binfmt_misc
	# Some distributions, like Fedora Core, perform
	# the following command automatically when the
	# binfmt_misc module is loaded into the kernel
	# or during normal boot up (systemd-based systems).
	# Thus, it is possible that the following line
	# is not needed at all.
	mount -t binfmt_misc none /proc/sys/fs/binfmt_misc
    fi

    # Register support for .NET CLR binaries
    if [ -e /proc/sys/fs/binfmt_misc/register ]; then
	# Replace /usr/bin/mono with the correct pathname to
	# the Mono CLR runtime (usually /usr/local/bin/mono
	# when compiling from sources or CVS).
        echo ':CLR:M::MZ::/usr/bin/mono:' > /proc/sys/fs/binfmt_misc/register
    else
        echo "No binfmt_misc support"
        exit 1
    fi

4) Check that ``.exe`` binaries can be ran without the need of a
   wrapper script, simply by launching the ``.exe`` file directly
   from a command prompt, for example::

	/usr/bin/xsd.exe

   .. note::

      If this fails with a permission denied error, check
      that the ``.exe`` file has execute permissions.
