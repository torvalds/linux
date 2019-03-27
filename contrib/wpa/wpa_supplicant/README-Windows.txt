wpa_supplicant for Windows
==========================

Copyright (c) 2003-2009, Jouni Malinen <j@w1.fi> and contributors
All Rights Reserved.

This program is licensed under the BSD license (the one with
advertisement clause removed).


wpa_supplicant has support for being used as a WPA/WPA2/IEEE 802.1X
Supplicant on Windows. The current port requires that WinPcap
(http://winpcap.polito.it/) is installed for accessing packets and the
driver interface. Both release versions 3.0 and 3.1 are supported.

The current port is still somewhat experimental. It has been tested
mainly on Windows XP (SP2) with limited set of NDIS drivers. In
addition, the current version has been reported to work with Windows
2000.

All security modes have been verified to work (at least complete
authentication and successfully ping a wired host):
- plaintext
- static WEP / open system authentication
- static WEP / shared key authentication
- IEEE 802.1X with dynamic WEP keys
- WPA-PSK, TKIP, CCMP, TKIP+CCMP
- WPA-EAP, TKIP, CCMP, TKIP+CCMP
- WPA2-PSK, TKIP, CCMP, TKIP+CCMP
- WPA2-EAP, TKIP, CCMP, TKIP+CCMP


Building wpa_supplicant with mingw
----------------------------------

The default build setup for wpa_supplicant is to use MinGW and
cross-compiling from Linux to MinGW/Windows. It should also be
possible to build this under Windows using the MinGW tools, but that
is not tested nor supported and is likely to require some changes to
the Makefile unless cygwin is used.


Building wpa_supplicant with MSVC
---------------------------------

wpa_supplicant can be built with Microsoft Visual C++ compiler. This
has been tested with Microsoft Visual C++ Toolkit 2003 and Visual
Studio 2005 using the included nmake.mak as a Makefile for nmake. IDE
can also be used by creating a project that includes the files and
defines mentioned in nmake.mak. Example VS2005 solution and project
files are included in vs2005 subdirectory. This can be used as a
starting point for building the programs with VS2005 IDE. Visual Studio
2008 Express Edition is also able to use these project files.

WinPcap development package is needed for the build and this can be
downloaded from http://www.winpcap.org/install/bin/WpdPack_4_0_2.zip. The
default nmake.mak expects this to be unpacked into C:\dev\WpdPack so
that Include and Lib directories are in this directory. The files can be
stored elsewhere as long as the WINPCAPDIR in nmake.mak is updated to
match with the selected directory. In case a project file in the IDE is
used, these Include and Lib directories need to be added to project
properties as additional include/library directories.

OpenSSL source package can be downloaded from
http://www.openssl.org/source/openssl-0.9.8i.tar.gz and built and
installed following instructions in INSTALL.W32. Note that if EAP-FAST
support will be included in the wpa_supplicant, OpenSSL needs to be
patched to# support it openssl-0.9.8i-tls-extensions.patch. The example
nmake.mak file expects OpenSSL to be installed into C:\dev\openssl, but
this directory can be modified by changing OPENSSLDIR variable in
nmake.mak.

If you do not need EAP-FAST support, you may also be able to use Win32
binary installation package of OpenSSL from
http://www.slproweb.com/products/Win32OpenSSL.html instead of building
the library yourself. In this case, you will need to copy Include and
Lib directories in suitable directory, e.g., C:\dev\openssl for the
default nmake.mak. Copy {Win32OpenSSLRoot}\include into
C:\dev\openssl\include and make C:\dev\openssl\lib subdirectory with
files from {Win32OpenSSLRoot}\VC (i.e., libeay*.lib and ssleay*.lib).
This will end up using dynamically linked OpenSSL (i.e., .dll files are
needed) for it. Alternative, you can copy files from
{Win32OpenSSLRoot}\VC\static to create a static build (no OpenSSL .dll
files needed).


Building wpa_supplicant for cygwin
----------------------------------

wpa_supplicant can be built for cygwin by installing the needed
development packages for cygwin. This includes things like compiler,
make, openssl development package, etc. In addition, developer's pack
for WinPcap (WPdpack.zip) from
http://winpcap.polito.it/install/default.htm is needed.

.config file should enable only one driver interface,
CONFIG_DRIVER_NDIS. In addition, include directories may need to be
added to match the system. An example configuration is available in
defconfig. The library and include files for WinPcap will either need
to be installed in compiler/linker default directories or their
location will need to be adding to .config when building
wpa_supplicant.

Othen than this, the build should be more or less identical to Linux
version, i.e., just run make after having created .config file. An
additional tool, win_if_list.exe, can be built by running "make
win_if_list".


Building wpa_gui
----------------

wpa_gui uses Qt application framework from Trolltech. It can be built
with the open source version of Qt4 and MinGW. Following commands can
be used to build the binary in the Qt 4 Command Prompt:

# go to the root directory of wpa_supplicant source code
cd wpa_gui-qt4
qmake -o Makefile wpa_gui.pro
make
# the wpa_gui.exe binary is created into 'release' subdirectory


Using wpa_supplicant for Windows
--------------------------------

wpa_supplicant, wpa_cli, and wpa_gui behave more or less identically to
Linux version, so instructions in README and example wpa_supplicant.conf
should be applicable for most parts. In addition, there is another
version of wpa_supplicant, wpasvc.exe, which can be used as a Windows
service and which reads its configuration from registry instead of
text file.

When using access points in "hidden SSID" mode, ap_scan=2 mode need to
be used (see wpa_supplicant.conf for more information).

Windows NDIS/WinPcap uses quite long interface names, so some care
will be needed when starting wpa_supplicant. Alternatively, the
adapter description can be used as the interface name which may be
easier since it is usually in more human-readable
format. win_if_list.exe can be used to find out the proper interface
name.

Example steps in starting up wpa_supplicant:

# win_if_list.exe
ifname: \Device\NPF_GenericNdisWanAdapter
description: Generic NdisWan adapter

ifname: \Device\NPF_{769E012B-FD17-4935-A5E3-8090C38E25D2}
description: Atheros Wireless Network Adapter (Microsoft's Packet Scheduler)

ifname: \Device\NPF_{732546E7-E26C-48E3-9871-7537B020A211}
description: Intel 8255x-based Integrated Fast Ethernet (Microsoft's Packet Scheduler)


Since the example configuration used Atheros WLAN card, the middle one
is the correct interface in this case. The interface name for -i
command line option is the full string following "ifname:" (the
"\Device\NPF_" prefix can be removed). In other words, wpa_supplicant
would be started with the following command:

# wpa_supplicant.exe -i'{769E012B-FD17-4935-A5E3-8090C38E25D2}' -c wpa_supplicant.conf -d

-d optional enables some more debugging (use -dd for even more, if
needed). It can be left out if debugging information is not needed.

With the alternative mechanism for selecting the interface, this
command has identical results in this case:

# wpa_supplicant.exe -iAtheros -c wpa_supplicant.conf -d


Simple configuration example for WPA-PSK:

#ap_scan=2
ctrl_interface=
network={
	ssid="test"
	key_mgmt=WPA-PSK
	proto=WPA
	pairwise=TKIP
	psk="secret passphrase"
}

(remove '#' from the comment out ap_scan line to enable mode in which
wpa_supplicant tries to associate with the SSID without doing
scanning; this allows APs with hidden SSIDs to be used)


wpa_cli.exe and wpa_gui.exe can be used to interact with the
wpa_supplicant.exe program in the same way as with Linux. Note that
ctrl_interface is using UNIX domain sockets when built for cygwin, but
the native build for Windows uses named pipes and the contents of the
ctrl_interface configuration item is used to control access to the
interface. Anyway, this variable has to be included in the configuration
to enable the control interface.


Example SDDL string formats:

(local admins group has permission, but nobody else):

ctrl_interface=SDDL=D:(A;;GA;;;BA)

("A" == "access allowed", "GA" == GENERIC_ALL == all permissions, and
"BA" == "builtin administrators" == the local admins.  The empty fields
are for flags and object GUIDs, none of which should be required in this
case.)

(local admins and the local "power users" group have permissions,
but nobody else):

ctrl_interface=SDDL=D:(A;;GA;;;BA)(A;;GA;;;PU)

(One ACCESS_ALLOWED ACE for GENERIC_ALL for builtin administrators, and
one ACCESS_ALLOWED ACE for GENERIC_ALL for power users.)

(close to wide open, but you have to be a valid user on
the machine):

ctrl_interface=SDDL=D:(A;;GA;;;AU)

(One ACCESS_ALLOWED ACE for GENERIC_ALL for the "authenticated users"
group.)

This one would allow absolutely everyone (including anonymous
users) -- this is *not* recommended, since named pipes can be attached
to from anywhere on the network (i.e. there's no "this machine only"
like there is with 127.0.0.1 sockets):

ctrl_interface=SDDL=D:(A;;GA;;;BU)(A;;GA;;;AN)

(BU == "builtin users", "AN" == "anonymous")

See also [1] for the format of ACEs, and [2] for the possible strings
that can be used for principal names.

[1]
http://msdn.microsoft.com/library/default.asp?url=/library/en-us/secauthz/security/ace_strings.asp
[2]
http://msdn.microsoft.com/library/default.asp?url=/library/en-us/secauthz/security/sid_strings.asp


Starting wpa_supplicant as a Windows service (wpasvc.exe)
---------------------------------------------------------

wpa_supplicant can be started as a Windows service by using wpasvc.exe
program that is alternative build of wpa_supplicant.exe. Most of the
core functionality of wpasvc.exe is identical to wpa_supplicant.exe,
but it is using Windows registry for configuration information instead
of a text file and command line parameters. In addition, it can be
registered as a service that can be started automatically or manually
like any other Windows service.

The root of wpa_supplicant configuration in registry is
HKEY_LOCAL_MACHINE\SOFTWARE\wpa_supplicant. This level includes global
parameters and a 'interfaces' subkey with all the interface configuration
(adapter to confname mapping). Each such mapping is a subkey that has
'adapter', 'config', and 'ctrl_interface' values.

This program can be run either as a normal command line application,
e.g., for debugging, with 'wpasvc.exe app' or as a Windows service.
Service need to be registered with 'wpasvc.exe reg <full path to
wpasvc.exe>'. Alternatively, 'wpasvc.exe reg' can be used to register
the service with the current location of wpasvc.exe. After this, wpasvc
can be started like any other Windows service (e.g., 'net start wpasvc')
or it can be configured to start automatically through the Services tool
in administrative tasks. The service can be unregistered with
'wpasvc.exe unreg'.

If the service is set to start during system bootup to make the
network connection available before any user has logged in, there may
be a long (half a minute or so) delay in starting up wpa_supplicant
due to WinPcap needing a driver called "Network Monitor Driver" which
is started by default on demand.

To speed up wpa_supplicant start during system bootup, "Network
Monitor Driver" can be configured to be started sooner by setting its
startup type to System instead of the default Demand. To do this, open
up Device Manager, select Show Hidden Devices, expand the "Non
Plug-and-Play devices" branch, double click "Network Monitor Driver",
go to the Driver tab, and change the Demand setting to System instead.

Configuration data is in HKEY_LOCAL_MACHINE\SOFTWARE\wpa_supplicant\configs
key. Each configuration profile has its own key under this. In terms of text
files, each profile would map to a separate text file with possibly multiple
networks. Under each profile, there is a networks key that lists all
networks as a subkey. Each network has set of values in the same way as
network block in the configuration file. In addition, blobs subkey has
possible blobs as values.

HKEY_LOCAL_MACHINE\SOFTWARE\wpa_supplicant\configs\test\networks\0000
   ssid="example"
   key_mgmt=WPA-PSK

See win_example.reg for an example on how to setup wpasvc.exe
parameters in registry. It can also be imported to registry as a
starting point for the configuration.
