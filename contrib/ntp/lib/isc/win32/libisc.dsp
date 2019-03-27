# Microsoft Developer Studio Project File - Name="libisc" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=libisc - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "libisc.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libisc.mak" CFG="libisc - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libisc - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libisc - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "libisc - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "BIND9" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBISC_EXPORTS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "./" /I "../../../" /I "../../../../libxml2-2.7.3/include" /I "include" /I "../include" /I "../noatomic/include" /I "win32" /I "../../isccfg/include" /D "BIND9" /D "WIN32" /D "NDEBUG" /D "__STDC__" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBISC_EXPORTS" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 ../../../../libxml2-2.7.3/win32/bin.msvc/libxml2.lib 
# ADD LINK32 user32.lib advapi32.lib ws2_32.lib /nologo /dll /machine:I386 /out:"../../../Build/Release/libisc.dll"
# SUBTRACT LINK32 /pdb:none

!ELSEIF  "$(CFG)" == "libisc - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "BIND9" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "LIBISC_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "./" /I "../../../" /I "../../../../libxml2-2.7.3/include" /I "include" /I "../include" /I "../noatomic/include" /I "win32" /I "../../isccfg/include" /D "BIND9" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "__STDC__" /D "_MBCS" /D "_USRDLL" /D "LIBISC_EXPORTS" /FR /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 ../../../../libxml2-2.7.3/win32/bin.msvc/libxml2.lib 
# ADD LINK32 user32.lib advapi32.lib ws2_32.lib /nologo /dll /map /debug /machine:I386 /out:"../../../Build/Debug/libisc.dll" /pdbtype:sept

!ENDIF 

# Begin Target

# Name "libisc - Win32 Release"
# Name "libisc - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=.\app.c
# End Source File
# Begin Source File

SOURCE=.\condition.c
# End Source File
# Begin Source File

SOURCE=.\dir.c
# End Source File
# Begin Source File

SOURCE=.\DLLMain.c
# End Source File
# Begin Source File

SOURCE=.\entropy.c
# End Source File
# Begin Source File

SOURCE=.\errno2result.c
# End Source File
# Begin Source File

SOURCE=.\file.c
# End Source File
# Begin Source File

SOURCE=.\fsaccess.c
# End Source File
# Begin Source File

SOURCE=.\interfaceiter.c
# End Source File
# Begin Source File

SOURCE=.\ipv6.c
# End Source File
# Begin Source File

SOURCE=..\iterated_hash.c
# End Source File
# Begin Source File

SOURCE=.\keyboard.c
# End Source File
# Begin Source File

SOURCE=.\net.c
# End Source File
# Begin Source File

SOURCE=.\ntpaths.c
# End Source File
# Begin Source File

SOURCE=.\once.c
# End Source File
# Begin Source File

SOURCE=.\os.c
# End Source File
# Begin Source File

SOURCE=.\resource.c
# End Source File
# Begin Source File

SOURCE=.\socket.c
# End Source File
# Begin Source File

SOURCE=.\strerror.c
# End Source File
# Begin Source File

SOURCE=.\stdio.c
# End Source File
# Begin Source File

SOURCE=.\stdtime.c
# End Source File
# Begin Source File

SOURCE=.\syslog.c
# End Source File
# Begin Source File

SOURCE=.\thread.c
# End Source File
# Begin Source File

SOURCE=.\time.c
# End Source File
# Begin Source File

SOURCE=.\version.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=.\include\isc\app.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\assertions.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\backtrace.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\backtrace-emptytbl.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\base32.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\base64.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\bind_registry.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\bindevt.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\bitstring.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\boolean.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\buffer.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\bufferlist.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\commandline.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\condition.h
# End Source File
# Begin Source File

SOURCE=..\..\..\config.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\dir.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\entropy.h
# End Source File
# Begin Source File

SOURCE=.\errno2result.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\error.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\event.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\eventclass.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\file.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\formatcheck.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\fsaccess.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\hash.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\heap.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\hex.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\hmacmd5.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\hmacsha.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\httpd.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\int.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\interfaceiter.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\ipv6.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\iterated_hash.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\keyboard.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\lang.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\lex.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\lfsr.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\lib.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\list.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\log.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\magic.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\md5.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\mem.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\msgcat.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\msioctl.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\mutex.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\mutexblock.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\net.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\netaddr.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\netscope.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\netdb.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\ntpaths.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\offset.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\once.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\ondestroy.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\parseint.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\portset.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\os.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\platform.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\print.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\quota.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\radix.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\random.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\ratelimiter.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\refcount.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\region.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\resource.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\result.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\resultclass.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\rwlock.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\serial.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\sha1.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\sha2.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\sockaddr.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\socket.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\stats.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\stdio.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\strerror.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\stdtime.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\string.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\symtab.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\syslog.h
# End Source File
# Begin Source File

SOURCE=.\syslog.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\task.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\taskpool.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\thread.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\time.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\timer.h
# End Source File
# Begin Source File

SOURCE=.\include\isc\win32os.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\types.h
# End Source File
# Begin Source File

SOURCE=.\unistd.h
# End Source File
# Begin Source File

SOURCE=..\include\isc\util.h
# End Source File
# Begin Source File

SOURCE=..\..\..\versions.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# Begin Group "Main Isc Lib"

# PROP Default_Filter "c"
# Begin Source File

SOURCE=..\assertions.c
# End Source File
# Begin Source File

SOURCE=..\backtrace.c
# End Source File
# Begin Source File

SOURCE=..\backtrace-emptytbl.c
# End Source File
# Begin Source File

SOURCE=..\base32.c
# End Source File
# Begin Source File

SOURCE=..\base64.c
# End Source File
# Begin Source File

SOURCE=..\bitstring.c
# End Source File
# Begin Source File

SOURCE=..\buffer.c
# End Source File
# Begin Source File

SOURCE=..\bufferlist.c
# End Source File
# Begin Source File

SOURCE=..\commandline.c
# End Source File
# Begin Source File

SOURCE=..\error.c
# End Source File
# Begin Source File

SOURCE=..\event.c
# End Source File
# Begin Source File

SOURCE=..\hash.c
# End Source File
# Begin Source File

SOURCE=..\heap.c
# End Source File
# Begin Source File

SOURCE=..\hex.c
# End Source File
# Begin Source File

SOURCE=..\hmacmd5.c
# End Source File
# Begin Source File

SOURCE=..\hmacsha.c
# End Source File
# Begin Source File

SOURCE=..\httpd.c
# End Source File
# Begin Source File

SOURCE=..\inet_aton.c
# End Source File
# Begin Source File

SOURCE=..\inet_ntop.c
# End Source File
# Begin Source File

SOURCE=..\inet_pton.c
# End Source File
# Begin Source File

SOURCE=..\lex.c
# End Source File
# Begin Source File

SOURCE=..\lfsr.c
# End Source File
# Begin Source File

SOURCE=..\lib.c
# End Source File
# Begin Source File

SOURCE=..\log.c
# End Source File
# Begin Source File

SOURCE=..\md5.c
# End Source File
# Begin Source File

SOURCE=..\mem.c
# End Source File
# Begin Source File

SOURCE=..\nls\msgcat.c
# End Source File
# Begin Source File

SOURCE=..\mutexblock.c
# End Source File
# Begin Source File

SOURCE=..\netaddr.c
# End Source File
# Begin Source File

SOURCE=..\netscope.c
# End Source File
# Begin Source File

SOURCE=..\ondestroy.c
# End Source File
# Begin Source File

SOURCE=..\parseint.c
# End Source File
# Begin Source File

SOURCE=..\portset.c
# End Source File
# Begin Source File

SOURCE=..\quota.c
# End Source File
# Begin Source File

SOURCE=..\radix.c
# End Source File
# Begin Source File

SOURCE=..\random.c
# End Source File
# Begin Source File

SOURCE=..\ratelimiter.c
# End Source File
# Begin Source File

SOURCE=..\refcount.c
# End Source File
# Begin Source File

SOURCE=..\region.c
# End Source File
# Begin Source File

SOURCE=..\result.c
# End Source File
# Begin Source File

SOURCE=..\rwlock.c
# End Source File
# Begin Source File

SOURCE=..\serial.c
# End Source File
# Begin Source File

SOURCE=..\sha1.c
# End Source File
# Begin Source File

SOURCE=..\sha2.c
# End Source File
# Begin Source File

SOURCE=..\sockaddr.c
# End Source File
# Begin Source File

SOURCE=..\stats.c
# End Source File
# Begin Source File

SOURCE=..\string.c
# End Source File
# Begin Source File

SOURCE=..\symtab.c
# End Source File
# Begin Source File

SOURCE=..\task.c
# End Source File
# Begin Source File

SOURCE=..\taskpool.c
# End Source File
# Begin Source File

SOURCE=..\timer.c
# End Source File
# Begin Source File

SOURCE=.\win32os.c
# End Source File
# End Group
# Begin Source File

SOURCE=..\noatomic\include\atomic.h
# End Source File
# Begin Source File

SOURCE=.\libisc.def
# End Source File
# End Target
# End Project
