Building the Mali Device Driver for Linux
-----------------------------------------

Build the Mali Device Driver for Linux by running the following make command:

KDIR=<kdir_path> USING_UMP=<ump_option> BUILD=<build_option> make

where
    kdir_path: Path to your Linux Kernel directory
    ump_option: 1 = Enable UMP support(*)
                0 = disable UMP support
    build_option: debug = debug build of driver
                  release = release build of driver

(*)  For newer Linux Kernels, the Module.symvers file for the UMP device driver
     must be available. The UMP_SYMVERS_FILE variable in the Makefile should
     point to this file. This file is generated when the UMP driver is built.

The result will be a mali.ko file, which can be loaded into the Linux kernel
by using the insmod command.

The kernel needs to be provided with a platform_device struct for the Mali GPU
device. See the mali_utgard.h header file for how to set up the Mali GPU
resources.
