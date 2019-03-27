# Microsoft Developer Studio Generated NMAKE File, Based on libapr.dsp
!IF "$(CFG)" == ""
CFG=libapr - Win32 Release
!MESSAGE No configuration specified. Defaulting to libapr - Win32 Release.
!ENDIF 

!IF "$(CFG)" != "libapr - Win32 Release" && "$(CFG)" != "libapr - Win32 Debug" && "$(CFG)" != "libapr - Win32 Release9x" && "$(CFG)" != "libapr - Win32 Debug9x" && "$(CFG)" != "libapr - x64 Release" && "$(CFG)" != "libapr - x64 Debug"
!MESSAGE Invalid configuration "$(CFG)" specified.
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libapr.mak" CFG="libapr - Win32 Release"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libapr - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libapr - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libapr - Win32 Release9x" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libapr - Win32 Debug9x" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libapr - x64 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libapr - x64 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 
!ERROR An invalid configuration is specified.
!ENDIF 

!IF "$(OS)" == "Windows_NT"
NULL=
!ELSE 
NULL=nul
!ENDIF 

!IF  "$(CFG)" == "libapr - Win32 Release"

OUTDIR=.\Release
INTDIR=.\Release
DS_POSTBUILD_DEP=$(INTDIR)\postbld.dep
# Begin Custom Macros
OutDir=.\Release
# End Custom Macros

ALL : ".\Release\gen_test_char.exe" "$(OUTDIR)\libapr-1.dll" "$(DS_POSTBUILD_DEP)"


CLEAN :
	-@erase "$(INTDIR)\apr_atomic.obj"
	-@erase "$(INTDIR)\apr_cpystrn.obj"
	-@erase "$(INTDIR)\apr_escape.obj"
	-@erase "$(INTDIR)\apr_fnmatch.obj"
	-@erase "$(INTDIR)\apr_getpass.obj"
	-@erase "$(INTDIR)\apr_hash.obj"
	-@erase "$(INTDIR)\apr_pools.obj"
	-@erase "$(INTDIR)\apr_random.obj"
	-@erase "$(INTDIR)\apr_skiplist.obj"
	-@erase "$(INTDIR)\apr_snprintf.obj"
	-@erase "$(INTDIR)\apr_strings.obj"
	-@erase "$(INTDIR)\apr_strnatcmp.obj"
	-@erase "$(INTDIR)\apr_strtok.obj"
	-@erase "$(INTDIR)\apr_tables.obj"
	-@erase "$(INTDIR)\buffer.obj"
	-@erase "$(INTDIR)\charset.obj"
	-@erase "$(INTDIR)\common.obj"
	-@erase "$(INTDIR)\copy.obj"
	-@erase "$(INTDIR)\dir.obj"
	-@erase "$(INTDIR)\dso.obj"
	-@erase "$(INTDIR)\env.obj"
	-@erase "$(INTDIR)\errorcodes.obj"
	-@erase "$(INTDIR)\fileacc.obj"
	-@erase "$(INTDIR)\filedup.obj"
	-@erase "$(INTDIR)\filepath.obj"
	-@erase "$(INTDIR)\filepath_util.obj"
	-@erase "$(INTDIR)\filestat.obj"
	-@erase "$(INTDIR)\filesys.obj"
	-@erase "$(INTDIR)\flock.obj"
	-@erase "$(INTDIR)\fullrw.obj"
	-@erase "$(INTDIR)\getopt.obj"
	-@erase "$(INTDIR)\groupinfo.obj"
	-@erase "$(INTDIR)\inet_ntop.obj"
	-@erase "$(INTDIR)\inet_pton.obj"
	-@erase "$(INTDIR)\internal.obj"
	-@erase "$(INTDIR)\libapr.res"
	-@erase "$(INTDIR)\libapr_src.idb"
	-@erase "$(INTDIR)\libapr_src.pdb"
	-@erase "$(INTDIR)\misc.obj"
	-@erase "$(INTDIR)\mktemp.obj"
	-@erase "$(INTDIR)\mmap.obj"
	-@erase "$(INTDIR)\multicast.obj"
	-@erase "$(INTDIR)\open.obj"
	-@erase "$(INTDIR)\otherchild.obj"
	-@erase "$(INTDIR)\pipe.obj"
	-@erase "$(INTDIR)\poll.obj"
	-@erase "$(INTDIR)\pollcb.obj"
	-@erase "$(INTDIR)\pollset.obj"
	-@erase "$(INTDIR)\proc.obj"
	-@erase "$(INTDIR)\proc_mutex.obj"
	-@erase "$(INTDIR)\rand.obj"
	-@erase "$(INTDIR)\readwrite.obj"
	-@erase "$(INTDIR)\seek.obj"
	-@erase "$(INTDIR)\select.obj"
	-@erase "$(INTDIR)\sendrecv.obj"
	-@erase "$(INTDIR)\sha2.obj"
	-@erase "$(INTDIR)\sha2_glue.obj"
	-@erase "$(INTDIR)\shm.obj"
	-@erase "$(INTDIR)\signals.obj"
	-@erase "$(INTDIR)\sockaddr.obj"
	-@erase "$(INTDIR)\socket_util.obj"
	-@erase "$(INTDIR)\sockets.obj"
	-@erase "$(INTDIR)\sockopt.obj"
	-@erase "$(INTDIR)\start.obj"
	-@erase "$(INTDIR)\tempdir.obj"
	-@erase "$(INTDIR)\thread.obj"
	-@erase "$(INTDIR)\thread_cond.obj"
	-@erase "$(INTDIR)\thread_mutex.obj"
	-@erase "$(INTDIR)\thread_rwlock.obj"
	-@erase "$(INTDIR)\threadpriv.obj"
	-@erase "$(INTDIR)\time.obj"
	-@erase "$(INTDIR)\timestr.obj"
	-@erase "$(INTDIR)\userinfo.obj"
	-@erase "$(INTDIR)\utf8.obj"
	-@erase "$(INTDIR)\version.obj"
	-@erase "$(OUTDIR)\libapr-1.dll"
	-@erase "$(OUTDIR)\libapr-1.exp"
	-@erase "$(OUTDIR)\libapr-1.lib"
	-@erase "$(OUTDIR)\libapr-1.pdb"
	-@erase ".\include\apr.h"
	-@erase ".\include\apr_gen_test_char.h"
	-@erase "$(OUTDIR)\gen_test_char.exe"
	-@erase "$(OUTDIR)\gen_test_char.obj"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

CPP=cl.exe
CPP_PROJ=/nologo /MD /W3 /Zi /O2 /Oy- /I "./include" /I "./include/arch" /I "./include/arch/win32" /I "./include/arch/unix" /D "NDEBUG" /D "APR_DECLARE_EXPORT" /D "WIN32" /D "WINNT" /D "_WINDOWS" /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\libapr_src" /FD /c 

.c{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.c{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

MTL=midl.exe
MTL_PROJ=/nologo /D "NDEBUG" /mktyplib203 /o /win32 "NUL" 
RSC=rc.exe
RSC_PROJ=/l 0x409 /fo"$(INTDIR)\libapr.res" /i "./include" /d "NDEBUG" /d "APR_VERSION_ONLY" 
BSC32=bscmake.exe
BSC32_FLAGS=/nologo /o"$(OUTDIR)\libapr.bsc" 
BSC32_SBRS= \
	
LINK32=link.exe
LINK32_FLAGS=kernel32.lib advapi32.lib ws2_32.lib mswsock.lib ole32.lib shell32.lib rpcrt4.lib /nologo /base:"0x6EEC0000" /subsystem:windows /dll /incremental:no /pdb:"$(OUTDIR)\libapr-1.pdb" /debug /out:"$(OUTDIR)\libapr-1.dll" /implib:"$(OUTDIR)\libapr-1.lib" /MACHINE:X86 /opt:ref 
LINK32_OBJS= \
	"$(INTDIR)\apr_atomic.obj" \
	"$(INTDIR)\dso.obj" \
	"$(INTDIR)\apr_escape.obj" \
	"$(INTDIR)\buffer.obj" \
	"$(INTDIR)\copy.obj" \
	"$(INTDIR)\dir.obj" \
	"$(INTDIR)\fileacc.obj" \
	"$(INTDIR)\filedup.obj" \
	"$(INTDIR)\filepath.obj" \
	"$(INTDIR)\filepath_util.obj" \
	"$(INTDIR)\filestat.obj" \
	"$(INTDIR)\filesys.obj" \
	"$(INTDIR)\flock.obj" \
	"$(INTDIR)\fullrw.obj" \
	"$(INTDIR)\mktemp.obj" \
	"$(INTDIR)\open.obj" \
	"$(INTDIR)\pipe.obj" \
	"$(INTDIR)\readwrite.obj" \
	"$(INTDIR)\seek.obj" \
	"$(INTDIR)\tempdir.obj" \
	"$(INTDIR)\proc_mutex.obj" \
	"$(INTDIR)\thread_cond.obj" \
	"$(INTDIR)\thread_mutex.obj" \
	"$(INTDIR)\thread_rwlock.obj" \
	"$(INTDIR)\apr_pools.obj" \
	"$(INTDIR)\charset.obj" \
	"$(INTDIR)\env.obj" \
	"$(INTDIR)\errorcodes.obj" \
	"$(INTDIR)\getopt.obj" \
	"$(INTDIR)\internal.obj" \
	"$(INTDIR)\misc.obj" \
	"$(INTDIR)\otherchild.obj" \
	"$(INTDIR)\rand.obj" \
	"$(INTDIR)\start.obj" \
	"$(INTDIR)\utf8.obj" \
	"$(INTDIR)\version.obj" \
	"$(INTDIR)\common.obj" \
	"$(INTDIR)\mmap.obj" \
	"$(INTDIR)\inet_ntop.obj" \
	"$(INTDIR)\inet_pton.obj" \
	"$(INTDIR)\multicast.obj" \
	"$(INTDIR)\sendrecv.obj" \
	"$(INTDIR)\sockaddr.obj" \
	"$(INTDIR)\sockets.obj" \
	"$(INTDIR)\socket_util.obj" \
	"$(INTDIR)\sockopt.obj" \
	"$(INTDIR)\apr_getpass.obj" \
	"$(INTDIR)\poll.obj" \
	"$(INTDIR)\pollcb.obj" \
	"$(INTDIR)\pollset.obj" \
	"$(INTDIR)\select.obj" \
	"$(INTDIR)\apr_random.obj" \
	"$(INTDIR)\sha2.obj" \
	"$(INTDIR)\sha2_glue.obj" \
	"$(INTDIR)\shm.obj" \
	"$(INTDIR)\apr_cpystrn.obj" \
	"$(INTDIR)\apr_fnmatch.obj" \
	"$(INTDIR)\apr_snprintf.obj" \
	"$(INTDIR)\apr_strings.obj" \
	"$(INTDIR)\apr_strnatcmp.obj" \
	"$(INTDIR)\apr_strtok.obj" \
	"$(INTDIR)\apr_hash.obj" \
	"$(INTDIR)\apr_tables.obj" \
	"$(INTDIR)\apr_skiplist.obj" \
	"$(INTDIR)\proc.obj" \
	"$(INTDIR)\signals.obj" \
	"$(INTDIR)\thread.obj" \
	"$(INTDIR)\threadpriv.obj" \
	"$(INTDIR)\time.obj" \
	"$(INTDIR)\timestr.obj" \
	"$(INTDIR)\groupinfo.obj" \
	"$(INTDIR)\userinfo.obj" \
	"$(INTDIR)\libapr.res"

"$(OUTDIR)\libapr-1.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

TargetPath=.\Release\libapr-1.dll
SOURCE="$(InputPath)"
PostBuild_Desc=Embed .manifest
DS_POSTBUILD_DEP=$(INTDIR)\postbld.dep

# Begin Custom Macros
OutDir=.\Release
# End Custom Macros

"$(DS_POSTBUILD_DEP)" : "$(OUTDIR)\libapr-1.dll"
   if exist .\Release\libapr-1.dll.manifest mt.exe -manifest .\Release\libapr-1.dll.manifest -outputresource:.\Release\libapr-1.dll;2
	echo Helper for Post-build step > "$(DS_POSTBUILD_DEP)"

!ELSEIF  "$(CFG)" == "libapr - Win32 Debug"

OUTDIR=.\Debug
INTDIR=.\Debug
DS_POSTBUILD_DEP=$(INTDIR)\postbld.dep
# Begin Custom Macros
OutDir=.\Debug
# End Custom Macros

ALL : ".\include\apr_escape_test_char.h" ".\Debug\gen_test_char.exe" "$(OUTDIR)\libapr-1.dll" "$(DS_POSTBUILD_DEP)"


CLEAN :
	-@erase "$(INTDIR)\apr_atomic.obj"
	-@erase "$(INTDIR)\apr_cpystrn.obj"
	-@erase "$(INTDIR)\apr_escape.obj"
	-@erase "$(INTDIR)\apr_fnmatch.obj"
	-@erase "$(INTDIR)\apr_getpass.obj"
	-@erase "$(INTDIR)\apr_hash.obj"
	-@erase "$(INTDIR)\apr_pools.obj"
	-@erase "$(INTDIR)\apr_random.obj"
	-@erase "$(INTDIR)\apr_skiplist.obj"
	-@erase "$(INTDIR)\apr_snprintf.obj"
	-@erase "$(INTDIR)\apr_strings.obj"
	-@erase "$(INTDIR)\apr_strnatcmp.obj"
	-@erase "$(INTDIR)\apr_strtok.obj"
	-@erase "$(INTDIR)\apr_tables.obj"
	-@erase "$(INTDIR)\buffer.obj"
	-@erase "$(INTDIR)\charset.obj"
	-@erase "$(INTDIR)\common.obj"
	-@erase "$(INTDIR)\copy.obj"
	-@erase "$(INTDIR)\dir.obj"
	-@erase "$(INTDIR)\dso.obj"
	-@erase "$(INTDIR)\env.obj"
	-@erase "$(INTDIR)\errorcodes.obj"
	-@erase "$(INTDIR)\fileacc.obj"
	-@erase "$(INTDIR)\filedup.obj"
	-@erase "$(INTDIR)\filepath.obj"
	-@erase "$(INTDIR)\filepath_util.obj"
	-@erase "$(INTDIR)\filestat.obj"
	-@erase "$(INTDIR)\filesys.obj"
	-@erase "$(INTDIR)\flock.obj"
	-@erase "$(INTDIR)\fullrw.obj"
	-@erase "$(INTDIR)\getopt.obj"
	-@erase "$(INTDIR)\groupinfo.obj"
	-@erase "$(INTDIR)\inet_ntop.obj"
	-@erase "$(INTDIR)\inet_pton.obj"
	-@erase "$(INTDIR)\internal.obj"
	-@erase "$(INTDIR)\libapr.res"
	-@erase "$(INTDIR)\libapr_src.idb"
	-@erase "$(INTDIR)\libapr_src.pdb"
	-@erase "$(INTDIR)\misc.obj"
	-@erase "$(INTDIR)\mktemp.obj"
	-@erase "$(INTDIR)\mmap.obj"
	-@erase "$(INTDIR)\multicast.obj"
	-@erase "$(INTDIR)\open.obj"
	-@erase "$(INTDIR)\otherchild.obj"
	-@erase "$(INTDIR)\pipe.obj"
	-@erase "$(INTDIR)\poll.obj"
	-@erase "$(INTDIR)\pollcb.obj"
	-@erase "$(INTDIR)\pollset.obj"
	-@erase "$(INTDIR)\proc.obj"
	-@erase "$(INTDIR)\proc_mutex.obj"
	-@erase "$(INTDIR)\rand.obj"
	-@erase "$(INTDIR)\readwrite.obj"
	-@erase "$(INTDIR)\seek.obj"
	-@erase "$(INTDIR)\select.obj"
	-@erase "$(INTDIR)\sendrecv.obj"
	-@erase "$(INTDIR)\sha2.obj"
	-@erase "$(INTDIR)\sha2_glue.obj"
	-@erase "$(INTDIR)\shm.obj"
	-@erase "$(INTDIR)\signals.obj"
	-@erase "$(INTDIR)\sockaddr.obj"
	-@erase "$(INTDIR)\socket_util.obj"
	-@erase "$(INTDIR)\sockets.obj"
	-@erase "$(INTDIR)\sockopt.obj"
	-@erase "$(INTDIR)\start.obj"
	-@erase "$(INTDIR)\tempdir.obj"
	-@erase "$(INTDIR)\thread.obj"
	-@erase "$(INTDIR)\thread_cond.obj"
	-@erase "$(INTDIR)\thread_mutex.obj"
	-@erase "$(INTDIR)\thread_rwlock.obj"
	-@erase "$(INTDIR)\threadpriv.obj"
	-@erase "$(INTDIR)\time.obj"
	-@erase "$(INTDIR)\timestr.obj"
	-@erase "$(INTDIR)\userinfo.obj"
	-@erase "$(INTDIR)\utf8.obj"
	-@erase "$(INTDIR)\version.obj"
	-@erase "$(OUTDIR)\libapr-1.dll"
	-@erase "$(OUTDIR)\libapr-1.exp"
	-@erase "$(OUTDIR)\libapr-1.lib"
	-@erase "$(OUTDIR)\libapr-1.pdb"
	-@erase ".\include\apr.h"
	-@erase ".\include\apr_gen_test_char.h"
	-@erase "$(OUTDIR)\gen_test_char.exe"
	-@erase "$(OUTDIR)\gen_test_char.obj"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

CPP=cl.exe
CPP_PROJ=/nologo /MDd /W3 /Zi /Od /I "./include" /I "./include/arch" /I "./include/arch/win32" /I "./include/arch/unix" /D "_DEBUG" /D "APR_DECLARE_EXPORT" /D "WIN32" /D "WINNT" /D "_WINDOWS" /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\libapr_src" /FD /EHsc /c 

.c{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.c{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

MTL=midl.exe
MTL_PROJ=/nologo /D "_DEBUG" /mktyplib203 /o /win32 "NUL" 
RSC=rc.exe
RSC_PROJ=/l 0x409 /fo"$(INTDIR)\libapr.res" /i "./include" /d "_DEBUG" /d "APR_VERSION_ONLY" 
BSC32=bscmake.exe
BSC32_FLAGS=/nologo /o"$(OUTDIR)\libapr.bsc" 
BSC32_SBRS= \
	
LINK32=link.exe
LINK32_FLAGS=kernel32.lib advapi32.lib ws2_32.lib mswsock.lib ole32.lib shell32.lib rpcrt4.lib /nologo /base:"0x6EEC0000" /subsystem:windows /dll /incremental:no /pdb:"$(OUTDIR)\libapr-1.pdb" /debug /out:"$(OUTDIR)\libapr-1.dll" /implib:"$(OUTDIR)\libapr-1.lib" /MACHINE:X86 
LINK32_OBJS= \
	"$(INTDIR)\apr_atomic.obj" \
	"$(INTDIR)\dso.obj" \
	"$(INTDIR)\apr_escape.obj" \
	"$(INTDIR)\buffer.obj" \
	"$(INTDIR)\copy.obj" \
	"$(INTDIR)\dir.obj" \
	"$(INTDIR)\fileacc.obj" \
	"$(INTDIR)\filedup.obj" \
	"$(INTDIR)\filepath.obj" \
	"$(INTDIR)\filepath_util.obj" \
	"$(INTDIR)\filestat.obj" \
	"$(INTDIR)\filesys.obj" \
	"$(INTDIR)\flock.obj" \
	"$(INTDIR)\fullrw.obj" \
	"$(INTDIR)\mktemp.obj" \
	"$(INTDIR)\open.obj" \
	"$(INTDIR)\pipe.obj" \
	"$(INTDIR)\readwrite.obj" \
	"$(INTDIR)\seek.obj" \
	"$(INTDIR)\tempdir.obj" \
	"$(INTDIR)\proc_mutex.obj" \
	"$(INTDIR)\thread_cond.obj" \
	"$(INTDIR)\thread_mutex.obj" \
	"$(INTDIR)\thread_rwlock.obj" \
	"$(INTDIR)\apr_pools.obj" \
	"$(INTDIR)\charset.obj" \
	"$(INTDIR)\env.obj" \
	"$(INTDIR)\errorcodes.obj" \
	"$(INTDIR)\getopt.obj" \
	"$(INTDIR)\internal.obj" \
	"$(INTDIR)\misc.obj" \
	"$(INTDIR)\otherchild.obj" \
	"$(INTDIR)\rand.obj" \
	"$(INTDIR)\start.obj" \
	"$(INTDIR)\utf8.obj" \
	"$(INTDIR)\version.obj" \
	"$(INTDIR)\common.obj" \
	"$(INTDIR)\mmap.obj" \
	"$(INTDIR)\inet_ntop.obj" \
	"$(INTDIR)\inet_pton.obj" \
	"$(INTDIR)\multicast.obj" \
	"$(INTDIR)\sendrecv.obj" \
	"$(INTDIR)\sockaddr.obj" \
	"$(INTDIR)\sockets.obj" \
	"$(INTDIR)\socket_util.obj" \
	"$(INTDIR)\sockopt.obj" \
	"$(INTDIR)\apr_getpass.obj" \
	"$(INTDIR)\poll.obj" \
	"$(INTDIR)\pollcb.obj" \
	"$(INTDIR)\pollset.obj" \
	"$(INTDIR)\select.obj" \
	"$(INTDIR)\apr_random.obj" \
	"$(INTDIR)\sha2.obj" \
	"$(INTDIR)\sha2_glue.obj" \
	"$(INTDIR)\shm.obj" \
	"$(INTDIR)\apr_cpystrn.obj" \
	"$(INTDIR)\apr_fnmatch.obj" \
	"$(INTDIR)\apr_snprintf.obj" \
	"$(INTDIR)\apr_strings.obj" \
	"$(INTDIR)\apr_strnatcmp.obj" \
	"$(INTDIR)\apr_strtok.obj" \
	"$(INTDIR)\apr_hash.obj" \
	"$(INTDIR)\apr_tables.obj" \
	"$(INTDIR)\apr_skiplist.obj" \
	"$(INTDIR)\proc.obj" \
	"$(INTDIR)\signals.obj" \
	"$(INTDIR)\thread.obj" \
	"$(INTDIR)\threadpriv.obj" \
	"$(INTDIR)\time.obj" \
	"$(INTDIR)\timestr.obj" \
	"$(INTDIR)\groupinfo.obj" \
	"$(INTDIR)\userinfo.obj" \
	"$(INTDIR)\libapr.res"

"$(OUTDIR)\libapr-1.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

TargetPath=.\Debug\libapr-1.dll
SOURCE="$(InputPath)"
PostBuild_Desc=Embed .manifest
DS_POSTBUILD_DEP=$(INTDIR)\postbld.dep

# Begin Custom Macros
OutDir=.\Debug
# End Custom Macros

"$(DS_POSTBUILD_DEP)" : "$(OUTDIR)\libapr-1.dll"
   if exist .\Debug\libapr-1.dll.manifest mt.exe -manifest .\Debug\libapr-1.dll.manifest -outputresource:.\Debug\libapr-1.dll;2
	echo Helper for Post-build step > "$(DS_POSTBUILD_DEP)"

!ELSEIF  "$(CFG)" == "libapr - Win32 Release9x"

OUTDIR=.\9x\Release
INTDIR=.\9x\Release
DS_POSTBUILD_DEP=$(INTDIR)\postbld.dep
# Begin Custom Macros
OutDir=.\9x\Release
# End Custom Macros

ALL : ".\include\apr_escape_test_char.h" ".\9x\Release\gen_test_char.exe" "$(OUTDIR)\libapr-1.dll" "$(DS_POSTBUILD_DEP)"


CLEAN :
	-@erase "$(INTDIR)\apr_atomic.obj"
	-@erase "$(INTDIR)\apr_cpystrn.obj"
	-@erase "$(INTDIR)\apr_escape.obj"
	-@erase "$(INTDIR)\apr_fnmatch.obj"
	-@erase "$(INTDIR)\apr_getpass.obj"
	-@erase "$(INTDIR)\apr_hash.obj"
	-@erase "$(INTDIR)\apr_pools.obj"
	-@erase "$(INTDIR)\apr_random.obj"
	-@erase "$(INTDIR)\apr_skiplist.obj"
	-@erase "$(INTDIR)\apr_snprintf.obj"
	-@erase "$(INTDIR)\apr_strings.obj"
	-@erase "$(INTDIR)\apr_strnatcmp.obj"
	-@erase "$(INTDIR)\apr_strtok.obj"
	-@erase "$(INTDIR)\apr_tables.obj"
	-@erase "$(INTDIR)\buffer.obj"
	-@erase "$(INTDIR)\charset.obj"
	-@erase "$(INTDIR)\common.obj"
	-@erase "$(INTDIR)\copy.obj"
	-@erase "$(INTDIR)\dir.obj"
	-@erase "$(INTDIR)\dso.obj"
	-@erase "$(INTDIR)\env.obj"
	-@erase "$(INTDIR)\errorcodes.obj"
	-@erase "$(INTDIR)\fileacc.obj"
	-@erase "$(INTDIR)\filedup.obj"
	-@erase "$(INTDIR)\filepath.obj"
	-@erase "$(INTDIR)\filepath_util.obj"
	-@erase "$(INTDIR)\filestat.obj"
	-@erase "$(INTDIR)\filesys.obj"
	-@erase "$(INTDIR)\flock.obj"
	-@erase "$(INTDIR)\fullrw.obj"
	-@erase "$(INTDIR)\getopt.obj"
	-@erase "$(INTDIR)\groupinfo.obj"
	-@erase "$(INTDIR)\inet_ntop.obj"
	-@erase "$(INTDIR)\inet_pton.obj"
	-@erase "$(INTDIR)\internal.obj"
	-@erase "$(INTDIR)\libapr.res"
	-@erase "$(INTDIR)\libapr_src.idb"
	-@erase "$(INTDIR)\libapr_src.pdb"
	-@erase "$(INTDIR)\misc.obj"
	-@erase "$(INTDIR)\mktemp.obj"
	-@erase "$(INTDIR)\mmap.obj"
	-@erase "$(INTDIR)\multicast.obj"
	-@erase "$(INTDIR)\open.obj"
	-@erase "$(INTDIR)\otherchild.obj"
	-@erase "$(INTDIR)\pipe.obj"
	-@erase "$(INTDIR)\poll.obj"
	-@erase "$(INTDIR)\pollcb.obj"
	-@erase "$(INTDIR)\pollset.obj"
	-@erase "$(INTDIR)\proc.obj"
	-@erase "$(INTDIR)\proc_mutex.obj"
	-@erase "$(INTDIR)\rand.obj"
	-@erase "$(INTDIR)\readwrite.obj"
	-@erase "$(INTDIR)\seek.obj"
	-@erase "$(INTDIR)\select.obj"
	-@erase "$(INTDIR)\sendrecv.obj"
	-@erase "$(INTDIR)\sha2.obj"
	-@erase "$(INTDIR)\sha2_glue.obj"
	-@erase "$(INTDIR)\shm.obj"
	-@erase "$(INTDIR)\signals.obj"
	-@erase "$(INTDIR)\sockaddr.obj"
	-@erase "$(INTDIR)\socket_util.obj"
	-@erase "$(INTDIR)\sockets.obj"
	-@erase "$(INTDIR)\sockopt.obj"
	-@erase "$(INTDIR)\start.obj"
	-@erase "$(INTDIR)\tempdir.obj"
	-@erase "$(INTDIR)\thread.obj"
	-@erase "$(INTDIR)\thread_cond.obj"
	-@erase "$(INTDIR)\thread_mutex.obj"
	-@erase "$(INTDIR)\thread_rwlock.obj"
	-@erase "$(INTDIR)\threadpriv.obj"
	-@erase "$(INTDIR)\time.obj"
	-@erase "$(INTDIR)\timestr.obj"
	-@erase "$(INTDIR)\userinfo.obj"
	-@erase "$(INTDIR)\utf8.obj"
	-@erase "$(INTDIR)\version.obj"
	-@erase "$(OUTDIR)\libapr-1.dll"
	-@erase "$(OUTDIR)\libapr-1.exp"
	-@erase "$(OUTDIR)\libapr-1.lib"
	-@erase "$(OUTDIR)\libapr-1.pdb"
	-@erase ".\include\apr.h"
	-@erase ".\include\apr_gen_test_char.h"
	-@erase "$(OUTDIR)\gen_test_char.exe"
	-@erase "$(OUTDIR)\gen_test_char.obj"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

CPP=cl.exe
CPP_PROJ=/nologo /MD /W3 /Zi /O2 /Oy- /I "./include" /I "./include/arch" /I "./include/arch/win32" /I "./include/arch/unix" /D "NDEBUG" /D "APR_DECLARE_EXPORT" /D "WIN32" /D "_WINDOWS" /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\libapr_src" /FD /c 

.c{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.c{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

MTL=midl.exe
MTL_PROJ=/nologo /D "NDEBUG" /mktyplib203 /o /win32 "NUL" 
RSC=rc.exe
RSC_PROJ=/l 0x409 /fo"$(INTDIR)\libapr.res" /i "./include" /d "NDEBUG" /d "APR_VERSION_ONLY" 
BSC32=bscmake.exe
BSC32_FLAGS=/nologo /o"$(OUTDIR)\libapr.bsc" 
BSC32_SBRS= \
	
LINK32=link.exe
LINK32_FLAGS=kernel32.lib advapi32.lib ws2_32.lib mswsock.lib ole32.lib shell32.lib rpcrt4.lib /nologo /base:"0x6EEC0000" /subsystem:windows /dll /incremental:no /pdb:"$(OUTDIR)\libapr-1.pdb" /debug /out:"$(OUTDIR)\libapr-1.dll" /implib:"$(OUTDIR)\libapr-1.lib" /MACHINE:X86 /opt:ref 
LINK32_OBJS= \
	"$(INTDIR)\apr_atomic.obj" \
	"$(INTDIR)\dso.obj" \
	"$(INTDIR)\apr_escape.obj" \
	"$(INTDIR)\buffer.obj" \
	"$(INTDIR)\copy.obj" \
	"$(INTDIR)\dir.obj" \
	"$(INTDIR)\fileacc.obj" \
	"$(INTDIR)\filedup.obj" \
	"$(INTDIR)\filepath.obj" \
	"$(INTDIR)\filepath_util.obj" \
	"$(INTDIR)\filestat.obj" \
	"$(INTDIR)\filesys.obj" \
	"$(INTDIR)\flock.obj" \
	"$(INTDIR)\fullrw.obj" \
	"$(INTDIR)\mktemp.obj" \
	"$(INTDIR)\open.obj" \
	"$(INTDIR)\pipe.obj" \
	"$(INTDIR)\readwrite.obj" \
	"$(INTDIR)\seek.obj" \
	"$(INTDIR)\tempdir.obj" \
	"$(INTDIR)\proc_mutex.obj" \
	"$(INTDIR)\thread_cond.obj" \
	"$(INTDIR)\thread_mutex.obj" \
	"$(INTDIR)\thread_rwlock.obj" \
	"$(INTDIR)\apr_pools.obj" \
	"$(INTDIR)\charset.obj" \
	"$(INTDIR)\env.obj" \
	"$(INTDIR)\errorcodes.obj" \
	"$(INTDIR)\getopt.obj" \
	"$(INTDIR)\internal.obj" \
	"$(INTDIR)\misc.obj" \
	"$(INTDIR)\otherchild.obj" \
	"$(INTDIR)\rand.obj" \
	"$(INTDIR)\start.obj" \
	"$(INTDIR)\utf8.obj" \
	"$(INTDIR)\version.obj" \
	"$(INTDIR)\common.obj" \
	"$(INTDIR)\mmap.obj" \
	"$(INTDIR)\inet_ntop.obj" \
	"$(INTDIR)\inet_pton.obj" \
	"$(INTDIR)\multicast.obj" \
	"$(INTDIR)\sendrecv.obj" \
	"$(INTDIR)\sockaddr.obj" \
	"$(INTDIR)\sockets.obj" \
	"$(INTDIR)\socket_util.obj" \
	"$(INTDIR)\sockopt.obj" \
	"$(INTDIR)\apr_getpass.obj" \
	"$(INTDIR)\poll.obj" \
	"$(INTDIR)\pollcb.obj" \
	"$(INTDIR)\pollset.obj" \
	"$(INTDIR)\select.obj" \
	"$(INTDIR)\apr_random.obj" \
	"$(INTDIR)\sha2.obj" \
	"$(INTDIR)\sha2_glue.obj" \
	"$(INTDIR)\shm.obj" \
	"$(INTDIR)\apr_cpystrn.obj" \
	"$(INTDIR)\apr_fnmatch.obj" \
	"$(INTDIR)\apr_snprintf.obj" \
	"$(INTDIR)\apr_strings.obj" \
	"$(INTDIR)\apr_strnatcmp.obj" \
	"$(INTDIR)\apr_strtok.obj" \
	"$(INTDIR)\apr_hash.obj" \
	"$(INTDIR)\apr_tables.obj" \
	"$(INTDIR)\apr_skiplist.obj" \
	"$(INTDIR)\proc.obj" \
	"$(INTDIR)\signals.obj" \
	"$(INTDIR)\thread.obj" \
	"$(INTDIR)\threadpriv.obj" \
	"$(INTDIR)\time.obj" \
	"$(INTDIR)\timestr.obj" \
	"$(INTDIR)\groupinfo.obj" \
	"$(INTDIR)\userinfo.obj" \
	"$(INTDIR)\libapr.res"

"$(OUTDIR)\libapr-1.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

TargetPath=.\9x\Release\libapr-1.dll
SOURCE="$(InputPath)"
PostBuild_Desc=Embed .manifest
DS_POSTBUILD_DEP=$(INTDIR)\postbld.dep

# Begin Custom Macros
OutDir=.\9x\Release
# End Custom Macros

"$(DS_POSTBUILD_DEP)" : "$(OUTDIR)\libapr-1.dll"
   if exist .\9x\Release\libapr-1.dll.manifest mt.exe -manifest .\9x\Release\libapr-1.dll.manifest -outputresource:.\9x\Release\libapr-1.dll;2
	echo Helper for Post-build step > "$(DS_POSTBUILD_DEP)"

!ELSEIF  "$(CFG)" == "libapr - Win32 Debug9x"

OUTDIR=.\9x\Debug
INTDIR=.\9x\Debug
DS_POSTBUILD_DEP=$(INTDIR)\postbld.dep
# Begin Custom Macros
OutDir=.\9x\Debug
# End Custom Macros

ALL : ".\include\apr_escape_test_char.h" ".\include\apr.h" ".\9x\Debug\gen_test_char.exe" "$(OUTDIR)\libapr-1.dll" "$(DS_POSTBUILD_DEP)"


CLEAN :
	-@erase "$(INTDIR)\apr_atomic.obj"
	-@erase "$(INTDIR)\apr_cpystrn.obj"
	-@erase "$(INTDIR)\apr_escape.obj"
	-@erase "$(INTDIR)\apr_fnmatch.obj"
	-@erase "$(INTDIR)\apr_getpass.obj"
	-@erase "$(INTDIR)\apr_hash.obj"
	-@erase "$(INTDIR)\apr_pools.obj"
	-@erase "$(INTDIR)\apr_random.obj"
	-@erase "$(INTDIR)\apr_skiplist.obj"
	-@erase "$(INTDIR)\apr_snprintf.obj"
	-@erase "$(INTDIR)\apr_strings.obj"
	-@erase "$(INTDIR)\apr_strnatcmp.obj"
	-@erase "$(INTDIR)\apr_strtok.obj"
	-@erase "$(INTDIR)\apr_tables.obj"
	-@erase "$(INTDIR)\buffer.obj"
	-@erase "$(INTDIR)\charset.obj"
	-@erase "$(INTDIR)\common.obj"
	-@erase "$(INTDIR)\copy.obj"
	-@erase "$(INTDIR)\dir.obj"
	-@erase "$(INTDIR)\dso.obj"
	-@erase "$(INTDIR)\env.obj"
	-@erase "$(INTDIR)\errorcodes.obj"
	-@erase "$(INTDIR)\fileacc.obj"
	-@erase "$(INTDIR)\filedup.obj"
	-@erase "$(INTDIR)\filepath.obj"
	-@erase "$(INTDIR)\filepath_util.obj"
	-@erase "$(INTDIR)\filestat.obj"
	-@erase "$(INTDIR)\filesys.obj"
	-@erase "$(INTDIR)\flock.obj"
	-@erase "$(INTDIR)\fullrw.obj"
	-@erase "$(INTDIR)\getopt.obj"
	-@erase "$(INTDIR)\groupinfo.obj"
	-@erase "$(INTDIR)\inet_ntop.obj"
	-@erase "$(INTDIR)\inet_pton.obj"
	-@erase "$(INTDIR)\internal.obj"
	-@erase "$(INTDIR)\libapr.res"
	-@erase "$(INTDIR)\libapr_src.idb"
	-@erase "$(INTDIR)\libapr_src.pdb"
	-@erase "$(INTDIR)\misc.obj"
	-@erase "$(INTDIR)\mktemp.obj"
	-@erase "$(INTDIR)\mmap.obj"
	-@erase "$(INTDIR)\multicast.obj"
	-@erase "$(INTDIR)\open.obj"
	-@erase "$(INTDIR)\otherchild.obj"
	-@erase "$(INTDIR)\pipe.obj"
	-@erase "$(INTDIR)\poll.obj"
	-@erase "$(INTDIR)\pollcb.obj"
	-@erase "$(INTDIR)\pollset.obj"
	-@erase "$(INTDIR)\proc.obj"
	-@erase "$(INTDIR)\proc_mutex.obj"
	-@erase "$(INTDIR)\rand.obj"
	-@erase "$(INTDIR)\readwrite.obj"
	-@erase "$(INTDIR)\seek.obj"
	-@erase "$(INTDIR)\select.obj"
	-@erase "$(INTDIR)\sendrecv.obj"
	-@erase "$(INTDIR)\sha2.obj"
	-@erase "$(INTDIR)\sha2_glue.obj"
	-@erase "$(INTDIR)\shm.obj"
	-@erase "$(INTDIR)\signals.obj"
	-@erase "$(INTDIR)\sockaddr.obj"
	-@erase "$(INTDIR)\socket_util.obj"
	-@erase "$(INTDIR)\sockets.obj"
	-@erase "$(INTDIR)\sockopt.obj"
	-@erase "$(INTDIR)\start.obj"
	-@erase "$(INTDIR)\tempdir.obj"
	-@erase "$(INTDIR)\thread.obj"
	-@erase "$(INTDIR)\thread_cond.obj"
	-@erase "$(INTDIR)\thread_mutex.obj"
	-@erase "$(INTDIR)\thread_rwlock.obj"
	-@erase "$(INTDIR)\threadpriv.obj"
	-@erase "$(INTDIR)\time.obj"
	-@erase "$(INTDIR)\timestr.obj"
	-@erase "$(INTDIR)\userinfo.obj"
	-@erase "$(INTDIR)\utf8.obj"
	-@erase "$(INTDIR)\version.obj"
	-@erase "$(OUTDIR)\libapr-1.dll"
	-@erase "$(OUTDIR)\libapr-1.exp"
	-@erase "$(OUTDIR)\libapr-1.lib"
	-@erase "$(OUTDIR)\libapr-1.pdb"
	-@erase ".\include\apr.h"
	-@erase ".\include\apr_gen_test_char.h"
	-@erase "$(OUTDIR)\gen_test_char.exe"
	-@erase "$(OUTDIR)\gen_test_char.obj"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

CPP=cl.exe
CPP_PROJ=/nologo /MDd /W3 /Zi /Od /I "./include" /I "./include/arch" /I "./include/arch/win32" /I "./include/arch/unix" /D "_DEBUG" /D "APR_DECLARE_EXPORT" /D "WIN32" /D "_WINDOWS" /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\libapr_src" /FD /EHsc /c 

.c{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.c{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

MTL=midl.exe
MTL_PROJ=/nologo /D "_DEBUG" /mktyplib203 /o /win32 "NUL" 
RSC=rc.exe
RSC_PROJ=/l 0x409 /fo"$(INTDIR)\libapr.res" /i "./include" /d "_DEBUG" /d "APR_VERSION_ONLY" 
BSC32=bscmake.exe
BSC32_FLAGS=/nologo /o"$(OUTDIR)\libapr.bsc" 
BSC32_SBRS= \
	
LINK32=link.exe
LINK32_FLAGS=kernel32.lib advapi32.lib ws2_32.lib mswsock.lib ole32.lib shell32.lib rpcrt4.lib /nologo /base:"0x6EEC0000" /subsystem:windows /dll /incremental:no /pdb:"$(OUTDIR)\libapr-1.pdb" /debug /out:"$(OUTDIR)\libapr-1.dll" /implib:"$(OUTDIR)\libapr-1.lib" /MACHINE:X86 
LINK32_OBJS= \
	"$(INTDIR)\apr_atomic.obj" \
	"$(INTDIR)\dso.obj" \
	"$(INTDIR)\apr_escape.obj" \
	"$(INTDIR)\buffer.obj" \
	"$(INTDIR)\copy.obj" \
	"$(INTDIR)\dir.obj" \
	"$(INTDIR)\fileacc.obj" \
	"$(INTDIR)\filedup.obj" \
	"$(INTDIR)\filepath.obj" \
	"$(INTDIR)\filepath_util.obj" \
	"$(INTDIR)\filestat.obj" \
	"$(INTDIR)\filesys.obj" \
	"$(INTDIR)\flock.obj" \
	"$(INTDIR)\fullrw.obj" \
	"$(INTDIR)\mktemp.obj" \
	"$(INTDIR)\open.obj" \
	"$(INTDIR)\pipe.obj" \
	"$(INTDIR)\readwrite.obj" \
	"$(INTDIR)\seek.obj" \
	"$(INTDIR)\tempdir.obj" \
	"$(INTDIR)\proc_mutex.obj" \
	"$(INTDIR)\thread_cond.obj" \
	"$(INTDIR)\thread_mutex.obj" \
	"$(INTDIR)\thread_rwlock.obj" \
	"$(INTDIR)\apr_pools.obj" \
	"$(INTDIR)\charset.obj" \
	"$(INTDIR)\env.obj" \
	"$(INTDIR)\errorcodes.obj" \
	"$(INTDIR)\getopt.obj" \
	"$(INTDIR)\internal.obj" \
	"$(INTDIR)\misc.obj" \
	"$(INTDIR)\otherchild.obj" \
	"$(INTDIR)\rand.obj" \
	"$(INTDIR)\start.obj" \
	"$(INTDIR)\utf8.obj" \
	"$(INTDIR)\version.obj" \
	"$(INTDIR)\common.obj" \
	"$(INTDIR)\mmap.obj" \
	"$(INTDIR)\inet_ntop.obj" \
	"$(INTDIR)\inet_pton.obj" \
	"$(INTDIR)\multicast.obj" \
	"$(INTDIR)\sendrecv.obj" \
	"$(INTDIR)\sockaddr.obj" \
	"$(INTDIR)\sockets.obj" \
	"$(INTDIR)\socket_util.obj" \
	"$(INTDIR)\sockopt.obj" \
	"$(INTDIR)\apr_getpass.obj" \
	"$(INTDIR)\poll.obj" \
	"$(INTDIR)\pollcb.obj" \
	"$(INTDIR)\pollset.obj" \
	"$(INTDIR)\select.obj" \
	"$(INTDIR)\apr_random.obj" \
	"$(INTDIR)\sha2.obj" \
	"$(INTDIR)\sha2_glue.obj" \
	"$(INTDIR)\shm.obj" \
	"$(INTDIR)\apr_cpystrn.obj" \
	"$(INTDIR)\apr_fnmatch.obj" \
	"$(INTDIR)\apr_snprintf.obj" \
	"$(INTDIR)\apr_strings.obj" \
	"$(INTDIR)\apr_strnatcmp.obj" \
	"$(INTDIR)\apr_strtok.obj" \
	"$(INTDIR)\apr_hash.obj" \
	"$(INTDIR)\apr_tables.obj" \
	"$(INTDIR)\apr_skiplist.obj" \
	"$(INTDIR)\proc.obj" \
	"$(INTDIR)\signals.obj" \
	"$(INTDIR)\thread.obj" \
	"$(INTDIR)\threadpriv.obj" \
	"$(INTDIR)\time.obj" \
	"$(INTDIR)\timestr.obj" \
	"$(INTDIR)\groupinfo.obj" \
	"$(INTDIR)\userinfo.obj" \
	"$(INTDIR)\libapr.res"

"$(OUTDIR)\libapr-1.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

TargetPath=.\9x\Debug\libapr-1.dll
SOURCE="$(InputPath)"
PostBuild_Desc=Embed .manifest
DS_POSTBUILD_DEP=$(INTDIR)\postbld.dep

# Begin Custom Macros
OutDir=.\9x\Debug
# End Custom Macros

"$(DS_POSTBUILD_DEP)" : "$(OUTDIR)\libapr-1.dll"
   if exist .\9x\Debug\libapr-1.dll.manifest mt.exe -manifest .\9x\Debug\libapr-1.dll.manifest -outputresource:.\9x\Debug\libapr-1.dll;2
	echo Helper for Post-build step > "$(DS_POSTBUILD_DEP)"

!ELSEIF  "$(CFG)" == "libapr - x64 Release"

OUTDIR=.\x64\Release
INTDIR=.\x64\Release
DS_POSTBUILD_DEP=$(INTDIR)\postbld.dep
# Begin Custom Macros
OutDir=.\x64\Release
# End Custom Macros

ALL : ".\x64\Release\gen_test_char.exe" ".\include\apr_escape_test_char.h" "$(OUTDIR)\libapr-1.dll" "$(DS_POSTBUILD_DEP)"


CLEAN :
	-@erase "$(INTDIR)\apr_atomic.obj"
	-@erase "$(INTDIR)\apr_cpystrn.obj"
	-@erase "$(INTDIR)\apr_escape.obj"
	-@erase "$(INTDIR)\apr_fnmatch.obj"
	-@erase "$(INTDIR)\apr_getpass.obj"
	-@erase "$(INTDIR)\apr_hash.obj"
	-@erase "$(INTDIR)\apr_pools.obj"
	-@erase "$(INTDIR)\apr_random.obj"
	-@erase "$(INTDIR)\apr_skiplist.obj"
	-@erase "$(INTDIR)\apr_snprintf.obj"
	-@erase "$(INTDIR)\apr_strings.obj"
	-@erase "$(INTDIR)\apr_strnatcmp.obj"
	-@erase "$(INTDIR)\apr_strtok.obj"
	-@erase "$(INTDIR)\apr_tables.obj"
	-@erase "$(INTDIR)\buffer.obj"
	-@erase "$(INTDIR)\charset.obj"
	-@erase "$(INTDIR)\common.obj"
	-@erase "$(INTDIR)\copy.obj"
	-@erase "$(INTDIR)\dir.obj"
	-@erase "$(INTDIR)\dso.obj"
	-@erase "$(INTDIR)\env.obj"
	-@erase "$(INTDIR)\errorcodes.obj"
	-@erase "$(INTDIR)\fileacc.obj"
	-@erase "$(INTDIR)\filedup.obj"
	-@erase "$(INTDIR)\filepath.obj"
	-@erase "$(INTDIR)\filepath_util.obj"
	-@erase "$(INTDIR)\filestat.obj"
	-@erase "$(INTDIR)\filesys.obj"
	-@erase "$(INTDIR)\flock.obj"
	-@erase "$(INTDIR)\fullrw.obj"
	-@erase "$(INTDIR)\getopt.obj"
	-@erase "$(INTDIR)\groupinfo.obj"
	-@erase "$(INTDIR)\inet_ntop.obj"
	-@erase "$(INTDIR)\inet_pton.obj"
	-@erase "$(INTDIR)\internal.obj"
	-@erase "$(INTDIR)\libapr.res"
	-@erase "$(INTDIR)\libapr_src.idb"
	-@erase "$(INTDIR)\libapr_src.pdb"
	-@erase "$(INTDIR)\misc.obj"
	-@erase "$(INTDIR)\mktemp.obj"
	-@erase "$(INTDIR)\mmap.obj"
	-@erase "$(INTDIR)\multicast.obj"
	-@erase "$(INTDIR)\open.obj"
	-@erase "$(INTDIR)\otherchild.obj"
	-@erase "$(INTDIR)\pipe.obj"
	-@erase "$(INTDIR)\poll.obj"
	-@erase "$(INTDIR)\pollcb.obj"
	-@erase "$(INTDIR)\pollset.obj"
	-@erase "$(INTDIR)\proc.obj"
	-@erase "$(INTDIR)\proc_mutex.obj"
	-@erase "$(INTDIR)\rand.obj"
	-@erase "$(INTDIR)\readwrite.obj"
	-@erase "$(INTDIR)\seek.obj"
	-@erase "$(INTDIR)\select.obj"
	-@erase "$(INTDIR)\sendrecv.obj"
	-@erase "$(INTDIR)\sha2.obj"
	-@erase "$(INTDIR)\sha2_glue.obj"
	-@erase "$(INTDIR)\shm.obj"
	-@erase "$(INTDIR)\signals.obj"
	-@erase "$(INTDIR)\sockaddr.obj"
	-@erase "$(INTDIR)\socket_util.obj"
	-@erase "$(INTDIR)\sockets.obj"
	-@erase "$(INTDIR)\sockopt.obj"
	-@erase "$(INTDIR)\start.obj"
	-@erase "$(INTDIR)\tempdir.obj"
	-@erase "$(INTDIR)\thread.obj"
	-@erase "$(INTDIR)\thread_cond.obj"
	-@erase "$(INTDIR)\thread_mutex.obj"
	-@erase "$(INTDIR)\thread_rwlock.obj"
	-@erase "$(INTDIR)\threadpriv.obj"
	-@erase "$(INTDIR)\time.obj"
	-@erase "$(INTDIR)\timestr.obj"
	-@erase "$(INTDIR)\userinfo.obj"
	-@erase "$(INTDIR)\utf8.obj"
	-@erase "$(INTDIR)\version.obj"
	-@erase "$(OUTDIR)\libapr-1.dll"
	-@erase "$(OUTDIR)\libapr-1.exp"
	-@erase "$(OUTDIR)\libapr-1.lib"
	-@erase "$(OUTDIR)\libapr-1.pdb"
	-@erase ".\include\apr.h"
	-@erase ".\include\apr_gen_test_char.h"
	-@erase "$(OUTDIR)\gen_test_char.exe"
	-@erase "$(OUTDIR)\gen_test_char.obj"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

CPP=cl.exe
CPP_PROJ=/nologo /MD /W3 /Zi /O2 /Oy- /I "./include" /I "./include/arch" /I "./include/arch/win32" /I "./include/arch/unix" /D "NDEBUG" /D "APR_DECLARE_EXPORT" /D "WIN32" /D "WINNT" /D "_WINDOWS" /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\libapr_src" /FD /c 

.c{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.c{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

MTL=midl.exe
MTL_PROJ=/nologo /D "NDEBUG" /mktyplib203 /o /win32 "NUL" 
RSC=rc.exe
RSC_PROJ=/l 0x409 /fo"$(INTDIR)\libapr.res" /i "./include" /d "NDEBUG" /d "APR_VERSION_ONLY" 
BSC32=bscmake.exe
BSC32_FLAGS=/nologo /o"$(OUTDIR)\libapr.bsc" 
BSC32_SBRS= \
	
LINK32=link.exe
LINK32_FLAGS=kernel32.lib advapi32.lib ws2_32.lib mswsock.lib ole32.lib shell32.lib rpcrt4.lib /nologo /base:"0x6EEC0000" /subsystem:windows /dll /incremental:no /pdb:"$(OUTDIR)\libapr-1.pdb" /debug /out:"$(OUTDIR)\libapr-1.dll" /implib:"$(OUTDIR)\libapr-1.lib" /MACHINE:X64 /opt:ref 
LINK32_OBJS= \
	"$(INTDIR)\apr_atomic.obj" \
	"$(INTDIR)\dso.obj" \
	"$(INTDIR)\apr_escape.obj" \
	"$(INTDIR)\buffer.obj" \
	"$(INTDIR)\copy.obj" \
	"$(INTDIR)\dir.obj" \
	"$(INTDIR)\fileacc.obj" \
	"$(INTDIR)\filedup.obj" \
	"$(INTDIR)\filepath.obj" \
	"$(INTDIR)\filepath_util.obj" \
	"$(INTDIR)\filestat.obj" \
	"$(INTDIR)\filesys.obj" \
	"$(INTDIR)\flock.obj" \
	"$(INTDIR)\fullrw.obj" \
	"$(INTDIR)\mktemp.obj" \
	"$(INTDIR)\open.obj" \
	"$(INTDIR)\pipe.obj" \
	"$(INTDIR)\readwrite.obj" \
	"$(INTDIR)\seek.obj" \
	"$(INTDIR)\tempdir.obj" \
	"$(INTDIR)\proc_mutex.obj" \
	"$(INTDIR)\thread_cond.obj" \
	"$(INTDIR)\thread_mutex.obj" \
	"$(INTDIR)\thread_rwlock.obj" \
	"$(INTDIR)\apr_pools.obj" \
	"$(INTDIR)\charset.obj" \
	"$(INTDIR)\env.obj" \
	"$(INTDIR)\errorcodes.obj" \
	"$(INTDIR)\getopt.obj" \
	"$(INTDIR)\internal.obj" \
	"$(INTDIR)\misc.obj" \
	"$(INTDIR)\otherchild.obj" \
	"$(INTDIR)\rand.obj" \
	"$(INTDIR)\start.obj" \
	"$(INTDIR)\utf8.obj" \
	"$(INTDIR)\version.obj" \
	"$(INTDIR)\common.obj" \
	"$(INTDIR)\mmap.obj" \
	"$(INTDIR)\inet_ntop.obj" \
	"$(INTDIR)\inet_pton.obj" \
	"$(INTDIR)\multicast.obj" \
	"$(INTDIR)\sendrecv.obj" \
	"$(INTDIR)\sockaddr.obj" \
	"$(INTDIR)\sockets.obj" \
	"$(INTDIR)\socket_util.obj" \
	"$(INTDIR)\sockopt.obj" \
	"$(INTDIR)\apr_getpass.obj" \
	"$(INTDIR)\poll.obj" \
	"$(INTDIR)\pollcb.obj" \
	"$(INTDIR)\pollset.obj" \
	"$(INTDIR)\select.obj" \
	"$(INTDIR)\apr_random.obj" \
	"$(INTDIR)\sha2.obj" \
	"$(INTDIR)\sha2_glue.obj" \
	"$(INTDIR)\shm.obj" \
	"$(INTDIR)\apr_cpystrn.obj" \
	"$(INTDIR)\apr_fnmatch.obj" \
	"$(INTDIR)\apr_snprintf.obj" \
	"$(INTDIR)\apr_strings.obj" \
	"$(INTDIR)\apr_strnatcmp.obj" \
	"$(INTDIR)\apr_strtok.obj" \
	"$(INTDIR)\apr_hash.obj" \
	"$(INTDIR)\apr_tables.obj" \
	"$(INTDIR)\apr_skiplist.obj" \
	"$(INTDIR)\proc.obj" \
	"$(INTDIR)\signals.obj" \
	"$(INTDIR)\thread.obj" \
	"$(INTDIR)\threadpriv.obj" \
	"$(INTDIR)\time.obj" \
	"$(INTDIR)\timestr.obj" \
	"$(INTDIR)\groupinfo.obj" \
	"$(INTDIR)\userinfo.obj" \
	"$(INTDIR)\libapr.res"

"$(OUTDIR)\libapr-1.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

TargetPath=.\x64\Release\libapr-1.dll
SOURCE="$(InputPath)"
PostBuild_Desc=Embed .manifest
DS_POSTBUILD_DEP=$(INTDIR)\postbld.dep

# Begin Custom Macros
OutDir=.\x64\Release
# End Custom Macros

"$(DS_POSTBUILD_DEP)" : "$(OUTDIR)\libapr-1.dll"
   if exist .\x64\Release\libapr-1.dll.manifest mt.exe -manifest .\x64\Release\libapr-1.dll.manifest -outputresource:.\x64\Release\libapr-1.dll;2
	echo Helper for Post-build step > "$(DS_POSTBUILD_DEP)"

!ELSEIF  "$(CFG)" == "libapr - x64 Debug"

OUTDIR=.\x64\Debug
INTDIR=.\x64\Debug
DS_POSTBUILD_DEP=$(INTDIR)\postbld.dep
# Begin Custom Macros
OutDir=.\x64\Debug
# End Custom Macros

ALL : ".\x64\Debug\gen_test_char.exe" ".\include\apr_escape_test_char.h" "$(OUTDIR)\libapr-1.dll" "$(DS_POSTBUILD_DEP)"


CLEAN :
	-@erase "$(INTDIR)\apr_atomic.obj"
	-@erase "$(INTDIR)\apr_cpystrn.obj"
	-@erase "$(INTDIR)\apr_escape.obj"
	-@erase "$(INTDIR)\apr_fnmatch.obj"
	-@erase "$(INTDIR)\apr_getpass.obj"
	-@erase "$(INTDIR)\apr_hash.obj"
	-@erase "$(INTDIR)\apr_pools.obj"
	-@erase "$(INTDIR)\apr_random.obj"
	-@erase "$(INTDIR)\apr_skiplist.obj"
	-@erase "$(INTDIR)\apr_snprintf.obj"
	-@erase "$(INTDIR)\apr_strings.obj"
	-@erase "$(INTDIR)\apr_strnatcmp.obj"
	-@erase "$(INTDIR)\apr_strtok.obj"
	-@erase "$(INTDIR)\apr_tables.obj"
	-@erase "$(INTDIR)\buffer.obj"
	-@erase "$(INTDIR)\charset.obj"
	-@erase "$(INTDIR)\common.obj"
	-@erase "$(INTDIR)\copy.obj"
	-@erase "$(INTDIR)\dir.obj"
	-@erase "$(INTDIR)\dso.obj"
	-@erase "$(INTDIR)\env.obj"
	-@erase "$(INTDIR)\errorcodes.obj"
	-@erase "$(INTDIR)\fileacc.obj"
	-@erase "$(INTDIR)\filedup.obj"
	-@erase "$(INTDIR)\filepath.obj"
	-@erase "$(INTDIR)\filepath_util.obj"
	-@erase "$(INTDIR)\filestat.obj"
	-@erase "$(INTDIR)\filesys.obj"
	-@erase "$(INTDIR)\flock.obj"
	-@erase "$(INTDIR)\fullrw.obj"
	-@erase "$(INTDIR)\getopt.obj"
	-@erase "$(INTDIR)\groupinfo.obj"
	-@erase "$(INTDIR)\inet_ntop.obj"
	-@erase "$(INTDIR)\inet_pton.obj"
	-@erase "$(INTDIR)\internal.obj"
	-@erase "$(INTDIR)\libapr.res"
	-@erase "$(INTDIR)\libapr_src.idb"
	-@erase "$(INTDIR)\libapr_src.pdb"
	-@erase "$(INTDIR)\misc.obj"
	-@erase "$(INTDIR)\mktemp.obj"
	-@erase "$(INTDIR)\mmap.obj"
	-@erase "$(INTDIR)\multicast.obj"
	-@erase "$(INTDIR)\open.obj"
	-@erase "$(INTDIR)\otherchild.obj"
	-@erase "$(INTDIR)\pipe.obj"
	-@erase "$(INTDIR)\poll.obj"
	-@erase "$(INTDIR)\pollcb.obj"
	-@erase "$(INTDIR)\pollset.obj"
	-@erase "$(INTDIR)\proc.obj"
	-@erase "$(INTDIR)\proc_mutex.obj"
	-@erase "$(INTDIR)\rand.obj"
	-@erase "$(INTDIR)\readwrite.obj"
	-@erase "$(INTDIR)\seek.obj"
	-@erase "$(INTDIR)\select.obj"
	-@erase "$(INTDIR)\sendrecv.obj"
	-@erase "$(INTDIR)\sha2.obj"
	-@erase "$(INTDIR)\sha2_glue.obj"
	-@erase "$(INTDIR)\shm.obj"
	-@erase "$(INTDIR)\signals.obj"
	-@erase "$(INTDIR)\sockaddr.obj"
	-@erase "$(INTDIR)\socket_util.obj"
	-@erase "$(INTDIR)\sockets.obj"
	-@erase "$(INTDIR)\sockopt.obj"
	-@erase "$(INTDIR)\start.obj"
	-@erase "$(INTDIR)\tempdir.obj"
	-@erase "$(INTDIR)\thread.obj"
	-@erase "$(INTDIR)\thread_cond.obj"
	-@erase "$(INTDIR)\thread_mutex.obj"
	-@erase "$(INTDIR)\thread_rwlock.obj"
	-@erase "$(INTDIR)\threadpriv.obj"
	-@erase "$(INTDIR)\time.obj"
	-@erase "$(INTDIR)\timestr.obj"
	-@erase "$(INTDIR)\userinfo.obj"
	-@erase "$(INTDIR)\utf8.obj"
	-@erase "$(INTDIR)\version.obj"
	-@erase "$(OUTDIR)\libapr-1.dll"
	-@erase "$(OUTDIR)\libapr-1.exp"
	-@erase "$(OUTDIR)\libapr-1.lib"
	-@erase "$(OUTDIR)\libapr-1.pdb"
	-@erase ".\include\apr.h"
	-@erase ".\include\apr_gen_test_char.h"
	-@erase "$(OUTDIR)\gen_test_char.exe"
	-@erase "$(OUTDIR)\gen_test_char.obj"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

CPP=cl.exe
CPP_PROJ=/nologo /MDd /W3 /Zi /Od /I "./include" /I "./include/arch" /I "./include/arch/win32" /I "./include/arch/unix" /D "_DEBUG" /D "APR_DECLARE_EXPORT" /D "WIN32" /D "WINNT" /D "_WINDOWS" /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\libapr_src" /FD /EHsc /c 

.c{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.c{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

MTL=midl.exe
MTL_PROJ=/nologo /D "_DEBUG" /mktyplib203 /o /win32 "NUL" 
RSC=rc.exe
RSC_PROJ=/l 0x409 /fo"$(INTDIR)\libapr.res" /i "./include" /d "_DEBUG" /d "APR_VERSION_ONLY" 
BSC32=bscmake.exe
BSC32_FLAGS=/nologo /o"$(OUTDIR)\libapr.bsc" 
BSC32_SBRS= \
	
LINK32=link.exe
LINK32_FLAGS=kernel32.lib advapi32.lib ws2_32.lib mswsock.lib ole32.lib shell32.lib rpcrt4.lib /nologo /base:"0x6EEC0000" /subsystem:windows /dll /incremental:no /pdb:"$(OUTDIR)\libapr-1.pdb" /debug /out:"$(OUTDIR)\libapr-1.dll" /implib:"$(OUTDIR)\libapr-1.lib" /MACHINE:X64 
LINK32_OBJS= \
	"$(INTDIR)\apr_atomic.obj" \
	"$(INTDIR)\dso.obj" \
	"$(INTDIR)\apr_escape.obj" \
	"$(INTDIR)\buffer.obj" \
	"$(INTDIR)\copy.obj" \
	"$(INTDIR)\dir.obj" \
	"$(INTDIR)\fileacc.obj" \
	"$(INTDIR)\filedup.obj" \
	"$(INTDIR)\filepath.obj" \
	"$(INTDIR)\filepath_util.obj" \
	"$(INTDIR)\filestat.obj" \
	"$(INTDIR)\filesys.obj" \
	"$(INTDIR)\flock.obj" \
	"$(INTDIR)\fullrw.obj" \
	"$(INTDIR)\mktemp.obj" \
	"$(INTDIR)\open.obj" \
	"$(INTDIR)\pipe.obj" \
	"$(INTDIR)\readwrite.obj" \
	"$(INTDIR)\seek.obj" \
	"$(INTDIR)\tempdir.obj" \
	"$(INTDIR)\proc_mutex.obj" \
	"$(INTDIR)\thread_cond.obj" \
	"$(INTDIR)\thread_mutex.obj" \
	"$(INTDIR)\thread_rwlock.obj" \
	"$(INTDIR)\apr_pools.obj" \
	"$(INTDIR)\charset.obj" \
	"$(INTDIR)\env.obj" \
	"$(INTDIR)\errorcodes.obj" \
	"$(INTDIR)\getopt.obj" \
	"$(INTDIR)\internal.obj" \
	"$(INTDIR)\misc.obj" \
	"$(INTDIR)\otherchild.obj" \
	"$(INTDIR)\rand.obj" \
	"$(INTDIR)\start.obj" \
	"$(INTDIR)\utf8.obj" \
	"$(INTDIR)\version.obj" \
	"$(INTDIR)\common.obj" \
	"$(INTDIR)\mmap.obj" \
	"$(INTDIR)\inet_ntop.obj" \
	"$(INTDIR)\inet_pton.obj" \
	"$(INTDIR)\multicast.obj" \
	"$(INTDIR)\sendrecv.obj" \
	"$(INTDIR)\sockaddr.obj" \
	"$(INTDIR)\sockets.obj" \
	"$(INTDIR)\socket_util.obj" \
	"$(INTDIR)\sockopt.obj" \
	"$(INTDIR)\apr_getpass.obj" \
	"$(INTDIR)\poll.obj" \
	"$(INTDIR)\pollcb.obj" \
	"$(INTDIR)\pollset.obj" \
	"$(INTDIR)\select.obj" \
	"$(INTDIR)\apr_random.obj" \
	"$(INTDIR)\sha2.obj" \
	"$(INTDIR)\sha2_glue.obj" \
	"$(INTDIR)\shm.obj" \
	"$(INTDIR)\apr_cpystrn.obj" \
	"$(INTDIR)\apr_fnmatch.obj" \
	"$(INTDIR)\apr_snprintf.obj" \
	"$(INTDIR)\apr_strings.obj" \
	"$(INTDIR)\apr_strnatcmp.obj" \
	"$(INTDIR)\apr_strtok.obj" \
	"$(INTDIR)\apr_hash.obj" \
	"$(INTDIR)\apr_tables.obj" \
	"$(INTDIR)\apr_skiplist.obj" \
	"$(INTDIR)\proc.obj" \
	"$(INTDIR)\signals.obj" \
	"$(INTDIR)\thread.obj" \
	"$(INTDIR)\threadpriv.obj" \
	"$(INTDIR)\time.obj" \
	"$(INTDIR)\timestr.obj" \
	"$(INTDIR)\groupinfo.obj" \
	"$(INTDIR)\userinfo.obj" \
	"$(INTDIR)\libapr.res"

"$(OUTDIR)\libapr-1.dll" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

TargetPath=.\x64\Debug\libapr-1.dll
SOURCE="$(InputPath)"
PostBuild_Desc=Embed .manifest
DS_POSTBUILD_DEP=$(INTDIR)\postbld.dep

# Begin Custom Macros
OutDir=.\x64\Debug
# End Custom Macros

"$(DS_POSTBUILD_DEP)" : "$(OUTDIR)\libapr-1.dll"
   if exist .\x64\Debug\libapr-1.dll.manifest mt.exe -manifest .\x64\Debug\libapr-1.dll.manifest -outputresource:.\x64\Debug\libapr-1.dll;2
	echo Helper for Post-build step > "$(DS_POSTBUILD_DEP)"

!ENDIF 


!IF "$(NO_EXTERNAL_DEPS)" != "1"
!IF EXISTS("libapr.dep")
!INCLUDE "libapr.dep"
!ELSE 
!MESSAGE Warning: cannot find "libapr.dep"
!ENDIF 
!ENDIF 


!IF "$(CFG)" == "libapr - Win32 Release" || "$(CFG)" == "libapr - Win32 Debug" || "$(CFG)" == "libapr - Win32 Release9x" || "$(CFG)" == "libapr - Win32 Debug9x" || "$(CFG)" == "libapr - x64 Release" || "$(CFG)" == "libapr - x64 Debug"
SOURCE=.\atomic\win32\apr_atomic.c

"$(INTDIR)\apr_atomic.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\dso\win32\dso.c

"$(INTDIR)\dso.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\encoding\apr_escape.c

"$(INTDIR)\apr_escape.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr_escape.h" ".\include\apr_escape_test_char.h" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\file_io\win32\buffer.c

"$(INTDIR)\buffer.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\file_io\unix\copy.c

"$(INTDIR)\copy.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\file_io\win32\dir.c

"$(INTDIR)\dir.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\file_io\unix\fileacc.c

"$(INTDIR)\fileacc.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\file_io\win32\filedup.c

"$(INTDIR)\filedup.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\file_io\win32\filepath.c

"$(INTDIR)\filepath.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\file_io\unix\filepath_util.c

"$(INTDIR)\filepath_util.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\file_io\win32\filestat.c

"$(INTDIR)\filestat.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\file_io\win32\filesys.c

"$(INTDIR)\filesys.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\file_io\win32\flock.c

"$(INTDIR)\flock.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\file_io\unix\fullrw.c

"$(INTDIR)\fullrw.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\file_io\unix\mktemp.c

"$(INTDIR)\mktemp.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\file_io\win32\open.c

"$(INTDIR)\open.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\file_io\win32\pipe.c

"$(INTDIR)\pipe.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\file_io\win32\readwrite.c

"$(INTDIR)\readwrite.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\file_io\win32\seek.c

"$(INTDIR)\seek.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\file_io\unix\tempdir.c

"$(INTDIR)\tempdir.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\locks\win32\proc_mutex.c

"$(INTDIR)\proc_mutex.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\locks\win32\thread_cond.c

"$(INTDIR)\thread_cond.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\locks\win32\thread_mutex.c

"$(INTDIR)\thread_mutex.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\locks\win32\thread_rwlock.c

"$(INTDIR)\thread_rwlock.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\memory\unix\apr_pools.c

"$(INTDIR)\apr_pools.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\misc\win32\apr_app.c
SOURCE=.\misc\win32\charset.c

"$(INTDIR)\charset.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\misc\win32\env.c

"$(INTDIR)\env.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\misc\unix\errorcodes.c

"$(INTDIR)\errorcodes.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\misc\unix\getopt.c

"$(INTDIR)\getopt.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\misc\win32\internal.c

"$(INTDIR)\internal.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\misc\win32\misc.c

"$(INTDIR)\misc.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\misc\unix\otherchild.c

"$(INTDIR)\otherchild.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\misc\win32\rand.c

"$(INTDIR)\rand.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\misc\win32\start.c

"$(INTDIR)\start.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\misc\win32\utf8.c

"$(INTDIR)\utf8.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\misc\unix\version.c

"$(INTDIR)\version.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\mmap\unix\common.c

"$(INTDIR)\common.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\mmap\win32\mmap.c

"$(INTDIR)\mmap.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\network_io\unix\inet_ntop.c

"$(INTDIR)\inet_ntop.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\network_io\unix\inet_pton.c

"$(INTDIR)\inet_pton.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\network_io\unix\multicast.c

"$(INTDIR)\multicast.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\network_io\win32\sendrecv.c

"$(INTDIR)\sendrecv.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\network_io\unix\sockaddr.c

"$(INTDIR)\sockaddr.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\network_io\unix\socket_util.c

"$(INTDIR)\socket_util.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\network_io\win32\sockets.c

"$(INTDIR)\sockets.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\network_io\win32\sockopt.c

"$(INTDIR)\sockopt.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\passwd\apr_getpass.c

"$(INTDIR)\apr_getpass.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\poll\unix\poll.c

"$(INTDIR)\poll.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\poll\unix\pollcb.c

"$(INTDIR)\pollcb.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\poll\unix\pollset.c

"$(INTDIR)\pollset.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\poll\unix\select.c

"$(INTDIR)\select.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\random\unix\apr_random.c

"$(INTDIR)\apr_random.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\random\unix\sha2.c

"$(INTDIR)\sha2.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\random\unix\sha2_glue.c

"$(INTDIR)\sha2_glue.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\shmem\win32\shm.c

"$(INTDIR)\shm.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\strings\apr_cpystrn.c

"$(INTDIR)\apr_cpystrn.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\strings\apr_fnmatch.c

"$(INTDIR)\apr_fnmatch.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\strings\apr_snprintf.c

"$(INTDIR)\apr_snprintf.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\strings\apr_strings.c

"$(INTDIR)\apr_strings.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\strings\apr_strnatcmp.c

"$(INTDIR)\apr_strnatcmp.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\strings\apr_strtok.c

"$(INTDIR)\apr_strtok.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\tables\apr_hash.c

"$(INTDIR)\apr_hash.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\tables\apr_skiplist.c

"$(INTDIR)\apr_skiplist.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\tables\apr_tables.c

"$(INTDIR)\apr_tables.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\threadproc\win32\proc.c

"$(INTDIR)\proc.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\threadproc\win32\signals.c

"$(INTDIR)\signals.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\threadproc\win32\thread.c

"$(INTDIR)\thread.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\threadproc\win32\threadpriv.c

"$(INTDIR)\threadpriv.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\time\win32\time.c

"$(INTDIR)\time.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\time\win32\timestr.c

"$(INTDIR)\timestr.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\user\win32\groupinfo.c

"$(INTDIR)\groupinfo.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\user\win32\userinfo.c

"$(INTDIR)\userinfo.obj" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\include\apr.hw

!IF  "$(CFG)" == "libapr - Win32 Release"

InputPath=.\include\apr.hw

".\include\apr.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	<<tempfile.bat 
	@echo off 
	type .\include\apr.hw > .\include\apr.h
<< 
	

!ELSEIF  "$(CFG)" == "libapr - Win32 Debug"

InputPath=.\include\apr.hw

".\include\apr.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	<<tempfile.bat 
	@echo off 
	type .\include\apr.hw > .\include\apr.h
<< 
	

!ELSEIF  "$(CFG)" == "libapr - Win32 Release9x"

InputPath=.\include\apr.hw

".\include\apr.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	<<tempfile.bat 
	@echo off 
	type .\include\apr.hw > .\include\apr.h
<< 
	

!ELSEIF  "$(CFG)" == "libapr - Win32 Debug9x"

InputPath=.\include\apr.hw

".\include\apr.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	<<tempfile.bat 
	@echo off 
	type .\include\apr.hw > .\include\apr.h
<< 
	

!ELSEIF  "$(CFG)" == "libapr - x64 Release"

InputPath=.\include\apr.hw

".\include\apr.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	<<tempfile.bat 
	@echo off 
	type .\include\apr.hw > .\include\apr.h
<< 
	

!ELSEIF  "$(CFG)" == "libapr - x64 Debug"

InputPath=.\include\apr.hw

".\include\apr.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	<<tempfile.bat 
	@echo off 
	type .\include\apr.hw > .\include\apr.h
<< 
	

!ENDIF 

SOURCE=.\include\apr_escape.h

!IF  "$(CFG)" == "libapr - Win32 Release"

InputPath=.\include\apr_escape.h

"$(INTDIR)\gen_test_char.exe" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	<<tempfile.bat 
	@echo off 
	cl.exe /nologo /W3 /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /FD /I ".\include" /Fo.\Release\gen_test_char /Fe.\Release\gen_test_char.exe .\tools\gen_test_char.c 
	.\Release\gen_test_char.exe > .\include\apr_escape_test_char.h
<< 
	

!ELSEIF  "$(CFG)" == "libapr - Win32 Debug"

InputPath=.\include\apr_escape.h

"$(INTDIR)\gen_test_char.exe" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	<<tempfile.bat 
	@echo off 
	cl.exe /nologo /W3 /EHsc /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /FD /I ".\include" /Fo.\Debug\gen_test_char /Fe.\Debug\gen_test_char.exe .\tools\gen_test_char.c 
	.\Debug\gen_test_char.exe > .\include\apr_escape_test_char.h
<< 
	

!ELSEIF  "$(CFG)" == "libapr - Win32 Release9x"

InputPath=.\include\apr_escape.h

"$(INTDIR)\gen_test_char.exe" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	<<tempfile.bat 
	@echo off 
	cl.exe /nologo /W3 /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /FD /I ".\include" /Fo.\9x\Release\gen_test_char /Fe.\9x\Release\gen_test_char.exe .\tools\gen_test_char.c 
	.\9x\Release\gen_test_char.exe > .\include\apr_escape_test_char.h
<< 
	

!ELSEIF  "$(CFG)" == "libapr - Win32 Debug9x"

InputPath=.\include\apr_escape.h

"$(INTDIR)\gen_test_char.exe" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	<<tempfile.bat 
	@echo off 
	cl.exe /nologo /W3 /EHsc /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /FD /I ".\include" /Fo.\9x\Debug\gen_test_char /Fe.\9x\Debug\gen_test_char.exe .\tools\gen_test_char.c 
	.\9x\Debug\gen_test_char.exe > .\include\apr_escape_test_char.h
<< 
	

!ELSEIF  "$(CFG)" == "libapr - x64 Release"

InputPath=.\include\apr_escape.h

"$(INTDIR)\gen_test_char.exe" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	<<tempfile.bat 
	@echo off 
	cl.exe /nologo /W3 /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /FD /I ".\include" /Fo.\x64\Release\gen_test_char /Fe.\x64\Release\gen_test_char.exe .\tools\gen_test_char.c 
	.\x64\Release\gen_test_char.exe > .\include\apr_escape_test_char.h
<< 
	

!ELSEIF  "$(CFG)" == "libapr - x64 Debug"

InputPath=.\include\apr_escape.h

"$(INTDIR)\gen_test_char.exe" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	<<tempfile.bat 
	@echo off 
	cl.exe /nologo /W3 /EHsc /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /FD /I ".\include" /Fo.\x64\Debug\gen_test_char /Fe.\x64\Debug\gen_test_char.exe .\tools\gen_test_char.c 
	.\x64\Debug\gen_test_char.exe > .\include\apr_escape_test_char.h
<< 
	

!ENDIF 

SOURCE=.\include\apr_want.h

!IF  "$(CFG)" == "libapr - Win32 Release"

InputPath=.\include\apr_want.h

".\include\apr_escape_test_char.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	<<tempfile.bat 
	@echo off 
	type .\include\apr.hw > .\include\apr.h
<< 
	

!ELSEIF  "$(CFG)" == "libapr - Win32 Debug"

InputPath=.\include\apr_want.h

".\include\apr_escape_test_char.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	<<tempfile.bat 
	@echo off 
	type .\include\apr.hw > .\include\apr.h
<< 
	

!ELSEIF  "$(CFG)" == "libapr - Win32 Release9x"

InputPath=.\include\apr_want.h

".\include\apr_escape_test_char.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	<<tempfile.bat 
	@echo off 
	type .\include\apr.hw > .\include\apr.h
<< 
	

!ELSEIF  "$(CFG)" == "libapr - Win32 Debug9x"

InputPath=.\include\apr_want.h

".\include\apr_escape_test_char.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	<<tempfile.bat 
	@echo off 
	type .\include\apr.hw > .\include\apr.h
<< 
	

!ELSEIF  "$(CFG)" == "libapr - x64 Release"

InputPath=.\include\apr_want.h

".\include\apr_escape_test_char.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	<<tempfile.bat 
	@echo off 
	type .\include\apr.hw > .\include\apr.h
<< 
	

!ELSEIF  "$(CFG)" == "libapr - x64 Debug"

InputPath=.\include\apr_want.h

".\include\apr_escape_test_char.h" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	<<tempfile.bat 
	@echo off 
	type .\include\apr.hw > .\include\apr.h
<< 
	

!ENDIF 

SOURCE=.\libapr.rc

"$(INTDIR)\libapr.res" : $(SOURCE) "$(INTDIR)" ".\include\apr.h"
	$(RSC) $(RSC_PROJ) $(SOURCE)



!ENDIF 

