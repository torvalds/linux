MDB Linux Kernel Debugger

MDB (Minimal Kernel Debugger) was written in 1998 and was one of the 
earliest debuggers on Linux. It was originally developed for Linux 
kernel file system development on the 2.2 series Linux kernels to be 
used as a loadable module.

MDB supports a lot of features and capabilities and enhances the range 
of tools for debugging kernel applications on Linux. If MDB is useful 
to you, please feel free to contribute any changes or enhancements back. 
MDB is fast, loads as a module, and supports complex conditional breakpoints 
and disassembly on the fly, and is extremely useful for debugging in the 
field.

Hardware Required: x86-64 or ia-32 system with Standard Keyboard or remote 
serial port. USB Keyboard not supported.

Installation

There are several options for installing MDB on your linux systems. You can 
download or clone the entire repository from git, you can download a simple 
patch or diff and patch your kernel (diff is easiest), or you can download 
a tar.gz file of all the current source code for each branch as a completely 
integrated kernel source tree. The simplest and fastest method is to 
download a diff and patch and build your kernel.

Download the diff which matches your particular linux version from 
http://jeffmerkey.github.io/linux, then apply the patch from your linux build 
directory. To download the diff and convert it into a patch file, click on 
the link for your kernel version for your system, then the patch text will 
display in your browser window, use your mouse to right click, then save as 
and save the patch into any filename you wish. It does not matter what 
filename you save the file under, you can choose any filename you wish, so 
long as it matches the filename you specify when you run the patch utility. 
To patch the kernel, from the linux build directory, type:

(linux build directory) patch -p1 < filename.diff

then type

make oldconfig

make bzImage modules modules_install install

You can also type "make menuconfig" to manually adjust kernel settings for 
your debugger under the section "Kernel Hacking". After the kernel rebuilds, 
reboot your system to the new kernel and you can load MDB as a module.

MDB Build Options

These are the following build options for enabling and configuring MDB to run 
on your linux kernel.

CONFIG_MDB

Kernel Debugger for Linux written to be used as a loadable module. 
'echo a > /proc/sysrq-trigger' will activate the debugger from the text 
based Linux Console, and the 'a' character (alt-printscreen/sysrq + 'a') 
with MAGIC_SYSRQ enabled to enter the debugger. X Windows is not supported. 
This debugger is minimal and text based. Excellent help can be obtained 
from the debugger by typing "Help Help" or "help" from the debugger command 
console. If you compiled MDB as a module you may need to load the debugger 
with the 'modprobe mdb' before you can access the debugger from the Linux 
Console. To enable the MAGIC-SYSRQ key type 
'echo 1 > /proc/sys/kernel/sysrq'. Use 'modinfo mdb' to display params 
for setting a serial port address for remote operations over serial link.

CONFIG_MDB_CONSOLE_REDIRECTION

Reset any Console Redirection to default system settings (0) when the MDB 
debugger is active. The Debugger will restore the redirection to the custom 
settings when the debugger is exited. This feature is useful on systems 
which by default redirect printk output and the screen debugger output to 
a log file or system device, which can prevent the debugger screen from 
being visible. Enabling this feature does not affect or disable remote 
operations via serial port. It is recommended to enable this feature by 
default to prevent the system console from being redirected during debugging.

CONFIG_MDB_DIRECT_MODE

Disable the default hardware breakpoint facility in Linux and allow MDB to 
control the debugger registers directly. This feature will disable KGBD, 
KDB-KGDB, and Ptrace but will still allow the userspace GDB debugger to 
function concurrently with MDB. Use this option if you want to disable 
other kernel debugging facilities other than mdb. Not recommended unless 
you really know what you are doing.

Loading MDB

To load MDB as a module, type:

modprobe mdb

To enter the debugger type:

echo a > /proc/sysrq-trigger

The debugger has excellent online help, just type 'h' or 'help' from the 
debugger console for a list of commands. For help with a specific command, 
just type 'help (command)' where (command) is the command you need help with.

Using MDB over a serial port

MDB is designed to be used from the local system console via keyboard, but 
can also be operated over a serial port and has very basic serial support. 
To set the serial port address, simply write the serial port address you
wish to use into the MDB module parameter mdb_serial_port located at 
sys/module/mdb/parameters/mdb_serial_port after you have loaded the MDB 
module.

cat /sys/module/mdb/parameters/mdb_serial_port

will display the current settings for the serial port. Default is "0".

echo 0x3f8 > /sys/module/mdb/parameters/mdb_serial_port

will set the serial port address to COM1 (ttyS0) which is port address 0x3f8.

To display other serial port address options most commonly used, type modinfo 
mdb. MDB expects the serial port address as a numerical value, so don't try 
to enter "ttyXX", use a numerical serial port address.

[root@localhost ~]# modinfo mdb
filename: /lib/modules/4.3.0+/kernel/arch/x86/kernel/debug/mdb/mdb.ko
license: GPL
description: Minimal Kernel Debugger
srcversion: C1CAC573BB00F3C65D21F7C
depends:
intree: Y
vermagic: 4.3.0+ SMP mod_unload modversions
parm: mdb_serial_port:MDB serial port address. 
i.e. 0x3f8(ttyS0), 0x2f8(ttyS1), 0x3e8(ttyS2), 0x2e8(ttyS3) (uint)
[root@localhost ~]#

Disabling KDB/KGDB

You need to check whether or not another kernel debugger is active on the 
system, and if so, disable it. Both the KGDB and KDB debuggers are available 
as default build options on Linux. Only one debugger can be running at a time 
on the system to function properly. Don't try to run MDB if KDB or KGDB are 
active, disable them first. If MDB is compiled as a module, during module 
loading MDB will attempt to disable KGDB/KDB if it detects either is active. 
You may see something like this in your /var/log/messages file after the 
module loads which indicates it detected and disabled KGDB or KDB:

localhost kernel: KGDB: Registered I/O driver kgdboc
localhost kernel: MDB: kgdb currently set to [kdb], attempting to disable.
localhost kernel: KGDB: Unregistered I/O driver kgdboc, debugger disabled
localhost kernel: MDB: kgdb/kdb set to disabled. MDB is enabled.

By default, MDB will always compile as a module unless you select it to 
compile directly into the kernel. If MDB is compiled in the kernel directly, 
you may need to check whether KGDB/KDB have been enabled before activating 
the MDB debugger.

To check whether kdb or kgdb are running on your system, type:

cat /sys/module/kgdboc/parameters/kgdboc

if the text string "kdb", "kbd", or "tty"(some numbers) show up disable this 
interface by typing:

echo "" > /sys/module/kgdboc/parameters/kgdboc

which will disable kgdb and kdb and allow MDB to function correctly.

Using Git to pull remote MDB Branches

You can also use git to pull any of the debugger branches into your local git 
clone of the default linux and linux-stable git trees from www.kernel.org and 
build MDB directly from your tree as each branch corresponds to a particular 
kernel build and revision. To pull a remote branch from your local repository 
type:

git remote add mdb https://github.com/jeffmerkey/linux.git

git fetch mdb mdb-(branch version) i.e. git fetch mdb mdb-v4.3

make certain you create a local branch to merge to

git checkout (kernel version) i.e. git checkout v4.3

git branch (mdb branch) i.e. git branch mdb-v4.3

git checkout (mdb branch) i.e. git checkout mdb-v4.3

then if you wish to commit those changes, type:

git merge mdb/(branch version) i.e. git merge mdb/mdb-v4.3

you can also just pull directly and skip the fetch/merge steps just make 
certain you have created and switched to a local branch to commit your 
changes into.

git pull mdb mdb-(branch version) i.e. git pull mdb mdb-v4.3

After you have merged your changes into your local branch, switch to your 
local linux build directory then you can build your kernel by typing:

make oldconfig

make bzImage modules modules_install install

then follow the instructions above for loading MDB from the linux command 
line.

Support or Contact
Need to report a bug or issue with the MDB Linux Kernel Debugger? Please 
post it to Issues.

Having trouble with Pages? Check out Github's documentation or contact Github 
support and we'll help you sort it out.

Attribution

Linux(r) is a registered trademark of Linus Torvalds in the US and other countries.
