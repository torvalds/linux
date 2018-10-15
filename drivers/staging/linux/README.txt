You must build the Linux driver against the version of the Linux kernel you have
installed. This will require the Linux kernel headers. After you've built the
driver you can install it in your system so that it loads at boot time. If the
driver is installed and there is a RIFFA 2.0.1 capable FPGA installed as well, 
the driver will detect it. Output in the system log will provide additional 
information. This makefile will also build and install the C/C++ native library.

Ensure you have the kernel headers installed:

sudo make setup

This will attempt to install the kernel headers using your system's package
manager. You can skip this step if you've already installed the kernel headers.

Compile the driver and C/C++ library:

make

or 

make debug

Using make debug will compile in code to output debug messages to the system log
at runtime. These messages are useful when developing your design. However they
pollute your system log and incur some overhead. So you may want to install the
non-debug version after you've completed development.

Install the driver and library:

sudo make install

The system will be configured to load the driver at boot time. The C/C++ library
will be installed in the default library path. The header files will be placed
in the default include path. You will want to reboot after you've installed for
the driver to be (re)loaded.

When compiling an application you should only need to include the <riffa.h> 
header file and link with -lriffa.
