# Microsoft Developer Studio Project File - Name="apr" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Static Library" 0x0104

CFG=apr - Win32 Release
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "apr.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "apr.mak" CFG="apr - Win32 Release"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "apr - Win32 Release" (based on "Win32 (x86) Static Library")
!MESSAGE "apr - Win32 Debug" (based on "Win32 (x86) Static Library")
!MESSAGE "apr - Win32 Release9x" (based on "Win32 (x86) Static Library")
!MESSAGE "apr - Win32 Debug9x" (based on "Win32 (x86) Static Library")
!MESSAGE "apr - x64 Release" (based on "Win32 (x86) Static Library")
!MESSAGE "apr - x64 Debug" (based on "Win32 (x86) Static Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "apr - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "LibR"
# PROP BASE Intermediate_Dir "LibR"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "LibR"
# PROP Intermediate_Dir "LibR"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MD /W3 /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /FD /c
# ADD CPP /nologo /MD /W3 /Zi /O2 /Oy- /I "./include" /I "./include/arch" /I "./include/arch/win32" /I "./include/arch/unix" /D "NDEBUG" /D "APR_DECLARE_STATIC" /D "WIN32" /D "WINNT" /D "_WINDOWS" /Fo"$(INTDIR)\" /Fd"$(OUTDIR)\apr-1" /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo /out:"LibR\apr-1.lib"

!ELSEIF  "$(CFG)" == "apr - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "LibD"
# PROP BASE Intermediate_Dir "LibD"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "LibD"
# PROP Intermediate_Dir "LibD"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MDd /W3 /Zi /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /FD /EHsc /c
# ADD CPP /nologo /MDd /W3 /Zi /Od /I "./include" /I "./include/arch" /I "./include/arch/win32" /I "./include/arch/unix" /D "_DEBUG" /D "APR_DECLARE_STATIC" /D "WIN32" /D "WINNT" /D "_WINDOWS" /Fo"$(INTDIR)\" /Fd"$(OUTDIR)\apr-1" /FD /EHsc /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo /out:"LibD\apr-1.lib"

!ELSEIF  "$(CFG)" == "apr - Win32 Release9x"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "9x\LibR"
# PROP BASE Intermediate_Dir "9x\LibR"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "9x\LibR"
# PROP Intermediate_Dir "9x\LibR"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MD /W3 /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /FD /c
# ADD CPP /nologo /MD /W3 /Zi /O2 /Oy- /I "./include" /I "./include/arch" /I "./include/arch/win32" /I "./include/arch/unix" /D "NDEBUG" /D "APR_DECLARE_STATIC" /D "WIN32" /D "_WINDOWS" /Fo"$(INTDIR)\" /Fd"$(OUTDIR)\apr-1" /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo /out:"9x\LibR\apr-1.lib"

!ELSEIF  "$(CFG)" == "apr - Win32 Debug9x"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "9x\LibD"
# PROP BASE Intermediate_Dir "9x\LibD"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "9x\LibD"
# PROP Intermediate_Dir "9x\LibD"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MDd /W3 /Zi /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /FD /EHsc /c
# ADD CPP /nologo /MDd /W3 /Zi /Od /I "./include" /I "./include/arch" /I "./include/arch/win32" /I "./include/arch/unix" /D "_DEBUG" /D "APR_DECLARE_STATIC" /D "WIN32" /D "_WINDOWS" /Fo"$(INTDIR)\" /Fd"$(OUTDIR)\apr-1" /FD /EHsc /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo /out:"9x\LibD\apr-1.lib"

!ELSEIF  "$(CFG)" == "apr - x64 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "x64\LibR"
# PROP BASE Intermediate_Dir "x64\LibR"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "x64\LibR"
# PROP Intermediate_Dir "x64\LibR"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MD /W3 /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /FD /c
# ADD CPP /nologo /MD /W3 /Zi /O2 /Oy- /I "./include" /I "./include/arch" /I "./include/arch/win32" /I "./include/arch/unix" /D "NDEBUG" /D "APR_DECLARE_STATIC" /D "WIN32" /D "WINNT" /D "_WINDOWS" /Fo"$(INTDIR)\" /Fd"$(OUTDIR)\apr-1" /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo /out:"x64\LibR\apr-1.lib"

!ELSEIF  "$(CFG)" == "apr - x64 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "x64\LibD"
# PROP BASE Intermediate_Dir "x64\LibD"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "x64\LibD"
# PROP Intermediate_Dir "x64\LibD"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MDd /W3 /Zi /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /FD /EHsc /c
# ADD CPP /nologo /MDd /W3 /Zi /Od /I "./include" /I "./include/arch" /I "./include/arch/win32" /I "./include/arch/unix" /D "_DEBUG" /D "APR_DECLARE_STATIC" /D "WIN32" /D "WINNT" /D "_WINDOWS" /Fo"$(INTDIR)\" /Fd"$(OUTDIR)\apr-1" /FD /EHsc /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo /out:"x64\LibD\apr-1.lib"

!ENDIF 

# Begin Target

# Name "apr - Win32 Release"
# Name "apr - Win32 Debug"
# Name "apr - Win32 Release9x"
# Name "apr - Win32 Debug9x"
# Name "apr - x64 Release"
# Name "apr - x64 Debug"
# Begin Group "Source Files"

# PROP Default_Filter ".c"
# Begin Group "atomic"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\atomic\win32\apr_atomic.c
# End Source File
# End Group
# Begin Group "dso"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\dso\win32\dso.c
# End Source File
# End Group
# Begin Group "encoding"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\encoding\apr_escape.c

# End Source File
# End Group
# Begin Group "file_io"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\file_io\win32\buffer.c
# End Source File
# Begin Source File

SOURCE=.\file_io\unix\copy.c
# End Source File
# Begin Source File

SOURCE=.\file_io\win32\dir.c
# End Source File
# Begin Source File

SOURCE=.\file_io\unix\fileacc.c
# End Source File
# Begin Source File

SOURCE=.\file_io\win32\filedup.c
# End Source File
# Begin Source File

SOURCE=.\file_io\win32\filepath.c
# End Source File
# Begin Source File

SOURCE=.\file_io\unix\filepath_util.c
# End Source File
# Begin Source File

SOURCE=.\file_io\win32\filestat.c
# End Source File
# Begin Source File

SOURCE=.\file_io\win32\filesys.c
# End Source File
# Begin Source File

SOURCE=.\file_io\win32\flock.c
# End Source File
# Begin Source File

SOURCE=.\file_io\unix\fullrw.c
# End Source File
# Begin Source File

SOURCE=.\file_io\unix\mktemp.c
# End Source File
# Begin Source File

SOURCE=.\file_io\win32\open.c
# End Source File
# Begin Source File

SOURCE=.\file_io\win32\pipe.c
# End Source File
# Begin Source File

SOURCE=.\file_io\win32\readwrite.c
# End Source File
# Begin Source File

SOURCE=.\file_io\win32\seek.c
# End Source File
# Begin Source File

SOURCE=.\file_io\unix\tempdir.c
# End Source File
# End Group
# Begin Group "locks"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\locks\win32\proc_mutex.c
# End Source File
# Begin Source File

SOURCE=.\locks\win32\thread_cond.c
# End Source File
# Begin Source File

SOURCE=.\locks\win32\thread_mutex.c
# End Source File
# Begin Source File

SOURCE=.\locks\win32\thread_rwlock.c
# End Source File
# End Group
# Begin Group "memory"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\memory\unix\apr_pools.c
# End Source File
# End Group
# Begin Group "misc"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\misc\win32\apr_app.c
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\misc\win32\charset.c
# End Source File
# Begin Source File

SOURCE=.\misc\win32\env.c
# End Source File
# Begin Source File

SOURCE=.\misc\unix\errorcodes.c
# End Source File
# Begin Source File

SOURCE=.\misc\unix\getopt.c
# End Source File
# Begin Source File

SOURCE=.\misc\win32\internal.c
# End Source File
# Begin Source File

SOURCE=.\misc\win32\misc.c
# End Source File
# Begin Source File

SOURCE=.\misc\unix\otherchild.c
# End Source File
# Begin Source File

SOURCE=.\misc\win32\rand.c
# End Source File
# Begin Source File

SOURCE=.\misc\win32\start.c
# End Source File
# Begin Source File

SOURCE=.\misc\win32\utf8.c
# End Source File
# Begin Source File

SOURCE=.\misc\unix\version.c
# End Source File
# End Group
# Begin Group "mmap"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\mmap\unix\common.c
# End Source File
# Begin Source File

SOURCE=.\mmap\win32\mmap.c
# End Source File
# End Group
# Begin Group "network_io"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\network_io\unix\inet_ntop.c
# End Source File
# Begin Source File

SOURCE=.\network_io\unix\inet_pton.c
# End Source File
# Begin Source File

SOURCE=.\network_io\unix\multicast.c
# End Source File
# Begin Source File

SOURCE=.\network_io\win32\sendrecv.c
# End Source File
# Begin Source File

SOURCE=.\network_io\unix\sockaddr.c
# End Source File
# Begin Source File

SOURCE=.\network_io\win32\sockets.c
# End Source File
# Begin Source File

SOURCE=.\network_io\unix\socket_util.c
# End Source File
# Begin Source File

SOURCE=.\network_io\win32\sockopt.c
# End Source File
# End Group
# Begin Group "passwd"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\passwd\apr_getpass.c
# End Source File
# End Group
# Begin Group "poll"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\poll\unix\poll.c
# End Source File
# Begin Source File

SOURCE=.\poll\unix\pollcb.c
# End Source File
# Begin Source File

SOURCE=.\poll\unix\pollset.c
# End Source File
# Begin Source File

SOURCE=.\poll\unix\select.c
# End Source File
# End Group
# Begin Group "random"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\random\unix\apr_random.c
# End Source File
# Begin Source File

SOURCE=.\random\unix\sha2.c
# End Source File
# Begin Source File

SOURCE=.\random\unix\sha2_glue.c
# End Source File
# End Group
# Begin Group "shmem"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\shmem\win32\shm.c
# End Source File
# End Group
# Begin Group "strings"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\strings\apr_cpystrn.c
# End Source File
# Begin Source File

SOURCE=.\strings\apr_fnmatch.c
# End Source File
# Begin Source File

SOURCE=.\strings\apr_snprintf.c
# End Source File
# Begin Source File

SOURCE=.\strings\apr_strings.c
# End Source File
# Begin Source File

SOURCE=.\strings\apr_strnatcmp.c
# End Source File
# Begin Source File

SOURCE=.\strings\apr_strtok.c
# End Source File
# End Group
# Begin Group "tables"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\tables\apr_hash.c
# End Source File
# Begin Source File

SOURCE=.\tables\apr_skiplist.c
# End Source File
# Begin Source File

SOURCE=.\tables\apr_tables.c
# End Source File
# End Group
# Begin Group "threadproc"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\threadproc\win32\proc.c
# End Source File
# Begin Source File

SOURCE=.\threadproc\win32\signals.c
# End Source File
# Begin Source File

SOURCE=.\threadproc\win32\thread.c
# End Source File
# Begin Source File

SOURCE=.\threadproc\win32\threadpriv.c
# End Source File
# End Group
# Begin Group "time"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\time\win32\time.c
# End Source File
# Begin Source File

SOURCE=.\time\win32\timestr.c
# End Source File
# End Group
# Begin Group "user"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\user\win32\groupinfo.c
# End Source File
# Begin Source File

SOURCE=.\user\win32\userinfo.c
# End Source File
# End Group
# End Group
# Begin Group "Private Header Files"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\include\arch\win32\apr_arch_atime.h
# End Source File
# Begin Source File

SOURCE=.\include\arch\win32\apr_arch_dso.h
# End Source File
# Begin Source File

SOURCE=.\include\arch\win32\apr_arch_file_io.h
# End Source File
# Begin Source File

SOURCE=.\include\arch\win32\apr_arch_inherit.h
# End Source File
# Begin Source File

SOURCE=.\include\arch\win32\apr_arch_misc.h
# End Source File
# Begin Source File

SOURCE=.\include\arch\win32\apr_arch_networkio.h
# End Source File
# Begin Source File

SOURCE=.\include\arch\win32\apr_arch_thread_mutex.h
# End Source File
# Begin Source File

SOURCE=.\include\arch\win32\apr_arch_thread_rwlock.h
# End Source File
# Begin Source File

SOURCE=.\include\arch\win32\apr_arch_threadproc.h
# End Source File
# Begin Source File

SOURCE=.\include\arch\win32\apr_arch_utf8.h
# End Source File
# Begin Source File

SOURCE=.\include\arch\win32\apr_private.h
# End Source File
# Begin Source File

SOURCE=.\include\arch\apr_private_common.h
# End Source File
# End Group
# Begin Group "Public Header Files"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\include\apr.h.in
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\include\apr.hnw
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\include\apr.hw

!IF  "$(CFG)" == "apr - Win32 Release"

# Begin Custom Build - Creating apr.h from apr.hw
InputPath=.\include\apr.hw

".\include\apr.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	type .\include\apr.hw > .\include\apr.h

# End Custom Build

!ELSEIF  "$(CFG)" == "apr - Win32 Debug"

# Begin Custom Build - Creating apr.h from apr.hw
InputPath=.\include\apr.hw

".\include\apr.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	type .\include\apr.hw > .\include\apr.h

# End Custom Build

!ELSEIF  "$(CFG)" == "apr - Win32 Release9x"

# Begin Custom Build - Creating apr.h from apr.hw
InputPath=.\include\apr.hw

".\include\apr.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	type .\include\apr.hw > .\include\apr.h

# End Custom Build

!ELSEIF  "$(CFG)" == "apr - Win32 Debug9x"

# Begin Custom Build - Creating apr.h from apr.hw
InputPath=.\include\apr.hw

".\include\apr.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	type .\include\apr.hw > .\include\apr.h

# End Custom Build

!ELSEIF  "$(CFG)" == "apr - x64 Release"

# Begin Custom Build - Creating apr.h from apr.hw
InputPath=.\include\apr.hw

".\include\apr.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	type .\include\apr.hw > .\include\apr.h

# End Custom Build

!ELSEIF  "$(CFG)" == "apr - x64 Debug"

# Begin Custom Build - Creating apr.h from apr.hw
InputPath=.\include\apr.hw

".\include\apr.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	type .\include\apr.hw > .\include\apr.h

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\include\apr_allocator.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_atomic.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_dso.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_env.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_errno.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_escape.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_escape.h

!IF  "$(CFG)" == "apr - Win32 Release"

# Begin Custom Build - Creating gen_test_char.exe and apr_escape_test_char.h
InputPath=.\include\apr_escape.h

".\include\apr_escape_test_char.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	cl.exe /nologo /W3 /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /FD /I ".\include" /Fo.\LibR\gen_test_char /Fe.\LibR\gen_test_char.exe .\tools\gen_test_char.c 
	.\LibR\gen_test_char.exe > .\include\apr_escape_test_char.h

# End Custom Build

!ELSEIF  "$(CFG)" == "apr - Win32 Debug"

# Begin Custom Build - Creating gen_test_char.exe and apr_escape_test_char.h
InputPath=.\include\apr_escape.h

".\include\apr_escape_test_char.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	cl.exe /nologo /W3 /EHsc /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /FD /I ".\include" /Fo.\LibD\gen_test_char /Fe.\LibD\gen_test_char.exe .\tools\gen_test_char.c  
	.\LibD\gen_test_char.exe > .\include\apr_escape_test_char.h

# End Custom Build

!ELSEIF  "$(CFG)" == "apr - Win32 Release9x"

# Begin Custom Build - Creating gen_test_char.exe and apr_escape_test_char.h
InputPath=.\include\apr_escape.h

".\include\apr_escape_test_char.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	cl.exe /nologo /W3 /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /FD /I ".\include" /Fo.\9x\LibR\gen_test_char /Fe.\9x\LibR\gen_test_char.exe .\tools\gen_test_char.c 
	.\9x\LibR\gen_test_char.exe > .\include\apr_escape_test_char.h

# End Custom Build

!ELSEIF  "$(CFG)" == "apr - Win32 Debug9x"

# Begin Custom Build - Creating gen_test_char.exe and apr_escape_test_char.h
InputPath=.\include\apr_escape.h

InputPath=.\include\apr_escape.h
".\include\apr_escape_test_char.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	cl.exe /nologo /W3 /EHsc /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /FD /I ".\include" /Fo.\9x\LibD\gen_test_char /Fe.\9x\LibD\gen_test_char.exe .\tools\gen_test_char.c  
	.\9x\LibD\gen_test_char.exe > .\include\apr_escape_test_char.h

# End Custom Build

!ELSEIF  "$(CFG)" == "apr - x64 Release"

# Begin Custom Build - Creating gen_test_char.exe and apr_escape_test_char.h
InputPath=.\include\apr_escape.h

InputPath=.\include\apr_escape.h
".\include\apr_escape_test_char.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	cl.exe /nologo /W3 /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /FD /I ".\include" /Fo.\x64\LibR\gen_test_char /Fe.\x64\LibR\gen_test_char.exe .\tools\gen_test_char.c 
	.\x64\LibR\gen_test_char.exe > .\include\apr_escape_test_char.h

# End Custom Build

!ELSEIF  "$(CFG)" == "apr - x64 Debug"

# Begin Custom Build - Creating gen_test_char.exe and apr_escape_test_char.h
InputPath=.\include\apr_escape.h

".\include\apr_escape_test_char.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	cl.exe /nologo /W3 /EHsc /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /FD /I ".\include" /Fo.\x64\LibD\gen_test_char /Fe.\x64\LibD\gen_test_char.exe .\tools\gen_test_char.c 
	.\x64\LibD\gen_test_char.exe > .\include\apr_escape_test_char.h

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\include\apr_file_info.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_file_io.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_fnmatch.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_general.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_getopt.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_global_mutex.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_hash.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_inherit.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_lib.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_mmap.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_network_io.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_poll.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_pools.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_portable.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_proc_mutex.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_random.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_ring.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_shm.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_signal.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_skiplist.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_strings.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_support.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_tables.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_thread_cond.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_thread_mutex.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_thread_proc.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_thread_rwlock.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_time.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_user.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_version.h
# End Source File
# Begin Source File

SOURCE=.\include\apr_want.h
# End Source File
# End Group
# End Target
# End Project
